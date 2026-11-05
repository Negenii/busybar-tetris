/**
 * @file tetris_render.c
 * @brief The tetris clock: the cover solver, the falling, the colours.
 *
 * A line-for-line port of the TC002 face. The covers come out the same as
 * there, because the solver tries the shape table in the same turning order.
 */
#include "tetris_render.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ shapes */

/* every orientation of every tetromino, as four offsets from the piece's first
 * cell. Written out rather than rotated at run time: these are the shapes
 * themselves, which can be read and checked. */
typedef struct {
    uint8_t kind;
    int8_t dx[4];
    int8_t dy[4];
} TetrisShape;

static const TetrisShape shapes[] = {
    /* I */
    {TetrisKindI, {0, 1, 2, 3}, {0, 0, 0, 0}},
    {TetrisKindI, {0, 0, 0, 0}, {0, 1, 2, 3}},
    /* O */
    {TetrisKindO, {0, 1, 0, 1}, {0, 0, 1, 1}},
    /* T */
    {TetrisKindT, {0, 1, 2, 1}, {0, 0, 0, 1}},
    {TetrisKindT, {0, 0, 0, 1}, {0, 1, 2, 1}},
    {TetrisKindT, {0, 1, 2, 1}, {1, 1, 1, 0}},
    {TetrisKindT, {1, 1, 1, 0}, {0, 1, 2, 1}},
    /* S */
    {TetrisKindS, {1, 2, 0, 1}, {0, 0, 1, 1}},
    {TetrisKindS, {0, 0, 1, 1}, {0, 1, 1, 2}},
    /* Z */
    {TetrisKindZ, {0, 1, 1, 2}, {0, 0, 1, 1}},
    {TetrisKindZ, {1, 0, 1, 0}, {0, 1, 1, 2}},
    /* J */
    {TetrisKindJ, {0, 0, 1, 2}, {0, 1, 1, 1}},
    {TetrisKindJ, {0, 1, 0, 0}, {0, 0, 1, 2}},
    {TetrisKindJ, {0, 1, 2, 2}, {0, 0, 0, 1}},
    {TetrisKindJ, {1, 1, 0, 1}, {0, 1, 2, 2}},
    /* L */
    {TetrisKindL, {0, 1, 2, 0}, {0, 0, 0, 1}},
    {TetrisKindL, {0, 0, 0, 1}, {0, 1, 2, 2}},
    {TetrisKindL, {2, 0, 1, 2}, {0, 1, 1, 1}},
    {TetrisKindL, {0, 1, 1, 1}, {0, 0, 1, 2}},
};
#define SHAPE_COUNT (sizeof(shapes) / sizeof(shapes[0]))

/* ------------------------------------------------------------------ glyphs */

/* the ten digits, two cells thick everywhere so a tetromino has somewhere to
 * sit. each one's area is a multiple of four, which the tests check. */
