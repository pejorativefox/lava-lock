#include "lock.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>

#include "field.h"
#include "font.h"
#include "pam_auth.h"
#include "render.h"
#include "sims.h"
#include "util.h"

#define PASSWORD_MAX 4096
#define FOCUS_INTERVAL_MS 1000
#define IDLE_POLL_MS 250

static const RGB RED = {0xE0, 0x6C, 0x75};
static const RGB BLUE = {0x61, 0xAF, 0xEF};

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_pause = 0;
static volatile sig_atomic_t g_resume = 0;
static bool g_verbose = false;

typedef struct {
  const Config *cfg;
  Sim *sim;
  Renderer renderer;
  PamAuth auth;

  char *password;
  size_t password_len;
  size_t password_cap;
  char message[256];
  RGB message_color;

  bool authenticating;
  bool running;
  bool paused;
  bool is_preview;
  bool is_saver;      /* drawing into a screensaver's window: animation only */
  bool auth_ready;
  bool locked;

  SDL_Window *window;
  SDL_Renderer *gpu;
  Display *dpy;
  Window xwin;
  Cursor blank_cursor;
  bool keyboard_grabbed;
  bool pointer_grabbed;
  bool x11;
  bool dpms;          /* the server supports DPMS */
  bool display_off;   /* DPMS has powered the monitors down */
  bool redraw;        /* the field or window needs repainting */

  SDL_Rect union_rect;
  SDL_Rect primary_rect;
  int font_scale;

  int lock_fd;
  char lock_path[512];

  uint64_t next_frame_ms;
  uint64_t last_focus_ms;
  double started;
} App;

/* ------------------------------------------------------------------------- */
/* Signals                                                                    */
/* ------------------------------------------------------------------------- */

static void signal_handler(int sig) {
  if (sig == SIGUSR1) {
    g_pause = 1;
  } else if (sig == SIGUSR2) {
    g_resume = 1;
  } else {
    g_stop = 1;
  }
}

static void install_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; /* let SDL_WaitEventTimeout() return on a signal */
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGUSR2, &sa, NULL);

  struct sigaction ignore;
  memset(&ignore, 0, sizeof(ignore));
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGHUP, &ignore, NULL);
}

/* ------------------------------------------------------------------------- */
/* Password buffer                                                            */
/* ------------------------------------------------------------------------- */

static bool password_reserve(App *app, size_t extra) {
  size_t needed = app->password_len + extra + 1;
  if (needed > (size_t)PASSWORD_MAX + 1) {
    return false;
  }
  if (needed <= app->password_cap) {
    return true;
  }
  size_t cap = app->password_cap > 0 ? app->password_cap : 64;
  while (cap < needed) {
    cap *= 2;
  }
  if (cap > (size_t)PASSWORD_MAX + 1) {
    cap = (size_t)PASSWORD_MAX + 1;
  }
  char *next = realloc(app->password, cap);
  if (next == NULL) {
    return false;
  }
  app->password = next;
  app->password_cap = cap;
  return true;
}

static void password_append(App *app, const char *text) {
  if (text == NULL) {
    return;
  }
  size_t len = strlen(text);
  if (len == 0) {
    return;
  }
  if (app->password_len + len > (size_t)PASSWORD_MAX) {
    len = (size_t)PASSWORD_MAX - app->password_len;
  }
  if (len == 0 || !password_reserve(app, len)) {
    return;
  }
  memcpy(app->password + app->password_len, text, len);
  app->password_len += len;
  app->password[app->password_len] = '\0';
  app->message[0] = '\0';
}

static void password_backspace(App *app) {
  if (app->password_len == 0) {
    return;
  }
  size_t prev =
      util_utf8_prev(app->password, app->password_len, app->password_len);
  util_secure_zero(app->password + prev, app->password_len - prev);
  app->password_len = prev;
  app->password[prev] = '\0';
  app->message[0] = '\0';
}

static void password_clear(App *app) {
  if (app->password != NULL) {
    util_secure_zero(app->password, app->password_cap);
    app->password[0] = '\0';
  }
  app->password_len = 0;
  app->message[0] = '\0';
}

static void set_message(App *app, const char *text, RGB color) {
  snprintf(app->message, sizeof(app->message), "%s", text != NULL ? text : "");
  app->message_color = color;
}

/* SDL text input is UTF-8.  Reject control characters so a stray XIM event
 * cannot inject newlines/tabs into the password. */
