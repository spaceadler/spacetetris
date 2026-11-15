/* old-vs-new trace comparison for tools/check.sh.
 *
 * built twice: once against the original v1 engine (-DOLD, its tetris.c and
 * tetris.h pulled from git) and once against the current one. both play the
 * same scripted games and write one line per tick (state, piece, score, a
 * hash of the grid) to $TRACE. identical traces mean the refactor kept the
 * engine's behaviour exactly.
 *
 * the randomness has to match too: v1 shuffled with rand() % (i + 1) and the
 * current engine with prng_next() % (i + 1). the OLD build's rand() returns
 * prng_next() % 420, and 420 is lcm(2..7), so for every bag size k the two
 * agree: (x % 420) % k == x % k.
 *
 * current engine only: `trace_harness grid` / `trace_harness score` corrupts
 * that state behind the engine's back and runs verify_engine_integrity().
 * exit 3: the check caught it (fault latched, safe mode would follow).
 * exit 0: the corruption went unnoticed.
 *
 * the report goes to stderr: v1's game_start printf()s a screen clear to
 * stdout, which check.sh throws away.
 *
 * test code, not flight code: it isn't held to the project's rules. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prng.h"
#include "tetris.h" /* v1's or the current one, picked by -I order */
#ifndef OLD
#include "assert_handler.h"
#endif

#define SEED 12345U
#define TICKS 500000U

#ifdef OLD
static prng_ctx_t g_ctx;
int rand(void) { return (int)(prng_next(&g_ctx) % 420U); }
void srand(unsigned s) { (void)s; }
void delay_ms(int16_t ms) { (void)ms; }
#endif

static FILE *trace;
static uint32_t lcg = 777U;
static uint32_t next_input(void) {
  lcg = lcg * 1664525U + 1013904223U;
  return lcg >> 16;
}

/* the original main.c input(), identical for both builds */
static void drive_input(tetris_engine_t *e, char k) {
  int8_t x = e->active_piece.x, y = e->active_piece.y;
  int8_t r = e->active_piece.rotation;
  switch (k) {
  case 'a': x--; break;
  case 'd': x++; break;
  case 's': y++; break;
  case 'w': r = (int8_t)((r + 1) % 4); break;
  default: return;
  }
  if (physics_get(e, x, y, r) == 0) {
    e->active_piece.x = x;
    e->active_piece.y = y;
    e->active_piece.rotation = r;
  }
}

static uint32_t grid_hash(const tetris_engine_t *e) {
  uint32_t h = 2166136261U;
  for (int r = 0; r < 20; r++)
    for (int c = 0; c < 10; c++) {
      h ^= (uint8_t)e->grid[r][c];
      h *= 16777619U;
    }
  return h;
}

static void dump(const char *tag, uint32_t t, const tetris_engine_t *e) {
  fprintf(trace, "%s %u st=%d x=%d y=%d r=%d sh=%d score=%d lines=%d g=%08x\n",
          tag, t, (int)e->current_state, e->active_piece.x, e->active_piece.y,
          e->active_piece.rotation, (int)e->active_piece.shape_type,
          (int)e->score, (int)e->lines_cleared, grid_hash(e));
}

static void step(tetris_engine_t *e) {
  (void)state_machine(e);
#ifndef OLD
  verify_engine_integrity(e);
  if (assert_fault_latched()) {
    fprintf(stderr, "unexpected fault at %s:%u\n", assert_first_fault()->file,
            (unsigned)assert_first_fault()->line);
    exit(2);
  }
#endif
}

static void fresh(tetris_engine_t *e) {
#ifdef OLD
  prng_init(&g_ctx, SEED);
#else
  prng_init(&e->prng, SEED);
#endif
  init(e);
}

/* fill rows [top, 19] except `width` columns from hole_col, put an I piece
 * at (x, y, rot), then lock it and line-check it */
static void scenario(const char *name, int top, int hole_col, int width,
                     int8_t x, int8_t y, int8_t rot) {
  tetris_engine_t e;
  fresh(&e);
  for (int r = top; r < 20; r++)
    for (int c = 0; c < 10; c++)
      e.grid[r][c] = (c >= hole_col && c < hole_col + width) ? 0 : 1;
  e.active_piece.shape_type = INDIA;
  e.active_piece.x = x;
  e.active_piece.y = y;
  e.active_piece.rotation = rot;
  e.current_state = block_stop;
  step(&e); /* block_stop: lock */
  step(&e); /* line_check: clear + score */
  dump(name, 0U, &e);
  fprintf(stderr, "  %-7s lines=%d score=%d\n", name, (int)e.lines_cleared,
          (int)e.score);
}

int main(int argc, char **argv) {
  tetris_engine_t e;
  uint32_t games = 0U, max_lines = 0U;

#ifndef OLD
  if (argc == 2) {
    fresh(&e);
    if (strcmp(argv[1], "grid") == 0) {
      e.grid[10][4] = 1;
    } else {
      e.score = 9999;
    }
    verify_engine_integrity(&e);
    if (assert_fault_latched()) {
      printf("caught at %s:%u\n", assert_first_fault()->file,
             (unsigned)assert_first_fault()->line);
      return 3;
    }
    return 0;
  }
#else
  (void)argc;
  (void)argv;
#endif

  trace = fopen(getenv("TRACE"), "w");
  if (trace == NULL) {
    return 2;
  }
  scenario("1-line", 19, 3, 4, 3, 18, 0); /* horizontal I fills row 19 */
  scenario("2-line", 18, 9, 1, 7, 16, 1); /* vertical I into column 9 */
  scenario("3-line", 17, 9, 1, 7, 16, 1);
  scenario("4-line", 16, 9, 1, 7, 16, 1);

  fresh(&e);
  e.current_state = game_start;
  for (uint32_t t = 0U; t < TICKS; t++) {
    uint32_t k = next_input() % 16U;
    char key = (k < 2U) ? 'a' : (k < 4U) ? 'd' : (k < 5U) ? 'w' : (k < 10U) ? 's' : '\0';
    if (e.current_state == block_fall && key != '\0') {
      drive_input(&e, key);
    }
    if (e.current_state == game_over) {
      games++;
      init(&e);
      e.current_state = game_start;
    }
    int16_t before = e.lines_cleared;
    step(&e);
    if ((uint32_t)(e.lines_cleared - before) > max_lines) {
      max_lines = (uint32_t)(e.lines_cleared - before);
    }
    dump("t", t, &e);
  }
  fprintf(stderr, "  random run: %u ticks, %u games, most lines in one clear %u\n",
          TICKS, games, max_lines);
  fclose(trace);
  return 0;
}
