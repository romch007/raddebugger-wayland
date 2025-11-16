#!/bin/sh

set -ex

if [ -z "$DESTDIR" ]; then
  echo "error: DESTDIR not set"
  exit 1
fi

install -Dm755 build/raddbg $DESTDIR/bin/raddbg
install -Dm644 data/logo.png $DESTDIR/share/icons/hicolor/256x256/apps/raddbg.png
install -Dm644 data/raddbg.desktop $DESTDIR/share/applications/raddbg.desktop
install -Dm644 LICENSE $DESTDIR/share/licenses/raddbg/LICENSE
