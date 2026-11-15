// Windows port: conio.h keyboard, ANSI escape rendering, clock() timing.
// the only files allowed stdio.h and time.h are the io_*.c ports.
//
// generated from io_unix.c with the keyboard and the seed swapped: everything
// else is the same code, so keep the two in step.

#include "io.h"

#include <inttypes.h> // PRId32 / PRIu32: printf formats that match stdint types
#include <stdio.h>
#include <time.h>

#include <conio.h>

#include "assert_handler.h"

static void put(const char_t *text);
static char_t read_line_first_char(void);
static void render_hud(const tetris_engine_t *engine);
static void render_board(const tetris_engine_t *engine);
static void render_game_over(const tetris_engine_t *engine);

// === timing and randomness ===

// clock-cycle waster
void io_delay_ms(uint16_t milliseconds) {
  // upper bound on the spin (JPL rule 2). one clock() call per iteration;
  // even the 1.5 s menu pause needs only a few million.
  static const uint32_t MAX_SPIN_ITERATIONS = 100000000U;
  const clock_t start_time = clock();
  // Calculate how many clock ticks we need to wait. in unsigned 64-bit: on
  // a 32-bit signed clock_t (32-bit Linux, CLOCKS_PER_SEC = 1000000) the
  // product overflows past ~2.1 s, and signed overflow is undefined.
  const uint64_t target_ticks =
      ((uint64_t)milliseconds * (uint64_t)CLOCKS_PER_SEC) / 1000U;

  // clock() returns (clock_t)-1 when processor time isn't available. then
  // there's nothing to wait against, so don't spin at all.
  uint32_t guard = 0U; // read after the loop, so it lives out here

  if (start_time != (clock_t)-1) {
    // Spin the CPU until the time has elapsed, or the guard runs out
    for (guard = 0U; guard < MAX_SPIN_ITERATIONS; guard++) {
      // unsigned subtraction wraps instead of overflowing, so a clock that
      // rolls over mid-wait just ends this delay early
      const uint64_t elapsed = (uint64_t)clock() - (uint64_t)start_time;
      if (elapsed >= target_ticks) {
        break;
      }
    }
  }
  // the spin should end because the time passed. running out of guard
  // means clock() stopped advancing: bounded, but worth hearing about
  (void)TETRIS_ASSERT(guard < MAX_SPIN_ITERATIONS);
}

uint32_t io_get_seed(void) {
  // the wall clock, as srand(time(NULL)) used before. truncating to 32 bits
  // is fine for a seed.
  return (uint32_t)time(NULL);
}

// === gameplay ===

// non-blocking keypress reader
char_t io_get_keypress(void) {
  char_t result = '\0';

  // Windows is wonderfully simple for this
  if (_kbhit() != 0) {
    result = (char_t)_getch();
  }
  return result;
}

void io_render(const tetris_engine_t *engine) {
  // "\033" (the escape character) sits in its own string literal: an
  // octal escape must end where its literal ends (MISRA 4.1), and the
  // compiler joins the pieces back into one string
  static const char_t ANSI_HOME[] = "\033" "[H";
  static const char_t ANSI_CLEAR_BELOW[] = "\033" "[J";

  if (TETRIS_ASSERT(engine != NULL)) {
    (void)TETRIS_ASSERT((uint32_t)engine->current_state <=
                        (uint32_t)game_over);

    // ANSI escape code: Moves cursor to the top-left of the terminal.
    // This overwrites the old frame instead of clearing the screen (which
    // causes ugly flickering)
    put(ANSI_HOME);

    render_hud(engine);
    render_board(engine);

    // Game over overlay (non-blocking — input is handled in main loop)
    if (engine->current_state == game_over) {
      render_game_over(engine);
    }

    // Wipe any leftover text below the current frame
    put(ANSI_CLEAR_BELOW);
    (void)fflush(stdout);
  }
}

static void render_hud(const tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    (void)TETRIS_ASSERT((engine->score >= 0) && (engine->lines_cleared >= 0));

    (void)printf("  Score: %" PRId32 "   Lines: %" PRId16 "\n", engine->score,
                 engine->lines_cleared);
  }
}

static void render_board(const tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    for (int8_t row = 0; row < TETRIS_ROWS; row++) {
      put(" |"); // Left wall

      for (int8_t col = 0; col < TETRIS_COLS; col++) {
        if (cell_filled(engine, row, col) != 0) {
          put("[]"); // Draw a solid block
        } else {
          put(" ."); // Draw empty space
        }
      }
      put("|\n"); // Right wall and newline
    }

    // Draw the floor
    put("  ");
    for (int8_t col = 0; col < TETRIS_COLS; col++) {
      put("--");
    }
    put("\n");
  }
}

