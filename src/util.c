#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

bool util_parse_size(const char *text, int *w, int *h) {
  char extra;
  int width, height;
  if (text == NULL) {
    return false;
  }
  if (sscanf(text, "%dx%d%c", &width, &height, &extra) != 2) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    return false;
  }
  *w = width;
  *h = height;
  return true;
}

bool util_parse_color(const char *text, RGB *out) {
  if (text == NULL) {
    return false;
  }
  if (*text == '#') {
    text++;
  }
  if (strlen(text) != 6) {
    return false;
  }
  unsigned int value = 0;
  for (int i = 0; i < 6; i++) {
    if (!isxdigit((unsigned char)text[i])) {
      return false;
    }
    value <<= 4;
    if (text[i] >= '0' && text[i] <= '9') {
      value |= (unsigned int)(text[i] - '0');
    } else {
      value |= (unsigned int)(tolower((unsigned char)text[i]) - 'a' + 10);
    }
  }
  out->r = (uint8_t)((value >> 16) & 0xFF);
  out->g = (uint8_t)((value >> 8) & 0xFF);
  out->b = (uint8_t)(value & 0xFF);
  return true;
}

void util_secure_zero(void *ptr, size_t len) {
  if (ptr != NULL && len > 0) {
    explicit_bzero(ptr, len);
  }
}

size_t util_utf8_count(const char *text, size_t len) {
  size_t count = 0;
  for (size_t i = 0; i < len; i++) {
    if (((unsigned char)text[i] & 0xC0) != 0x80) {
      count++;
    }
  }
  return count;
}

size_t util_utf8_prev(const char *text, size_t len, size_t pos) {
  if (pos > len) {
    pos = len;
  }
  if (pos == 0) {
    return 0;
  }
  size_t i = pos;
  while (i > 0 && ((unsigned char)text[i - 1] & 0xC0) == 0x80) {
    i--;
  }
  if (i > 0) {
    i--;
  }
  return i;
}

double util_now(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0.0;
  }
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
