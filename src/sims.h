#ifndef LAVA_SIMS_H
#define LAVA_SIMS_H

#include <stdbool.h>
#include <stdint.h>

#include "util.h"

typedef struct {
  uint8_t c[256][3];
} Palette;

typedef struct Sim Sim;
typedef struct SimOps SimOps;

struct SimOps {
  const char *name;
  int base_w;
  int base_h;
  int prewarm;
  bool (*init)(Sim *sim, int w, int h, int blobs);
  void (*tick)(Sim *sim);
  const uint8_t *(*visible)(Sim *sim, int *w, int *h);
  void (*destroy)(Sim *sim);
  void (*palette)(RGB core, Palette *out);
};

struct Sim {
  const SimOps *ops;
  int w;
  int h;
  void *state;
};

const SimOps *sim_ops_lava(void);
const SimOps *sim_ops_fire(void);
RGB sim_default_core(void);

Sim *sim_create(const SimOps *ops, int w, int h, int blobs);
void sim_destroy(Sim *sim);
void sim_tick(Sim *sim);
const uint8_t *sim_visible(Sim *sim, int *w, int *h);
void sim_build_palette(const SimOps *ops, RGB core, Palette *out);

#endif /* LAVA_SIMS_H */
