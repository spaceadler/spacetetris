// the cyclic executive: drives the engine (tetris.c) and the platform port
// (io.h). no platform headers here; the build picks the port at link time.

#include "assert_handler.h"
#include "io.h"
#include "tetris.h"

static int8_t run_tick(tetris_engine_t *engine);
static int8_t handle_key(tetris_engine_t *engine, char_t key);
static void input(tetris_engine_t *engine, char_t button_pressed);

int main(void) {
  // JPL rule 2: the game loop needs a bound a tool can prove, so it's a
  // counted for loop instead of while (1). 10 hours of frames at
  // TARGET_FPS, then the program exits on its own.
  static const uint32_t MAX_GAME_TICKS = (uint32_t)TARGET_FPS * 3600U * 10U;
  tetris_engine_t engine;
  int32_t exit_code = 0;

  // power-on self-test: the shape table, the CRC and the PRNG are checked
  // against known answers before anything relies on them
  if (engine_self_test()) {
    int8_t running = 1;
    prng_init(&engine.prng, io_get_seed()); // before init(), which checks it
    init(&engine);

    for (uint32_t tick = 0U; (tick < MAX_GAME_TICKS) && (running != 0);
         tick++) {
      running = run_tick(&engine);
    }
  }

  // safe mode (JPL rule 5's recovery action): a failed check anywhere, in
  // the engine, the port or the self-test, ends the loop above within the
  // tick it happened in (a failed self-test means it never starts). report
  // where the first one sits, and say so in the exit status.
  if (assert_fault_latched()) {
    const assert_fault_t *fault = assert_first_fault();
    if (fault != NULL) {
      io_display_fault(fault->file, fault->line);
    }
    exit_code = 1;
  }

  return exit_code;
}

// one pass of the cyclic executive: read a key, advance the state machine
// once, check the engine's memory, draw, then wait out the rest of the
// frame. returns 0 to stop: the player quit, or a check failed.
static int8_t run_tick(tetris_engine_t *engine) {
  int8_t running = 0;

  // a fault latched during the last tick (in the renderer, say) stops the
  // game before it advances again
  if (!assert_fault_latched()) {
    if (TETRIS_ASSERT(engine != NULL)) {
      running = handle_key(engine, io_get_keypress());

      if (running != 0) {
        if (state_machine(engine) == 1) {
          running = 0;
        } else {
          verify_engine_integrity(engine);

          if (!assert_fault_latched()) {
            // Only render the board during active gameplay
            if (engine->current_state != menu) {
              io_render(engine);
            }
            io_delay_ms(FRAME_DELAY_MS);
          } else {
            running = 0; // safe mode: don't draw a state that failed a check
          }
        }
      }
    }
  }
  return running;
}

// Handle input based on current state. returns 0 when the player chose to
// quit, 1 to keep running.
static int8_t handle_key(tetris_engine_t *engine, char_t key) {
  int8_t running = 0; // an engine that can't be checked stops the game

  if (TETRIS_ASSERT(engine != NULL)) {
    running = 1;
    // this branches on the state before state_machine() checks it this tick
    (void)TETRIS_ASSERT((uint32_t)engine->current_state <=
                        (uint32_t)game_over);

    if (key != '\0') {
      if (engine->current_state == block_fall) {
        input(engine, key);
      } else if (engine->current_state == game_over) {
        // Non-blocking game-over input
        if ((key == 'y') || (key == 'Y')) {
          init(engine);
          engine->current_state = game_start;
        } else if ((key == 'n') || (key == 'N')) {
          running = 0;
        } else {
          // any other key: keep showing the game over screen
        }
      } else {
        // keys in the other states are dropped, as before
      }
    }
  }
  return running;
}

// player controls (move left/right, rotate, drop)
static void input(tetris_engine_t *engine, char_t button_pressed) {
  if (TETRIS_ASSERT(engine != NULL)) {
    int8_t valid_key = 1;
    // (x + 1) % 4 below would quietly "repair" a corrupted rotation; catch it
    (void)TETRIS_ASSERT((engine->active_piece.rotation >= 0) &&
                        (engine->active_piece.rotation < ROTATION_COUNT));

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
      test_rot = (int8_t)((test_rot + 1) % ROTATION_COUNT);
      break; // Rotate clockwise
    default:
      valid_key = 0;
      break; // Ignore invalid keys
    }

    // Fire the radar! If the coast is clear, commit the move.
    if (valid_key != 0) {
      if (physics_get(engine, test_x, test_y, test_rot) == 0) {
        engine->active_piece.x = test_x;
        engine->active_piece.y = test_y;
        engine->active_piece.rotation = test_rot;
      }
    }
  }
}
