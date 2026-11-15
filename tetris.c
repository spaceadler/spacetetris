#include "tetris.h"

#include "assert_handler.h"
#include "io.h"

// how every check in this file recovers (JPL rule 5): a failed
// TETRIS_ASSERT() latches a fault that stops the game at the end of the tick
// (safe mode, see main.c). until then the code keeps itself safe: a check
// that guards a pointer or an array index wraps the code it protects, and the
// function falls back to a safe default (a shape cell reads as solid, a move
// as blocked) instead of touching memory it can't trust. a check that only
// observes, like a post-condition, is written (void)TETRIS_ASSERT(...).

// new pieces enter with their box at columns 3-6 (init() and spawn_piece())
static const int8_t SPAWN_X = 3;

// per-state handlers (JPL rule 4: state_machine() was 74 lines)
static int8_t handle_menu(tetris_engine_t *engine);
static void handle_game_start(tetris_engine_t *engine);
static void handle_block_fall(tetris_engine_t *engine);
static void handle_block_stop(tetris_engine_t *engine);
static void handle_line_check(tetris_engine_t *engine);
static void handle_tetriswait(tetris_engine_t *engine);

// RNG functions
static void shuffle_bag(tetris_engine_t *engine);
static void spawn_piece(tetris_engine_t *engine);

// physics
static int8_t shape_cell(shape_type_t shape, int8_t rotation, int8_t row, int8_t col);
static void physics_set(tetris_engine_t *engine);
static int8_t row_is_full(const tetris_engine_t *engine, int8_t row);
static void remove_row(tetris_engine_t *engine, int8_t row);
static int8_t line_checker(tetris_engine_t *engine);
static int32_t points_for_lines(int8_t lines);

// data integrity
static uint8_t crc8_update(uint8_t crc, uint8_t data);
static uint8_t compute_grid_checksum(const tetris_engine_t *engine);
static uint8_t compute_score_checksum(const tetris_engine_t *engine);

void init(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // seed with prng_init() first. init() never touches the PRNG, so a zero
    // state here means nobody seeded it
    (void)TETRIS_ASSERT(engine->prng.state != 0U);

    for (int8_t i = 0; i < TETRIS_ROWS; i++) {
      for (int8_t j = 0; j < TETRIS_COLS; j++) {
        engine->grid[i][j] = 0;
      }
    }
    // game_start shuffles this; filling it here just means no field of the
    // engine is ever left holding stack garbage (MISRA 9.1)
    for (int8_t i = 0; i < SHAPE_COUNT; i++) {
      engine->bag[i] = (shape_type_t)i;
    }

    engine->score = 0;
    engine->lines_cleared = 0;
    engine->current_state = menu;
    engine->active_piece.x = SPAWN_X;
    engine->active_piece.y = 0;
    engine->active_piece.rotation = 0;
    engine->active_piece.shape_type = INDIA;
    engine->bag_index = 0;
    engine->gravity_counter = 0;
    engine->wait_counter = 0;

    engine->grid_checksum = compute_grid_checksum(engine);
    engine->score_checksum = compute_score_checksum(engine);
  }
}

int8_t state_machine(tetris_engine_t *engine) {
  int8_t result = 0;

  if (TETRIS_ASSERT(engine != NULL)) {
    // cast to unsigned folds both range checks into one: a negative value
    // wraps to a huge number and fails the <= too. a bad state then falls
    // through to default below, which does nothing.
    (void)TETRIS_ASSERT((uint32_t)engine->current_state <= (uint32_t)game_over);

    switch (engine->current_state) {
    case menu:
      result = handle_menu(engine);
      break;
    case game_start:
      handle_game_start(engine);
      break;
    case block_fall:
      handle_block_fall(engine);
      break;
    case block_stop:
      handle_block_stop(engine);
      break;
    case line_check:
      handle_line_check(engine);
      break;
    case tetriswait:
      handle_tetriswait(engine);
      break;
    case game_over:
      // Non-blocking: display is handled by render(), input by main loop
      break;
    default:
      // only a corrupted state gets here, and the check above caught it
      break;
    }
  }
  return result;
}