static void render_game_over(const tetris_engine_t *engine) {
  if (TETRIS_ASSERT(engine != NULL)) {
    (void)TETRIS_ASSERT(engine->current_state == game_over);

    (void)printf("\n  === GAME OVER ===\n"
                 "  Final Score: %" PRId32 "\n"
                 "  Lines Cleared: %" PRId16 "\n"
                 "\n  Play again? (y/n): ",
                 engine->score, engine->lines_cleared);
  }
}

// every piece of fixed text goes out through here. the result isn't
// checked (deviation D3): a failed terminal write has no better response
// than carrying on, and Power of 10 rule 7 names printf as the example of
// a return value fine to discard
static void put(const char_t *text) {
  if (TETRIS_ASSERT(text != NULL)) {
    (void)fputs(text, stdout);
  }
}

// === screens and choices ===

// the terminal's version of each screen. the engine only says which one.
void io_show(io_screen_t screen) {
  // ANSI: clear screen, cursor to top-left (split for MISRA 4.1, as above)
  static const char_t ANSI_CLEAR_SCREEN[] = "\033" "[2J" "\033" "[H";

  // the id comes from the engine; one outside the list is a bug there
  if (TETRIS_ASSERT((uint32_t)screen <= (uint32_t)IO_SCREEN_GAME)) {
    switch (screen) {
    case IO_SCREEN_MENU:
      put(ANSI_CLEAR_SCREEN);
      put("Welcome to spacetetris!\n"
          "Please select one of the following options:\n\n"
          "1. New Game\n2. Guide\n3. About\n4. Exit\n\n"
          "Selection: ");
      break;
    case IO_SCREEN_GUIDE:
      put(ANSI_CLEAR_SCREEN);
      put("blocks are falling from the sky!\n"
          "perform wizardry and form horizontal lines that make them "
          "disappear.\n"
          "if the stack reaches the top of the screen, it's over.\n\n"
          "controls:\n"
          "  a - move left\n"
          "  d - move right\n"
          "  s - soft drop\n"
          "  w - rotate\n\n"
          "press Enter to return: ");
      break;
    case IO_SCREEN_ABOUT:
      put(ANSI_CLEAR_SCREEN);
      put("spacetetris. a spaceadler production.\n\n"
          "made from scratch to bridge the gap to RTOS/embedded programming.\n"
          "read the code here: https://github.com/spaceadler/spacetetris\n\n"
          "press Enter to return: ");
      break;
    case IO_SCREEN_BAD_CHOICE:
      put("\nUnknown option. Please try again.\n");
      io_delay_ms(1500U); // long enough to read before the menu redraws
      break;
    case IO_SCREEN_GOODBYE:
      put("Thanks for playing spacetetris! quitting...\n");
      break;
    case IO_SCREEN_GAME:
      put(ANSI_CLEAR_SCREEN); // so menu text doesn't ghost behind the board
      break;
    default:
      // only a corrupted id gets here, and the check above caught it
      break;
    }
  }
}

char_t io_read_menu_selection(void) { return read_line_first_char(); }

char_t io_read_char(void) { return read_line_first_char(); }

// reads one whole line, up to Enter, and returns its first non-blank
// character: '\0' for a bare Enter, IO_INPUT_CLOSED if stdin has ended (or
// failed: getchar() returns EOF for both, and both get the same answer).
// replaces scanf(" %c"), whose leading space skips newlines too, so a bare
// Enter never finished "press Enter to return".
static char_t read_line_first_char(void) {
  // the most characters read looking for the end of one line
  static const uint16_t MAX_LINE_LENGTH = 256U;
  char_t result = '\0';

  // the prompts end without '\n', so push them out before blocking
  (void)fflush(stdout);

  for (uint16_t i = 0U; i < MAX_LINE_LENGTH; i++) {
    const int32_t ch = getchar();
    // EOF is an int outside char's range, so test for it while ch is still
    // an integer; after that, compare chars with chars (MISRA 10.4)
    const int8_t at_eof = (ch == EOF) ? 1 : 0;
    const char_t c = (at_eof != 0) ? '\0' : (char_t)ch;

    if ((at_eof != 0) || (c == '\n')) {
      if ((at_eof != 0) && (result == '\0')) {
        result = IO_INPUT_CLOSED;
      }
      break;
    }
    // '\r' too: a Windows console line can end "\r\n"
    if ((result == '\0') && (c != ' ') && (c != '\t') && (c != '\r')) {
      result = c;
    }
  }
  return result;
}

// === safe mode ===

void io_display_fault(const char_t *file, uint32_t line) {
  // to stderr, the error channel: the game screen stays on stdout as it
  // froze. no check on the result (deviation D3): a failure to report a
  // fault can't itself be reported anywhere.
  if (file != NULL) {
    (void)fprintf(stderr,
                  "\n  === SAFE MODE ===\n"
                  "  check failed at %s:%" PRIu32 "\n"
                  "  the game was stopped to protect its state.\n",
                  file, line);
  }
}
