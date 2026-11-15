// RTOS / embedded port. this grew out of the "FLIPPER ZERO / EMBEDDED" stub
// at the bottom of the old get_raw_keypress(): no target is chosen yet, so
// every function is a placeholder, and each TODO names what the real port
// supplies.
//
// it still links and runs anywhere, as a headless game: the menu always
// starts a new game, no keys arrive, nothing is drawn, the stack tops out,
// and the bounded loop in main.c runs out its ticks and returns.

#include "io.h"

#include "assert_handler.h"

// === timing and randomness ===

void io_delay_ms(uint16_t milliseconds) {
  (void)milliseconds; // TODO: vTaskDelay(pdMS_TO_TICKS(milliseconds))
}

uint32_t io_get_seed(void) {
  // TODO: read a free-running hardware timer register. 0 for now, which
  // prng_init() swaps for its fixed seed, so every run deals the same pieces.
  return 0U;
}

// === gameplay ===

char_t io_get_keypress(void) {
  return '\0'; // TODO: map GPIO buttons to 'a' / 'd' / 's' / 'w'
}

void io_render(const tetris_engine_t *engine) {
  // the io.h contract, same as the terminal ports
  // TODO: draw cell_filled() into the LCD framebuffer
  (void)TETRIS_ASSERT(engine != NULL);
}

// === screens and choices ===

void io_show(io_screen_t screen) {
  // the same check as the terminal ports: the id comes from the engine
  // TODO: draw each screen on the LCD: menu and guide panels, a "bad
  // choice" toast, a goodbye panel, and a blank board for IO_SCREEN_GAME
  (void)TETRIS_ASSERT((uint32_t)screen <= (uint32_t)IO_SCREEN_GAME);
}

char_t io_read_menu_selection(void) {
  return '1'; // no menu buttons yet: always start a new game
}

char_t io_read_char(void) {
  return '\0'; // TODO: wait for a button press
}

// === safe mode ===

void io_display_fault(const char_t *file, uint32_t line) {
  // TODO: the spec's safe mode, "logs to UART/flash": write where the
  // failed check sits to the debug UART, or append it to a fault log in
  // flash, then leave the game stopped. no TETRIS_ASSERT here: a failure to
  // report can't itself be reported. a plain if, as in the terminal ports.
  if (file != NULL) {
    (void)line; // TODO: write "<file>:<line>" to the UART
  }
}
