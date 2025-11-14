#!/bin/sh

set -ex

if [ -z "$DESTDIR" ]; then
  echo "error: DESTDIR not set"
  exit 1
fi

install -D build/raddbg $DESTDIR/bin/raddbg
install -D data/logo.png $DESTDIR/share/icons/hicolor/256x256/apps/raddbg.png
install -D data/raddbg.desktop $DESTDIR/share/applications/raddbg.desktop
install -D LICENSE $DESTDIR/share/licenses/raddbg/LICENSE
