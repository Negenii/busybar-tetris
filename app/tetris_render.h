/**
 * @file tetris_render.h
 * @brief A clock whose digits are built out of falling tetrominoes.
 *
 * A port of the Ulanzi TC002 tetris face (scene/tetris.zig and
 * tetris_cart_main.zig) to the 72x16 front panel. A digit is a 6x12 bitmap and
 * it is covered exactly by tetrominoes; only the digits that change come apart
 * and rebuild, lowest piece first.
 *
 * Integer only, no dependencies beyond the C standard headers, so the same file
 * compiles on the host for tests and in the FAP for the panel.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TETRIS_PANEL_W     (72)
#define TETRIS_PANEL_H     (16)
#define TETRIS_FRAME_BYTES (TETRIS_PANEL_W * TETRIS_PANEL_H * 3)

#define TETRIS_DIGIT_W     (6)
#define TETRIS_DIGIT_H     (12)
#define TETRIS_DIGIT_CELLS (TETRIS_DIGIT_W * TETRIS_DIGIT_H)
/* a digit of 60 cells is 15 pieces; the widest is 0 at 56 and 8 at 60 */
#define TETRIS_MAX_PIECES (TETRIS_DIGIT_CELLS / 4)

/* the seven tetrominoes, in the order everyone names them, which is also the colour order */
typedef enum {
    TetrisKindI = 0,
    TetrisKindO,
    TetrisKindT,
    TetrisKindS,
    TetrisKindZ,
    TetrisKindJ,
    TetrisKindL,
    TetrisKindCount,
} TetrisKind;

/* a placed piece: four cells, and which tetromino they are */
typedef struct {
    uint8_t kind;
    uint8_t x[4];
    uint8_t y[4];
} TetrisPiece;

typedef struct {
    TetrisPiece pieces[TETRIS_MAX_PIECES];
    uint8_t count;
} TetrisCover;

/** The lowest row a piece occupies, which decides the order it falls in. */
uint8_t tetris_piece_bottom(const TetrisPiece* piece);

/** 1 where digit @p d lights cell (@p x, @p y), 0 elsewhere. */
uint8_t tetris_glyph(uint8_t d, uint8_t x, uint8_t y);

/** How many cells digit @p d lights. */
uint32_t tetris_area(uint8_t d);

/**
 * Cover digit @p d with tetrominoes, exactly, lowest piece first.
 * Returns false (and leaves @p out empty) if the digit cannot be tiled.
 * Backtracking: call it once, not per frame. tetris_covers() keeps the result.
 */
bool tetris_solve(uint8_t d, TetrisCover* out);

/** The ten covers, solved on the first call and kept. NULL if any digit failed. */
const TetrisCover* tetris_covers(void);

typedef enum {
    /* the tetromino colours everybody knows: I cyan, O yellow, T purple, S green,
     * Z red, J blue, L orange */
    TetrisColourClassic = 0,
    TetrisColourAmber,
    TetrisColourWhite,
    TetrisColourGreen,
    /* the piece's colour, but only while it falls; it settles to amber */
    TetrisColourSettle,
    TetrisColourCount,
} TetrisColour;

typedef enum {
    TetrisColonBlink = 0,
    TetrisColonSolid,
    TetrisColonOff,
    TetrisColonCount,
} TetrisColon;

/* how the seconds are shown */
typedef enum {
    TetrisSecondsOff = 0, /* hh:mm only, the TC002 layout */
    TetrisSecondsClassic, /* seconds animate exactly like the minutes */
    TetrisSecondsQuick, /* no slide out; pieces land in 2-row waves, ~70 ms each */
    TetrisSecondsQuicker, /* no slide out; 3-row waves, ~60 ms each */
    TetrisSecondsSnap, /* no slide out; the whole digit drops in at once, ~150 ms */
    TetrisSecondsCount,
} TetrisSeconds;

typedef struct {
    uint8_t colour; /* TetrisColour */
    bool hour24; /* off drops the leading zero on the hour */
    uint8_t speed; /* 0 slow .. 255 brisk; 128 is about a second a digit */
    uint8_t colon; /* TetrisColon */
    uint8_t seconds; /* TetrisSeconds */
    bool still; /* no animation: a changed digit is simply there */
} TetrisLook;

typedef enum {
    TetrisPhaseStill = 0,
    TetrisPhaseClearing,
    TetrisPhaseBuilding,
} TetrisPhase;

typedef struct {
    uint8_t value; /* the digit on the panel, or being built towards */
    uint8_t next; /* the digit being cleared towards, while value is still the one leaving */
    uint8_t phase; /* TetrisPhase */
    uint8_t landed; /* how many of the cover's pieces have landed */
    uint32_t at_ms; /* ms into the current piece's fall, or into the clear */
    uint8_t wave; /* fast builds: the wave falling now */
} TetrisColumn;

/* Stateful: a piece halfway down stays halfway down between frames. */
typedef struct {
    TetrisColumn columns[6];
    bool started;
    int64_t last_ms;
} TetrisState;

/** Forget everything: the next frame assembles the time from nothing. */
void tetris_state_reset(TetrisState* state);

/**
 * Draw one frame into @p rgb (TETRIS_FRAME_BYTES, row-major, 3 bytes a pixel),
 * advancing @p state. The buffer is cleared first.
 *
 * @param local_s    local time in seconds; only the time of day is used, and
 *                   it has to keep counting across frames (ms deltas come from it)
 * @param ms         millisecond within that second, 0..999
 * @param clock_set  false while the clock has not been set: the digits stay at 0
 */
void tetris_render(
    uint8_t* rgb,
    TetrisState* state,
    const TetrisLook* look,
    int64_t local_s,
    uint32_t ms,
    bool clock_set);

#ifdef __cplusplus
}
#endif
