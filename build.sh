#!/bin/sh
# OpenEdit for OS 3.2 with the os32 stove (m68k-amigaos-gcc, NDK 3.2), and
# OpenGadTools' sources beside it. 68020 and up, integer maths only.
#   ./build.sh [OUT_DIR]           (default build/os3)
#   OGT=path/to/opengadtools ./build.sh
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
OGT=${OGT:-$HERE/../opengadtools}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT/Kinds"
FLAGS="-noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-common -DOE_AMIGA -I$HERE/engine -I$HERE/app -I$OGT/lib"
"$CC" $FLAGS -o "$OUT/OpenEdit" \
    "$HERE/app/openedit.c" "$HERE/app/oe_draw.c" "$HERE/app/oe_file.c" "$HERE/app/oe_prefs.c" \
    "$HERE/app/oe_req.c" "$HERE/app/oe_rexx.c" "$HERE/app/oe_stack.c" \
    "$HERE/engine/oe_buf.c" "$HERE/engine/oe_doc.c" "$HERE/engine/oe_find.c" "$HERE/engine/oe_pat.c" \
    "$HERE/engine/oe_syntax.c" \
    "$OGT/lib/ogt_theme.c" "$OGT/lib/ogt_draw.c" "$OGT/lib/ogt_font.c"
cp "$HERE"/kinds/*.kind "$OUT/Kinds/"
echo "$OUT/OpenEdit ($(wc -c < "$OUT/OpenEdit") bytes)"
