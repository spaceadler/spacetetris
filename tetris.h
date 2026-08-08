#ifndef TETRIS_H
#define TETRIS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TETRIS_ROWS 20
#define TETRIS_COLS 10
#define TARGET_FPS 60 // set target FPS (higher = faster)
#define FRAME_DELAY_MS (1000 / TARGET_FPS)
#define GRAVITY_FRAMES 30 // gravity ticks every 30 frames (~0.5s at 60 FPS)
#define LOCK_DELAY_FRAMES                                                      \
  15 // brief pause after a piece locks before next spawn

extern const int16_t SHAPE_TABLE[7][4][4][4];

typedef enum { // shape type enum (compiles down to numbers 0 - 6.
               // handy for the array above!)
  INDIA,
  JULIETT,
  LIMA,
  OSCAR,
  SIERRA,
  ZULU,
  TANGO
} shape_type_t;

typedef enum { // the state machine.
  menu,
  game_start,
  block_fall,
  block_stop,
  line_check,
  wait,
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
  shape_type_t bag[7];                   // an array for active piece decision.
  int16_t gravity_counter;               // frame counter for gravity ticks
  int16_t wait_counter;                  // frame counter for lock delay
} tetris_engine_t;

// === Engine functions (tetris.c) ===

// initialize game by resetting every value
void init(tetris_engine_t *engine);

// state machine function
int8_t state_machine(tetris_engine_t *engine);

// collision engine
int8_t physics_get(tetris_engine_t *engine, int8_t test_x, int8_t test_y,
                   int8_t test_rotation);

// block setter
void physics_set(tetris_engine_t *engine);

// deletes lines and ends game if overboard
int8_t line_checker(tetris_engine_t *engine);

// RNG functions
void shuffle_bag(tetris_engine_t *engine);
void spawn_piece(tetris_engine_t *engine);

// text-displaying functions
void guide(void);
void about(void);
int8_t call_menu(tetris_engine_t *engine);

// === IO functions (main.c) ===

// player controls (move left/right, rotate, drop)
void input(tetris_engine_t *engine, char button_pressed);

// renderer
void render(tetris_engine_t *engine);

// clock-cycle waster
void delay_ms(int16_t milliseconds);

// non-blocking keypress reader
char get_raw_keypress(void);

#endif // TETRIS_H
