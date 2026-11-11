#ifndef LAVA_LOCK_H
#define LAVA_LOCK_H

#include "config.h"

int lava_run_lock(const Config *cfg);
int lava_run_preview(const Config *cfg);
int lava_run_shot(const Config *cfg);
int lava_run_saver(const Config *cfg);

#endif /* LAVA_LOCK_H */
