#!/bin/sh
# OpenEdit for OS 3.2 with the os32-gcc16 stove (m68k-amigaos-gcc 16, NDK 3.2), and
# OpenGadTools' sources beside it. Built for the 68040, integer maths only.
#   ./build.sh [OUT_DIR]           (default build/os3)
#   OGT=path/to/opengadtools ./build.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
# The compiler: OS32_GCC16, else STOVE, else ~/AmigaChrome/stoves/os32-gcc16. It stops when given an older GCC
# (the os32 stove's GCC 6.5.0b merges a word clear and the long clear after it into one clr.l at -O2) and builds
# -fno-tree-loop-distribute-patterns (GCC 16.2.0b's loop-shift trap).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# Soft float: the specs file links libnix's soft libm020 (maths through the
# mathieee libraries), not libm881, so one build runs on every 68040.
SPECS="-specs=$HERE/toolchain/m68k-soft-m68040.specs"
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
OGT=${OGT:-$HERE/../opengadtools}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: this needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT/Kinds"
FLAGS="-noixemul -m68040 $SPECS -std=gnu99 -Wall -Werror -O2 -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fno-common -DOE_AMIGA -I$HERE/engine -I$HERE/app -I$OGT/lib"
"$CC" $FLAGS -o "$OUT/OpenEdit" \
    "$HERE/app/openedit.c" "$HERE/app/oe_draw.c" "$HERE/app/oe_file.c" "$HERE/app/oe_prefs.c" \
    "$HERE/app/oe_req.c" "$HERE/app/oe_rexx.c" "$HERE/app/oe_stack.c" \
    "$HERE/engine/oe_buf.c" "$HERE/engine/oe_doc.c" "$HERE/engine/oe_find.c" "$HERE/engine/oe_pat.c" \
    "$HERE/engine/oe_syntax.c" \
    "$OGT/lib/ogt_theme.c" "$OGT/lib/ogt_draw.c" "$OGT/lib/ogt_font.c" "$OGT/lib/ogt_look.c"
cp "$HERE"/kinds/*.kind "$OUT/Kinds/"
echo "$OUT/OpenEdit ($(wc -c < "$OUT/OpenEdit") bytes)"
