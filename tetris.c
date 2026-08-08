#include "tetris.h"

const int16_t SHAPE_TABLE[7][4][4][4] = {
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

void init(tetris_engine_t *engine) {
  for (int8_t i = 0; i < TETRIS_ROWS; i++) {
    for (int8_t j = 0; j < TETRIS_COLS; j++) {
      engine->grid[i][j] = 0;
    }
  }

  engine->score = 0;
  engine->lines_cleared = 0;
  engine->current_state = menu;
  engine->active_piece.x = 3;
  engine->active_piece.y = 0;
  engine->active_piece.rotation = 0;
  engine->bag_index = 0;
  engine->gravity_counter = 0;
  engine->wait_counter = 0;
}

int8_t state_machine(tetris_engine_t *engine) {
  switch (engine->current_state) {

  case menu:
    if (call_menu(engine) == 1) {
      return 1;
    }
    break;

  case game_start:
    // Clear the screen once so menu text doesn't ghost behind the board
    printf("\033[2J\033[H");
    // Shuffle bag, spawn first piece, switch to block_fall
    shuffle_bag(engine);
    spawn_piece(engine);
    engine->gravity_counter = 0;
    engine->current_state = block_fall;
    break;

  case block_fall:
    // GRAVITY HAPPENS HERE, but only every GRAVITY_FRAMES frames,
    // not every single frame (which would be instant).
    engine->gravity_counter++;
    if (engine->gravity_counter >= GRAVITY_FRAMES) {
      engine->gravity_counter = 0;
      if (physics_get(engine, engine->active_piece.x,
                      engine->active_piece.y + 1,
                      engine->active_piece.rotation) == 0) {
        engine->active_piece.y++;
      } else {
        engine->current_state = block_stop;
      }
    }
    break;

  case block_stop:
    // Copy the active_piece 4x4 array into the static 20x10 grid
    // Switch to line_check
    physics_set(engine);
    engine->current_state = line_check;
    break;

  case line_check:
    // Scan rows. If a row is full, clear it and drop everything above it.
    // After checking, set up the lock delay before spawning the next piece.
    if (line_checker(engine) == 1) {
      engine->current_state = game_over;
    } else {
      engine->wait_counter = LOCK_DELAY_FRAMES;
      engine->current_state = wait;
    }
    break;

  case wait:
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
    break;

  case game_over:
    // Non-blocking: display is handled by render(), input by main loop
    break;
  }
  return 0;
}

void guide(void) {
  printf("\033[2J\033[H");
  printf(
      "blocks are falling from the sky!\n"
      "perform wizardry and form horizontal lines that make them disappear.\n"
      "if the stack reaches the top of the screen, it's over.\n\n"
      "controls:\n"
      "  a - move left\n"
      "  d - move right\n"
      "  s - soft drop\n"
      "  w - rotate\n\n"
      "press Enter to return: ");
  char return_key;
  scanf(" %c", &return_key);
}

void about(void) {
  printf("\033[2J\033[H");
  printf("spacetetris. a spaceadler production.\n\n"
         "made from scratch to bridge the gap to RTOS/embedded programming.\n"
         "read the code here: https://github.com/spaceadler/spacetetris\n\n"
         "press Enter to return: ");
  char return_key;
  scanf(" %c", &return_key);
}

int8_t call_menu(tetris_engine_t *engine) {
  printf("\033[2J\033[H");
  printf("Welcome to spacetetris!\n"
         "Please select one of the following options:\n\n"
         "1. New Game\n2. Guide\n3. About\n4. Exit\n\n"
         "Selection: ");

  char selection;
  scanf(" %c", &selection);

  switch (selection) {
  case '1':
    engine->current_state = game_start;
    break;

  case '2':
    guide();
    break;

  case '3':
    about();
    break;

  case '4':
    printf("Thanks for playing spacetetris! quitting...\n");
    return 1;

  default:
    printf("\nUnknown option. Please try again.\n");
    delay_ms(1500);
    break;
  }
  return 0;
}

void shuffle_bag(tetris_engine_t *engine) {
  int8_t temp = 0;

  for (int8_t i = 0; i < 7; i++) {
    engine->bag[i] = i;
  } // [0, 1, 2, 3, 4, 5, 6]

  for (int8_t i = 6; i > 0; i--) {
    int8_t seed = rand() % (i + 1);

    temp = engine->bag[i];
    engine->bag[i] = engine->bag[seed];
    engine->bag[seed] = temp;
  }
}

void spawn_piece(tetris_engine_t *engine) {
  if (engine->bag_index > 6) {
    shuffle_bag(engine);
    engine->bag_index = 0;
  }

  engine->active_piece.shape_type = engine->bag[engine->bag_index];
  engine->active_piece.x = 3;
  engine->active_piece.y = 0;
  engine->active_piece.rotation = 0;
  engine->bag_index++;
}

int8_t physics_get(tetris_engine_t *engine, int8_t test_x, int8_t test_y,
                   int8_t test_rotation) {

  shape_type_t shape = engine->active_piece.shape_type;

  for (int8_t row = 0; row < 4; row++) {
    for (int8_t col = 0; col < 4; col++) {
      if (SHAPE_TABLE[shape][test_rotation][row][col] != 0) {
        int8_t board_x = test_x + col;
        int8_t board_y = test_y + row;

        // 1. Wall Checks (Left, Right, Floor)
        if (board_x < 0 || board_x >= TETRIS_COLS || board_y >= TETRIS_ROWS) {
          return 1;
        }
        // 2. Frozen Block Check (Only check if y >= 0 so we don't check above
        //    screen)
        if (board_y >= 0 && engine->grid[board_y][board_x] != 0) {
          return 1;
        }
      }
    }
  }
  return 0;
}

void physics_set(tetris_engine_t *engine) {
  shape_type_t shape = engine->active_piece.shape_type;
  int8_t rotation = engine->active_piece.rotation;
  int8_t px = engine->active_piece.x;
  int8_t py = engine->active_piece.y;

  for (int8_t row = 0; row < 4; row++) {
    for (int8_t col = 0; col < 4; col++) {
      if (SHAPE_TABLE[shape][rotation][row][col] != 0) {
        int8_t board_x = px + col;
        int8_t board_y = py + row;
        // Bounds check: don't write above the board (would corrupt memory)
        if (board_y >= 0 && board_y < TETRIS_ROWS && board_x >= 0 &&
            board_x < TETRIS_COLS) {
          engine->grid[board_y][board_x] = 1;
        }
      }
    }
  }
}

int8_t line_checker(tetris_engine_t *engine) {
  int8_t lines_cleared_now = 0;

  for (int8_t row = TETRIS_ROWS - 1; row >= 0; row--) {
    for (int8_t col = 0; col < TETRIS_COLS; col++) {

      if (engine->grid[row][col] == 0) {
        break;
      }

      if (col == TETRIS_COLS - 1) {
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

        lines_cleared_now++;
        row++; // Re-check this row
      }
    }
  }

  // UPDATE THE SCORE (classic Tetris scoring)
  switch (lines_cleared_now) {
  case 1:
    engine->score += 100;
    break;
  case 2:
    engine->score += 300;
    break;
  case 3:
    engine->score += 500;
    break;
  case 4:
    engine->score += 800;
    break;
  }

  engine->lines_cleared += lines_cleared_now;

  // CHECK GAME OVER (blocks in top row after clearing)
  for (int8_t col = 0; col < TETRIS_COLS; col++) {
    if (engine->grid[0][col] != 0) {
      return 1;
    }
  }

  return 0; // GAME CONTINUES
}
