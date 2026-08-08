#include "tetris.h"

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
}

int8_t state_machine(tetris_engine_t *engine) {
  switch (engine->current_state) {

  case menu:
    if (call_menu(engine) == 1) {
      return 1;
    }
    break;

  case game_start:
    // Shuffle bag, spawn first piece, switch to block_fall
    shuffle_bag(engine);
    spawn_piece(engine);
    engine->current_state = block_fall;
    break;

  case block_fall:
    // GRAVITY HAPPENS HERE
    if (physics_get(engine, engine->active_piece.x, engine->active_piece.y + 1,
                    engine->active_piece.rotation) == 0) {
      engine->active_piece.y++;
    } else {
      engine->current_state = block_stop;
    }
    break;

  case block_stop:
    // Copy the active_piece 4x4 array into the static 20x10 grid
    // Switch to line_check
    physics_set(engine);
    engine->current_state = line_check;
    break;

  case line_check:
    // Scan rows 0 to 19. If a row is full, clear it and drop everything above
    // it. After checking, call spawn_piece() and switch back to block_fall. (If
    // spawn_piece() detects a block already at top, switch to game_over).
    if (line_checker(engine) == 1) {
      engine->current_state = game_over;
    } else {
      engine->current_state = wait;
    }
    break;

  case wait:
    delay_ms(1000);
    engine->current_state = block_fall;
    break;

  case game_over:
    printf("Play again? y/N: ");
    char return_key;
    scanf(" %c", &return_key);
    if (!(return_key == 'y' || return_key == 'Y')) {
      return 1;
    }
    engine->current_state = game_start;
    return 0;
    break;
  }
  return 0;
}

void guide(void) {
  printf(
      "blocks are falling from the sky!\n"
      "perform wizardry and form horizontal lines that make them disappear.\n"
      "if the stack reaches the top of the screen, it's over.\n\n"
      "press Enter to return: ");
  char return_key;
  scanf(" %c", &return_key);
}

void about(void) {
  printf("spacetetris. a spaceadler production.\n\n"
         "made from to bridge the gap to RTOS/embedded programming.\n"
         "read the code here: https://github.com/spaceadler/spacetetris\n\n"
         "press Enter to return: ");
  char return_key;
  scanf(" %c", &return_key);
}

int8_t call_menu(tetris_engine_t *engine) {
  printf("Welcome to spacetetris!\n"
         "Please select one of the following options:\n\n"
         "1. New Game\n2. Guide\n3. About\n4. Exit\n");
  int8_t selection = 0;
  scanf(" %s", &selection);

  switch (selection) {
  case 1:
    engine->current_state = game_start;
    break;

  case 2:
    guide();
    break;

  case 3:
    about();
    break;

  case 4:
    printf("Thanks for playing spacetetris! quitting...");
    return 1;
    break;

  default:
    printf("Unkown value, quitting...");
    return 1;
    break;
  }
  return 0;
}

void shuffle_bag(tetris_engine_t *engine) {
  int8_t temp = 0;

  for (int8_t i = 0; i < 7; i++) {
    engine->bag[i] = i;
  } // [0, 1, 2, 3, 4, 5, 6]

  for (int i = 6; i > -1; i--) {
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

  // basically we want to check the 4x4 array for the current piece, log the
  // [row][col] positions for them, and check if the [x][y+1] IN THE GRID is 0
  // or 1. if y already equals 19 then just return 1 and put state to block_stop
  // (in the state, not here) (note, this is a loop for every x to check the y+1
  // for it.) if zero for all of the values, then set engine->active_piece.y++,
  // and return 0. else, return 1 and put state to block_stop.

  // to do the function i want to do a loop x from 0 to 3 and y from 0 to 3
  // if any of the cell values is != 0, check if grid[x][current y++] == 1.
  // and also if current y++ is > the amount of rows. then return 1

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
        // screen)
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
        engine->grid[board_y][board_x] = 1;
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

  // 2. UPDATE THE SCORE
  engine->lines_cleared += lines_cleared_now;

  // 3. CHECK GAME OVER
  for (int8_t col = 0; col < TETRIS_COLS; col++) {
    if (engine->grid[0][col] != 0) {
      return 1;
    }
  }

  return 0; // GAME CONTINUES
}
