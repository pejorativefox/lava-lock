#!/bin/sh
# Headless smoke test: render both simulations, then exercise the preview, the
# real lock path and screensaver mode on a throwaway Xvfb display.  Never
# touches :0.
set -eu

BIN="${1:-${MESON_BUILD_ROOT:-build}/lava-lock}"
TMP="${TMPDIR:-/tmp}/lava-lock-smoke-$$"
mkdir -p "$TMP"
XVFB_PID=""
cleanup() {
  if [ -n "$XVFB_PID" ]; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

if [ ! -x "$BIN" ]; then
  echo "smoke: missing binary $BIN" >&2
  exit 1
fi

echo "smoke: headless lava shot"
"$BIN" --shot "$TMP/lava.bmp" --lava --frames 20
test -s "$TMP/lava.bmp"

echo "smoke: headless fire shot"
"$BIN" --shot "$TMP/fire.bmp" --fire --frames 20
test -s "$TMP/fire.bmp"

if command -v Xvfb >/dev/null 2>&1 && command -v xwininfo >/dev/null 2>&1; then
  DISP=":$((90 + ($$ % 100)))"
  echo "smoke: starting Xvfb on $DISP"
  Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 &
  XVFB_PID=$!

  n=0
  while ! xwininfo -display "$DISP" -root >/dev/null 2>&1; do
    n=$((n + 1))
    if [ "$n" -gt 50 ]; then
      echo "smoke: Xvfb did not start" >&2
      exit 1
    fi
    sleep 0.1
  done

  echo "smoke: preview"
  DISPLAY="$DISP" timeout 15 "$BIN" --preview --no-auth --unlock-after 1 \
    --window 640x480

  echo "smoke: real lock path"
  DISPLAY="$DISP" timeout 15 "$BIN" --no-auth --unlock-after 1 --verbose

  echo "smoke: screensaver mode into the root window"
  ROOT=$(xwininfo -display "$DISP" -root | awk '/Window id:/ {print $4}')
  DISPLAY="$DISP" XSCREENSAVER_WINDOW="$ROOT" "$BIN" --saver --fire &
  SAVER_PID=$!
  sleep 1
  kill -0 "$SAVER_PID"
  kill -TERM "$SAVER_PID"
  wait "$SAVER_PID"
else
  echo "smoke: Xvfb/xdpyinfo not available, skipping session tests"
fi

echo "smoke: ok"
