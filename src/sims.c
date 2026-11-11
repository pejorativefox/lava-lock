#include "sims.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_CORE ((RGB){0xB6, 0xD6, 0xF2})

typedef struct {
  float pos;
  RGB rgb;
} Stop;

static uint8_t clamp_byte(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 255.0f) {
    return 255;
  }
  return (uint8_t)(value + 0.5f);
}

/* Linear interpolation over (position, colour) stops.  The last stop's colour
 * is replaced by `core`, matching the Python _ramp() helper. */
static void interp_stops(const Stop *stops, int n, RGB core, int count,
                         uint8_t out[][3]) {
  for (int i = 0; i < count; i++) {
    float t = (count > 1) ? (float)i / (float)(count - 1) : 0.0f;
    int seg = 0;
    while (seg < n - 2 && t > stops[seg + 1].pos) {
      seg++;
    }
    float p0 = stops[seg].pos;
    float p1 = stops[seg + 1].pos;
    float u = (p1 > p0) ? (t - p0) / (p1 - p0) : 0.0f;
    if (u < 0.0f) {
      u = 0.0f;
    } else if (u > 1.0f) {
      u = 1.0f;
    }
    RGB c0 = stops[seg].rgb;
    RGB c1 = (seg == n - 2) ? core : stops[seg + 1].rgb;
    out[i][0] = clamp_byte((float)c0.r + ((float)c1.r - (float)c0.r) * u);
    out[i][1] = clamp_byte((float)c0.g + ((float)c1.g - (float)c0.g) * u);
    out[i][2] = clamp_byte((float)c0.b + ((float)c1.b - (float)c0.b) * u);
  }
}

/* ------------------------------------------------------------------------- */
/* Fire: Jare's demoscene fire, ported from animlib/sims.py.                  */
/* ------------------------------------------------------------------------- */

typedef struct {
  int w, h;
  int cool_from;
  int32_t *src;
  int32_t *temp;
  uint8_t *vis;
} Fire;

static bool fire_init(Sim *sim, int w, int h, int blobs) {
  (void)blobs;
  if (w < 3 || h < 8) {
    return false;
  }
  Fire *fire = calloc(1, sizeof(*fire));
  if (fire == NULL) {
    return false;
  }
  fire->w = w;
  fire->h = h;
  fire->cool_from = w * (h - 4);
  fire->src = calloc((size_t)w * (size_t)h, sizeof(int32_t));
  fire->temp = calloc((size_t)w * (size_t)h, sizeof(int32_t));
  fire->vis = calloc((size_t)w * (size_t)(h - 2), 1);
  if (fire->src == NULL || fire->temp == NULL || fire->vis == NULL) {
    free(fire->src);
    free(fire->temp);
    free(fire->vis);
    free(fire);
    return false;
  }
  sim->state = fire;
  return true;
}

static void fire_tick(Sim *sim) {
  Fire *fire = sim->state;
  const int w = fire->w;
  const int h = fire->h;
  int32_t *src = fire->src;
  int32_t *temp = fire->temp;

  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      int idx = y * w + x;
      int v = src[idx - 1 - w] + src[idx - w] + src[idx + 1 - w] +
              src[idx - 1] + src[idx + 1] +
              src[idx - 1 + w] + src[idx + w] + src[idx + 1 + w];
      int finalv = v >> 3;
      bool cool = ((v & 3) == 0) &&
                  ((idx >= fire->cool_from) || (finalv > 0));
      temp[idx] = cool ? ((finalv - 1) & 0xFF) : finalv;
    }
  }

  /* Scroll up one row; src and temp are separate allocations. */
  memcpy(src, temp + w, (size_t)w * (size_t)(h - 2) * sizeof(int32_t));

  /* Smooth the low values in the bottom rows (rendering nicety). */
  for (int i = w * (h - 7); i < w * (h - 1); i++) {
    if (temp[i] < 15) {
      temp[i] = (256 - temp[i] + 22) % 256;
    }
  }

  for (int y = 1; y < h - 1; y++) {
    for (int x = 0; x < w; x++) {
      fire->vis[(size_t)(y - 1) * (size_t)w + (size_t)x] =
          (uint8_t)temp[(size_t)y * (size_t)w + (size_t)x];
    }
  }
}

