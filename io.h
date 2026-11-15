#ifndef IO_H
#define IO_H

#include <stdint.h>

#include "char_t.h"
#include "tetris.h"

// the platform port. exactly one io_<platform>.c defines every function here,
// and the build picks which by naming that file on the command line:
//
//   gcc ... assert_handler.c io_unix.c ...     (Linux / macOS terminal)
//   gcc ... assert_handler.c io_windows.c ...  (Windows console)
//   gcc ... assert_handler.c io_rtos.c ...     (embedded stub)
//
// that is link-time selection: no .c file needs an #ifdef to pick a platform.
//
// the line between the two sides: the engine decides WHAT happens (a state,
// a screen to show, a choice to ask for) and never HOW it looks. text,
// clearing, pauses, colours and panels all live in the port.

// returned by the blocking reads when stdin has ended (piped input ran out,
// terminal closed). ASCII EOT, the character Ctrl-D stands for.
#define IO_INPUT_CLOSED ((char_t)0x04)

// what the engine wants the player to see. each port decides how it looks:
// the terminal port clears and prints text; an LCD port would draw panels.
typedef enum {
  IO_SCREEN_MENU,       // the main menu, about to ask for a choice
  IO_SCREEN_GUIDE,      // how to play, about to wait for Enter
  IO_SCREEN_ABOUT,      // credits, about to wait for Enter
  IO_SCREEN_BAD_CHOICE, // the menu choice wasn't one of the options
  IO_SCREEN_GOODBYE,    // the player quit from the menu
  IO_SCREEN_GAME        // a new game is starting: make room for the board
} io_screen_t;

// === timing and randomness ===

// wait this long before returning
void io_delay_ms(uint16_t milliseconds);

// a seed for prng_init(). 0 is allowed: prng_init() replaces it.
uint32_t io_get_seed(void);

// === gameplay ===

// non-blocking keypress reader: '\0' when no key is waiting
char_t io_get_keypress(void);

// renderer: draws the board, score and game over screen
void io_render(const tetris_engine_t *engine);

// === screens and choices (these may block) ===

// show one of the screens above
void io_show(io_screen_t screen);

// wait for the player's menu choice: '1'..'4' expected, anything else is
// passed through for the menu to reject, IO_INPUT_CLOSED if stdin ended
char_t io_read_menu_selection(void);

// wait for the player to press Enter. returns the first character they typed
// ('\0' for a bare Enter), IO_INPUT_CLOSED if stdin ended
char_t io_read_char(void);

// === safe mode ===

// report where the first failed check sits, once, as the program stops. no
// implementation uses TETRIS_ASSERT: a failure to report can't be reported.
void io_display_fault(const char_t *file, uint32_t line);

#endif // IO_H
