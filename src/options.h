#ifndef LAVA_OPTIONS_H
#define LAVA_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>

#include "util.h"

typedef enum {
  SIM_LAVA,
  SIM_FIRE,
} SimKind;

typedef enum {
  MODE_LOCK,
  MODE_PREVIEW,
  MODE_SHOT,
  MODE_SAVER,
} Mode;

typedef struct {
  Mode mode;
  SimKind sim;

  bool base_set;
  int base_w, base_h;
  bool core_set;
  RGB core;
  int blobs;
  int fps;

  const char *user;
  const char *pam_service;
  bool no_auth;
  double unlock_after;

  bool verbose;
  bool fork;

  int preview_w, preview_h;
  const char *shot_path;
  unsigned long window_id; /* --saver target; 0 = $XSCREENSAVER_WINDOW */
  int frames;
  int font_scale;

  bool show_help;
  bool show_version;
} Options;

void options_init(Options *opt);

/* Returns NULL on success, or a static error string. */
const char *options_parse(Options *opt, int argc, char **argv);

void options_usage(FILE *out);
void options_version(FILE *out);

#endif /* LAVA_OPTIONS_H */
