#include "field.h"

#include <stdio.h>
#include <string.h>

#include "font.h"

/* Atom One Dark accents. */
static const RGB CORE = {0xB6, 0xD6, 0xF2};
static const RGB MUTED = {0x98, 0xA1, 0xB0};
static const RGB FIELD = {0x21, 0x25, 0x2B};      /* alpha 245 below */
static const RGB FIELD_BORDER = {0x3E, 0x44, 0x51};
static const RGB PLACEHOLDER = {0x61, 0xAF, 0xEF};

#define FIELD_ALPHA 245

/* The field is drawn into a transparent ARGB8888 overlay that the caller
 * composites over the animation (SDL_BLENDMODE_BLEND), so pixels are written,
 * not blended: the translucent fill carries its alpha into the overlay. */
static void put_pixel(SDL_Surface *dst, int x, int y, RGB color,
                      uint8_t alpha) {
  if (x < 0 || y < 0 || x >= dst->w || y >= dst->h) {
    return;
  }
  uint32_t *row = (uint32_t *)((uint8_t *)dst->pixels + (size_t)y * (size_t)dst->pitch);
  row[x] = ((uint32_t)alpha << 24) | ((uint32_t)color.r << 16) |
           ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

static bool inside_round_rect(int px, int py, int x, int y, int w, int h,
                              int radius) {
  if (px < x || py < y || px >= x + w || py >= y + h) {
    return false;
  }
  if (radius <= 0) {
    return true;
  }
  int cx = px < x + radius ? x + radius
                           : (px >= x + w - radius ? x + w - 1 - radius : px);
  int cy = py < y + radius ? y + radius
                           : (py >= y + h - radius ? y + h - 1 - radius : py);
  int dx = px - cx;
  int dy = py - cy;
  return dx * dx + dy * dy <= radius * radius;
}

static void fill_round_rect(SDL_Surface *dst, int x, int y, int w, int h,
                            int radius, RGB color, uint8_t alpha) {
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      if (inside_round_rect(px, py, x, y, w, h, radius)) {
        put_pixel(dst, px, py, color, alpha);
      }
    }
  }
}

static void border_round_rect(SDL_Surface *dst, int x, int y, int w, int h,
                              int radius, RGB color) {
  int inner_r = radius > 1 ? radius - 1 : 0;
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      if (!inside_round_rect(px, py, x, y, w, h, radius)) {
        continue;
      }
      if (inside_round_rect(px, py, x + 1, y + 1, w - 2, h - 2, inner_r)) {
        continue;
      }
      put_pixel(dst, px, py, color, 255);
    }
  }
}

static void fill_circle(SDL_Surface *dst, int cx, int cy, int radius,
                        RGB color) {
  int r2 = radius * radius;
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= r2) {
        put_pixel(dst, cx + dx, cy + dy, color, 255);
      }
    }
  }
}

static int field_height(int viewport_height) {
  int fh = viewport_height / 13;
  if (fh > 84) {
    fh = 84;
  }
  if (fh < 56) {
    fh = 56;
  }
  return fh / 2;
}

int field_pick_font_scale(int viewport_height, int requested) {
  return font_scale_for_height(viewport_height, field_height(viewport_height),
                               requested);
}

static void grow(SDL_Rect *b, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  if (b->w <= 0 || b->h <= 0) {
    *b = (SDL_Rect){x, y, w, h};
    return;
  }
  int x1 = b->x + b->w > x + w ? b->x + b->w : x + w;
  int y1 = b->y + b->h > y + h ? b->y + b->h : y + h;
  b->x = b->x < x ? b->x : x;
  b->y = b->y < y ? b->y : y;
  b->w = x1 - b->x;
  b->h = y1 - b->y;
}

static void draw_text(SDL_Surface *dst, SDL_Rect *bounds, int x, int y,
                      const char *text, RGB color, int scale) {
  size_t len = strlen(text);
  grow(bounds, x, y, font_text_width(text, len, scale), font_text_height(scale));
  if (dst != NULL) {
    font_draw(dst, x, y, text, len, color, scale);
  }
}

