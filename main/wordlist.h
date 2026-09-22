#pragma once

// Full guess list: valid secrets and guesses (sorted for binary search).
extern const char wordlist[][6];
extern const int wordlist_count;

// Curated common-word list: random secrets for single-player mode.
extern const char answerlist[][6];
extern const int answerlist_count;
