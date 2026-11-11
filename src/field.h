#ifndef LAVA_FIELD_H
#define LAVA_FIELD_H

#include <stddef.h>

#include <SDL3/SDL.h>

#include "util.h"

typedef struct {
  const char *username;
  const char *message;
  RGB message_color;
  const char *password;
  size_t password_len;
  const char *placeholder;
} FieldContent;

int field_pick_font_scale(int viewport_height, int requested);

/* Draw the Atom One Dark password field inside `viewport` (which is in the
 * destination surface's coordinate space). */
void field_render(SDL_Surface *dst, SDL_Rect viewport,
                  const FieldContent *content, int font_scale);

#endif /* LAVA_FIELD_H */
