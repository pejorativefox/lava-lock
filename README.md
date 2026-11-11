# lava-lock

An X11 screen locker built on **SDL3**, with the lava-lamp / fire animation,
Atom One Dark password field, and PAM authentication from the Wayland locker in
`~/.dotfiles/scripts/anim.py` (`animlib/lock.py`).

The Python Wayland implementation uses `ext-session-lock-v1`. X11 has no
equivalent protocol, so this port uses the same approach as i3lock/slock:

- one borderless, override-redirect window spanning every XRandR output;
- `XGrabKeyboard` and `XGrabPointer` so WM shortcuts and clicks cannot reach the
  desktop while locked;
- a blank pointer cursor, periodic raise/refocus, and re-sizing on monitor
  hotplug;
- PAM authentication in a worker thread; on success the grabs are released and
  the window is destroyed.

## Build

Dependencies (Fedora names):

```sh
sudo dnf install gcc meson ninja-build SDL3-devel libX11-devel libXext-devel pam-devel
```

`SDL3`, `libX11`, `libXext` (DPMS), and `PAM` are found through `pkg-config`; there is no
`SDL3_ttf`, SDL_image, Python, or font-file dependency.

```sh
meson setup build
meson compile -C build
./build/lava-lock --help
```

Install with `meson install -C build`.

## Usage

```sh
# Lock with the lava lamp (default)
./build/lava-lock

# Lock with fire and detach after locking
./build/lava-lock --fire --fork

# Preview in a normal window, accepting any password
./build/lava-lock --preview --lava --no-auth

# Render one frame without a display
./build/lava-lock --shot /tmp/lava.bmp --frames 120
```

Options:

| Option | Meaning |
| --- | --- |
| `--lava` / `--fire` | animation (default lava) |
| `--base WxH` | internal simulation resolution |
| `--core RRGGBB` | hot-core colour |
| `--blobs N` | lava blob count (default 9) |
| `--fps N` | target frames per second (default 30) |
| `--user NAME` | user to authenticate (default: current user) |
| `--pam-service NAME` | PAM service (default `login`) |
| `--no-auth` | debug: accept any password |
| `--unlock-after SECONDS` | debug: auto-unlock |
| `--preview` | windowed, no grabs, cursor visible |
| `--shot FILE` | headless BMP shot |
| `--window WxH` | preview/shot size (default 1280x720) |
| `--frames N` | simulation ticks before a shot |
| `--font-scale N` | bitmap font scale (0 = automatic) |
| `--fork` | detach after locking (lock mode only) |
| `--verbose` | state changes on stderr |

Rendering: the simulation runs at a low internal resolution (`--base`) and is
uploaded as a small texture each frame; the GPU scales it to the screen, and the
password field is a cached overlay redrawn only when its text changes.  SDL
falls back to its software renderer if no GPU driver is available.  While DPMS
has the monitors in standby/suspend/off, the animation stops entirely.

Signals: `SIGUSR1` pauses the animation, `SIGUSR2` resumes it (the lock stays
held), and `SIGINT`/`SIGTERM` unlock and exit.

The prompt is placed on the primary output; the animation spans the whole X
screen. Use `--preview` to try it without locking the session.

## Tests

```sh
meson test -C build
```

The smoke test renders both animations headlessly, then runs the preview and the
real lock path on a private `Xvfb` display. It never touches the live `:0`
session.

## Font

The password field uses a small 8x16 ASCII bitmap extracted from the Terminus
Font console font and committed as `src/font_data.h`, so the binary needs no
font files. Terminus is licensed under the SIL Open Font License 1.1; the full
text is in `third_party/terminus-OFL.txt`. The embedded table is a bitmap
extraction for rendering, not the original font file.

## Security notes

- X11 cannot prevent a same-user process from using XTEST, reading the X
  connection, taking screenshots, or killing the locker. This locks the *person
  at the keyboard* out, like i3lock/slock.
- If the keyboard or pointer cannot be grabbed, the program exits rather than
  presenting a fullscreen window that is not actually locked.
- VT switching is handled by the X server; the session stays locked when you
  return. If `Ctrl+Alt+Backspace` is enabled, it terminates the X session.
- The process runs as the logged-in user, not root, and PAM performs the
  password check. Core dumps are disabled and the password buffer is wiped.
- Run it from `xss-lock`, a `xflock4` wrapper, or a WM keybinding, for example:
  `xss-lock -- ./lava-lock --lava --fork`.
