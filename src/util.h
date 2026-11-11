#ifndef LAVA_UTIL_H
#define LAVA_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t r, g, b;
} RGB;

/* "1920x1080" -> 1920, 1080.  Returns false on anything else. */
bool util_parse_size(const char *text, int *w, int *h);

/* "RRGGBB" or "#RRGGBB" -> RGB.  Returns false on anything else. */
bool util_parse_color(const char *text, RGB *out);

/* Wipe a buffer in a way the compiler may not optimise away. */
void util_secure_zero(void *ptr, size_t len);

/* Number of UTF-8 code points (not bytes).  Malformed input is counted as
 * best it can be; the string comes from SDL text input, which is valid UTF-8. */
size_t util_utf8_count(const char *text, size_t len);

/* Start index of the code point immediately before `pos` (pos is a byte
 * offset, normally the end of the string). */
size_t util_utf8_prev(const char *text, size_t len, size_t pos);

/* Monotonic seconds, for --unlock-after and logging. */
double util_now(void);

#endif /* LAVA_UTIL_H */
