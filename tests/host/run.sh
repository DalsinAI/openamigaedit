#!/bin/sh
# OpenEdit host tests: the engine, built for the machine running them.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/openedit-host-tests
mkdir -p "$OUT"
cc -std=c99 -Wall -Wextra -Werror -O1 -g ${CFLAGS:-} -I"$HERE/engine" \
    "$HERE/engine/oe_buf.c" "$HERE/engine/oe_doc.c" "$HERE/engine/oe_pat.c" \
    "$HERE/engine/oe_find.c" "$HERE/engine/oe_syntax.c" \
    "$HERE/tests/host/test_engine.c" -o "$OUT/test_engine"
"$OUT/test_engine" "$HERE/kinds"
