#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "config.h"
#include "lock.h"
#include "options.h"
#include "sims.h"
#include "util.h"

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  Options opt;
  options_init(&opt);
  const char *error = options_parse(&opt, argc, argv);
  if (error != NULL) {
    fprintf(stderr, "lava-lock: %s\n", error);
    fprintf(stderr, "Try 'lava-lock --help'.\n");
    return 2;
  }
  if (opt.show_help) {
    options_usage(stdout);
    return 0;
  }
  if (opt.show_version) {
    options_version(stdout);
    return 0;
  }

  Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.opt = opt;
  cfg.ops = opt.sim == SIM_FIRE ? sim_ops_fire() : sim_ops_lava();
  cfg.base_w = opt.base_set ? opt.base_w : cfg.ops->base_w;
  cfg.base_h = opt.base_set ? opt.base_h : cfg.ops->base_h;
  cfg.core = opt.core_set ? opt.core : sim_default_core();

  if (opt.user != NULL) {
    snprintf(cfg.username, sizeof(cfg.username), "%s", opt.user);
  } else {
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL || pw->pw_name == NULL || pw->pw_name[0] == '\0') {
      fprintf(stderr, "lava-lock: cannot determine the current user name\n");
      return 1;
    }
    snprintf(cfg.username, sizeof(cfg.username), "%s", pw->pw_name);
  }
  if (cfg.username[0] == '\0') {
    fprintf(stderr, "lava-lock: empty user name\n");
    return 1;
  }

  if (opt.fork) {
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "lava-lock: fork failed: %s\n", strerror(errno));
      return 1;
    }
    if (pid > 0) {
      return 0;
    }
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) {
        close(devnull);
      }
    }
  }

  /* A lock process holds the password in memory.  Do not leave it in a core
   * dump, and do not let same-user processes ptrace it (Linux only). */
  struct rlimit core_limit = {0, 0};
  setrlimit(RLIMIT_CORE, &core_limit);
#ifdef __linux__
  prctl(PR_SET_DUMPABLE, 0);
#endif

  switch (opt.mode) {
    case MODE_PREVIEW:
      return lava_run_preview(&cfg);
    case MODE_SHOT:
      return lava_run_shot(&cfg);
    case MODE_SAVER:
      return lava_run_saver(&cfg);
    case MODE_LOCK:
    default:
      return lava_run_lock(&cfg);
  }
}