static void handle_game_start(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // a new game starts from a freshly init()ed engine
    (void)TETRIS_ASSERT((engine->score == 0) && (engine->lines_cleared == 0));

    // the port makes room for the board (the terminal port clears the screen)
    io_show(IO_SCREEN_GAME);
    // Shuffle bag, spawn first piece, switch to block_fall
    shuffle_bag(engine);
    spawn_piece(engine);
    engine->gravity_counter = 0;
    engine->current_state = block_fall;
  }
}

static void handle_block_fall(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // every path into block_fall zeroes it, and it resets on reaching the top
    (void)TETRIS_ASSERT((engine->gravity_counter >= 0) &&
                        (engine->gravity_counter < GRAVITY_FRAMES));

    // GRAVITY HAPPENS HERE, but only every GRAVITY_FRAMES frames,
    // not every single frame (which would be instant).
    engine->gravity_counter++;
    if (engine->gravity_counter >= GRAVITY_FRAMES) {
      engine->gravity_counter = 0;
      if (physics_get(engine, engine->active_piece.x,
                      (int8_t)(engine->active_piece.y + 1),
                      engine->active_piece.rotation) == 0) {
        engine->active_piece.y++;
      } else {
        engine->current_state = block_stop;
      }
    }
  }
}

static void handle_block_stop(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // the piece being locked must sit in free space: an overlap would merge
    // into frozen blocks and silently lose cells. worked out before the
    // check, so the checked condition has no side effects (JPL rule 5).
    const int8_t overlaps =
        physics_get(engine, engine->active_piece.x, engine->active_piece.y,
                    engine->active_piece.rotation);
    (void)TETRIS_ASSERT(overlaps == 0);

    // Copy the active_piece 4x4 array into the static 20x10 grid
    // Switch to line_check
    physics_set(engine);
    engine->current_state = line_check;
  }
}

static void handle_line_check(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // Scan rows. If a row is full, clear it and drop everything above it.
    // After checking, set up the lock delay before spawning the next piece.
    if (line_checker(engine) == 1) {
      engine->current_state = game_over;
    } else {
      engine->wait_counter = LOCK_DELAY_FRAMES;
      engine->current_state = tetriswait;
    }
  }
}

static void handle_tetriswait(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // line_check sets it to LOCK_DELAY_FRAMES; tetriswait leaves at 0
    (void)TETRIS_ASSERT((engine->wait_counter > 0) &&
                        (engine->wait_counter <= LOCK_DELAY_FRAMES));

    // Frame-based lock delay (replaces the old blocking delay_ms(1000))
    engine->wait_counter--;
    if (engine->wait_counter <= 0) {
      spawn_piece(engine);
      // Check if the new piece spawns inside existing blocks (game over)
      if (physics_get(engine, engine->active_piece.x, engine->active_piece.y,
                      engine->active_piece.rotation) != 0) {
        engine->current_state = game_over;
      } else {
        engine->gravity_counter = 0;
        engine->current_state = block_fall;
      }
    }
  }
}

static int8_t handle_menu(tetris_engine_t *engine) {
  int8_t result = 0;

  if (TETRIS_ASSERT(engine != NULL)) {
    io_show(IO_SCREEN_MENU);
    const char_t selection = io_read_menu_selection();

    switch (selection) {
    case '1':
      engine->current_state = game_start;
      break;

    case '2':
      io_show(IO_SCREEN_GUIDE);
      (void)io_read_char(); // any line will do: only the Enter matters
      break;

    case '3':
      io_show(IO_SCREEN_ABOUT);
      (void)io_read_char();
      break;

    case '4':
    case IO_INPUT_CLOSED: // stdin is gone, so nobody can pick anything else
      io_show(IO_SCREEN_GOODBYE);
      result = 1;
      break;

    default:
      io_show(IO_SCREEN_BAD_CHOICE); // the port decides how long it stays up
      break;
    }
  }
  return result;
}