void field_render(SDL_Surface *dst, SDL_Rect viewport,
                  const FieldContent *content, int font_scale,
                  SDL_Rect *bounds_out) {
  SDL_Rect bounds = {0, 0, 0, 0};
  if (bounds_out != NULL) {
    *bounds_out = bounds;
  }
  if (content == NULL) {
    return;
  }
  const int vw = viewport.w;
  const int vh = viewport.h;
  if (vw <= 0 || vh <= 0 || font_scale <= 0) {
    return;
  }

  int fw = (int)((double)vw * 0.42);
  if (fw > 560) {
    fw = 560;
  }
  fw = fw * 3 / 5;
  if (fw < 40) {
    fw = 40;
  }

  const int fh = field_height(vh);
  const int radius = fh / 5 < 8 ? 8 : fh / 5;
  const int pad = fh / 3 < 12 ? 12 : fh / 3;
  const int font_h = font_text_height(font_scale);
  const int fy = (vh - fh) / 2 + (int)((double)vh * 0.40);

  bool has_msg = content->message != NULL && content->message[0] != '\0';
  bool has_cap = content->username != NULL && content->username[0] != '\0';
  int msg_w = has_msg ? font_text_width_cstr(content->message, font_scale) : 0;
  int cap_w = has_cap ? font_text_width_cstr(content->username, font_scale) : 0;

  int sw = fw;
  if (msg_w + 24 > sw) {
    sw = msg_w + 24;
  }
  if (cap_w + 24 > sw) {
    sw = cap_w + 24;
  }

  const int x0 = viewport.x + (vw - sw) / 2;
  const int y0 = viewport.y + fy;
  const int fx = x0 + (sw - fw) / 2;

  grow(&bounds, fx, y0, fw, fh);
  if (dst != NULL) {
    fill_round_rect(dst, fx, y0, fw, fh, radius, FIELD, FIELD_ALPHA);
    border_round_rect(dst, fx, y0, fw, fh, radius, FIELD_BORDER);
  }

  const int text_x = fx + pad;
  size_t chars = util_utf8_count(content->password, content->password_len);
  if (content->password != NULL && content->password_len > 0 && chars > 0) {
    int spacing = font_h - 2;
    if (spacing < 14) {
      spacing = 14;
    }
    int max_dots = (fw - pad - 12) / spacing;
    if (max_dots < 1) {
      max_dots = 1;
    }
    int shown = chars > (size_t)max_dots ? max_dots : (int)chars;
    int dot_y = y0 + fh / 2;
    int dot_r = fh / 13;
    if (dot_r < 4) {
      dot_r = 4;
    }
    for (int i = 0; i < shown; i++) {
      int cx = text_x + spacing / 2 + i * spacing;
      grow(&bounds, cx - dot_r, dot_y - dot_r, 2 * dot_r + 1, 2 * dot_r + 1);
      if (dst != NULL) {
        fill_circle(dst, cx, dot_y, dot_r, CORE);
      }
    }
    if (chars > (size_t)max_dots) {
      char extra[32];
      snprintf(extra, sizeof(extra), "+%d", (int)(chars - (size_t)max_dots));
      draw_text(dst, &bounds, text_x + shown * spacing, dot_y - font_h / 2,
                extra, MUTED, font_scale);
    }
  } else {
    const char *placeholder = content->placeholder;
    if (placeholder == NULL || placeholder[0] == '\0') {
      placeholder = "Password";
    }
    draw_text(dst, &bounds, text_x, y0 + (fh - font_h) / 2, placeholder,
              PLACEHOLDER, font_scale);
  }

  int ty = y0 + fh + 12;
  if (has_msg) {
    draw_text(dst, &bounds, x0 + (sw - msg_w) / 2, ty, content->message,
              content->message_color, font_scale);
    ty += font_h + 4;
  }
  if (has_cap) {
    draw_text(dst, &bounds, x0 + (sw - cap_w) / 2, ty, content->username,
              MUTED, font_scale);
  }
  if (bounds_out != NULL) {
    *bounds_out = bounds;
  }
}
