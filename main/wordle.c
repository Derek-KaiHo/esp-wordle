#include "wordle.h"

#include <string.h>

#include "wordlist.h"

bool wordle_valid_word(const char *w)
{
    if (strlen(w) != WORD_LEN) {
        return false;
    }
    for (int i = 0; i < WORD_LEN; i++) {
        if (w[i] < 'a' || w[i] > 'z') {
            return false;
        }
    }
    int lo = 0, hi = wordlist_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(w, wordlist[mid]);
        if (cmp == 0) {
            return true;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return false;
}

void wordle_score(const char *secret, const char *guess, char *out)
{
    int spare[26] = {0};
    // Greens first; count the secret's unmatched letters.
    for (int i = 0; i < WORD_LEN; i++) {
        if (guess[i] == secret[i]) {
            out[i] = 'G';
        } else {
            out[i] = '?';
            spare[secret[i] - 'a']++;
        }
    }
    // Yellows limited by remaining occurrences, left to right.
    for (int i = 0; i < WORD_LEN; i++) {
        if (out[i] == '?') {
            int idx = guess[i] - 'a';
            if (spare[idx] > 0) {
                spare[idx]--;
                out[i] = 'Y';
            } else {
                out[i] = 'B';
            }
        }
    }
    out[WORD_LEN] = '\0';
}