static void shuffle_bag(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    for (int8_t i = 0; i < SHAPE_COUNT; i++) {
      engine->bag[i] = (shape_type_t)i;
    } // [0, 1, 2, 3, 4, 5, 6]

    // Fisher-Yates, back to front: swap each slot with a random slot at or
    // before it
    for (int8_t i = (int8_t)(SHAPE_COUNT - 1); i > 0; i--) {
      const uint32_t raw = prng_next(&engine->prng);
      // the % happens in uint32_t; keeping that in a uint32_t makes the cast
      // below convert a value, not hide the width of a calculation (MISRA
      // 10.8)
      const uint32_t pick = raw % ((uint32_t)i + 1U); // 0..i
      const int8_t j = (int8_t)pick;

      const shape_type_t temp = engine->bag[i]; // smallest scope (JPL rule 6)
      engine->bag[i] = engine->bag[j];
      engine->bag[j] = temp;
    }

    // post-condition: still exactly one of each shape, only reordered.
    // (the guard keeps a corrupted value from shifting past bit 31.)
    uint8_t seen = 0U;
    for (int8_t i = 0; i < SHAPE_COUNT; i++) {
      const uint32_t shape = (uint32_t)engine->bag[i];
      if (shape < (uint32_t)SHAPE_COUNT) {
        seen = (uint8_t)(seen | (uint8_t)(1U << shape));
      }
    }
    (void)TETRIS_ASSERT(seen == 0x7FU); // bits 0-6: one per shape
  }
}

static void spawn_piece(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // spawn_piece() is the only writer: 0 after a shuffle, +1 per piece.
    // out of range it would index bag[] wildly, so the recovery forces a
    // fresh bag, which puts the index back on a known value.
    if (!TETRIS_ASSERT((engine->bag_index >= 0) &&
                       (engine->bag_index <= SHAPE_COUNT))) {
      engine->bag_index = SHAPE_COUNT;
    }

    if (engine->bag_index >= SHAPE_COUNT) {
      shuffle_bag(engine);
      engine->bag_index = 0;
    }

    engine->active_piece.shape_type = engine->bag[engine->bag_index];
    engine->active_piece.x = SPAWN_X;
    engine->active_piece.y = 0;
    engine->active_piece.rotation = 0;
    engine->bag_index++;
  }
}

