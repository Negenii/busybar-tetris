/* Dump frames for a promo render:
 * render_seq <out.raw> <start hh:mm:ss> <seconds> <fps> <colour> <seconds_style> <preroll_s>
 * seconds_style is a TetrisSeconds value (0 hides the seconds). The first
 * preroll_s seconds are rendered but not written, so the clip opens on a
 * built clock. Raw RGB frames, 72x16x3 each, back to back. */
#include "../tetris_render.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    if(argc < 8) { fprintf(stderr, "args\n"); return 1; }
    int hh, mm, ss; sscanf(argv[2], "%d:%d:%d", &hh, &mm, &ss);
    const int seconds = atoi(argv[3]), fps = atoi(argv[4]), preroll = atoi(argv[7]);
    const TetrisLook look = {
        .colour = (uint8_t)atoi(argv[5]),
        .hour24 = true,
        .speed = 128U,
        .colon = TetrisColonBlink,
        .seconds = (uint8_t)atoi(argv[6]),
        .still = false,
    };
    TetrisState state; tetris_state_reset(&state);
    FILE* o = fopen(argv[1], "wb");
    uint8_t f[TETRIS_FRAME_BYTES];
    const int64_t t0 = hh * 3600 + mm * 60 + ss - preroll;
    for(int i = 0; i < (seconds + preroll) * fps; i++) {
        const int64_t ms = (int64_t)i * 1000 / fps;
        tetris_render(f, &state, &look, t0 + ms / 1000, (uint32_t)(ms % 1000), true);
        if(i >= preroll * fps) fwrite(f, 1, sizeof f, o);
    }
    fclose(o);
    return 0;
}
