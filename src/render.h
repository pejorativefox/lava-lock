#ifndef LAVA_RENDER_H
#define LAVA_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "field.h"
#include "sims.h"
#include "util.h"

typedef struct {
  Sim *sim;
  Palette palette;
  uint32_t pal32[256];
  SDL_Surface *anim;
  SDL_Surface *frame;
  int base_w, base_h;
  int frame_w, frame_h;
  SDL_Rect primary;
  int font_scale;
} Renderer;

bool render_init(Renderer *r, Sim *sim, RGB core, int frame_w, int frame_h,
                 SDL_Rect primary, int font_scale);
bool render_resize(Renderer *r, int frame_w, int frame_h);
void render_set_primary(Renderer *r, SDL_Rect primary);
void render_set_font_scale(Renderer *r, int font_scale);
bool render_draw(Renderer *r, const FieldContent *content);
bool render_present(Renderer *r, SDL_Window *window);
bool render_save_bmp(Renderer *r, const char *path);
void render_destroy(Renderer *r);

#endif /* LAVA_RENDER_H */
