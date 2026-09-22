// Duel Wordle: each player secretly picks a word for the opponent to guess.
// A board only ever scores guesses against its OWN secret, so secrets never
// cross the wire. Every guess/result does, so both boards can independently
// tell when the game is over and who won.
//
// Protocol (newline-delimited, see link.c):
//   WORD_SET             my player chose a secret (not the word itself)
//   GUESS <word>         my player guessed <word> against YOUR secret
//   RESULT <word> <gyb>  scoring of your guess, e.g. RESULT crane GYBBB
//   REVEAL <word>        my secret, sent once the game is over
//   READY                my player entered duel mode (start-of-round sync)

#include "game.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_random.h"

#include "link.h"
#include "wordle.h"
#include "wordlist.h"

typedef struct {
    char word[WORD_LEN + 1];
    char score[WORD_LEN + 1];
} guess_row_t;

static struct {
    char secret[WORD_LEN + 1];            // my word, which the opponent guesses
    guess_row_t mine[MAX_GUESSES];        // my guesses at the opponent's word
    volatile int my_count;
    volatile bool my_solved;
    volatile int opp_count;               // opponent's progress on my word
    volatile bool opp_solved;
    volatile bool peer_word_set;
    volatile bool peer_ready;
    char opp_secret[WORD_LEN + 1];        // revealed at game end
    volatile bool opp_revealed;
} s;

static QueueHandle_t s_result_q;

// ---- rendering ----------------------------------------------------------

static void render_row(const char *word, const char *score)
{
    for (int i = 0; i < WORD_LEN; i++) {
        const char *color = (score[i] == 'G') ? "\033[42;30m"
                          : (score[i] == 'Y') ? "\033[43;30m"
                          : "\033[100;97m";
        printf("%s %c \033[0m", color, toupper((unsigned char)word[i]));
    }
    printf("\n");
}

static void render_board(void)
{
    printf("\n");
    for (int i = 0; i < s.my_count; i++) {
        render_row(s.mine[i].word, s.mine[i].score);
    }
    printf("(%d/%d guesses)\n\n", s.my_count, MAX_GUESSES);
}

// ---- peer messages (run in the link task) -------------------------------

void game_on_peer_line(const char *line)
{
    if (strcmp(line, "WORD_SET") == 0) {
        s.peer_word_set = true;
        printf("\n(opponent has chosen their word)\n");
    } else if (strncmp(line, "GUESS ", 6) == 0) {
        const char *w = line + 6;
        if (strlen(w) != WORD_LEN || s.secret[0] == '\0') {
            return;
        }
        char score[WORD_LEN + 1];
        wordle_score(s.secret, w, score);
        if (s.opp_count < MAX_GUESSES) {
            s.opp_count++;
        }
        if (strcmp(score, "GGGGG") == 0) {
            s.opp_solved = true;
        }
        char msg[32];
        snprintf(msg, sizeof(msg), "RESULT %s %s", w, score);
        link_send_line(msg);
        printf("\nopponent guess %d/%d: ", s.opp_count, MAX_GUESSES);
        render_row(w, score);
    } else if (strncmp(line, "RESULT ", 7) == 0) {
        guess_row_t r;
        if (sscanf(line + 7, "%5s %5s", r.word, r.score) == 2) {
            xQueueSend(s_result_q, &r, 0);
        }
    } else if (strncmp(line, "REVEAL ", 7) == 0) {
        snprintf(s.opp_secret, sizeof(s.opp_secret), "%s", line + 7);
        s.opp_revealed = true;
    } else if (strcmp(line, "READY") == 0) {
        s.peer_ready = true;
        printf("\n(opponent is ready for a duel)\n");
    }
}

// ---- console input (runs in the main task) ------------------------------

