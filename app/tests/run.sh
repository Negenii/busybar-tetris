#!/bin/sh
# Build and run the host test, from any directory.
set -e
cd "$(dirname "$0")"
cc -std=c99 -Wall -Wextra -Werror -O1 -I.. -o tetris_render_test tetris_render_test.c ../tetris_render.c
./tetris_render_test covers_tc002.txt