static const uint8_t *fire_visible(Sim *sim, int *w, int *h) {
  Fire *fire = sim->state;
  *w = fire->w;
  *h = fire->h - 2;
  return fire->vis;
}

static void fire_destroy(Sim *sim) {
  Fire *fire = sim->state;
  if (fire != NULL) {
    free(fire->src);
    free(fire->temp);
    free(fire->vis);
    free(fire);
  }
  sim->state = NULL;
}

static void fire_palette(RGB core, Palette *out) {
  static const Stop stops[] = {
    {0.00f, {0x28, 0x2C, 0x34}},
    {0.15f, {0x1A, 0x2B, 0x40}},
    {0.35f, {0x27, 0x52, 0x7F}},
    {0.55f, {0x3D, 0x74, 0xB4}},
    {0.75f, {0x61, 0xAF, 0xEF}},
    {1.00f, {0xB6, 0xD6, 0xF2}},
  };
  interp_stops(stops, 6, core, 64, out->c);
  for (int i = 64; i < 256; i++) {
    out->c[i][0] = core.r;
    out->c[i][1] = core.g;
    out->c[i][2] = core.b;
  }
}

static const SimOps fire_ops = {
  .name = "fire",
  .base_w = 80,
  .base_h = 50,
  .prewarm = 150,
  .init = fire_init,
  .tick = fire_tick,
  .visible = fire_visible,
  .destroy = fire_destroy,
  .palette = fire_palette,
};

/* ------------------------------------------------------------------------- */
/* Plasma: the metaball lava lamp, ported from animlib/sims.py.               */
/* ------------------------------------------------------------------------- */

typedef struct {
  int w, h, n, t;
  float *px, *py, *r, *stretch, *tilt, *rise, *drift, *phase, *pulse;
  float *cos_t, *sin_t, *rax, *ray;
  float *row;      /* per-row field accumulator, w floats */
  float *bg_x;     /* per-column background term, w floats */
  int *active;     /* blobs touching the current row, n ints */
  uint8_t *vis;
  uint64_t rng;
} Plasma;

static uint64_t rng_next(Plasma *p) {
  uint64_t x = p->rng;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  p->rng = x;
  return x * UINT64_C(2685821657736338717);
}

static float rng_uniform(Plasma *p, float lo, float hi) {
  double unit = (double)(rng_next(p) >> 40) / (double)(UINT64_C(1) << 24);
  return lo + (hi - lo) * (float)unit;
}

static bool plasma_init(Sim *sim, int w, int h, int blobs) {
  if (w < 1 || h < 1) {
    return false;
  }
  int n = blobs;
  if (n < 1) {
    n = 1;
  }
  Plasma *p = calloc(1, sizeof(*p));
  if (p == NULL) {
    return false;
  }
  p->w = w;
  p->h = h;
  p->n = n;
  p->rng = 7;
  p->px = calloc((size_t)n, sizeof(float));
  p->py = calloc((size_t)n, sizeof(float));
  p->r = calloc((size_t)n, sizeof(float));
  p->stretch = calloc((size_t)n, sizeof(float));
  p->tilt = calloc((size_t)n, sizeof(float));
  p->rise = calloc((size_t)n, sizeof(float));
  p->drift = calloc((size_t)n, sizeof(float));
  p->phase = calloc((size_t)n, sizeof(float));
  p->pulse = calloc((size_t)n, sizeof(float));
  p->cos_t = calloc((size_t)n, sizeof(float));
  p->sin_t = calloc((size_t)n, sizeof(float));
  p->rax = calloc((size_t)n, sizeof(float));
  p->ray = calloc((size_t)n, sizeof(float));
  p->vis = calloc((size_t)w * (size_t)h, 1);
  p->row = calloc((size_t)w, sizeof(float));
  p->bg_x = calloc((size_t)w, sizeof(float));
  p->active = calloc((size_t)n, sizeof(int));
  if (p->row == NULL || p->bg_x == NULL || p->active == NULL ||
      p->px == NULL || p->py == NULL || p->r == NULL || p->stretch == NULL ||
      p->tilt == NULL || p->rise == NULL || p->drift == NULL ||
      p->phase == NULL || p->pulse == NULL || p->cos_t == NULL ||
      p->sin_t == NULL || p->rax == NULL || p->ray == NULL ||
      p->vis == NULL) {
    free(p->px); free(p->py); free(p->r); free(p->stretch); free(p->tilt);
    free(p->rise); free(p->drift); free(p->phase); free(p->pulse);
    free(p->cos_t); free(p->sin_t); free(p->rax); free(p->ray); free(p->vis);
    free(p->row); free(p->bg_x); free(p->active);
    free(p);
    return false;
  }

  for (int i = 0; i < n; i++) {
    p->px[i] = rng_uniform(p, 0.10f * (float)w, 0.90f * (float)w);
    p->py[i] = rng_uniform(p, 0.0f, (float)h);
    p->r[i] = rng_uniform(p, 0.20f, 0.40f) * (float)h;
    p->stretch[i] = rng_uniform(p, 1.0f, 1.6f);
    p->tilt[i] = rng_uniform(p, -0.5f, 0.5f);
    p->rise[i] = rng_uniform(p, 0.05f, 0.14f);
    p->drift[i] = rng_uniform(p, -0.12f, 0.12f);
    p->phase[i] = rng_uniform(p, 0.0f, 2.0f * (float)M_PI);
    p->pulse[i] = rng_uniform(p, 0.006f, 0.016f);
    /* The tilt never changes, so its rotation is computed once. */
    p->cos_t[i] = cosf(p->tilt[i]);
    p->sin_t[i] = sinf(p->tilt[i]);
  }
  sim->state = p;
  return true;
}

