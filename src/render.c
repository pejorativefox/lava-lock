#include "render.h"

#include <string.h>

bool render_init(Renderer *r, Sim *sim, RGB core, int frame_w, int frame_h,
                 SDL_Rect primary, int font_scale) {
  memset(r, 0, sizeof(*r));
  r->sim = sim;
  r->frame_w = frame_w;
  r->frame_h = frame_h;
  r->primary = primary;
  r->font_scale = font_scale;

  int base_w = 0;
  int base_h = 0;
  sim_visible(sim, &base_w, &base_h);
  if (base_w <= 0 || base_h <= 0) {
    return false;
  }
  r->base_w = base_w;
  r->base_h = base_h;

  sim_build_palette(sim->ops, core, &r->palette);
  r->anim = SDL_CreateSurface(base_w, base_h, SDL_PIXELFORMAT_ARGB8888);
  r->frame = SDL_CreateSurface(frame_w, frame_h, SDL_PIXELFORMAT_ARGB8888);
  if (r->anim == NULL || r->frame == NULL) {
    render_destroy(r);
    return false;
  }
  SDL_SetSurfaceBlendMode(r->anim, SDL_BLENDMODE_NONE);

  for (int i = 0; i < 256; i++) {
    r->pal32[i] = SDL_MapSurfaceRGBA(r->anim, r->palette.c[i][0],
                                     r->palette.c[i][1], r->palette.c[i][2],
                                     255);
  }
  return true;
}

bool render_resize(Renderer *r, int frame_w, int frame_h) {
  if (frame_w <= 0 || frame_h <= 0) {
    return false;
  }
  if (r->frame != NULL && frame_w == r->frame_w && frame_h == r->frame_h) {
    return true;
  }
  SDL_Surface *frame = SDL_CreateSurface(frame_w, frame_h,
                                         SDL_PIXELFORMAT_ARGB8888);
  if (frame == NULL) {
    return false;
  }
  if (r->frame != NULL) {
    SDL_DestroySurface(r->frame);
  }
  r->frame = frame;
  r->frame_w = frame_w;
  r->frame_h = frame_h;
  return true;
}

void render_set_primary(Renderer *r, SDL_Rect primary) {
  r->primary = primary;
}

void render_set_font_scale(Renderer *r, int font_scale) {
  r->font_scale = font_scale;
}

bool render_draw(Renderer *r, const FieldContent *content) {
  if (r->sim == NULL || r->anim == NULL || r->frame == NULL) {
    return false;
  }

  int vw = 0;
  int vh = 0;
  const uint8_t *visible = sim_visible(r->sim, &vw, &vh);
  int copy_w = vw < r->base_w ? vw : r->base_w;
  int copy_h = vh < r->base_h ? vh : r->base_h;

  if (SDL_MUSTLOCK(r->anim)) {
    SDL_LockSurface(r->anim);
  }
  uint32_t *pixels = r->anim->pixels;
  int pitch = r->anim->pitch / (int)sizeof(uint32_t);
  for (int y = 0; y < copy_h; y++) {
    for (int x = 0; x < copy_w; x++) {
      uint8_t value = visible[(size_t)y * (size_t)vw + (size_t)x];
      pixels[(size_t)y * (size_t)pitch + (size_t)x] = r->pal32[value];
    }
  }
  if (SDL_MUSTLOCK(r->anim)) {
    SDL_UnlockSurface(r->anim);
  }

  if (!SDL_BlitSurfaceScaled(r->anim, NULL, r->frame, NULL,
                             SDL_SCALEMODE_NEAREST)) {
    return false;
  }

  field_render(r->frame, r->primary, content, r->font_scale);
  return true;
}

bool render_present(Renderer *r, SDL_Window *window) {
  if (r->frame == NULL || window == NULL) {
    return false;
  }
  SDL_Surface *window_surface = SDL_GetWindowSurface(window);
  if (window_surface == NULL) {
    return false;
  }
  if (!SDL_BlitSurface(r->frame, NULL, window_surface, NULL)) {
    return false;
  }
  return SDL_UpdateWindowSurface(window);
}

bool render_save_bmp(Renderer *r, const char *path) {
  if (r->frame == NULL || path == NULL) {
    return false;
  }
  return SDL_SaveBMP(r->frame, path);
}

void render_destroy(Renderer *r) {
  if (r->anim != NULL) {
    SDL_DestroySurface(r->anim);
    r->anim = NULL;
  }
  if (r->frame != NULL) {
    SDL_DestroySurface(r->frame);
    r->frame = NULL;
  }
}
