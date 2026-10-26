#!/bin/bash
# build_one.sh GROUP OUTDIR - build a test CLI with only sec_GROUP.c (+ the finished sec_cpu.c);
# every other group table is replaced by an empty one so parallel work never breaks this build.
# Then test with:  OUTDIR/srs-test '-save=[item1][item2]=OUTDIR/out.txt'   and   OUTDIR/srs-test --list
set -e
G=$1
OUT=$2
SRC=$(cd "$(dirname "$0")" && pwd)
[ -n "$G" ] && [ -n "$OUT" ] || { echo "usage: build_one.sh GROUP OUTDIR"; exit 2; }
mkdir -p "$OUT"
EMPTY="$OUT/empties.c"
echo '#include "sec.h"' > "$EMPTY"
for g in machine sensors storage network os gpu bus tools siv; do
  [ "$g" = "$G" ] && continue
  echo "const srs_item sec_$g[] = { { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL } };" >> "$EMPTY"
done
/usr/bin/gcc -std=c11 -D_DEFAULT_SOURCE -DSRS_HAVE_SECTIONS -O2 -Wall -Wextra -Wno-format-truncation -ffp-contract=off -pthread -I"$SRC" \
  -o "$OUT/srs-test" "$SRC/core.c" "$SRC/cli.c" "$SRC/plat_posix.c" "$SRC/burner.c" "$SRC/bench.c" "$SRC/sec_core.c" \
  "$SRC/sec_stubs.c" "$SRC/sec_cpu.c" "$SRC/sec_$G.c" "$EMPTY" -lm
echo "built $OUT/srs-test"
