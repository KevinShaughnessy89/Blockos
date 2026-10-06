#!/bin/sh
set -eu
ROOT=${ROOT:-$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)}
SYSROOT=${SYSROOT:-$ROOT/build/sysroot}
OUT=${OUT:-$SYSROOT/System/bin}
mkdir -p "$OUT" "$SYSROOT/System/lib"
: "${CLANG_BIN:?Set CLANG_BIN to a BlockOS-compatible clang executable}"
cp -f "$CLANG_BIN" "$OUT/clang"
chmod +x "$OUT/clang"
if [ -n "${CLANG_LIBDIR:-}" ] && [ -d "$CLANG_LIBDIR" ]; then cp -a "$CLANG_LIBDIR"/. "$SYSROOT/System/lib/"; fi
echo "BlockOS clang staged at $OUT/clang"
