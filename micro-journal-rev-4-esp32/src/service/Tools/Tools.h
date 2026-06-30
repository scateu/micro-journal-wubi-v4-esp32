#pragma once

#include <Arduino.h>

String formatNumber(int num);
size_t fileSize(String fileName);
String asciiToUnicode(uint8_t value);
String format(const char *format, ...);

//
// UTF-8 helpers
//
// Number of bytes in the UTF-8 character that starts with `lead`.
// Returns 1 for ASCII / invalid lead bytes so callers always advance.
int utf8_char_len(uint8_t lead);

// True when `b` is a UTF-8 continuation byte (10xxxxxx).
bool utf8_is_continuation(uint8_t b);

// Decode the UTF-8 character beginning at `s`. Writes the consumed byte count
// into `len` (always >= 1) and returns the Unicode codepoint. Treats malformed
// sequences as a single Latin-1 byte so editing never gets stuck.
uint32_t utf8_decode(const char *s, int &len);


#if defined(DEBUG) && defined(BOARD_PICO)
void printMemoryUsage();
#endif