static void plasma_tick(Sim *sim) {
  Plasma *p = sim->state;
  const int w = p->w;
  const int h = p->h;
  const int n = p->n;
  const int t = ++p->t;

  for (int i = 0; i < n; i++) {
    p->py[i] -= p->rise[i];
    float wobble = 0.25f * sinf((float)t * 0.008f + p->phase[i]);
    p->px[i] += p->drift[i] + wobble;

    float margin = p->r[i] * 0.3f;
    float lo = margin;
    float hi = (float)w - margin;
    if (lo > hi) {
      lo = hi = 0.5f * (lo + hi);
    }
    if (p->px[i] < lo) {
      p->drift[i] = fabsf(p->drift[i]);
    }
    if (p->px[i] > hi) {
      p->drift[i] = -fabsf(p->drift[i]);
    }
    if (p->px[i] < lo) {
      p->px[i] = lo;
    } else if (p->px[i] > hi) {
      p->px[i] = hi;
    }

    if (p->py[i] < -p->r[i]) {
      p->py[i] = (float)h + p->r[i] * 1.2f + 0.15f * (float)h;
      p->px[i] = rng_uniform(p, 0.15f * (float)w, 0.85f * (float)w);
    }

    float rr = p->r[i] *
               (1.0f + 0.14f * sinf((float)t * p->pulse[i] + p->phase[i]));
    p->rax[i] = 0.80f * rr / p->stretch[i];
    p->ray[i] = rr;
  }

  /* The background term sin(a(x)) * sin(b(y)) is separable: one sinf per
   * column and one per row instead of two per pixel. */
  for (int x = 0; x < w; x++) {
    p->bg_x[x] = sinf((float)x * 0.03f + (float)t * 0.005f);
  }

  /* A blob only contributes where q > 0, i.e. inside its ellipse, which lies
   * within max(rax, ray) of its centre whatever the tilt.  Each blob is
   * therefore only evaluated over its bounding box.  Contributions are still
   * added per pixel in blob order, so the result matches the brute-force sum
   * bit for bit. */
  float *row = p->row;
  for (int y = 0; y < h; y++) {
    float fy = (float)y;
    int n_active = 0;
    for (int i = 0; i < n; i++) {
      float reach = fmaxf(p->rax[i], p->ray[i]) + 1.0f;
      if (fabsf(fy - p->py[i]) < reach) {
        p->active[n_active++] = i;
      }
    }

    memset(row, 0, (size_t)w * sizeof(float));
    for (int k = 0; k < n_active; k++) {
      int i = p->active[k];
      float reach = fmaxf(p->rax[i], p->ray[i]) + 1.0f;
      int x0 = (int)floorf(p->px[i] - reach);
      int x1 = (int)ceilf(p->px[i] + reach);
      if (x0 < 0) {
        x0 = 0;
      }
      if (x1 > w - 1) {
        x1 = w - 1;
      }
      const float cx = p->px[i];
      const float dy = fy - p->py[i];
      const float c = p->cos_t[i];
      const float sn = p->sin_t[i];
      const float rax = p->rax[i];
      const float ray = p->ray[i];
      for (int x = x0; x <= x1; x++) {
        float dx = (float)x - cx;
        float ux = dx * c + dy * sn;
        float uy = -dx * sn + dy * c;
        float qx = ux / rax;
        float qy = uy / ray;
        float q = 1.0f - qx * qx - qy * qy;
        if (q > 0.0f) {
          if (q > 1.0f) {
            q = 1.0f;
          }
          row[x] += q * q * q;
        }
      }
    }

    float bg_y = sinf(fy * 0.04f - (float)t * 0.007f);
    uint8_t *out = p->vis + (size_t)y * (size_t)w;
    for (int x = 0; x < w; x++) {
      float bg = p->bg_x[x] * bg_y;
      float field = row[x] + 0.05f * (0.5f + 0.5f * bg);
      float s = (field - 0.10f) / 0.55f;
      if (s < 0.0f) {
        s = 0.0f;
      } else if (s > 1.0f) {
        s = 1.0f;
      }
      out[x] = clamp_byte(255.0f * (s * s * (3.0f - 2.0f * s)));
    }
  }
}

