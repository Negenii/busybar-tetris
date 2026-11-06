#!/bin/sh
# Build Tetris.fap against VeryBUSY.
#
# Clones the fork at the pinned release (or uses VERYBUSY_DIR), runs the host
# tests, drops the app into applications/external and builds it. The result
# lands in dist/Tetris.fap.
#
# Needs: git, python3, curl, tar, a C compiler (the fork fetches its own
# toolchain).
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PIN="1ec765be291fad99d7888a66e544fc34d1ef9e19"   # VeryBUSY-r14
DIR="${VERYBUSY_DIR:-$HERE/build/VeryBUSY}"

"$HERE/app/tests/run.sh"

if [ ! -d "$DIR/.git" ]; then
    mkdir -p "$(dirname "$DIR")"
    git clone --recursive https://code.mccullough.dev/christian/VeryBUSY.git "$DIR"
fi
cd "$DIR"
git fetch -q origin
git checkout -q "$PIN"
git submodule update --init --recursive -q

rm -rf applications/external/tetris
mkdir -p applications/external/tetris
cp -R "$HERE/app/." applications/external/tetris/
rm -f applications/external/tetris/tests/tetris_render_test applications/external/tetris/tests/*.ppm

python3 scripts/apply_custom_patches.py
./fbt TARGET_HW=22 COMPACT=1 FIRMWARE_ORIGIN=VeryBUSY INTERCOM_FORCE_VERSION=8edd2da4 fap_tetris
rm -rf applications/external/tetris

mkdir -p "$HERE/dist"
cp fbt_layers/fbtng/build/f22-firmware-DC/.extapps/Tetris.fap "$HERE/dist/Tetris.fap"
echo "built: $HERE/dist/Tetris.fap"
