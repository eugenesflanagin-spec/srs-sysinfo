#!/bin/bash
# build_gui.sh OUTDIR [groups...] - build the native GTK srs-gui with the listed section groups
# (default: all); groups not listed get empty tables so unfinished files cannot break the build.
set -e
OUT=$1; shift
SRC=$(cd "$(dirname "$0")" && pwd)
ALL="cpu machine sensors storage network os gpu bus tools siv"
USE=${*:-$ALL}
mkdir -p "$OUT"
EMPTY="$OUT/empties.c"
echo '#include "sec.h"' > "$EMPTY"
FILES=""
for g in $ALL; do
  if [[ " $USE " == *" $g "* ]]; then FILES="$FILES $SRC/sec_$g.c"
  else echo "const srs_item sec_$g[] = { { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL } };" >> "$EMPTY"; fi
done
/usr/bin/gcc -std=c11 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -O2 -Wall -Wextra -Wno-format-truncation -Wno-deprecated-declarations -pthread -I"$SRC" \
  $(pkg-config --cflags gtk+-3.0) -DSRS_NO_MAIN -o "$OUT/srs-gui" "$SRC/gui_gtk.c" "$SRC/core.c" "$SRC/cli.c" \
  "$SRC/plat_posix.c" "$SRC/burner.c" "$SRC/bench.c" "$SRC/sec_core.c" "$SRC/sec_stubs.c" $FILES "$EMPTY" \
  $(pkg-config --libs gtk+-3.0) -lm
echo "built $OUT/srs-gui with: $USE"
