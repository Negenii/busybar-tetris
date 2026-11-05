#!/bin/sh
# Build Tetris.fap inside a VeryBUSY checkout (VERYBUSY_DIR, default the local
# clone). The checkout's firmware must already be built once; this only builds
# the app. The result lands in dist/Tetris.fap.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${VERYBUSY_DIR:-$HOME/Developer/Busybar/VVBB/VeryBUSY}"

"$HERE/app/tests/run.sh"

rm -rf "$DIR/applications/external/tetris"
mkdir -p "$DIR/applications/external/tetris"
cp -R "$HERE/app/." "$DIR/applications/external/tetris/"
rm -f "$DIR/applications/external/tetris/tests/tetris_render_test" "$DIR/applications/external/tetris/tests/"*.ppm

cd "$DIR"
./fbt TARGET_HW=22 COMPACT=1 FIRMWARE_ORIGIN=VeryBUSY fap_tetris
rm -rf "$DIR/applications/external/tetris"

mkdir -p "$HERE/dist"
cp fbt_layers/fbtng/build/f22-firmware-DC/.extapps/Tetris.fap "$HERE/dist/Tetris.fap"
echo "built: $HERE/dist/Tetris.fap"