static bool text_is_printable(const char *text) {
  if (text == NULL) {
    return false;
  }
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    if (*p < 0x20 || *p == 0x7F) {
      return false;
    }
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* X11                                                                        */
/* ------------------------------------------------------------------------- */

static int x11_error_handler(Display *dpy, XErrorEvent *event) {
  (void)dpy;
  if (g_verbose) {
    fprintf(stderr,
            "lava-lock: X error %d (request %d, minor %d, resource 0x%lx)\n",
            event->error_code, event->request_code, event->minor_code,
            event->resourceid);
  }
  return 0;
}

static bool get_window_x11(App *app) {
  SDL_PropertiesID props = SDL_GetWindowProperties(app->window);
  app->dpy = SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
  app->xwin = (Window)SDL_GetNumberProperty(
      props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  app->x11 = app->dpy != NULL && app->xwin != 0;
  return app->x11;
}

static Cursor make_blank_cursor(Display *dpy, Window window) {
  static const char empty[1] = {0};
  Pixmap bitmap = XCreateBitmapFromData(dpy, window, empty, 1, 1);
  if (bitmap == None) {
    return None;
  }
  XColor dummy;
  memset(&dummy, 0, sizeof(dummy));
  Cursor cursor = XCreatePixmapCursor(dpy, bitmap, bitmap, &dummy, &dummy, 0, 0);
  XFreePixmap(dpy, bitmap);
  return cursor;
}

static bool grab_input(App *app) {
  for (int attempt = 0; attempt < 40; attempt++) {
    int result = XGrabKeyboard(app->dpy, app->xwin, False, GrabModeAsync,
                               GrabModeAsync, CurrentTime);
    if (result == GrabSuccess) {
      app->keyboard_grabbed = true;
      break;
    }
    if (result != AlreadyGrabbed && result != GrabNotViewable) {
      fprintf(stderr, "lava-lock: XGrabKeyboard failed (%d)\n", result);
      return false;
    }
    usleep(50000);
  }
  if (!app->keyboard_grabbed) {
    fprintf(stderr, "lava-lock: keyboard is grabbed by another client\n");
    return false;
  }

  for (int attempt = 0; attempt < 40; attempt++) {
    int result = XGrabPointer(
        app->dpy, app->xwin, False,
        PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
        GrabModeAsync, app->xwin, app->blank_cursor, CurrentTime);
    if (result == GrabSuccess) {
      app->pointer_grabbed = true;
      XSync(app->dpy, False);
      return true;
    }
    if (result != AlreadyGrabbed && result != GrabNotViewable) {
      fprintf(stderr, "lava-lock: XGrabPointer failed (%d)\n", result);
      return false;
    }
    usleep(50000);
  }
  fprintf(stderr, "lava-lock: pointer is grabbed by another client\n");
  return false;
}

static void release_input(App *app) {
  if (app->dpy == NULL) {
    return;
  }
  if (app->pointer_grabbed) {
    XUngrabPointer(app->dpy, CurrentTime);
    app->pointer_grabbed = false;
  }
  if (app->keyboard_grabbed) {
    XUngrabKeyboard(app->dpy, CurrentTime);
    app->keyboard_grabbed = false;
  }
  if (app->blank_cursor != None) {
    XFreeCursor(app->dpy, app->blank_cursor);
    app->blank_cursor = None;
  }
  XFlush(app->dpy);
}

static void reassert_lock(App *app) {
  if (!app->x11 || app->dpy == NULL || app->xwin == 0) {
    return;
  }
  XWindowAttributes attrs;
  if (XGetWindowAttributes(app->dpy, app->xwin, &attrs)) {
    if (attrs.map_state != IsViewable) {
      XMapRaised(app->dpy, app->xwin);
    } else {
      XRaiseWindow(app->dpy, app->xwin);
    }
    Window focus = None;
    int revert = 0;
    XGetInputFocus(app->dpy, &focus, &revert);
    if (focus != app->xwin) {
      XSetInputFocus(app->dpy, app->xwin, RevertToPointerRoot, CurrentTime);
    }
    if (app->blank_cursor != None) {
      XDefineCursor(app->dpy, app->xwin, app->blank_cursor);
    }
  }
  XFlush(app->dpy);
}

/* True only while DPMS has the monitors fully off.  Standby and suspend are
 * deliberately treated as "on": on many panels they leave the screen visible,
 * and pausing the animation there would look like a frozen frame. */
static bool display_is_off(App *app) {
  if (!app->dpms || app->dpy == NULL) {
    return false;
  }
  CARD16 level = DPMSModeOn;
  BOOL enabled = False;
  if (!DPMSInfo(app->dpy, &level, &enabled) || !enabled) {
    return false;
  }
  return level == DPMSModeOff;
}

/* ------------------------------------------------------------------------- */
/* Display geometry                                                           */
/* ------------------------------------------------------------------------- */

static void get_display_geometry(SDL_Rect *out_union, SDL_Rect *out_primary) {
  SDL_Rect fallback = {0, 0, 1920, 1080};
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  SDL_DisplayID primary_id = SDL_GetPrimaryDisplay();

  bool have = false;
  bool have_primary = false;
  SDL_Rect first = fallback;
  SDL_Rect primary = fallback;
  SDL_Rect union_rect = fallback;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

  for (int i = 0; i < count; i++) {
    SDL_Rect r;
    if (!SDL_GetDisplayBounds(displays[i], &r) || r.w <= 0 || r.h <= 0) {
      continue;
    }
    if (!have) {
      first = r;
      union_rect = r;
      x0 = r.x;
      y0 = r.y;
      x1 = r.x + r.w;
      y1 = r.y + r.h;
      have = true;
    } else {
      if (r.x < x0) x0 = r.x;
      if (r.y < y0) y0 = r.y;
      if (r.x + r.w > x1) x1 = r.x + r.w;
      if (r.y + r.h > y1) y1 = r.y + r.h;
    }
    if (displays[i] == primary_id) {
      primary = r;
      have_primary = true;
    }
  }

  if (have) {
    union_rect.x = x0;
    union_rect.y = y0;
    union_rect.w = x1 - x0;
    union_rect.h = y1 - y0;
    if (!have_primary) {
      primary = first;
    }
  }

  if (displays != NULL) {
    SDL_free(displays);
  }
  *out_union = union_rect;
  *out_primary = primary;
}

static void update_lock_geometry(App *app) {
  SDL_Rect union_rect;
  SDL_Rect primary;
  get_display_geometry(&union_rect, &primary);
  if (union_rect.w <= 0 || union_rect.h <= 0 || primary.w <= 0 ||
      primary.h <= 0) {
    return;
  }

  app->union_rect = union_rect;
  app->primary_rect = (SDL_Rect){
    primary.x - union_rect.x,
    primary.y - union_rect.y,
    primary.w,
    primary.h,
  };

  if (app->window != NULL) {
    SDL_SetWindowPosition(app->window, union_rect.x, union_rect.y);
    SDL_SetWindowSize(app->window, union_rect.w, union_rect.h);
    SDL_SyncWindow(app->window);
  }
  if (app->x11 && app->dpy != NULL && app->xwin != 0) {
    XRaiseWindow(app->dpy, app->xwin);
    XFlush(app->dpy);
  }

  render_resize(&app->renderer, union_rect.w, union_rect.h);
  render_set_primary(&app->renderer, app->primary_rect);
  app->font_scale =
      field_pick_font_scale(primary.h, app->cfg->opt.font_scale);
  render_set_font_scale(&app->renderer, app->font_scale);
  reassert_lock(app);
}

/* ------------------------------------------------------------------------- */
/* SDL window setup                                                           */
/* ------------------------------------------------------------------------- */

/* SDL disables the X11 screensaver (and its DPMS timer) by default, as if it
 * were a game.  A locker or screensaver theme must do the opposite: the
 * display has to be allowed to blank and power off while it runs.  The hint
 * must be set before SDL_Init so the server's idle countdown is not reset. */
static void request_display_sleep_allowed(void) {
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1",
                          SDL_HINT_OVERRIDE);
}

/* Fallback for a video backend that ignored the hint.  A no-op when the hint
 * was applied, because SDL then never suspended the screensaver. */
static void allow_display_sleep(void) {
  if (!SDL_EnableScreenSaver() && g_verbose) {
    fprintf(stderr, "lava-lock: could not re-enable display blanking: %s\n",
            SDL_GetError());
  }
}

static bool create_gpu_renderer(App *app) {
  /* Let SDL pick the best driver; it falls back to its software renderer when
   * no GPU driver works, so locking never depends on GL being available. */
  app->gpu = SDL_CreateRenderer(app->window, NULL);
  if (app->gpu == NULL) {
    fprintf(stderr, "lava-lock: SDL_CreateRenderer failed: %s\n",
            SDL_GetError());
    return false;
  }
  if (g_verbose) {
    fprintf(stderr, "lava-lock: renderer %s\n", SDL_GetRendererName(app->gpu));
  }
  return true;
}

static bool setup_lock_window(App *app) {
  /* Force X11 even if SDL_VIDEODRIVER or a hint would pick another driver:
   * the grabs and override-redirect window are X11-specific. */
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
  request_display_sleep_allowed();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "lava-lock: SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  allow_display_sleep();

  const char *driver = SDL_GetCurrentVideoDriver();
  if (driver == NULL || strcmp(driver, "x11") != 0) {
    fprintf(stderr,
            "lava-lock: lock mode needs the X11 SDL video driver (got %s)\n",
            driver != NULL ? driver : "none");
    return false;
  }

  SDL_Rect union_rect;
  SDL_Rect primary;
  get_display_geometry(&union_rect, &primary);
  if (union_rect.w <= 0 || union_rect.h <= 0 || primary.w <= 0 ||
      primary.h <= 0) {
    fprintf(stderr, "lava-lock: could not determine the X screen size\n");
    return false;
  }
  app->union_rect = union_rect;
  app->primary_rect = (SDL_Rect){
    primary.x - union_rect.x,
    primary.y - union_rect.y,
    primary.w,
    primary.h,
  };
  app->font_scale =
      field_pick_font_scale(primary.h, app->cfg->opt.font_scale);

  SDL_WindowFlags flags =
      SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALWAYS_ON_TOP;
  app->window = SDL_CreateWindow("lava-lock", union_rect.w, union_rect.h,
                                 flags);
  if (app->window == NULL) {
    fprintf(stderr, "lava-lock: SDL_CreateWindow failed: %s\n",
            SDL_GetError());
    return false;
  }
  SDL_SetWindowPosition(app->window, union_rect.x, union_rect.y);
  SDL_SetWindowSize(app->window, union_rect.w, union_rect.h);

  /* Create the renderer while the window is still hidden and unmapped: a GL
   * or Vulkan renderer may make SDL recreate the X window with a different
   * visual, so the XID is only read afterwards, guaranteeing that the window we
   * set override-redirect and grab on is the final one.  The window stays
   * unmapped until SDL_ShowWindow(), after the override-redirect bit is set, so
   * the window manager never sees a managed window. */
  XSetErrorHandler(x11_error_handler);
  if (!create_gpu_renderer(app)) {
    return false;
  }
  if (!get_window_x11(app)) {
    fprintf(stderr, "lava-lock: could not obtain the X11 window handle\n");
    return false;
  }
  XSetWindowAttributes attrs;
  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  XChangeWindowAttributes(app->dpy, app->xwin, CWOverrideRedirect, &attrs);
  XSync(app->dpy, False);

  SDL_ShowWindow(app->window);
  SDL_SyncWindow(app->window);
  XRaiseWindow(app->dpy, app->xwin);
  XSetInputFocus(app->dpy, app->xwin, RevertToPointerRoot, CurrentTime);
  XSync(app->dpy, False);

  int dpms_event = 0;
  int dpms_error = 0;
  app->dpms = DPMSQueryExtension(app->dpy, &dpms_event, &dpms_error) &&
              DPMSCapable(app->dpy);

  app->blank_cursor = make_blank_cursor(app->dpy, app->xwin);
  if (!grab_input(app)) {
    return false;
  }
  app->locked = true;

  SDL_HideCursor();
  if (!SDL_StartTextInput(app->window) && g_verbose) {
    fprintf(stderr, "lava-lock: SDL_StartTextInput failed: %s\n",
            SDL_GetError());
  }

  if (!render_init(&app->renderer, app->sim, app->cfg->core, app->gpu,
                   union_rect.w, union_rect.h, app->primary_rect,
                   app->font_scale)) {
    fprintf(stderr, "lava-lock: could not create the render surfaces: %s\n",
            SDL_GetError());
    return false;
  }

  return true;
}

static bool setup_preview_window(App *app) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "lava-lock: SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  app->window = SDL_CreateWindow(
      "lava-lock preview", app->cfg->opt.preview_w, app->cfg->opt.preview_h,
      SDL_WINDOW_RESIZABLE);
  if (app->window == NULL) {
    fprintf(stderr, "lava-lock: SDL_CreateWindow failed: %s\n",
            SDL_GetError());
    return false;
  }

  if (!create_gpu_renderer(app)) {
    return false;
  }

  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(app->window, &width, &height);
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "lava-lock: preview window has no usable size\n");
    return false;
  }

  app->primary_rect = (SDL_Rect){0, 0, width, height};
  app->font_scale =
      field_pick_font_scale(height, app->cfg->opt.font_scale);
  if (!render_init(&app->renderer, app->sim, app->cfg->core, app->gpu, width,
                   height, app->primary_rect, app->font_scale)) {
    fprintf(stderr, "lava-lock: could not create the render surfaces: %s\n",
            SDL_GetError());
    return false;
  }

  SDL_StartTextInput(app->window);
  SDL_ShowCursor();
  return true;
}

