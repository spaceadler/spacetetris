#include "prng.h"

#include <stddef.h> // NULL

#include "assert_handler.h"

void prng_init(prng_ctx_t *ctx, uint32_t seed) {
  // any non-zero value works; this is the one REFACTOR-GOALS.md picked
  static const uint32_t PRNG_FALLBACK_SEED = 0xA634716AU;

  if (TETRIS_ASSERT(ctx != NULL)) {
    ctx->state = (seed == 0U) ? PRNG_FALLBACK_SEED : seed;
  }
}

uint32_t prng_next(prng_ctx_t *ctx) {
  uint32_t x = 0U; // nothing to step: 0 keeps a caller's % in range

  if (TETRIS_ASSERT(ctx != NULL)) {
    // can't be zero after prng_init(), so if it is, something else wrote
    // here. (a zero state would only ever give 0 back anyway.)
    if (TETRIS_ASSERT(ctx->state != 0U)) {
      x = ctx->state;
      x ^= (x << 13U);
      x ^= (x >> 17U);
      x ^= (x << 5U);
      ctx->state = x;
    }
  }
  return x;
}
