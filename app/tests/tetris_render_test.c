/* Host test for the tetris renderer. Run tests/run.sh. */
#include "../tetris_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, ...)                                \
    do {                                                \
        if(!(cond)) {                                   \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while(0)

static uint8_t frame[TETRIS_FRAME_BYTES];

static int lit(const uint8_t* rgb, int x, int y) {
    const uint8_t* p = rgb + (y * TETRIS_PANEL_W + x) * 3;
    return p[0] || p[1] || p[2];
}

static void write_ppm(const char* path, const uint8_t* rgb) {
    FILE* f = fopen(path, "wb");
    if(!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TETRIS_PANEL_W, TETRIS_PANEL_H);
    fwrite(rgb, 1, TETRIS_FRAME_BYTES, f);
    fclose(f);
}

/* the digit drawn at column x0 matches glyph d exactly, rows 2..13 */
static int digit_on_panel(const uint8_t* rgb, int x0, uint8_t d) {
    for(int y = 0; y < TETRIS_DIGIT_H; y++) {
        for(int x = 0; x < TETRIS_DIGIT_W; x++) {
            if(lit(rgb, x0 + x, 2 + y) != (int)tetris_glyph(d, (uint8_t)x, (uint8_t)y)) return 0;
        }
    }
    return 1;
}

/* 17 + 6 + 2, then the colon block (4 + 2 + 4) between the pairs */
static const int digit_x[4] = {17, 25, 41, 49};

/* run frames from t (seconds) for ms milliseconds at 30 fps */
static void run(TetrisState* st, const TetrisLook* look, int64_t t, uint32_t start_ms, uint32_t for_ms) {
    for(uint32_t e = 0; e <= for_ms; e += 33) {
        const uint32_t at = start_ms + e;
        tetris_render(frame, st, look, t + at / 1000, at % 1000, true);
    }
}

static void test_covers(void) {
    const TetrisCover* all = tetris_covers();
    CHECK(all != NULL, "a digit cannot be tiled");
    if(!all) return;

    int kinds_seen[TetrisKindCount] = {0};
    for(uint8_t d = 0; d < 10; d++) {
        const uint32_t area = tetris_area(d);
        CHECK(area > 0 && area % 4 == 0, "digit %d area %u is not whole pieces", d, area);
        CHECK(all[d].count == area / 4, "digit %d has %d pieces for area %u", d, all[d].count, area);

        /* exact: every lit cell once, every dark cell never, nothing outside */
        int seen[TETRIS_DIGIT_CELLS] = {0};
        int kinds[TetrisKindCount] = {0};
        int n_kinds = 0;
        uint8_t last_bottom = 255, lowest = 255;
        for(int i = 0; i < all[d].count; i++) {
            const TetrisPiece* p = &all[d].pieces[i];
            for(int k = 0; k < 4; k++) {
                CHECK(p->x[k] < TETRIS_DIGIT_W && p->y[k] < TETRIS_DIGIT_H, "digit %d piece outside", d);
                CHECK(tetris_glyph(d, p->x[k], p->y[k]), "digit %d piece on a dark cell", d);
                seen[p->y[k] * TETRIS_DIGIT_W + p->x[k]]++;
                if(p->y[k] < lowest) lowest = p->y[k];
            }
            /* lowest piece first, so a digit builds up rather than hanging */
            CHECK(tetris_piece_bottom(p) <= last_bottom, "digit %d does not fall bottom first", d);
            last_bottom = tetris_piece_bottom(p);
            if(!kinds[p->kind]) n_kinds++;
            kinds[p->kind] = 1;
            kinds_seen[p->kind] = 1;
        }
        for(int i = 0; i < TETRIS_DIGIT_CELLS; i++) {
            const int want = tetris_glyph(d, (uint8_t)(i % TETRIS_DIGIT_W), (uint8_t)(i / TETRIS_DIGIT_W));
            CHECK(seen[i] == want, "digit %d cell %d covered %d times", d, i, seen[i]);
        }
        /* the top row is used: a cover starting at row one draws every digit a pixel low */
        CHECK(lowest == 0, "digit %d never reaches the top row", d);
        CHECK(n_kinds >= 2, "digit %d is one kind of piece", d);
    }
    int total_kinds = 0;
    for(int k = 0; k < TetrisKindCount; k++) total_kinds += kinds_seen[k];
    CHECK(total_kinds >= 5, "the clock only shows %d of seven pieces", total_kinds);
}

/* The covers are the TC002's, piece for piece: same look on both devices. */
static void test_same_as_tc002(const char* fixture) {
    FILE* f = fopen(fixture, "r");
    CHECK(f != NULL, "cannot open %s", fixture);
    if(!f) return;
    const TetrisCover* all = tetris_covers();
    int index[10] = {0};
    int lines = 0, d, kind, x[4], y[4];
    while(fscanf(f, "%d %d %d,%d %d,%d %d,%d %d,%d", &d, &kind, &x[0], &y[0], &x[1], &y[1], &x[2], &y[2], &x[3], &y[3]) == 10) {
        lines++;
        if(d < 0 || d > 9 || !all) continue;
        const int i = index[d]++;
        if(i >= all[d].count) {
            CHECK(0, "digit %d has fewer pieces than the TC002", d);
            continue;
        }
        const TetrisPiece* p = &all[d].pieces[i];
        int same = (p->kind == kind);
        for(int k = 0; k < 4; k++) same = same && p->x[k] == x[k] && p->y[k] == y[k];
        CHECK(same, "digit %d piece %d differs from the TC002", d, i);
    }
    fclose(f);
    CHECK(lines == 118, "fixture has %d pieces, expected 118", lines);
    for(int i = 0; i < 10 && all; i++) CHECK(index[i] == all[i].count, "digit %d piece count differs", i);
}

static void test_layout_and_settle(void) {
    TetrisLook look = {TetrisColourClassic, true, 128, TetrisColonSolid, TetrisSecondsOff};
    TetrisState st;
    tetris_state_reset(&st);
    const int64_t t = 13 * 3600 + 45 * 60 + 10; /* 13:45:10 */

    run(&st, &look, t, 0, 3000);
    write_ppm("frame_1345.ppm", frame);
    CHECK(digit_on_panel(frame, digit_x[0], 1), "13:45: first digit is not 1");
    CHECK(digit_on_panel(frame, digit_x[1], 3), "13:45: second digit is not 3");
    CHECK(digit_on_panel(frame, digit_x[2], 4), "13:45: third digit is not 4");
    CHECK(digit_on_panel(frame, digit_x[3], 5), "13:45: fourth digit is not 5");
    for(int y = 0; y < TETRIS_PANEL_H; y++) {
        for(int x = 0; x < 17; x++) CHECK(!lit(frame, x, y), "left margin lit at %d,%d", x, y);
        for(int x = 55; x < TETRIS_PANEL_W; x++) CHECK(!lit(frame, x, y), "right margin lit at %d,%d", x, y);
    }
    CHECK(lit(frame, 35, 5) && lit(frame, 36, 10), "colon missing");
}

static void test_only_changed_digit_moves(void) {
    TetrisLook look = {TetrisColourClassic, true, 128, TetrisColonOff, TetrisSecondsOff};
    TetrisState st;
    tetris_state_reset(&st);
    const int64_t t = 12 * 3600 + 59;
    run(&st, &look, 12 * 3600, 0, 3000); /* 12:00 settled */

    /* into 12:01: the last digit leaves, the other three stand still throughout */
    for(uint32_t e = 0; e <= 3000; e += 33) {
        tetris_render(frame, &st, &look, t + 1 + e / 1000, e % 1000, true);
        CHECK(digit_on_panel(frame, digit_x[0], 1) && digit_on_panel(frame, digit_x[1], 2) &&
                  digit_on_panel(frame, digit_x[2], 0),
              "a digit that did not change moved at +%u ms", e);
    }
    CHECK(digit_on_panel(frame, digit_x[3], 1), "12:01 did not settle");
    CHECK(st.columns[3].phase == TetrisPhaseStill, "last digit still moving");
}

/* the TC002 bug: being told the same digit mid-build restarted the clear forever */
static void test_settles_while_told_again(void) {
    TetrisLook look = {TetrisColourClassic, true, 128, TetrisColonOff, TetrisSecondsOff};
    TetrisState st;
    tetris_state_reset(&st);
    run(&st, &look, 8 * 3600 + 30 * 60, 0, 5000);
    for(int i = 0; i < 4; i++) CHECK(st.columns[i].phase == TetrisPhaseStill, "column %d never settles", i);
}

static void test_twelve_hour(void) {
    TetrisLook look = {TetrisColourAmber, false, 255, TetrisColonOff, TetrisSecondsOff};
    TetrisState st;
    tetris_state_reset(&st);
    run(&st, &look, 21 * 3600 + 7 * 60, 0, 3000); /* 21:07 -> 9:07 */
    for(int y = 0; y < TETRIS_PANEL_H; y++) {
        for(int x = digit_x[0]; x < digit_x[0] + 6; x++) CHECK(!lit(frame, x, y), "12h leading zero drawn");
    }
    CHECK(digit_on_panel(frame, digit_x[1], 9), "9:07 hour digit wrong");
    CHECK(digit_on_panel(frame, digit_x[3], 7), "9:07 minute digit wrong");
    write_ppm("frame_0907_amber.ppm", frame);
}

static void test_unset_clock_shows_zeros(void) {
    TetrisLook look = {TetrisColourClassic, true, 128, TetrisColonBlink, TetrisSecondsOff};
    TetrisState st;
    tetris_state_reset(&st);
    for(uint32_t e = 0; e <= 3000; e += 33) tetris_render(frame, &st, &look, 77777 + e / 1000, e % 1000, false);
    for(int i = 0; i < 4; i++) CHECK(digit_on_panel(frame, digit_x[i], 0), "unset clock digit %d not 0", i);
    CHECK(!lit(frame, 35, 5), "colon lit while the clock is unset");
}

/* with seconds on, the colons hold steady even when Blink is chosen; without, they still blink */
static void test_colon_steady_with_seconds(void) {
    TetrisLook look = {TetrisColourClassic, true, 128, TetrisColonBlink, TetrisSecondsQuick};
    TetrisState st;
    tetris_state_reset(&st);
    run(&st, &look, 10 * 3600, 0, 2000);
    tetris_render(frame, &st, &look, 10 * 3600 + 3, 700, true);
    CHECK(lit(frame, 23, 5) && lit(frame, 47, 5), "a colon is dark at 700 ms with seconds on");

    look.seconds = TetrisSecondsOff;
    tetris_state_reset(&st);
    run(&st, &look, 10 * 3600, 0, 2000);
    tetris_render(frame, &st, &look, 10 * 3600 + 3, 700, true);
    CHECK(!lit(frame, 35, 5), "hh:mm colon did not blink");

    look.seconds = TetrisSecondsQuick;
    look.colon = TetrisColonOff;
    tetris_render(frame, &st, &look, 10 * 3600 + 3, 200, true);
    CHECK(!lit(frame, 23, 5), "Off colon lit with seconds on");
}

int main(int argc, char** argv) {
    test_covers();
    test_same_as_tc002(argc > 1 ? argv[1] : "covers_tc002.txt");
    test_layout_and_settle();
    test_only_changed_digit_moves();
    test_settles_while_told_again();
    test_twelve_hour();
    test_unset_clock_shows_zeros();
    test_colon_steady_with_seconds();

    if(failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tetris checks passed\n");
    return 0;
}
