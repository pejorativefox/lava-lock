#ifndef LAVA_RENDER_H
#define LAVA_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "field.h"
#include "sims.h"
#include "util.h"

/* What the cached field overlay was drawn from.  The password itself is never
 * stored: only its length in code points matters to the drawing. */
typedef struct {
  char username[256];
  char message[256];
  char placeholder[64];
  RGB message_color;
  size_t password_chars;
  int font_scale;
  SDL_Rect primary;
} FieldKey;

/* Two backends share the same simulation upload and field cache:
 *
 * - GPU (`gpu != NULL`, lock and preview): the low-resolution simulation is
 *   streamed into a base_w x base_h texture and the GPU scales it to the
 *   window; the field overlay is a small texture re-uploaded only when its
 *   content changes.  The CPU never touches a full-screen buffer.
 *
 * - Software (`gpu == NULL`, --shot): the same images are composed into a
 *   full-size surface that can be written to a BMP. */
typedef struct {
  Sim *sim;
  Palette palette;
  uint32_t pal32[256]; /* ARGB8888 */
  int base_w, base_h;
  int frame_w, frame_h;
  SDL_Rect primary;
  int font_scale;

  SDL_Renderer *gpu;
  SDL_Texture *anim_tex;
  SDL_Texture *field_tex;

  SDL_Surface *anim;  /* software only */
  SDL_Surface *frame; /* software only */

  SDL_Surface *field_surf;
  SDL_Rect field_rect; /* frame coordinates */
  FieldKey field_key;
  bool field_valid;
} Renderer;

/* `gpu` may be NULL for the software (shot) backend. */
bool render_init(Renderer *r, Sim *sim, RGB core, SDL_Renderer *gpu,
                 int frame_w, int frame_h, SDL_Rect primary, int font_scale);
bool render_resize(Renderer *r, int frame_w, int frame_h);
void render_set_primary(Renderer *r, SDL_Rect primary);
void render_set_font_scale(Renderer *r, int font_scale);

/* Copy the simulation's current frame into the animation texture/surface. */
bool render_upload(Renderer *r);

/* GPU backend: draw the last uploaded animation and the field, then present.
 * Cheap enough to call again (e.g. on expose) without a new upload. */
bool render_present(Renderer *r, const FieldContent *content);

/* Software backend: compose the frame and write it as a BMP. */
bool render_save_bmp(Renderer *r, const FieldContent *content,
                     const char *path);

void render_destroy(Renderer *r);

#endif /* LAVA_RENDER_H */