static const uint8_t *plasma_visible(Sim *sim, int *w, int *h) {
  Plasma *p = sim->state;
  *w = p->w;
  *h = p->h;
  return p->vis;
}

static void plasma_destroy(Sim *sim) {
  Plasma *p = sim->state;
  if (p != NULL) {
    free(p->px); free(p->py); free(p->r); free(p->stretch); free(p->tilt);
    free(p->rise); free(p->drift); free(p->phase); free(p->pulse);
    free(p->cos_t); free(p->sin_t); free(p->rax); free(p->ray); free(p->vis);
    free(p->row); free(p->bg_x); free(p->active);
    free(p);
  }
  sim->state = NULL;
}

static void plasma_palette(RGB core, Palette *out) {
  static const Stop stops[] = {
    {0.00f, {0x21, 0x25, 0x2B}},
    {0.30f, {0x28, 0x2C, 0x34}},
    {0.50f, {0x4B, 0x52, 0x63}},
    {0.64f, {0xC6, 0x78, 0xDD}},
    {0.80f, {0x61, 0xAF, 0xEF}},
    {0.92f, {0x56, 0xB6, 0xC2}},
    {1.00f, {0xB6, 0xD6, 0xF2}},
  };
  interp_stops(stops, 7, core, 256, out->c);
}

static const SimOps plasma_ops = {
  .name = "lava",
  .base_w = 160,
  .base_h = 100,
  .prewarm = 90,
  .init = plasma_init,
  .tick = plasma_tick,
  .visible = plasma_visible,
  .destroy = plasma_destroy,
  .palette = plasma_palette,
};

/* ------------------------------------------------------------------------- */
/* Shared entry points.                                                       */
/* ------------------------------------------------------------------------- */

const SimOps *sim_ops_lava(void) {
  return &plasma_ops;
}

const SimOps *sim_ops_fire(void) {
  return &fire_ops;
}

Sim *sim_create(const SimOps *ops, int w, int h, int blobs) {
  if (ops == NULL) {
    return NULL;
  }
  Sim *sim = calloc(1, sizeof(*sim));
  if (sim == NULL) {
    return NULL;
  }
  sim->ops = ops;
  sim->w = w;
  sim->h = h;
  if (!ops->init(sim, w, h, blobs)) {
    free(sim);
    return NULL;
  }
  return sim;
}

void sim_destroy(Sim *sim) {
  if (sim != NULL) {
    sim->ops->destroy(sim);
    free(sim);
  }
}

void sim_tick(Sim *sim) {
  sim->ops->tick(sim);
}

const uint8_t *sim_visible(Sim *sim, int *w, int *h) {
  return sim->ops->visible(sim, w, h);
}

void sim_build_palette(const SimOps *ops, RGB core, Palette *out) {
  ops->palette(core, out);
}

RGB sim_default_core(void) {
  return DEFAULT_CORE;
}
