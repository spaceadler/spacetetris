#ifndef ASSERT_HANDLER_H
#define ASSERT_HANDLER_H

#include <stdbool.h>
#include <stddef.h> // NULL
#include <stdint.h>

#include "char_t.h"

// the project's assertion (JPL Power of 10 rule 5), shaped like Holzmann's
// c_assert. TETRIS_ASSERT(cond) is an expression that is true when cond
// holds, so the code around it can take a recovery action when it doesn't:
//
//   if (TETRIS_ASSERT(engine != NULL)) {
//     ...only now is *engine touched...
//   }
//
// unlike <assert.h> it never aborts (MISRA 21.8 keeps abort() out of the
// code) and never compiles away (NDEBUG has no effect on it). a failed check
// latches a fault and records where the first one sits; main() sees the
// latch at the end of the tick and stops in safe mode.
//
// when cond holds the value is plain `true`, so a static analyzer can see
// that cond holds inside the if, and a passing check costs no call at all.
//
// it has to be a function-like macro to say where it sits (__FILE__,
// __LINE__): deviation D4 in COMPLIANCE.md (MISRA Dir 4.9). it doesn't turn
// the condition into text: that would take the # operator (MISRA 20.10),
// and on a microcontroller the text of every condition would sit in flash.
// file:line points at the exact check. cond must have no side effects: work
// values out first, then check them.
#define TETRIS_ASSERT(cond) ((cond) ? true : assert_fail(__FILE__, __LINE__))

// a tagged struct with the typedef on its own line, so no `typedef` sits
// next to a `*`: Cobra's rule 9 check reads that as a typedef hiding a
// pointer, and this pointer is a plain member
struct assert_fault {
  const char_t *file;
  uint32_t line;
};
typedef struct assert_fault assert_fault_t;

// records a failed check if it's the first (later ones are usually its
// fallout) and latches the fault. assert_handler.c; reached only through
// assert_fail() below.
void assert_record(const char_t *file, uint32_t line);

// called by TETRIS_ASSERT when its condition is false: records the failure
// and returns false. it's defined here, static inline (MISRA 8.10), so every
// file and every static analyzer can see that a failed check is false. a
// function in another file could, as far as an analyzer knows, return true
// and let a NULL pointer into the code a check guards.
static inline bool assert_fail(const char_t *file, uint32_t line) {
  assert_record((file != NULL) ? file : "?", line);
  return false;
}

// true from the first failed check on. it never resets.
bool assert_fault_latched(void);

// the first failed check. meaningful once assert_fault_latched() is true.
const assert_fault_t *assert_first_fault(void);

#endif // ASSERT_HANDLER_H