static const char* const glyph_rows[10][TETRIS_DIGIT_H] = {
    {"######", "######", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "######", "######"},
    {"####..", "####..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "..##..", "######", "######"},
    {"######", "######", "....##", "....##", "....##", "######", "######", "##....", "##....", "##....", "######", "######"},
    {"######", "######", "....##", "....##", "....##", "######", "######", "....##", "....##", "....##", "######", "######"},
    {"##..##", "##..##", "##..##", "##..##", "######", "######", "....##", "....##", "....##", "....##", "....##", "....##"},
    {"######", "######", "##....", "##....", "##....", "######", "######", "....##", "....##", "....##", "######", "######"},
    {"######", "######", "##....", "##....", "##....", "##....", "######", "######", "##..##", "##..##", "######", "######"},
    {"######", "######", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##"},
    {"######", "######", "##..##", "##..##", "##..##", "######", "######", "##..##", "##..##", "##..##", "######", "######"},
    {"######", "######", "##..##", "##..##", "######", "######", "....##", "....##", "....##", "....##", "######", "######"},
};

uint8_t tetris_glyph(uint8_t d, uint8_t x, uint8_t y) {
    if((d > 9U) || (x >= TETRIS_DIGIT_W) || (y >= TETRIS_DIGIT_H)) return 0U;
    return (glyph_rows[d][y][x] == '#') ? 1U : 0U;
}

uint32_t tetris_area(uint8_t d) {
    uint32_t n = 0U;
    for(uint8_t y = 0U; y < TETRIS_DIGIT_H; y++) {
        for(uint8_t x = 0U; x < TETRIS_DIGIT_W; x++) n += tetris_glyph(d, x, y);
    }
    return n;
}

uint8_t tetris_piece_bottom(const TetrisPiece* piece) {
    uint8_t m = 0U;
    for(int k = 0; k < 4; k++) {
        if(piece->y[k] > m) m = piece->y[k];
    }
    return m;
}

/* ------------------------------------------------------------------ solver */

/* Plain backtracking over the first uncovered cell. That cell has to be covered
 * by something, and only shapes whose first offset lands on it are worth
 * trying, which prunes hard enough that a digit solves in well under a
 * millisecond. */
static bool solve(uint8_t d, bool* filled, TetrisCover* out) {
    int start = -1;
    for(int i = 0; i < TETRIS_DIGIT_CELLS; i++) {
        if(tetris_glyph(d, (uint8_t)(i % TETRIS_DIGIT_W), (uint8_t)(i / TETRIS_DIGIT_W)) &&
           !filled[i]) {
            start = i;
            break;
        }
    }
    if(start < 0) return true; /* nothing left: the digit is covered */

    const int sx = start % TETRIS_DIGIT_W;
    const int sy = start / TETRIS_DIGIT_W;
    /* The table is tried in a different order for each digit and each step
     * down: in its written order I-pieces were offered first every time, took
     * almost every cell, and digit 0 came out entirely cyan. */
    const size_t spin = ((size_t)d * 5U + (size_t)out->count * 3U) % SHAPE_COUNT;

    for(size_t n = 0U; n < SHAPE_COUNT; n++) {
        const TetrisShape* s = &shapes[(n + spin) % SHAPE_COUNT];
        int idx[4];
        bool fits = true;
        for(int k = 0; k < 4; k++) {
            const int x = sx + s->dx[k] - s->dx[0];
            const int y = sy + s->dy[k] - s->dy[0];
            if((x < 0) || (x >= TETRIS_DIGIT_W) || (y < 0) || (y >= TETRIS_DIGIT_H) ||
               !tetris_glyph(d, (uint8_t)x, (uint8_t)y)) {
                fits = false;
                break;
            }
            idx[k] = y * TETRIS_DIGIT_W + x;
            if(filled[idx[k]]) {
                fits = false;
                break;
            }
        }
        if(!fits) continue;

        TetrisPiece* p = &out->pieces[out->count];
        p->kind = s->kind;
        for(int k = 0; k < 4; k++) {
            filled[idx[k]] = true;
            p->x[k] = (uint8_t)(idx[k] % TETRIS_DIGIT_W);
            p->y[k] = (uint8_t)(idx[k] / TETRIS_DIGIT_W);
        }
        out->count++;

        if(solve(d, filled, out)) return true;

        out->count--;
        for(int k = 0; k < 4; k++) filled[idx[k]] = false;
    }
    return false;
}

/* lowest piece first, ties left to right: a stack fills from the floor. A
 * stable insertion sort, the same order Zig's std.mem.sort gives. */
static bool falls_before(const TetrisPiece* a, const TetrisPiece* b) {
    const uint8_t ab = tetris_piece_bottom(a);
    const uint8_t bb = tetris_piece_bottom(b);
    if(ab != bb) return ab > bb;
    return a->x[0] < b->x[0];
}

static void sort_by_fall(TetrisCover* c) {
    for(uint8_t i = 1U; i < c->count; i++) {
        const TetrisPiece p = c->pieces[i];
        int j = (int)i - 1;
        while((j >= 0) && falls_before(&p, &c->pieces[j])) {
            c->pieces[j + 1] = c->pieces[j];
            j--;
        }
        c->pieces[j + 1] = p;
    }
}

bool tetris_solve(uint8_t d, TetrisCover* out) {
    bool filled[TETRIS_DIGIT_CELLS];
    memset(filled, 0, sizeof(filled));
    memset(out, 0, sizeof(*out));
    if((d > 9U) || !solve(d, filled, out)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    sort_by_fall(out);
    return true;
}

static TetrisCover covers[10];
static int covers_state = 0; /* 0 unsolved, 1 solved, -1 failed */

const TetrisCover* tetris_covers(void) {
    if(covers_state == 0) {
        covers_state = 1;
        for(uint8_t d = 0U; d < 10U; d++) {
            if(!tetris_solve(d, &covers[d])) covers_state = -1;
        }
    }
    return (covers_state == 1) ? covers : NULL;
}

/* ------------------------------------------------------------------ layout */

/* four digits of six, a colon of two, and the gaps left over: two columns
 * between the digits of a pair, four either side of the colon, the whole thing
 * centred. 38 wide, as on the TC002, so 17 dark columns either side here. */
#define DIGIT_GAP (2)
#define COLON_GAP (4)
#define COLON_W   (2)
#define TOTAL_W   (4 * TETRIS_DIGIT_W + 2 * DIGIT_GAP + 2 * COLON_GAP + COLON_W)
#define LEFT      ((TETRIS_PANEL_W - TOTAL_W) / 2)
#define TOP       ((TETRIS_PANEL_H - TETRIS_DIGIT_H) / 2)
#define COLON_X   (LEFT + 2 * TETRIS_DIGIT_W + DIGIT_GAP + COLON_GAP)

/* SPIKE: hh:mm:ss, six digits of six, the same gaps, two colons: 62 wide, 5 dark either side */
#define LEFT6 ((TETRIS_PANEL_W - (6 * TETRIS_DIGIT_W + 3 * DIGIT_GAP + 2 * (2 * COLON_GAP + COLON_W))) / 2)
#define PAIR6 (2 * TETRIS_DIGIT_W + DIGIT_GAP + 2 * COLON_GAP + COLON_W)
static const int digit_x6[6] = {
    LEFT6,
    LEFT6 + TETRIS_DIGIT_W + DIGIT_GAP,
    LEFT6 + PAIR6,
    LEFT6 + PAIR6 + TETRIS_DIGIT_W + DIGIT_GAP,
    LEFT6 + 2 * PAIR6,
    LEFT6 + 2 * PAIR6 + TETRIS_DIGIT_W + DIGIT_GAP,
};
static const int colon_x6[2] = {
    LEFT6 + 2 * TETRIS_DIGIT_W + DIGIT_GAP + COLON_GAP,
    LEFT6 + PAIR6 + 2 * TETRIS_DIGIT_W + DIGIT_GAP + COLON_GAP,
};

static const int digit_x[4] = {
    LEFT,
    LEFT + TETRIS_DIGIT_W + DIGIT_GAP,
    LEFT + 2 * TETRIS_DIGIT_W + DIGIT_GAP + COLON_GAP + COLON_W + COLON_GAP,
    LEFT + 3 * TETRIS_DIGIT_W + 2 * DIGIT_GAP + COLON_GAP + COLON_W + COLON_GAP,
};

/* ----------------------------------------------------------------- colours */

/* the seven tetromino colours, dimmed to what a small panel at close range can
 * be looked at. full-brightness cyan next to a face is a lamp, not a colour. */
static const uint8_t piece_rgb[TetrisKindCount][3] = {
    {0, 190, 200}, /* I cyan */
    {225, 185, 0}, /* O yellow */
    {165, 70, 200}, /* T purple */
    {60, 195, 70}, /* S green */
    {215, 55, 55}, /* Z red */
    {50, 95, 225}, /* J blue */
    {230, 125, 20}, /* L orange */
};
static const uint8_t amber_rgb[3] = {255, 150, 40};
static const uint8_t white_rgb[3] = {235, 235, 225};
static const uint8_t green_rgb[3] = {70, 220, 90};

static const uint8_t* colour_of(const TetrisLook* look, uint8_t kind, bool falling) {
    const uint8_t c = (look->colour < TetrisColourCount) ? look->colour : TetrisColourSettle;
    switch(c) {
    case TetrisColourAmber:
        return amber_rgb;
    case TetrisColourWhite:
        return white_rgb;
    case TetrisColourGreen:
        return green_rgb;
    case TetrisColourSettle:
        return falling ? piece_rgb[kind % TetrisKindCount] : amber_rgb;
    default:
        return piece_rgb[kind % TetrisKindCount];
    }
}

/* ------------------------------------------------------------------ timing */

/* ms a single piece takes to fall: 150 at the slow end, 40 at the fast */
static uint32_t piece_ms(const TetrisLook* look) {
    return 150U - (uint32_t)look->speed * 110U / 255U;
}

/* ms a digit takes to fall out of the panel before its replacement starts */
static uint32_t clear_ms(const TetrisLook* look) {
    return piece_ms(look) * 4U;
}

/* ----------------------------------------------------------------- drawing */

static void put(uint8_t* rgb, int x, int y, const uint8_t* c) {
    if((x < 0) || (y < 0) || (x >= TETRIS_PANEL_W) || (y >= TETRIS_PANEL_H)) return;
    uint8_t* p = rgb + ((size_t)y * TETRIS_PANEL_W + (size_t)x) * 3U;
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
}

static void draw_piece(
    uint8_t* rgb,
    const TetrisLook* look,
    const TetrisPiece* p,
    int ox,
    int oy,
    bool falling) {
    const uint8_t* c = colour_of(look, p->kind, falling);
    for(int k = 0; k < 4; k++) put(rgb, ox + p->x[k], oy + p->y[k], c);
}

static const TetrisCover* cover_of(const TetrisColumn* col) {
    const TetrisCover* all = tetris_covers();
    return all ? &all[(col->value > 9U) ? 9U : col->value] : NULL;
}

/* SPIKE: the seconds' quick builds. A wave is every piece whose bottom row is
 * in the same band, counted from the floor; a wave's pieces fall together. */
static bool fast_style(uint8_t style) {
    return (style == TetrisSecondsQuick) || (style == TetrisSecondsQuicker) ||
           (style == TetrisSecondsSnap);
}

static uint8_t wave_band(uint8_t style) {
    return (style == TetrisSecondsQuick) ? 2U : (style == TetrisSecondsQuicker) ? 3U : TETRIS_DIGIT_H;
}

static uint32_t wave_ms(uint8_t style) {
    return (style == TetrisSecondsQuick) ? 70U : (style == TetrisSecondsQuicker) ? 60U : 150U;
}

static uint8_t wave_of(const TetrisPiece* p, uint8_t style) {
    return (uint8_t)((TETRIS_DIGIT_H - 1U - tetris_piece_bottom(p)) / wave_band(style));
}

static uint8_t last_wave(const TetrisCover* cover, uint8_t style) {
    uint8_t m = 0U;
    for(uint8_t i = 0U; i < cover->count; i++) {
        const uint8_t w = wave_of(&cover->pieces[i], style);
        if(w > m) m = w;
    }
    return m;
}

static void draw_fast(uint8_t* rgb, const TetrisLook* look, const TetrisColumn* col, int ox, uint8_t style) {
    const TetrisCover* cover = cover_of(col);
    if(!cover) return;
    for(uint8_t i = 0U; i < cover->count; i++) {
        const TetrisPiece* p = &cover->pieces[i];
        const uint8_t w = wave_of(p, style);
        if((col->phase == TetrisPhaseStill) || (w < col->wave)) {
            draw_piece(rgb, look, p, ox, TOP, false);
        } else if(w == col->wave) {
            const int travel = TOP + (int)tetris_piece_bottom(p) + 1;
            int gone = (int)(col->at_ms * (uint32_t)travel / wave_ms(style));
            if(gone > travel) gone = travel;
            draw_piece(rgb, look, p, ox, TOP - travel + gone, true);
        }
    }
}

static void draw_column(uint8_t* rgb, const TetrisLook* look, const TetrisColumn* col, int ox) {
    const TetrisCover* cover = cover_of(col);
    if(!cover) return;

    switch(col->phase) {
    case TetrisPhaseStill:
        for(uint8_t i = 0U; i < cover->count; i++) {
            draw_piece(rgb, look, &cover->pieces[i], ox, TOP, false);
        }
        break;

    case TetrisPhaseClearing: {
        /* the whole digit slides out of the bottom of the panel together */
        const uint32_t span = TETRIS_PANEL_H + TETRIS_DIGIT_H;
        uint32_t drop = col->at_ms * span / (clear_ms(look) ? clear_ms(look) : 1U);
        if(drop > span) drop = span;
        for(uint8_t i = 0U; i < cover->count; i++) {
            draw_piece(rgb, look, &cover->pieces[i], ox, TOP + (int)drop, false);
        }
        break;
    }

    case TetrisPhaseBuilding:
        for(uint8_t i = 0U; (i < col->landed) && (i < cover->count); i++) {
            draw_piece(rgb, look, &cover->pieces[i], ox, TOP, false);
        }
        if(col->landed < cover->count) {
            const TetrisPiece* p = &cover->pieces[col->landed];
            /* from above the panel down to where it belongs, linearly: a
             * falling tetromino does not accelerate in any tetris anybody has
             * played */
            const int travel = TOP + (int)tetris_piece_bottom(p) + 1;
            const uint32_t pm = piece_ms(look) ? piece_ms(look) : 1U;
            int gone = (int)(col->at_ms * (uint32_t)travel / pm);
            if(gone > travel) gone = travel;
            draw_piece(rgb, look, p, ox, TOP - travel + gone, true);
        }
        break;

    default:
        break;
    }
}

static void draw_colon_at(uint8_t* rgb, const TetrisLook* look, uint32_t ms, bool lit, int colon_x) {
    bool on;
    switch(look->colon) {
    case TetrisColonSolid:
        on = lit;
        break;
    case TetrisColonOff:
        on = false;
        break;
    default:
        on = lit && (ms < 500U);
        break;
    }
    if(!on) return;

    const uint8_t* c = colour_of(look, TetrisKindO, false);
    /* two squares, a third of the way down and a third up, so they sit with the digits */
    for(int dy = 0; dy < 2; dy++) {
        for(int dx = 0; dx < 2; dx++) {
            put(rgb, colon_x + dx, TOP + 3 + dy, c);
            put(rgb, colon_x + dx, TOP + TETRIS_DIGIT_H - 5 + dy, c);
        }
    }
}

/* ------------------------------------------------------------------- state */

static void column_show(TetrisColumn* col, uint8_t value) {
    const TetrisCover* all = tetris_covers();
    col->value = (value > 9U) ? 9U : value;
    col->next = col->value;
    col->landed = all ? all[col->value].count : 0U;
    col->phase = TetrisPhaseStill;
    col->at_ms = 0U;
}

/* `value` is the digit this column is heading for, which is the only question
 * worth asking. The first TC002 version compared value and phase, so being told
 * the same digit while it was still building counted as a change: it cleared,
 * rebuilt and never settled, which reads as a digit permanently a row low. The
 * digit falling out is the old one, so `value` waits until the clear is over. */
static void column_change_to(TetrisColumn* col, uint8_t value, bool fast) {
    const uint8_t want = (value > 9U) ? 9U : value;
    if((col->value == want) || (col->next == want)) return;
    col->next = want;
    col->at_ms = 0U;
    if(fast) {
        /* SPIKE: no slide out; the old digit is gone and the new one lands in waves */
        col->value = want;
        col->landed = 0U;
        col->wave = 0U;
        col->phase = TetrisPhaseBuilding;
    } else {
        col->phase = TetrisPhaseClearing;
    }
}

static void step_fast(TetrisColumn* col, uint8_t style, uint32_t delta_ms) {
    if(col->phase != TetrisPhaseBuilding) return;
    const TetrisCover* cover = cover_of(col);
    if(!cover) return;
    col->at_ms += delta_ms;
    while(col->at_ms >= wave_ms(style)) {
        col->at_ms -= wave_ms(style);
        if(col->wave >= last_wave(cover, style)) {
            col->phase = TetrisPhaseStill;
            col->landed = cover->count;
            col->at_ms = 0U;
            return;
        }
        col->wave++;
    }
}

static void step(TetrisState* state, const TetrisLook* look, uint32_t delta_ms) {
    const uint32_t pm = piece_ms(look);
    const bool fast = fast_style(look->seconds);
    const int n = (look->seconds == TetrisSecondsOff) ? 4 : 6;
    for(int i = 0; i < n; i++) {
        if((i >= 4) && fast) {
            step_fast(&state->columns[i], look->seconds, delta_ms);
            continue;
        }
        TetrisColumn* col = &state->columns[i];
        switch(col->phase) {
        case TetrisPhaseClearing:
            col->at_ms += delta_ms;
            if(col->at_ms >= clear_ms(look)) {
                /* the old digit is off the panel; the new one starts falling in */
                col->value = col->next;
                col->landed = 0U;
                col->at_ms = 0U;
                col->phase = TetrisPhaseBuilding;
            }
            break;

        case TetrisPhaseBuilding: {
            col->at_ms += delta_ms;
            const TetrisCover* cover = cover_of(col);
            const uint8_t total = cover ? cover->count : 0U;
            while((col->at_ms >= pm) && (col->landed < total)) {
                col->at_ms -= pm;
                col->landed++;
            }
            if(col->landed >= total) {
                col->phase = TetrisPhaseStill;
                col->at_ms = 0U;
            }
            break;
        }

        default:
            break;
        }
    }
}

void tetris_state_reset(TetrisState* state) {
    memset(state, 0, sizeof(*state));
    for(int i = 0; i < 6; i++) column_show(&state->columns[i], 0U);
}

void tetris_render(
    uint8_t* rgb,
    TetrisState* state,
    const TetrisLook* look,
    int64_t local_s,
    uint32_t ms,
    bool clock_set) {
    memset(rgb, 0, TETRIS_FRAME_BYTES);

    const int64_t day = ((local_s % 86400) + 86400) % 86400;
    const uint8_t h24 = (uint8_t)(day / 3600);
    const uint8_t minute = (uint8_t)((day % 3600) / 60);
    uint8_t hour = h24;
    if(!look->hour24) {
        hour = (uint8_t)(h24 % 12U);
        if(hour == 0U) hour = 12U;
    }
    const uint8_t second = (uint8_t)(day % 60);
    const uint8_t want[6] = {
        (uint8_t)(hour / 10U),
        (uint8_t)(hour % 10U),
        (uint8_t)(minute / 10U),
        (uint8_t)(minute % 10U),
        (uint8_t)(second / 10U),
        (uint8_t)(second % 10U)};
    const bool six = look->seconds != TetrisSecondsOff;
    const int n = six ? 6 : 4;
    const bool fast = fast_style(look->seconds);

    const int64_t now = local_s * 1000 + (int64_t)ms;
    if(!state->started) {
        state->started = true;
        state->last_ms = now;
        /* the first frame assembles rather than appearing: it is the nicest
         * moment this face has. staggered, so four digits do not fall in lockstep */
        for(int i = 0; i < 6; i++) {
            TetrisColumn* col = &state->columns[i];
            column_show(col, clock_set ? want[i] : 0U);
            col->landed = 0U;
            col->phase = TetrisPhaseBuilding;
            col->at_ms = (uint32_t)i * piece_ms(look) / 2U;
        }
    }

    /* clamped: a clock correction or a pause would otherwise fling every piece
     * through the floor at once */
    int64_t delta = now - state->last_ms;
    if(delta < 0) delta = 0;
    if(delta > 500) delta = 500;
    state->last_ms = now;

    /* a leading zero on the hour is a blank column in 12-hour mode, which is
     * what a clock on a wall does. in 24-hour mode it is a zero like any other */
    const bool blank_lead = !look->hour24 && (want[0] == 0U);

    if(look->still) {
        /* no animation: whatever a column is doing, it shows its digit now.
         * Switching animation off mid-fall lands everything at once. */
        for(int i = 0; i < 6; i++) {
            TetrisColumn* col = &state->columns[i];
            const uint8_t d = clock_set ? want[i] : 0U;
            if((col->value != d) || (col->phase != TetrisPhaseStill)) column_show(col, d);
        }
    } else if(clock_set) {
        for(int i = 0; i < n; i++) {
            if((i == 0) && blank_lead) continue;
            column_change_to(&state->columns[i], want[i], fast && (i >= 4));
        }
    }
    step(state, look, (uint32_t)delta);

    for(int i = 0; i < n; i++) {
        if((i == 0) && blank_lead && (state->columns[0].phase == TetrisPhaseStill)) continue;
        const int x = six ? digit_x6[i] : digit_x[i];
        if((i >= 4) && fast && (state->columns[i].phase != TetrisPhaseClearing)) {
            draw_fast(rgb, look, &state->columns[i], x, look->seconds);
        } else {
            draw_column(rgb, look, &state->columns[i], x);
        }
    }
    if(six) {
        /* the seconds already tick; a blinking colon beside them is a second
         * thing counting the same second, so it holds steady */
        TetrisLook steady = *look;
        if(steady.colon == TetrisColonBlink) steady.colon = TetrisColonSolid;
        look = &steady;
        draw_colon_at(rgb, look, ms, clock_set, colon_x6[0]);
        draw_colon_at(rgb, look, ms, clock_set, colon_x6[1]);
    } else {
        draw_colon_at(rgb, look, ms, clock_set, COLON_X);
    }
}
