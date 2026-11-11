#ifndef LAVA_CONFIG_H
#define LAVA_CONFIG_H

#include "options.h"
#include "sims.h"
#include "util.h"

typedef struct {
  Options opt;
  const SimOps *ops;
  int base_w;
  int base_h;
  RGB core;
  char username[256];
} Config;

#endif /* LAVA_CONFIG_H */