static void preview_resize(App *app) {
  if (app->window == NULL) {
    return;
  }
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(app->window, &width, &height);
  if (width <= 0 || height <= 0) {
    return;
  }
  if (!render_resize(&app->renderer, width, height)) {
    return;
  }
  app->primary_rect = (SDL_Rect){0, 0, width, height};
  render_set_primary(&app->renderer, app->primary_rect);
  app->font_scale =
      field_pick_font_scale(height, app->cfg->opt.font_scale);
  render_set_font_scale(&app->renderer, app->font_scale);
  app->next_frame_ms = SDL_GetTicks();
}

static bool setup_saver_window(App *app, unsigned long xid) {
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
  request_display_sleep_allowed();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "lava-lock: SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  allow_display_sleep();

  /* Wrap the window the screensaver created.  It owns the window: we only
   * draw into it, and it is gone once the screensaver stops us. */
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
                        (Sint64)xid);
  app->window = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);
  if (app->window == NULL) {
    fprintf(stderr, "lava-lock: cannot use window 0x%lx: %s\n", xid,
            SDL_GetError());
    return false;
  }
  if (!create_gpu_renderer(app)) {
    return false;
  }
  if (get_window_x11(app)) {
    XSetErrorHandler(x11_error_handler);
    int dpms_event = 0;
    int dpms_error = 0;
    app->dpms = DPMSQueryExtension(app->dpy, &dpms_event, &dpms_error) &&
                DPMSCapable(app->dpy);
  }

  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(app->window, &width, &height);
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "lava-lock: window 0x%lx has no usable size\n", xid);
    return false;
  }
  app->primary_rect = (SDL_Rect){0, 0, width, height};
  app->font_scale = field_pick_font_scale(height, app->cfg->opt.font_scale);
  if (!render_init(&app->renderer, app->sim, app->cfg->core, app->gpu, width,
                   height, app->primary_rect, app->font_scale)) {
    fprintf(stderr, "lava-lock: could not create the render surfaces: %s\n",
            SDL_GetError());
    return false;
  }
  if (g_verbose) {
    fprintf(stderr, "lava-lock: saver in window 0x%lx (%dx%d)\n", xid, width,
            height);
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* Authentication                                                             */
/* ------------------------------------------------------------------------- */

static void submit_password(App *app) {
  if (app->authenticating) {
    return;
  }
  if (app->cfg->opt.no_auth) {
    app->running = false;
    return;
  }
  if (app->password_len == 0) {
    return;
  }
  if (!pam_auth_start(&app->auth, app->password, app->password_len)) {
    set_message(app, "Authentication busy", RED);
    return;
  }
  app->authenticating = true;
  set_message(app, "Authenticating...", BLUE);
}

static void poll_auth(App *app) {
  bool success = false;
  char reason[256];
  reason[0] = '\0';
  if (!pam_auth_poll(&app->auth, &success, reason, sizeof(reason))) {
    return;
  }
  app->authenticating = false;
  if (success) {
    app->running = false;
  } else {
    password_clear(app);
    set_message(app, "Authentication failed", RED);
    if (g_verbose) {
      fprintf(stderr, "lava-lock: auth failed: %s\n",
              reason[0] != '\0' ? reason : "unknown reason");
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Event loop                                                                 */
/* ------------------------------------------------------------------------- */

static FieldContent make_content(App *app) {
  return (FieldContent){
    .username = app->cfg->username,
    .message = app->message,
    .message_color = app->message_color,
    .password = app->password != NULL ? app->password : "",
    .password_len = app->password_len,
    .placeholder = "Enter password",
  };
}

static void handle_event(App *app, const SDL_Event *event) {
  /* Anything below can change the field or the window contents; repainting is
   * cheap, so do not track it finer than this. */
  app->redraw = true;
  if (app->is_saver) {
    /* The screensaver owns input; only follow the window. */
    switch (event->type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      case SDL_EVENT_WINDOW_DESTROYED:
        app->running = false;
        break;
      case SDL_EVENT_WINDOW_EXPOSED:
        app->next_frame_ms = SDL_GetTicks();
        break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        preview_resize(app);
        break;
      default:
        break;
    }
    return;
  }
  switch (event->type) {
    case SDL_EVENT_QUIT:
      if (app->is_preview) {
        app->running = false;
      }
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      if (app->is_preview) {
        app->running = false;
      }
      break;
    case SDL_EVENT_KEY_DOWN:
      switch (event->key.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          submit_password(app);
          break;
        case SDLK_BACKSPACE:
          password_backspace(app);
          break;
        case SDLK_ESCAPE:
          password_clear(app);
          break;
        default:
          break;
      }
      break;
    case SDL_EVENT_TEXT_INPUT:
      if (!app->authenticating && text_is_printable(event->text.text)) {
        password_append(app, event->text.text);
      }
      break;
    case SDL_EVENT_WINDOW_EXPOSED:
      app->next_frame_ms = SDL_GetTicks();
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (app->locked) {
        reassert_lock(app);
      }
      break;
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
      if (app->locked) {
        update_lock_geometry(app);
      }
      break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (app->is_preview) {
        preview_resize(app);
      } else if (app->locked) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(app->window, &width, &height);
        if (width != app->union_rect.w || height != app->union_rect.h) {
          update_lock_geometry(app);
        }
      }
      break;
    default:
      break;
  }
}

static void render_one_frame(App *app, bool advance) {
  if (advance) {
    sim_tick(app->sim);
    if (!render_upload(&app->renderer) && g_verbose) {
      fprintf(stderr, "lava-lock: upload failed: %s\n", SDL_GetError());
    }
  }
  FieldContent content = make_content(app);
  if (!render_present(&app->renderer, app->is_saver ? NULL : &content) &&
      g_verbose) {
    fprintf(stderr, "lava-lock: present failed: %s\n", SDL_GetError());
  }
  app->redraw = false;
}

static void run_loop(App *app) {
  int frame_ms = 1000 / app->cfg->opt.fps;
  if (frame_ms < 1) {
    frame_ms = 1;
  }

  app->started = util_now();
  app->next_frame_ms = SDL_GetTicks();
  app->last_focus_ms = app->next_frame_ms;

  while (app->running) {
    if (g_stop) {
      app->running = false;
    }
    if (g_pause) {
      g_pause = 0;
      app->paused = true;
    }
    if (g_resume) {
      g_resume = 0;
      app->paused = false;
      app->next_frame_ms = SDL_GetTicks();
    }
    if (!app->running) {
      break;
    }

    uint64_t now = SDL_GetTicks();
    bool idle = app->paused || app->display_off;
    int timeout;
    if (idle) {
      timeout = IDLE_POLL_MS;
    } else if (app->authenticating) {
      timeout = 100;
    } else {
      int64_t delta = (int64_t)app->next_frame_ms - (int64_t)now;
      if (delta < 0) {
        delta = 0;
      }
      if (delta > 1000) {
        delta = 1000;
      }
      timeout = (int)delta;
    }
    if ((app->locked || app->dpms) &&
        now - app->last_focus_ms >= FOCUS_INTERVAL_MS && timeout > 200) {
      timeout = 200;
    }

    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, timeout)) {
      handle_event(app, &event);
      while (SDL_PollEvent(&event)) {
        handle_event(app, &event);
      }
    }

    now = SDL_GetTicks();
    bool periodic = false;
    if (now - app->last_focus_ms >= FOCUS_INTERVAL_MS) {
      if (app->locked) {
        reassert_lock(app);
      }
      app->last_focus_ms = now;
      periodic = true;
    }
    /* Polled every second while the monitors are on, and on every wake-up
     * while they are off so the animation resumes as soon as they come back. */
    if (app->dpms && (app->display_off || periodic)) {
      bool off = display_is_off(app);
      if (off != app->display_off) {
        app->display_off = off;
        if (g_verbose) {
          fprintf(stderr, "lava-lock: display %s\n", off ? "off" : "on");
        }
        if (!off) {
          app->next_frame_ms = now;
        }
      }
    }

    poll_auth(app);
    if (app->cfg->opt.unlock_after > 0.0 &&
        util_now() - app->started >= app->cfg->opt.unlock_after) {
      app->running = false;
    }
    if (!app->running) {
      break;
    }

    now = SDL_GetTicks();
    idle = app->paused || app->display_off;
    if (!idle && now >= app->next_frame_ms) {
      render_one_frame(app, true);
      app->next_frame_ms += (uint64_t)frame_ms;
      if (app->next_frame_ms <= now) {
        app->next_frame_ms = now + (uint64_t)frame_ms;
      }
    } else if (app->paused && app->redraw) {
      /* Keep the frozen frame and the field current (typing, expose). */
      render_one_frame(app, false);
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

static void app_cleanup(App *app) {
  if (app->window != NULL) {
    SDL_StopTextInput(app->window);
    SDL_ShowCursor();
  }
  if (app->keyboard_grabbed || app->pointer_grabbed ||
      app->blank_cursor != None) {
    release_input(app);
  }
  app->locked = false;
  render_destroy(&app->renderer);
  if (app->gpu != NULL) {
    SDL_DestroyRenderer(app->gpu);
    app->gpu = NULL;
  }
  if (app->window != NULL) {
    SDL_DestroyWindow(app->window);
    app->window = NULL;
  }
  app->dpy = NULL;
  app->xwin = 0;
  app->x11 = false;
  SDL_Quit();
  if (app->auth_ready) {
    pam_auth_destroy(&app->auth);
    app->auth_ready = false;
  }
  password_clear(app);
  free(app->password);
  app->password = NULL;
  if (app->lock_fd >= 0) {
    close(app->lock_fd);
    app->lock_fd = -1;
  }
}

static bool acquire_lock_file(App *app) {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime != NULL && runtime[0] != '\0') {
    snprintf(app->lock_path, sizeof(app->lock_path), "%s/lava-lock.lock",
             runtime);
  } else {
    snprintf(app->lock_path, sizeof(app->lock_path), "/tmp/lava-lock-%ld.lock",
             (long)getuid());
  }

  app->lock_fd = open(app->lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (app->lock_fd < 0) {
    fprintf(stderr, "lava-lock: cannot open %s: %s\n", app->lock_path,
            strerror(errno));
    return false;
  }
  if (flock(app->lock_fd, LOCK_EX | LOCK_NB) != 0) {
    fprintf(stderr, "lava-lock: another locker holds %s\n", app->lock_path);
    close(app->lock_fd);
    app->lock_fd = -1;
    return false;
  }
  if (ftruncate(app->lock_fd, 0) == 0) {
    char pid[32];
    int len = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    if (len > 0) {
      ssize_t ignored = write(app->lock_fd, pid, (size_t)len);
      (void)ignored;
    }
  }
  return true;
}

int lava_run_lock(const Config *cfg) {
  App app;
  memset(&app, 0, sizeof(app));
  app.cfg = cfg;
  app.lock_fd = -1;
  app.running = true;
  app.is_preview = false;
  g_verbose = cfg->opt.verbose;
  set_message(&app, "", RED);

  if (!acquire_lock_file(&app)) {
    return 1;
  }

  app.sim = sim_create(cfg->ops, cfg->base_w, cfg->base_h, cfg->opt.blobs);
  if (app.sim == NULL) {
    fprintf(stderr, "lava-lock: could not create the %s simulation\n",
            cfg->ops->name);
    close(app.lock_fd);
    return 1;
  }
  pam_auth_init(&app.auth, cfg->username, cfg->opt.pam_service);
  app.auth_ready = true;
  install_signals();

  if (!setup_lock_window(&app)) {
    app_cleanup(&app);
    return 1;
  }

  for (int i = 0; i < cfg->ops->prewarm; i++) {
    sim_tick(app.sim);
  }
  render_upload(&app.renderer);

  if (g_verbose) {
    fprintf(stderr,
            "lava-lock: locked %dx%d at %d,%d (sim %s %dx%d, prompt %dx%d)\n",
            app.union_rect.w, app.union_rect.h, app.union_rect.x,
            app.union_rect.y, cfg->ops->name, cfg->base_w, cfg->base_h,
            app.primary_rect.w, app.primary_rect.h);
  }

  run_loop(&app);
  app_cleanup(&app);
  return 0;
}

int lava_run_preview(const Config *cfg) {
  App app;
  memset(&app, 0, sizeof(app));
  app.cfg = cfg;
  app.lock_fd = -1;
  app.running = true;
  app.is_preview = true;
  g_verbose = cfg->opt.verbose;
  set_message(&app, "", RED);

  app.sim = sim_create(cfg->ops, cfg->base_w, cfg->base_h, cfg->opt.blobs);
  if (app.sim == NULL) {
    fprintf(stderr, "lava-lock: could not create the %s simulation\n",
            cfg->ops->name);
    return 1;
  }
  pam_auth_init(&app.auth, cfg->username, cfg->opt.pam_service);
  app.auth_ready = true;
  install_signals();

  if (!setup_preview_window(&app)) {
    app_cleanup(&app);
    return 1;
  }

  for (int i = 0; i < cfg->ops->prewarm; i++) {
    sim_tick(app.sim);
  }
  render_upload(&app.renderer);

  run_loop(&app);
  app_cleanup(&app);
  return 0;
}

int lava_run_saver(const Config *cfg) {
  App app;
  memset(&app, 0, sizeof(app));
  app.cfg = cfg;
  app.lock_fd = -1;
  app.running = true;
  app.is_saver = true;
  g_verbose = cfg->opt.verbose;

  unsigned long xid = cfg->opt.window_id;
  if (xid == 0 &&
      !util_parse_window_id(getenv("XSCREENSAVER_WINDOW"), &xid)) {
    fprintf(stderr,
            "lava-lock: --saver needs $XSCREENSAVER_WINDOW or --window-id\n");
    return 2;
  }

  app.sim = sim_create(cfg->ops, cfg->base_w, cfg->base_h, cfg->opt.blobs);
  if (app.sim == NULL) {
    fprintf(stderr, "lava-lock: could not create the %s simulation\n",
            cfg->ops->name);
    return 1;
  }
  install_signals();

  if (!setup_saver_window(&app, xid)) {
    app_cleanup(&app);
    return 1;
  }

  for (int i = 0; i < cfg->ops->prewarm; i++) {
    sim_tick(app.sim);
  }
  render_upload(&app.renderer);

  run_loop(&app);
  app_cleanup(&app);
  return 0;
}

int lava_run_shot(const Config *cfg) {
  g_verbose = cfg->opt.verbose;
  if (!SDL_Init(0)) {
    fprintf(stderr, "lava-lock: SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  Sim *sim = sim_create(cfg->ops, cfg->base_w, cfg->base_h, cfg->opt.blobs);
  if (sim == NULL) {
    fprintf(stderr, "lava-lock: could not create the %s simulation\n",
            cfg->ops->name);
    SDL_Quit();
    return 1;
  }

  for (int i = 0; i < cfg->ops->prewarm; i++) {
    sim_tick(sim);
  }
  for (int i = 0; i < cfg->opt.frames; i++) {
    sim_tick(sim);
  }

  const int width = cfg->opt.preview_w;
  const int height = cfg->opt.preview_h;
  SDL_Rect primary = {0, 0, width, height};
  int font_scale = field_pick_font_scale(height, cfg->opt.font_scale);

  Renderer renderer;
  if (!render_init(&renderer, sim, cfg->core, NULL, width, height, primary,
                   font_scale)) {
    fprintf(stderr, "lava-lock: could not create the render surfaces: %s\n",
            SDL_GetError());
    sim_destroy(sim);
    SDL_Quit();
    return 1;
  }

  FieldContent content = {
    .username = cfg->username,
    .message = "",
    .message_color = RED,
    .password = "",
    .password_len = 0,
    .placeholder = "Enter password",
  };

  if (!render_save_bmp(&renderer, &content, cfg->opt.shot_path)) {
    fprintf(stderr, "lava-lock: cannot write %s: %s\n", cfg->opt.shot_path,
            SDL_GetError());
    render_destroy(&renderer);
    sim_destroy(sim);
    SDL_Quit();
    return 1;
  }

  if (g_verbose) {
    fprintf(stderr, "lava-lock: wrote %s (%dx%d, sim %s %dx%d)\n",
            cfg->opt.shot_path, width, height, cfg->ops->name, cfg->base_w,
            cfg->base_h);
  }

  render_destroy(&renderer);
  sim_destroy(sim);
  SDL_Quit();
  return 0;
}
