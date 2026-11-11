#include "font.h"

#include <stdint.h>
#include <string.h>

#include "font_data.h"

int font_scale_for_height(int screen_height, int field_height, int requested) {
  if (requested > 0) {
    int scale = requested > 8 ? 8 : requested;
    /* Keep an explicit scale from spilling out of the password field. */
    while (scale > 1 && LAVA_FONT_CELL_H * scale > field_height - 6) {
      scale--;
    }
    return scale;
  }

  int target = screen_height / 52;
  if (target < 18) {
    target = 18;
  }
  int scale = (target + LAVA_FONT_CELL_H / 2) / LAVA_FONT_CELL_H;
  if (scale < 1) {
    scale = 1;
  }
  while (scale > 1 && LAVA_FONT_CELL_H * scale > field_height - 6) {
    scale--;
  }
  if (scale > 6) {
    scale = 6;
  }
  return scale;
}

int font_text_height(int scale) {
  return LAVA_FONT_CELL_H * scale;
}

int font_text_width(const char *text, size_t len, int scale) {
  if (text == NULL) {
    return 0;
  }
  return (int)(util_utf8_count(text, len) * (size_t)LAVA_FONT_CELL_W *
               (size_t)scale);
}

int font_text_width_cstr(const char *text, int scale) {
  return text == NULL ? 0 : font_text_width(text, strlen(text), scale);
}

static uint32_t decode_utf8(const char *text, size_t len, size_t *index) {
  size_t i = *index;
  unsigned char b = (unsigned char)text[i];
  uint32_t cp;
  size_t advance;

  if (b < 0x80) {
    cp = b;
    advance = 1;
  } else if ((b & 0xE0) == 0xC0 && i + 1 < len) {
    cp = ((uint32_t)(b & 0x1F) << 6) | ((unsigned char)text[i + 1] & 0x3F);
    advance = 2;
  } else if ((b & 0xF0) == 0xE0 && i + 2 < len) {
    cp = ((uint32_t)(b & 0x0F) << 12) |
         (((unsigned char)text[i + 1] & 0x3F) << 6) |
         ((unsigned char)text[i + 2] & 0x3F);
    advance = 3;
  } else if ((b & 0xF8) == 0xF0 && i + 3 < len) {
    cp = ((uint32_t)(b & 0x07) << 18) |
         (((unsigned char)text[i + 1] & 0x3F) << 12) |
         (((unsigned char)text[i + 2] & 0x3F) << 6) |
         ((unsigned char)text[i + 3] & 0x3F);
    advance = 4;
  } else {
    cp = '?';
    advance = 1;
  }

  *index = i + advance;
  return cp;
}

void font_draw(SDL_Surface *dst, int x, int y, const char *text, size_t len,
               RGB color, int scale) {
  if (dst == NULL || text == NULL || len == 0 || scale <= 0) {
    return;
  }

  int pen = x;
  size_t i = 0;
  while (i < len && pen < dst->w) {
    uint32_t cp = decode_utf8(text, len, &i);
    if (cp < 32 || cp > 126) {
      cp = '?';
    }
    const uint8_t *glyph = lava_font_ascii[cp - 32];
    for (int row = 0; row < LAVA_FONT_CELL_H; row++) {
      uint8_t bits = glyph[row];
      if (bits == 0) {
        continue;
      }
      for (int col = 0; col < LAVA_FONT_CELL_W; col++) {
        if ((bits & (0x80u >> col)) == 0) {
          continue;
        }
        int gx = pen + col * scale;
        int gy = y + row * scale;
        if (gx < 0 || gy < 0 || gx + scale > dst->w || gy + scale > dst->h) {
          continue;
        }
        for (int dy = 0; dy < scale; dy++) {
          for (int dx = 0; dx < scale; dx++) {
            SDL_WriteSurfacePixel(dst, gx + dx, gy + dy, color.r, color.g,
                                  color.b, 255);
          }
        }
      }
    }
    pen += LAVA_FONT_CELL_W * scale;
  }
}
