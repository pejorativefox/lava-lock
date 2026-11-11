#include "options.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
  OPT_LAVA = 1000,
  OPT_FIRE,
  OPT_BASE,
  OPT_CORE,
  OPT_BLOBS,
  OPT_FPS,
  OPT_USER,
  OPT_PAM_SERVICE,
  OPT_NO_AUTH,
  OPT_UNLOCK_AFTER,
  OPT_VERBOSE,
  OPT_FORK,
  OPT_PREVIEW,
  OPT_WINDOW,
  OPT_SHOT,
  OPT_FRAMES,
  OPT_FONT_SCALE,
  OPT_HELP,
  OPT_VERSION,
};

static char error_buffer[256];

static const char *set_error(const char *text) {
  snprintf(error_buffer, sizeof(error_buffer), "%s", text);
  return error_buffer;
}

static bool parse_int(const char *text, int min, int max, int *out) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return false;
  }
  if (value < min || value > max) {
    return false;
  }
  *out = (int)value;
  return true;
}

static bool parse_double(const char *text, double min, double max, double *out) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  errno = 0;
  char *end = NULL;
  double value = strtod(text, &end);
  if (errno != 0 || end == text || *end != '\0') {
    return false;
  }
  /* Reject NaN: every comparison with a NaN is false. */
  if (!(value >= min && value <= max)) {
    return false;
  }
  *out = value;
  return true;
}

void options_init(Options *opt) {
  memset(opt, 0, sizeof(*opt));
  opt->mode = MODE_LOCK;
  opt->sim = SIM_LAVA;
  opt->blobs = 9;
  opt->fps = 30;
  opt->pam_service = "login";
  opt->preview_w = 1280;
  opt->preview_h = 720;
  opt->frames = 90;
}

const char *options_parse(Options *opt, int argc, char **argv) {
  bool sim_seen = false;
  bool preview_seen = false;
  bool shot_seen = false;
  bool window_seen = false;
  bool frames_seen = false;

  static const struct option long_options[] = {
    {"lava", no_argument, NULL, OPT_LAVA},
    {"fire", no_argument, NULL, OPT_FIRE},
    {"base", required_argument, NULL, OPT_BASE},
    {"core", required_argument, NULL, OPT_CORE},
    {"blobs", required_argument, NULL, OPT_BLOBS},
    {"fps", required_argument, NULL, OPT_FPS},
    {"user", required_argument, NULL, OPT_USER},
    {"pam-service", required_argument, NULL, OPT_PAM_SERVICE},
    {"no-auth", no_argument, NULL, OPT_NO_AUTH},
    {"unlock-after", required_argument, NULL, OPT_UNLOCK_AFTER},
    {"verbose", no_argument, NULL, OPT_VERBOSE},
    {"fork", no_argument, NULL, OPT_FORK},
    {"preview", no_argument, NULL, OPT_PREVIEW},
    {"window", required_argument, NULL, OPT_WINDOW},
    {"shot", required_argument, NULL, OPT_SHOT},
    {"frames", required_argument, NULL, OPT_FRAMES},
    {"font-scale", required_argument, NULL, OPT_FONT_SCALE},
    {"help", no_argument, NULL, OPT_HELP},
    {"version", no_argument, NULL, OPT_VERSION},
    {NULL, 0, NULL, 0},
  };

  optind = 1;
  opterr = 0;
  for (;;) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hV", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
      case OPT_LAVA:
        if (sim_seen && opt->sim != SIM_LAVA) {
          return set_error("--lava and --fire are mutually exclusive");
        }
        opt->sim = SIM_LAVA;
        sim_seen = true;
        break;
      case OPT_FIRE:
        if (sim_seen && opt->sim != SIM_FIRE) {
          return set_error("--lava and --fire are mutually exclusive");
        }
        opt->sim = SIM_FIRE;
        sim_seen = true;
        break;
      case OPT_BASE:
        if (!util_parse_size(optarg, &opt->base_w, &opt->base_h) ||
            opt->base_w < 16 || opt->base_h < 16 ||
            opt->base_w > 4096 || opt->base_h > 4096) {
          return set_error("--base must be WxH with both between 16 and 4096");
        }
        opt->base_set = true;
        break;
      case OPT_CORE:
        if (!util_parse_color(optarg, &opt->core)) {
          return set_error("--core must be RRGGBB or #RRGGBB");
        }
        opt->core_set = true;
        break;
      case OPT_BLOBS:
        if (!parse_int(optarg, 1, 64, &opt->blobs)) {
          return set_error("--blobs must be between 1 and 64");
        }
        break;
      case OPT_FPS:
        if (!parse_int(optarg, 1, 240, &opt->fps)) {
          return set_error("--fps must be between 1 and 240");
        }
        break;
      case OPT_USER:
        if (optarg[0] == '\0') {
          return set_error("--user must not be empty");
        }
        opt->user = optarg;
        break;
      case OPT_PAM_SERVICE:
        if (optarg[0] == '\0') {
          return set_error("--pam-service must not be empty");
        }
        opt->pam_service = optarg;
        break;
      case OPT_NO_AUTH:
        opt->no_auth = true;
        break;
      case OPT_UNLOCK_AFTER:
        if (!parse_double(optarg, 0.0, 86400.0, &opt->unlock_after)) {
          return set_error("--unlock-after must be a number of seconds");
        }
        break;
      case OPT_VERBOSE:
        opt->verbose = true;
        break;
      case OPT_FORK:
        opt->fork = true;
        break;
      case OPT_PREVIEW:
        if (shot_seen) {
          return set_error("--preview and --shot are mutually exclusive");
        }
        opt->mode = MODE_PREVIEW;
        preview_seen = true;
        break;
      case OPT_WINDOW:
        if (!util_parse_size(optarg, &opt->preview_w, &opt->preview_h) ||
            opt->preview_w < 64 || opt->preview_h < 64 ||
            opt->preview_w > 16384 || opt->preview_h > 16384) {
          return set_error("--window must be WxH with both between 64 and 16384");
        }
        window_seen = true;
        break;
      case OPT_SHOT:
        if (preview_seen) {
          return set_error("--preview and --shot are mutually exclusive");
        }
        opt->mode = MODE_SHOT;
        opt->shot_path = optarg;
        shot_seen = true;
        break;
      case OPT_FRAMES:
        if (!parse_int(optarg, 0, 1000000, &opt->frames)) {
          return set_error("--frames must be a non-negative integer");
        }
        frames_seen = true;
        break;
      case OPT_FONT_SCALE:
        if (!parse_int(optarg, 0, 8, &opt->font_scale)) {
          return set_error("--font-scale must be between 0 (auto) and 8");
        }
        break;
      case OPT_HELP:
      case 'h':
        opt->show_help = true;
        return NULL;
      case OPT_VERSION:
      case 'V':
        opt->show_version = true;
        return NULL;
      case '?':
      default:
        if (optopt != 0) {
          snprintf(error_buffer, sizeof(error_buffer),
                   "option '-%c' requires an argument or is invalid", optopt);
          return error_buffer;
        }
        return set_error("unrecognized option");
    }
  }

  if (optind < argc) {
    return set_error("unexpected positional argument");
  }
  if (opt->fork && opt->mode != MODE_LOCK) {
    return set_error("--fork is only valid in lock mode");
  }
  if (window_seen && opt->mode == MODE_LOCK) {
    return set_error("--window is only valid with --preview or --shot");
  }
  if (frames_seen && opt->mode != MODE_SHOT) {
    return set_error("--frames is only valid with --shot");
  }
  if (opt->mode == MODE_SHOT && (opt->shot_path == NULL || opt->shot_path[0] == '\0')) {
    return set_error("--shot requires a file name");
  }
  return NULL;
}

