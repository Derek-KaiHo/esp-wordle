#pragma once

#include <stdbool.h>

#define WORD_LEN    5
#define MAX_GUESSES 6

// True if w is exactly 5 lowercase letters and in the word list.
bool wordle_valid_word(const char *w);

// Scores guess against secret into out (WORD_LEN chars + NUL):
// 'G' right letter right spot, 'Y' right letter wrong spot, 'B' absent.
void wordle_score(const char *secret, const char *guess, char *out);
