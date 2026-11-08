#!/bin/bash
# usage: tools/preview/preview.sh "<button script>" "<frames to dump, comma separated>" [font.ttf]
# buttons: U D L R X O T S [ ]  ('.' = nothing), one per frame
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
OUT="${PREVIEW_OUT:-/tmp/preview}"; mkdir -p "$OUT/build"
FONT="${3:-$HERE/font.ttf}"
python3 - "$FONT" "$OUT/build" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
open(sys.argv[2] + '/dejavusans_ttf_bin.h', 'w').write('extern const unsigned char dejavusans_ttf_bin[];\nextern const unsigned int dejavusans_ttf_bin_size;\n')
open(sys.argv[2] + '/font_data.c', 'w').write('const unsigned char dejavusans_ttf_bin[] = {' + ','.join(str(b) for b in d) + '};\nconst unsigned int dejavusans_ttf_bin_size = %d;\n' % len(d))
PY
gcc -O1 -w -I"$HERE/mock" -I"$OUT/build" -I/usr/include/freetype2 \
    "$ROOT/source/main.c" "$ROOT/source/gfx.c" "$HERE/mock/mock.c" "$HERE/mock/stubs.c" "$OUT/build/font_data.c" \
    -lfreetype -lm -o "$OUT/build/dash"
rm -f "$OUT"/frame_*.txt "$OUT"/frame_*.png
PREVIEW_OUT="$OUT" PREVIEW_SCRIPT="$1" PREVIEW_DUMP="$2" "$OUT/build/dash" || true
for f in "$OUT"/frame_*.txt; do python3 "$HERE/render.py" "$f" "${f%.txt}.png" "$OUT/glyphs.bin"; echo "$f -> ${f%.txt}.png"; done
