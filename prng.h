#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

// xorshift32 (George Marsaglia, 2003). replaces rand()/srand(), which MISRA
// 21.24 bans: the C standard leaves rand()'s algorithm and range up to each
// library, so one seed deals different pieces on glibc, MSVC and newlib.
// this is three shifts and three XORs, identical everywhere.
typedef struct {
  uint32_t state; // never 0: zero shifted and XORed with itself stays zero
} prng_ctx_t;

// seed the generator. a seed of 0 is swapped for a fixed non-zero one.
void prng_init(prng_ctx_t *ctx, uint32_t seed);

// advance one step and return the new 32-bit value
uint32_t prng_next(prng_ctx_t *ctx);

#endif // PRNG_H