// the only way to read a shape: every index is checked first (JPL rule 5).
// the table lives inside this function (MISRA 8.9), so nothing else in the
// file can index it without going through these checks. its size is
// SHAPE_COUNT_CHECKED (tetris.h): the build stops unless there are exactly
// 7 shapes.
static int8_t shape_cell(shape_type_t shape, int8_t rotation, int8_t row,
                         int8_t col) {
  static const int16_t SHAPE_TABLE[SHAPE_COUNT_CHECKED][ROTATION_COUNT]
                                  [SHAPE_SIZE][SHAPE_SIZE] = {
      {// INDIA
       {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}},
       {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}}},
      {// JULIET
       {{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}}},
      {// LIMA
       {{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
       {{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},

      {// OSCAR
       {{0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}}},

      {// SIERRA
       {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},

      {// ZULU
       {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {1, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {1, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}}},

      {// TANGO
       {{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
       {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
       {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}}};

  int8_t filled = 1; // a cell that can't be looked up reads as solid: for
                     // collision, "blocked" is the safe answer
  const bool shape_ok = TETRIS_ASSERT((uint32_t)shape < (uint32_t)SHAPE_COUNT);
  const bool rotation_ok =
      TETRIS_ASSERT((rotation >= 0) && (rotation < ROTATION_COUNT));
  const bool row_ok = TETRIS_ASSERT((row >= 0) && (row < SHAPE_SIZE));
  const bool col_ok = TETRIS_ASSERT((col >= 0) && (col < SHAPE_SIZE));

  if ((shape_ok && rotation_ok) && (row_ok && col_ok)) {
    filled = (SHAPE_TABLE[shape][rotation][row][col] != 0) ? 1 : 0;
  }
  return filled;
}

int8_t physics_get(const tetris_engine_t *engine, int8_t test_x, int8_t test_y,
                   int8_t test_rotation) {
  int8_t result = 1; // a position that can't be checked counts as blocked
  const bool engine_ok = TETRIS_ASSERT(engine != NULL);
  const bool rotation_ok =
      TETRIS_ASSERT((test_rotation >= 0) && (test_rotation < ROTATION_COUNT));
  // a legal test position is never more than one box outside the board.
  // that also keeps test_x + col and test_y + row inside int8_t below.
  const bool x_ok =
      TETRIS_ASSERT((test_x >= -SHAPE_SIZE) && (test_x <= TETRIS_COLS));
  const bool y_ok =
      TETRIS_ASSERT((test_y >= -SHAPE_SIZE) && (test_y <= TETRIS_ROWS));

  if ((engine_ok && rotation_ok) && (x_ok && y_ok)) {
    const shape_type_t shape = engine->active_piece.shape_type;
    result = 0;

    // no early exit: all 16 cells are always checked, so the worst case and
    // the usual case take the same time
    for (int8_t row = 0; row < SHAPE_SIZE; row++) {
      for (int8_t col = 0; col < SHAPE_SIZE; col++) {
        if (shape_cell(shape, test_rotation, row, col) != 0) {
          const int8_t board_x = (int8_t)(test_x + col);
          const int8_t board_y = (int8_t)(test_y + row);

          // 1. Wall Checks (Left, Right, Floor)
          // 2. Frozen Block Check (Only check if y >= 0 so we don't check
          //    above screen). reaching it means check 1 already pinned
          //    board_x and the floor, so the else-if is also the grid's
          //    bounds check.
          if ((board_x < 0) || (board_x >= TETRIS_COLS) ||
              (board_y >= TETRIS_ROWS)) {
            result = 1;
          } else if ((board_y >= 0) &&
                     (engine->grid[board_y][board_x] != 0)) {
            result = 1;
          } else {
            // free cell
          }
        }
      }
    }
  }
  return result;
}

static void physics_set(tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    const bool x_ok = TETRIS_ASSERT((engine->active_piece.x >= -SHAPE_SIZE) &&
                                    (engine->active_piece.x <= TETRIS_COLS));
    const bool y_ok = TETRIS_ASSERT((engine->active_piece.y >= -SHAPE_SIZE) &&
                                    (engine->active_piece.y <= TETRIS_ROWS));

    if (x_ok && y_ok) {
      const shape_type_t shape = engine->active_piece.shape_type;
      const int8_t rotation = engine->active_piece.rotation;
      const int8_t px = engine->active_piece.x;
      const int8_t py = engine->active_piece.y;

      for (int8_t row = 0; row < SHAPE_SIZE; row++) {
        for (int8_t col = 0; col < SHAPE_SIZE; col++) {
          if (shape_cell(shape, rotation, row, col) != 0) {
            const int8_t board_x = (int8_t)(px + col);
            const int8_t board_y = (int8_t)(py + row);
            // Bounds check: don't write above the board (would corrupt
            // memory)
            if ((board_y >= 0) && (board_y < TETRIS_ROWS) && (board_x >= 0) &&
                (board_x < TETRIS_COLS)) {
              engine->grid[board_y][board_x] = 1;
            }
          }
        }
      }
    }

    engine->grid_checksum = compute_grid_checksum(engine); // the grid changed
  }
}

int8_t cell_filled(const tetris_engine_t *engine, int8_t row, int8_t col) {
  int8_t result = 0; // a cell that can't be checked draws as empty
  const bool engine_ok = TETRIS_ASSERT(engine != NULL);
  const bool row_ok = TETRIS_ASSERT((row >= 0) && (row < TETRIS_ROWS));
  const bool col_ok = TETRIS_ASSERT((col >= 0) && (col < TETRIS_COLS));

  if ((engine_ok && row_ok) && col_ok) {
    if (engine->grid[row][col] != 0) {
      result = 1;
    } else if ((engine->current_state == block_fall) ||
               (engine->current_state == tetriswait)) {
      // Check if the falling piece is currently occupying this specific x/y
      // pixel: it isn't in grid[][] yet, so look inside its 4x4 box
      const int8_t box_row = (int8_t)(row - engine->active_piece.y);
      const int8_t box_col = (int8_t)(col - engine->active_piece.x);

      if ((box_row >= 0) && (box_row < SHAPE_SIZE) && (box_col >= 0) &&
          (box_col < SHAPE_SIZE)) {
        result = shape_cell(engine->active_piece.shape_type,
                            engine->active_piece.rotation, box_row, box_col);
      }
    } else {
      // no falling piece in this state
    }
  }
  return result;
}

static int8_t row_is_full(const tetris_engine_t *engine, int8_t row) {
  int8_t full = 0; // a row that can't be checked is never removed
  const bool engine_ok = TETRIS_ASSERT(engine != NULL);
  const bool row_ok = TETRIS_ASSERT((row >= 0) && (row < TETRIS_ROWS));

  if (engine_ok && row_ok) {
    full = 1;
    for (int8_t col = 0; col < TETRIS_COLS; col++) {
      if (engine->grid[row][col] == 0) {
        full = 0;
      }
    }
  }
  return full;
}

static void remove_row(tetris_engine_t *engine, int8_t row) {
  const bool engine_ok = TETRIS_ASSERT(engine != NULL);
  const bool row_ok = TETRIS_ASSERT((row >= 0) && (row < TETRIS_ROWS));

  if (engine_ok && row_ok) {
    // copy board 1 line down
    for (int8_t shift_y = row; shift_y > 0; shift_y--) {
      for (int8_t x = 0; x < TETRIS_COLS; x++) {
        engine->grid[shift_y][x] = engine->grid[shift_y - 1][x];
      }
    }
    // erase copied board
    for (int8_t x = 0; x < TETRIS_COLS; x++) {
      engine->grid[0][x] = 0;
    }
  }
}

static int8_t line_checker(tetris_engine_t *engine) {
  // the loop bound, worked out in the comment above the loop
  static const int8_t LINE_CHECK_PASSES = (int8_t)(2 * TETRIS_ROWS);
  int8_t result = 1; // an engine that can't be checked ends the game

  if (TETRIS_ASSERT(engine != NULL)) {
    int8_t lines_cleared_now = 0;
    int8_t row = (int8_t)(TETRIS_ROWS - 1); // start at the floor
    (void)TETRIS_ASSERT((engine->score >= 0) && (engine->lines_cleared >= 0));

    // each pass either clears the current row and stays on it (the row above
    // just fell into it), or moves up one. that's at most TETRIS_ROWS of
    // each, so 2 * TETRIS_ROWS passes always reach the top (JPL rule 2: a
    // bound you can prove by reading the loop header).
    for (int8_t pass = 0; pass < LINE_CHECK_PASSES; pass++) {
      if (row >= 0) {
        if (row_is_full(engine, row) != 0) {
          remove_row(engine, row);
          lines_cleared_now++;
        } else {
          row--;
        }
      }
    }
    (void)TETRIS_ASSERT(row < 0); // every row got checked

    // UPDATE THE SCORE (classic Tetris scoring)
    engine->score += points_for_lines(lines_cleared_now);
    engine->lines_cleared =
        (int16_t)(engine->lines_cleared + lines_cleared_now);

    // CHECK GAME OVER (blocks in top row after clearing)
    result = 0;
    for (int8_t col = 0; col < TETRIS_COLS; col++) {
      if (engine->grid[0][col] != 0) {
        result = 1;
      }
    }

    engine->grid_checksum = compute_grid_checksum(engine);
    engine->score_checksum = compute_score_checksum(engine);
  }
  return result; // 1 = GAME OVER, 0 = GAME CONTINUES
}

// classic Tetris scoring: 100 / 300 / 500 / 800 for 1 / 2 / 3 / 4 lines
static int32_t points_for_lines(int8_t lines) {
  int32_t points = 0;

  // full rows never outlive a line_check, so only the last piece's rows can
  // be full, and a piece spans at most 4
  (void)TETRIS_ASSERT((lines >= 0) && (lines <= SHAPE_SIZE));

  switch (lines) {
  case 1:
    points = 100;
    break;
  case 2:
    points = 300;
    break;
  case 3:
    points = 500;
    break;
  case 4:
    points = 800;
    break;
  default:
    // no lines this time (MISRA 16.4)
    break;
  }
  return points;
}

// === data integrity ===
// every write to grid/score/lines_cleared refreshes these checksums, and the
// main loop checks them once per tick. on a PC they'll never fire; on flight
// hardware a stray write or a radiation bit-flip is exactly what they catch.

// CRC-8, polynomial 0x07 (x^8 + x^2 + x + 1), one bit at a time: no lookup
// table, so no ROM cost, and a fixed 8 steps per byte. unlike an XOR of the
// cells (which, for cells that are only ever 0 or 1, is a single parity
// bit), a CRC depends on where each value sits, so a moved block or two
// flipped cells change the result too. every byte is a valid input, so
// there's nothing to check in here: engine_self_test() checks the result.
static uint8_t crc8_update(uint8_t crc, uint8_t data) {
  uint8_t c = (uint8_t)(crc ^ data);

  for (uint8_t bit = 0U; bit < 8U; bit++) {
    if ((c & 0x80U) != 0U) {
      c = (uint8_t)((uint8_t)(c << 1U) ^ 0x07U);
    } else {
      c = (uint8_t)(c << 1U);
    }
  }
  return c;
}

static uint8_t compute_grid_checksum(const tetris_engine_t *engine) {
  uint8_t crc = 0U;

  if (TETRIS_ASSERT(engine != NULL)) {
    for (int8_t row = 0; row < TETRIS_ROWS; row++) {
      for (int8_t col = 0; col < TETRIS_COLS; col++) {
        const int8_t cell = engine->grid[row][col];
        // a cell is only ever 0 (empty) or 1 (frozen block). the CRC catches
        // a change; this catches a value no code path should ever write
        (void)TETRIS_ASSERT((cell == 0) || (cell == 1));
        crc = crc8_update(crc, (uint8_t)cell);
      }
    }
  }
  return crc;
}

static uint8_t compute_score_checksum(const tetris_engine_t *engine) {
  uint8_t crc = 0U;

  if (TETRIS_ASSERT(engine != NULL)) {
    // bytes come out by shifting, not by casting &score to a byte pointer
    // (MISRA 11.3), and on unsigned copies, because shifting a negative
    // number is implementation-defined (MISRA 10.1)
    const uint32_t score = (uint32_t)engine->score;
    const uint32_t lines = (uint32_t)engine->lines_cleared;

    crc = crc8_update(crc, (uint8_t)(score & 0xFFU));
    crc = crc8_update(crc, (uint8_t)((score >> 8U) & 0xFFU));
    crc = crc8_update(crc, (uint8_t)((score >> 16U) & 0xFFU));
    crc = crc8_update(crc, (uint8_t)((score >> 24U) & 0xFFU));
    crc = crc8_update(crc, (uint8_t)(lines & 0xFFU));
    crc = crc8_update(crc, (uint8_t)((lines >> 8U) & 0xFFU));
  }
  return crc;
}

void verify_engine_integrity(const tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    // worked out first, so the checked conditions have no side effects
    const uint8_t grid_now = compute_grid_checksum(engine);
    const uint8_t score_now = compute_score_checksum(engine);

    (void)TETRIS_ASSERT(grid_now == engine->grid_checksum);
    (void)TETRIS_ASSERT(score_now == engine->score_checksum);
  }
}

// power-on self-test: flight software checks its own tables and algorithms
// against known answers before trusting them. a table in flash can be
// corrupted, and an edit to SHAPE_TABLE can leave a typo.
bool engine_self_test(void) {
  // CRC-8 with this polynomial and a zero start (CRC-8/SMBUS) turns the ASCII
  // digits "123456789" into 0xF4, the published check value
  static const uint8_t CHECK_DIGITS[9] = {0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                                          0x36U, 0x37U, 0x38U, 0x39U};
  // xorshift32 from seed 1, its three steps by hand: 1 -> 8193 -> 8193 ->
  // 270369 (x ^= x << 13, x ^= x >> 17, x ^= x << 5)
  static const uint32_t XORSHIFT_FIRST_FROM_1 = 270369U;
  uint8_t crc = 0U;
  prng_ctx_t probe;
  bool shapes_ok = true;

  for (uint8_t i = 0U; i < 9U; i++) {
    crc = crc8_update(crc, CHECK_DIGITS[i]);
  }
  const bool crc_ok = TETRIS_ASSERT(crc == 0xF4U);

  prng_init(&probe, 1U);
  const uint32_t first = prng_next(&probe);
  const bool prng_ok = TETRIS_ASSERT(first == XORSHIFT_FIRST_FROM_1);

  // every shape in every rotation is a tetromino: exactly 4 cells
  for (int8_t shape = 0; shape < SHAPE_COUNT; shape++) {
    for (int8_t rotation = 0; rotation < ROTATION_COUNT; rotation++) {
      int8_t cells = 0;
      for (int8_t row = 0; row < SHAPE_SIZE; row++) {
        for (int8_t col = 0; col < SHAPE_SIZE; col++) {
          cells = (int8_t)(cells + shape_cell((shape_type_t)shape, rotation,
                                              row, col));
        }
      }
      if (!TETRIS_ASSERT(cells == 4)) {
        shapes_ok = false;
      }
    }
  }
  return (crc_ok && prng_ok) && shapes_ok;
}
