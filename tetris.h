#ifndef TETRIS_H
#define TETRIS_H

#include <stdbool.h>
#include <stddef.h> // NULL, for the engine's checks
#include <stdint.h>

#include "prng.h"

// constants carry their type in a cast, so nothing has to guess what type a
// bare 20 is (MISRA 10.x). a cast constant is still a compile-time constant
// and can size an array; a `static const int8_t` can't do that in C (C++ only).
#define TETRIS_ROWS ((int8_t)20)
#define TETRIS_COLS ((int8_t)10)
#define SHAPE_COUNT ((int8_t)7)    // seven tetrominoes, so seven per bag
#define SHAPE_SIZE ((int8_t)4)     // every shape fits in a 4x4 box
#define ROTATION_COUNT ((int8_t)4) // up, right, down, left
#define TARGET_FPS ((uint16_t)60U) // set target FPS (higher = faster)
#define FRAME_DELAY_MS ((uint16_t)(1000U / TARGET_FPS))
// gravity ticks every 30 frames (~0.5s at 60 FPS)
#define GRAVITY_FRAMES ((int16_t)30)
// brief pause after a piece locks before next spawn
#define LOCK_DELAY_FRAMES ((int16_t)15)

typedef enum { // shape type enum (compiles down to numbers 0 - 6.
               // handy for SHAPE_TABLE in tetris.c!)
  INDIA,
  JULIETT,
  LIMA,
  OSCAR,
  SIERRA,
  ZULU,
  TANGO
} shape_type_t;

// SHAPE_COUNT with a compile-time check folded in (the "static assertions"
// of JPL rule 16). the bag, SHAPE_TABLE and shuffle_bag()'s one-of-each
// mask (0x7F) all assume exactly 7 shapes, with TANGO, the last one, as the
// 7th: a shape inserted or removed before it would shift every shape's
// number out of step with SHAPE_TABLE. if either stops being true this is
// -1, an array size no C compiler accepts, so the build stops: no -Werror
// needed, nothing at run time. it sizes SHAPE_TABLE in tetris.c, so the
// check lives in an object the code really uses. (a shape appended after
// TANGO isn't caught, and can't do harm: the bag only deals the first
// SHAPE_COUNT shapes.) the Apollo lesson: the case that "can't happen" is
// the one worth a check.
#define SHAPE_COUNT_CHECKED                                                    \
  (((SHAPE_COUNT == 7) && ((int32_t)TANGO == ((int32_t)SHAPE_COUNT - 1)))      \
       ? SHAPE_COUNT                                                           \
       : -1)

typedef enum { // the state machine.
  menu,
  game_start,
  block_fall,
  block_stop,
  line_check,
  tetriswait,
  game_over
} current_state_t;

typedef struct {
  int8_t x;        // signed so that going left from zero is -1 not 255.
  int8_t y;        // signed for continuity.
  int8_t rotation; // (0 = up, 1 = right, 2 = down, 3 = left)
  shape_type_t shape_type;
} active_piece_t;

typedef struct {
  int8_t grid[TETRIS_ROWS][TETRIS_COLS]; // defining a 20x10 grid
  active_piece_t active_piece;           // what is the active piece,
  current_state_t current_state;         // the current state,
  int32_t score;                         // how much the current score is,
  int16_t lines_cleared;                 // how many lines have been cleared,
  int8_t bag_index;                      // current bag index, and
  shape_type_t bag[SHAPE_COUNT];         // an array for active piece decision.
  int16_t gravity_counter;               // frame counter for gravity ticks
  int16_t wait_counter;                  // frame counter for lock delay
  prng_ctx_t prng;                       // xorshift32 state for the bag
  uint8_t grid_checksum;                 // CRC-8 of grid[][]
  uint8_t score_checksum;                // CRC-8 of score + lines_cleared
} tetris_engine_t;

// === Engine functions (tetris.c) ===
// everything else in tetris.c is static: nothing outside the engine calls it.

// initialize game by resetting every value. seed engine->prng with
// prng_init() before the first call; init() leaves the PRNG alone, so a
// restart deals a fresh sequence instead of replaying the last game.
void init(tetris_engine_t *engine);

// state machine function. returns 1 when the player chose to quit.
int8_t state_machine(tetris_engine_t *engine);

// collision engine: 1 if the active piece would hit a wall, the floor or a
// frozen block at this position and rotation, 0 if the space is free
int8_t physics_get(const tetris_engine_t *engine, int8_t test_x, int8_t test_y,
                   int8_t test_rotation);

// 1 if this board cell should be drawn solid: a frozen block, or part of the
// falling piece. the renderer asks this instead of reading SHAPE_TABLE.
int8_t cell_filled(const tetris_engine_t *engine, int8_t row, int8_t col);

// recompute the checksums and check them against the stored ones. the main
// loop calls this once per tick; a mismatch latches a fault (safe mode).
void verify_engine_integrity(const tetris_engine_t *engine);

// power-on self-test: the shape table, the CRC and the PRNG checked against
// known answers. run it before anything relies on them; false: don't start.
bool engine_self_test(void);

#endif // TETRIS_H
