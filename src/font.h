#ifndef LAVA_FONT_H
#define LAVA_FONT_H

#include <stddef.h>

#include <SDL3/SDL.h>

#include "util.h"

#define LAVA_FONT_CELL_W 8
#define LAVA_FONT_CELL_H 16

/* Pick an integer text scale.  `requested` > 0 overrides the automatic
 * choice.  Automatic scaling follows the original font size (max(18, h/52))
 * but is quantised to the 16px font cell and capped so it fits the field. */
int font_scale_for_height(int screen_height, int field_height, int requested);

int font_text_height(int scale);

/* Width in pixels of a UTF-8 string (all code points advance one cell). */
int font_text_width(const char *text, size_t len, int scale);
int font_text_width_cstr(const char *text, int scale);

/* Draw UTF-8 text into an SDL surface.  Code points outside printable ASCII
 * render as '?', which matches the embedded bitmap font's repertoire. */
void font_draw(SDL_Surface *dst, int x, int y, const char *text, size_t len,
               RGB color, int scale);

#endif /* LAVA_FONT_H */
