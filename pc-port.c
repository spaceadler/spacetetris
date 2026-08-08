#include "tetris.h"
#ifdef _WIN32
#include <conio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

int main() {
  srand(time(NULL));
  tetris_engine_t engine;
  init(&engine);

  int8_t exit = 0;
  while (exit != 1) {
    char key = get_raw_keypress();

    // 2. The Architect's Traffic Director:
    if (engine.current_state == block_fall && key != '\0') {
      input(&engine, key);
    }

    if (state_machine(&engine) == 1) {
      exit = 1;
    }
    render(&engine);
    delay_ms(FRAME_DELAY_MS);
  }
  return 0;
}

void delay_ms(int16_t milliseconds) {
  clock_t start_time = clock();
  // Calculate how many clock ticks we need to wait
  clock_t target_ticks = (milliseconds * CLOCKS_PER_SEC) / 1000;

  // Spin the CPU until the time has elapsed
  while ((clock() - start_time) < target_ticks) {
    // Do nothing
  }
}

void input(tetris_engine_t *engine, char button_pressed) {
  // copy current state to test variables
  int8_t test_x = engine->active_piece.x;
  int8_t test_y = engine->active_piece.y;
  int8_t test_rot = engine->active_piece.rotation;

  // modify test variables based on input
  switch (button_pressed) {
  case 'a':
    test_x--;
    break; // Left
  case 'd':
    test_x++;
    break; // Right
  case 's':
    test_y++;
    break; // Soft Drop
  case 'w':
    test_rot = (test_rot + 1) % 4;
    break; // Rotate clockwise
  default:
    return; // Ignore invalid keys
  }

  // 3. Fire the radar! If the coast is clear, commit the move.
  if (physics_get(engine, test_x, test_y, test_rot) == 0) {
    engine->active_piece.x = test_x;
    engine->active_piece.y = test_y;
    engine->active_piece.rotation = test_rot;
  }
}

void render(tetris_engine_t *engine) {
  // ANSI escape code: Moves cursor to the top-left of the terminal.
  // This overwrites the old frame instead of clearing the screen (which causes
  // ugly flickering)
  printf("\033[H"); // On the very first frame, clear screen. After that,
                    // just use "\033[H"

  printf(" Score: %d   Lines: %d\n", engine->score, engine->lines_cleared);

  for (int8_t row = 0; row < TETRIS_ROWS; row++) {
    printf("|"); // Left wall

    for (int8_t col = 0; col < TETRIS_COLS; col++) {

      // 1. Check if the falling piece is currently occupying this specific x/y
      // pixel
      int8_t is_active_block = 0;
      if (engine->current_state == block_fall ||
          engine->current_state == wait) {
        int8_t px = engine->active_piece.x;
        int8_t py = engine->active_piece.y;
        shape_type_t shape = engine->active_piece.shape_type;
        int8_t rot = engine->active_piece.rotation;

        // If the pixel is inside the 4x4 bounding box of the active piece...
        if (row >= py && row < py + 4 && col >= px && col < px + 4) {
          // ...and the piece's array has a 1 there...
          if (SHAPE_TABLE[shape][rot][row - py][col - px] != 0) {
            is_active_block = 1;
          }
        }
      }

      // 2. Draw the pixel
      if (is_active_block || engine->grid[row][col] != 0) {
        printf("[]"); // Draw a solid block
      } else {
        printf(" ."); // Draw empty space
      }
    }
    printf("|\n"); // Right wall and newline
  }

  // Draw the floor
  printf(" ");
  for (int8_t col = 0; col < TETRIS_COLS; col++) {
    printf("--");
  }
  printf("\n");
}

char get_raw_keypress() {
#ifdef _WIN32
  // Windows is wonderfully simple for this
  if (_kbhit()) {
    return _getch();
  }
  return '\0';

#elif defined(__linux__)
  // Linux requires hijacking the terminal configuration
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO); // Disable "Enter" buffering and echo
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK); // Make it non-blocking

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore terminal
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if (ch != EOF)
    return (char)ch;
  return '\0';

#else
  // FLIPPER ZERO / EMBEDDED
  // not yet
  return '\0';
#endif
}