static void read_line(char *buf, size_t max)
{
    size_t len = 0;
    while (true) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }
        if ((c == 0x7f || c == '\b') && len > 0) {
            len--;
            printf("\b \b");
            fflush(stdout);
            continue;
        }
        if (c >= 0x20 && len < max - 1) {
            buf[len++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
    buf[len] = '\0';
}

// Prompts until the player enters a word that's in the word list.
static void read_valid_word(const char *prompt, char *out)
{
    char buf[32];
    while (true) {
        printf("%s", prompt);
        fflush(stdout);
        read_line(buf, sizeof(buf));
        for (char *p = buf; *p; p++) {
            *p = tolower((unsigned char)*p);
        }
        if (wordle_valid_word(buf)) {
            strcpy(out, buf);
            return;
        }
        printf("  not a valid 5-letter word, try again\n");
    }
}

static void wait_flag(volatile bool *flag, const char *msg)
{
    if (!*flag) {
        printf("%s\n", msg);
        while (!*flag) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static bool my_done(void)  { return s.my_solved || s.my_count >= MAX_GUESSES; }
static bool opp_done(void) { return s.opp_solved || s.opp_count >= MAX_GUESSES; }

static void reset_my_board(void)
{
    memset(s.mine, 0, sizeof(s.mine));
    s.my_count = 0;
    s.my_solved = false;
}

// Full duel reset. Deliberately does NOT clear peer_ready: the opponent may
// have entered duel mode (and sent READY) before we did.
static void reset_round(void)
{
    guess_row_t drain;
    while (xQueueReceive(s_result_q, &drain, 0) == pdTRUE) {}
    s.secret[0] = '\0';
    s.opp_secret[0] = '\0';
    reset_my_board();
    s.opp_count = 0;
    s.opp_solved = false;
    s.peer_word_set = false;
    s.opp_revealed = false;
}

static void play_single(void)
{
    char secret[WORD_LEN + 1];
    strcpy(secret, answerlist[esp_random() % answerlist_count]);
    reset_my_board();
    printf("\n--- single player: I've picked a word, you have %d tries ---\n",
           MAX_GUESSES);

    while (!my_done()) {
        guess_row_t *row = &s.mine[s.my_count];
        read_valid_word("your guess: ", row->word);
        wordle_score(secret, row->word, row->score);
        s.my_count++;
        if (strcmp(row->score, "GGGGG") == 0) {
            s.my_solved = true;
        }
        render_board();
    }

    if (s.my_solved) {
        printf("you solved it in %d!\n", s.my_count);
    } else {
        printf("out of guesses! the word was: %s\n", secret);
    }
}

static void play_round(void)
{
    // Word entry: my secret is what the OPPONENT will guess.
    read_valid_word("choose a secret word for your opponent: ", s.secret);
    link_send_line("WORD_SET");
    wait_flag(&s.peer_word_set, "waiting for opponent to choose their word...");
    printf("\n--- game on! guess your opponent's word ---\n");

    // Guessing: send each guess to the opponent's board, which scores it.
    while (!my_done()) {
        char guess[WORD_LEN + 1];
        read_valid_word("your guess: ", guess);
        char msg[32];
        snprintf(msg, sizeof(msg), "GUESS %s", guess);
        if (!link_send_line(msg)) {
            printf("  (link down, wait for reconnect and try again)\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        guess_row_t r;
        bool got = false;
        while (xQueueReceive(s_result_q, &r, pdMS_TO_TICKS(10000)) == pdTRUE) {
            if (strcmp(r.word, guess) == 0) {   // skip any stale result
                got = true;
                break;
            }
        }
        if (!got) {
            printf("  (no reply from the other board, try again)\n");
            continue;
        }
        s.mine[s.my_count] = r;
        s.my_count++;
        if (strcmp(r.score, "GGGGG") == 0) {
            s.my_solved = true;
        }
        render_board();
    }

    if (s.my_solved) {
        printf("you solved it in %d!\n", s.my_count);
    } else {
        printf("out of guesses!\n");
    }
    if (!opp_done()) {
        printf("waiting for your opponent to finish...\n");
        while (!opp_done()) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    // Both finished: exchange answers and declare the result.
    char msg[32];
    snprintf(msg, sizeof(msg), "REVEAL %s", s.secret);
    link_send_line(msg);
    wait_flag(&s.opp_revealed, "");

    int my_score  = s.my_solved  ? s.my_count  : MAX_GUESSES + 1;
    int opp_score = s.opp_solved ? s.opp_count : MAX_GUESSES + 1;
    printf("\n=== game over ===\n");
    printf("the word you were guessing: %s\n", s.opp_secret);
    printf("you:      %s\n", s.my_solved  ? "solved" : "not solved");
    printf("opponent: %s\n", s.opp_solved ? "solved" : "not solved");
    if (my_score < opp_score) {
        printf(">>> YOU WIN! <<<\n");
    } else if (my_score > opp_score) {
        printf(">>> you lose :( <<<\n");
    } else {
        printf(">>> it's a draw <<<\n");
    }
}

void game_run(void)
{
    s_result_q = xQueueCreate(4, sizeof(guess_row_t));

    while (true) {
        printf("\n===== ESP-Wordle =====\n"
               "  1) single player\n"
               "  2) two-player duel (needs the other board)\n"
               "mode: ");
        fflush(stdout);
        char buf[8];
        read_line(buf, sizeof(buf));

        if (buf[0] == '1') {
            play_single();
        } else if (buf[0] == '2') {
            if (!link_connected()) {
                printf("waiting for the other board...\n");
                while (!link_connected()) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
            }
            // Reset before READY so nothing sent by the peer afterwards is
            // lost; the peer only sends WORD_SET once it has seen our READY.
            reset_round();
            link_send_line("READY");
            wait_flag(&s.peer_ready, "waiting for opponent to pick duel mode...");
            s.peer_ready = false;
            play_round();
        } else {
            printf("  pick 1 or 2\n");
        }
    }
}
