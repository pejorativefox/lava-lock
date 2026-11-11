#!/bin/sh
# Link lava-lock into xfce4-screensaver's engine directory.  The screensaver
# only runs theme programs from its own directories, so the themes run this
# link.  The directory is normally root-owned: when a user-prefix install
# cannot write there, say how to finish instead of failing the install.
# usage: install-saver-link.sh SAVER_DIR BINARY
set -u
dir="${DESTDIR:-}$1"
bin="$2"
link="$dir/lava-lock"

if [ -L "$link" ] && [ "$(readlink "$link")" = "$bin" ]; then
  echo "lava-lock: screensaver link already in place: $link"
  exit 0
fi
if mkdir -p "$dir" 2>/dev/null && ln -sfn "$bin" "$link" 2>/dev/null; then
  echo "lava-lock: linked $link -> $bin"
  exit 0
fi

cat >&2 <<MSG

lava-lock: WARNING: cannot write $dir, so the xfce4-screensaver themes
lava-lock: will not appear until you create the link yourself:
lava-lock:
lava-lock:     sudo ln -sfn $bin $link
lava-lock:
MSG
exit 0
