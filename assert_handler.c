#include "assert_handler.h"

// the program's only mutable file-scope state: system health is global by
// nature. static, so only the three functions below can touch it.
static bool fault_latched = false;
static assert_fault_t first_fault = {"", 0U};

// no TETRIS_ASSERT anywhere in this file: the handler can't check itself,
// and calling anything that checks would put a cycle in the call graph
// (JPL rule 1). so it reports nothing either; main() does that in safe mode.
void assert_record(const char_t *file, uint32_t line) {
  if (!fault_latched) {
    fault_latched = true;
    first_fault.file = file; // never NULL: assert_fail() sees to it
    first_fault.line = line;
  }
}

bool assert_fault_latched(void) { return fault_latched; }

const assert_fault_t *assert_first_fault(void) { return &first_fault; }