void options_version(FILE *out) {
  fprintf(out, "lava-lock 0.1.0\n");
}

void options_usage(FILE *out) {
  fprintf(out,
    "Usage: lava-lock [options]\n"
    "\n"
    "An X11 screen locker with the lava-lamp / fire animation from anim.py.\n"
    "\n"
    "Modes:\n"
    "  (default)              lock the running X session\n"
    "  --preview              run in a window; no grabs, cursor visible\n"
    "  --shot FILE            render one frame to FILE (BMP) and exit\n"
    "\n"
    "Animation:\n"
    "  --lava                 lava lamp (default)\n"
    "  --fire                 demoscene fire\n"
    "  --base WxH             internal simulation resolution\n"
    "  --core RRGGBB          colour of the hot cores\n"
    "  --blobs N              number of lava blobs (default 9)\n"
    "  --fps N                target frames per second (default 30)\n"
    "\n"
    "Authentication:\n"
    "  --user NAME            user to authenticate (default: current user)\n"
    "  --pam-service NAME     PAM service (default: login)\n"
    "  --no-auth              DEBUG: accept any password\n"
    "  --unlock-after SECONDS DEBUG: unlock automatically after N seconds\n"
    "\n"
    "Display:\n"
    "  --window WxH           preview/shot size (default 1280x720)\n"
    "  --frames N             simulation ticks before --shot (default 90)\n"
    "  --font-scale N         bitmap font scale (0 = automatic, default 0)\n"
    "  --fork                 detach after locking (lock mode only)\n"
    "  --verbose              log state changes to stderr\n"
    "  --help                 show this help\n"
    "  --version              show the version\n"
    "\n"
    "Examples:\n"
    "  lava-lock --lava\n"
    "  lava-lock --fire --fork\n"
    "  lava-lock --preview --lava --no-auth\n"
    "  lava-lock --shot /tmp/lava.bmp --frames 120\n"
    "\n"
    "Security: X11 has no session-lock protocol.  This program covers every\n"
    "output with an override-redirect window and grabs the keyboard and\n"
    "pointer, like i3lock/slock.  Same-user processes can still use XTEST or\n"
    "read the X connection; VT switching is handled by the X server.\n");
}
