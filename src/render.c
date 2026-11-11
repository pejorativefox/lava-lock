#include "render.h"

#include <stdio.h>
#include <string.h>

bool render_init(Renderer *r, Sim *sim, RGB core, SDL_Renderer *gpu,
                 int frame_w, int frame_h, SDL_Rect primary, int font_scale) {
  memset(r, 0, sizeof(*r));
  r->sim = sim;
  r->gpu = gpu;
  r->frame_w = frame_w;
  r->frame_h = frame_h;
  r->primary = primary;
  r->font_scale = font_scale;

  int base_w = 0;
  int base_h = 0;
  sim_visible(sim, &base_w, &base_h);
  if (base_w <= 0 || base_h <= 0 || frame_w <= 0 || frame_h <= 0) {
    return false;
  }
  r->base_w = base_w;
  r->base_h = base_h;

  sim_build_palette(sim->ops, core, &r->palette);
  for (int i = 0; i < 256; i++) {
    r->pal32[i] = 0xFF000000u | ((uint32_t)r->palette.c[i][0] << 16) |
                  ((uint32_t)r->palette.c[i][1] << 8) |
                  (uint32_t)r->palette.c[i][2];
  }

  if (gpu != NULL) {
    r->anim_tex = SDL_CreateTexture(gpu, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, base_w,
                                    base_h);
    if (r->anim_tex == NULL) {
      render_destroy(r);
      return false;
    }
    SDL_SetTextureBlendMode(r->anim_tex, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(r->anim_tex, SDL_SCALEMODE_NEAREST);
    return true;
  }

  r->anim = SDL_CreateSurface(base_w, base_h, SDL_PIXELFORMAT_ARGB8888);
  r->frame = SDL_CreateSurface(frame_w, frame_h, SDL_PIXELFORMAT_ARGB8888);
  if (r->anim == NULL || r->frame == NULL) {
    render_destroy(r);
    return false;
  }
  SDL_SetSurfaceBlendMode(r->anim, SDL_BLENDMODE_NONE);
  return true;
}

bool render_resize(Renderer *r, int frame_w, int frame_h) {
  if (frame_w <= 0 || frame_h <= 0) {
    return false;
  }
  if (frame_w == r->frame_w && frame_h == r->frame_h) {
    return true;
  }
  if (r->gpu == NULL) {
    SDL_Surface *frame = SDL_CreateSurface(frame_w, frame_h,
                                           SDL_PIXELFORMAT_ARGB8888);
    if (frame == NULL) {
      return false;
    }
    if (r->frame != NULL) {
      SDL_DestroySurface(r->frame);
    }
    r->frame = frame;
  }
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

static void write_anim(Renderer *r, uint32_t *pixels, int pitch_bytes) {
  int vw = 0;
  int vh = 0;
  const uint8_t *visible = sim_visible(r->sim, &vw, &vh);
  int copy_w = vw < r->base_w ? vw : r->base_w;
  int copy_h = vh < r->base_h ? vh : r->base_h;
  for (int y = 0; y < copy_h; y++) {
    const uint8_t *src = visible + (size_t)y * (size_t)vw;
    uint32_t *dst =
        (uint32_t *)((uint8_t *)pixels + (size_t)y * (size_t)pitch_bytes);
    for (int x = 0; x < copy_w; x++) {
      dst[x] = r->pal32[src[x]];
    }
  }
}

bool render_upload(Renderer *r) {
  if (r->sim == NULL) {
    return false;
  }
  if (r->gpu != NULL) {
    void *pixels = NULL;
    int pitch = 0;
    if (r->anim_tex == NULL ||
        !SDL_LockTexture(r->anim_tex, NULL, &pixels, &pitch)) {
      return false;
    }
    write_anim(r, pixels, pitch);
    SDL_UnlockTexture(r->anim_tex);
    return true;
  }
  if (r->anim == NULL) {
    return false;
  }
  if (SDL_MUSTLOCK(r->anim)) {
    SDL_LockSurface(r->anim);
  }
  write_anim(r, r->anim->pixels, r->anim->pitch);
  if (SDL_MUSTLOCK(r->anim)) {
    SDL_UnlockSurface(r->anim);
  }
  return true;
}

static FieldKey make_key(const Renderer *r, const FieldContent *c) {
  FieldKey key;
  memset(&key, 0, sizeof(key)); /* compared with memcmp */
  snprintf(key.username, sizeof(key.username), "%s",
           c->username != NULL ? c->username : "");
  snprintf(key.message, sizeof(key.message), "%s",
           c->message != NULL ? c->message : "");
  snprintf(key.placeholder, sizeof(key.placeholder), "%s",
           c->placeholder != NULL ? c->placeholder : "");
  key.message_color = c->message_color;
  key.password_chars = c->password != NULL
                           ? util_utf8_count(c->password, c->password_len)
                           : 0;
  key.font_scale = r->font_scale;
  key.primary = r->primary;
  return key;
}

/* Redraw the field overlay only when something it shows has changed. */
static bool update_field(Renderer *r, const FieldContent *content) {
  FieldKey key = make_key(r, content);
  if (r->field_valid && memcmp(&key, &r->field_key, sizeof(key)) == 0) {
    return true;
  }
  r->field_valid = false;

  SDL_Rect bounds;
  field_render(NULL, r->primary, content, r->font_scale, &bounds);
  if (r->field_surf != NULL) {
    SDL_DestroySurface(r->field_surf);
    r->field_surf = NULL;
  }
  if (r->field_tex != NULL) {
    SDL_DestroyTexture(r->field_tex);
    r->field_tex = NULL;
  }
  r->field_rect = bounds;
  if (bounds.w > 0 && bounds.h > 0) {
    r->field_surf =
        SDL_CreateSurface(bounds.w, bounds.h, SDL_PIXELFORMAT_ARGB8888);
    if (r->field_surf == NULL) {
      return false;
    }
    SDL_FillSurfaceRect(r->field_surf, NULL, 0);
    SDL_Rect viewport = r->primary;
    viewport.x -= bounds.x;
    viewport.y -= bounds.y;
    field_render(r->field_surf, viewport, content, r->font_scale, NULL);
    SDL_SetSurfaceBlendMode(r->field_surf, SDL_BLENDMODE_BLEND);

    if (r->gpu != NULL) {
      r->field_tex = SDL_CreateTextureFromSurface(r->gpu, r->field_surf);
      if (r->field_tex == NULL) {
        return false;
      }
      SDL_SetTextureBlendMode(r->field_tex, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(r->field_tex, SDL_SCALEMODE_NEAREST);
      SDL_DestroySurface(r->field_surf);
      r->field_surf = NULL;
    }
  }
  r->field_key = key;
  r->field_valid = true;
  return true;
}

bool render_present(Renderer *r, const FieldContent *content) {
  if (r->gpu == NULL || r->anim_tex == NULL || content == NULL) {
    return false;
  }
  bool ok = update_field(r, content);
  /* No clear: the animation covers the whole output every frame. */
  ok = SDL_RenderTexture(r->gpu, r->anim_tex, NULL, NULL) && ok;
  if (r->field_tex != NULL) {
    SDL_FRect dst = {(float)r->field_rect.x, (float)r->field_rect.y,
                     (float)r->field_rect.w, (float)r->field_rect.h};
    ok = SDL_RenderTexture(r->gpu, r->field_tex, NULL, &dst) && ok;
  }
  return SDL_RenderPresent(r->gpu) && ok;
}

bool render_save_bmp(Renderer *r, const FieldContent *content,
                     const char *path) {
  if (r->gpu != NULL || r->frame == NULL || content == NULL || path == NULL) {
    return false;
  }
  if (!render_upload(r) ||
      !SDL_BlitSurfaceScaled(r->anim, NULL, r->frame, NULL,
                             SDL_SCALEMODE_NEAREST) ||
      !update_field(r, content)) {
    return false;
  }
  if (r->field_surf != NULL) {
    SDL_Rect dst = r->field_rect;
    if (!SDL_BlitSurface(r->field_surf, NULL, r->frame, &dst)) {
      return false;
    }
  }
  return SDL_SaveBMP(r->frame, path);
}

void render_destroy(Renderer *r) {
  if (r->field_tex != NULL) {
    SDL_DestroyTexture(r->field_tex);
    r->field_tex = NULL;
  }
  if (r->anim_tex != NULL) {
    SDL_DestroyTexture(r->anim_tex);
    r->anim_tex = NULL;
  }
  if (r->field_surf != NULL) {
    SDL_DestroySurface(r->field_surf);
    r->field_surf = NULL;
  }
  if (r->anim != NULL) {
    SDL_DestroySurface(r->anim);
    r->anim = NULL;
  }
  if (r->frame != NULL) {
    SDL_DestroySurface(r->frame);
    r->frame = NULL;
  }
  r->field_valid = false;
}
