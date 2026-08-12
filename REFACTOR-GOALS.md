# MISRA C:2012 & JPL Power of 10 Compliance Analysis (v2)

> Full rule-by-rule audit against the published standards. Every violation cites the specific rule.

---

## 1. Proposed File Structure (After Refactoring)

```
spacetetris/
├── tetris.h          Engine interface — no stdio.h, no stdlib.h, no time.h
├── tetris.c          Pure game logic — zero platform calls, zero banned headers
├── prng.h            xorshift32 PRNG — replaces rand()/srand()
├── prng.c            PRNG implementation
├── io.h              I/O abstraction — function declarations only
├── io_unix.c         Linux: termios, ANSI escape, printf
├── io_windows.c      Windows: conio.h, printf
├── io_rtos.c         RTOS: LCD driver, GPIO, RTOS delay
└── main.c            Cyclic executive loop — calls engine + I/O
```

### Build (link-time platform selection, no #ifdefs)

```bash
# Linux
gcc -Wall -Wextra -Werror -pedantic -std=c99 tetris.c prng.c io_unix.c main.c -o spacetetris

# Windows (MinGW)
gcc -Wall -Wextra -Werror -pedantic -std=c99 tetris.c prng.c io_windows.c main.c -o spacetetris.exe

# RTOS (cross-compile)
arm-none-eabi-gcc -Wall -Wextra -Werror -pedantic -std=c99 tetris.c prng.c io_rtos.c main.c -o spacetetris.elf
```

---

## 2. JPL Power of 10 — Rule-by-Rule Audit

### Rule 1: Simple Control Flow (no goto, setjmp, recursion)

✅ **Compliant.** No `goto`, `setjmp`/`longjmp`, or recursion (direct or indirect) anywhere in the codebase.

---

### Rule 2: All Loops Must Have a Fixed Upper Bound

A static analysis tool must be able to **prove** termination. `while(1)` fails this even with internal `break`.

| Location | Violation | Fix |
|---|---|---|
| [main.c:16](file:///home/adler/dev/spacetetris/main.c#L16) `while (1)` | Unbounded game loop | **Bounded cyclic executive** (see §4 below) |
| [main.c:53](file:///home/adler/dev/spacetetris/main.c#L53) `while ((clock() - start_time) < target_ticks)` | Unbounded busy-wait spin | Add `uint32_t guard` counter with `MAX_SPIN_ITERATIONS` upper bound |

#### Bounded Cyclic Executive Pattern

Replace `while(1)` with a bounded `for` loop. The game loop becomes a **cyclic executive** — a fixed-rate scheduler that runs for a provable maximum number of ticks:

```c
#define MAX_GAME_TICKS  ((uint32_t)2160000U)  /* 10 hours at 60 FPS */

int8_t running = 1;
for (uint32_t tick = 0U; (tick < MAX_GAME_TICKS) && (running != 0); tick++) {
    /* ... game loop body ... */
}
```

This is both JPL Rule 2 compliant and matches the **cyclic executive** architecture used in real flight software — a time-triggered loop with a known worst-case iteration count.

#### Bounded Busy-Wait Pattern

```c
#define MAX_SPIN_ITERATIONS  ((uint32_t)100000000U)

void delay_ms(int16_t milliseconds) {
    /* ... compute target ... */
    for (uint32_t guard = 0U; guard < MAX_SPIN_ITERATIONS; guard++) {
        if ((clock() - start_time) >= target_ticks) {
            break;
        }
    }
}
```

> [!WARNING]
> `clock()` itself is banned by MISRA Rule 21.10 (see §3). On the PC port this lives in `io_unix.c`/`io_windows.c` anyway, so it's isolated from the engine. On the RTOS port, `delay_ms()` would call `vTaskDelay()` or a hardware timer instead.

---

### Rule 3: No Dynamic Memory After Initialization

✅ **Compliant.** No `malloc`, `calloc`, `realloc`, `free`, `alloca`, or `sbrk` anywhere. All state lives in `tetris_engine_t` on the stack.

---

### Rule 4: Functions ≤ 60 Lines

| Function | Lines | Status | Fix |
|---|---|---|---|
| [state_machine](file:///home/adler/dev/spacetetris/tetris.c#L62-L136) | 74 | ❌ Over | Split into `static` per-state handlers |
| [render](file:///home/adler/dev/spacetetris/main.c#L90-L150) | 60 | ⚠️ Borderline | Moves to `io_unix.c`, split into `render_board()` + `render_hud()` |
| All others | <40 | ✅ OK | — |

**Split pattern for state_machine:**
```c
static void handle_game_start(tetris_engine_t *engine);
static void handle_block_fall(tetris_engine_t *engine);
static void handle_block_stop(tetris_engine_t *engine);
static void handle_line_check(tetris_engine_t *engine);
static void handle_wait(tetris_engine_t *engine);

int8_t state_machine(tetris_engine_t *engine) {
    int8_t result = 0;
    switch (engine->current_state) {
        case menu:        result = handle_menu(engine);       break;
        case game_start:  handle_game_start(engine);          break;
        /* ... */
        default:                                              break;
    }
    return result;
}
```

---

### Rule 5: Minimum 2 Assertions Per Function (Side-Effect-Free)

**Currently: ZERO assertions in the entire codebase.**

This is the single biggest compliance gap. The standard specifies:
- Assertions must be **side-effect-free** Boolean tests
- When an assertion fails, an **explicit recovery action** must be taken
- Any assertion a static checker can prove will **never fail** (or never hold) violates this rule ("anti-gaming")

#### What Needs Assertions

**A) Parameter validation** — every function entry:
```c
void physics_set(tetris_engine_t *engine) {
    assert(engine != NULL);
    assert((engine->active_piece.rotation >= 0) && (engine->active_piece.rotation < 4));
    /* ... */
}
```

**B) Before every array index** — this is critical:
```c
/* tetris.c — physics_get() */
assert((shape >= 0) && (shape < 7));
assert((test_rotation >= 0) && (test_rotation < 4));
assert((row >= 0) && (row < 4));
assert((col >= 0) && (col < 4));

/* Before accessing engine->grid[board_y][board_x] */
assert((board_y >= 0) && (board_y < TETRIS_ROWS));
assert((board_x >= 0) && (board_x < TETRIS_COLS));
```

**C) State invariant checks:**
```c
assert((engine->current_state >= menu) && (engine->current_state <= game_over));
assert(engine->bag_index >= 0 && engine->bag_index <= 7);
assert(engine->score >= 0);
```

> [!IMPORTANT]
> For the RTOS port, `assert()` should be redefined to a project-specific handler that logs to UART/flash and enters safe mode — not `abort()`. Define this in a project `assert_handler.h`.

#### Functions and Their Required Assertion Count

| Function | Min. Assertions Needed | Key Checks |
|---|---|---|
| `init` | 2 | `engine != NULL`, post-condition: `engine->score == 0` |
| `state_machine` | 2 | `engine != NULL`, `current_state` is valid enum value |
| `physics_get` | 4+ | `engine != NULL`, rotation range, shape range, array bounds before every `SHAPE_TABLE` and `grid` access |
| `physics_set` | 4+ | Same as above |
| `line_checker` | 3+ | `engine != NULL`, row/col bounds before `grid` access |
| `shuffle_bag` | 2 | `engine != NULL`, seed index range |
| `spawn_piece` | 2 | `engine != NULL`, `bag_index` range |
| `input` | 2 | `engine != NULL`, x/y within plausible range |
| `render` | 2 | `engine != NULL`, state is valid |

---

### Rule 6: Smallest Possible Scope

| Location | Issue | Fix |
|---|---|---|
| [tetris.c:200](file:///home/adler/dev/spacetetris/tetris.c#L200) `int8_t temp = 0;` | Declared outside the `for` body where it's used | Move inside `for` body |
| [main.c:60-62](file:///home/adler/dev/spacetetris/main.c#L60-L62) `test_x`, `test_y`, `test_rot` | Already at correct scope | ✅ OK |

---

### Rule 7: Check Return Values + Validate Parameters

**A) Unchecked return values:**

| Location | Unchecked Call | MISRA Rule |
|---|---|---|
| [tetris.c:151](file:///home/adler/dev/spacetetris/tetris.c#L151) `scanf(...)` | Return ignored | 17.7 |
| [tetris.c:161](file:///home/adler/dev/spacetetris/tetris.c#L161) `scanf(...)` | Return ignored | 17.7 |
| [tetris.c:172](file:///home/adler/dev/spacetetris/tetris.c#L172) `scanf(...)` | Return ignored | 17.7 |
| [main.c:94-148](file:///home/adler/dev/spacetetris/main.c#L94-L148) all `printf(...)` | Return ignored | 17.7 |
| [main.c:166](file:///home/adler/dev/spacetetris/main.c#L166) `tcgetattr(...)` | Return ignored | 17.7 |
| [main.c:169](file:///home/adler/dev/spacetetris/main.c#L169) `tcsetattr(...)` | Return ignored | 17.7 |
| [main.c:171-172](file:///home/adler/dev/spacetetris/main.c#L171-L172) `fcntl(...)` | Return ignored | 17.7 |
| [main.c:176](file:///home/adler/dev/spacetetris/main.c#L176) `tcsetattr(...)` | Return ignored | 17.7 |

**Fix:** Either check and handle, or explicitly cast to `(void)` with a comment:
```c
(void)printf("...");  /* Return value intentionally discarded — no recovery possible */
```

**B) Unchecked parameters** — no function currently validates its inputs. Every function taking `tetris_engine_t *engine` must check `engine != NULL` (covered by assertions above).

---

### Rule 8: Preprocessor Limited to Includes and Simple Macros

| Location | Issue | Fix |
|---|---|---|
| [main.c:3-9](file:///home/adler/dev/spacetetris/main.c#L3-L9) `#ifdef _WIN32` / `#elif __linux__` | Conditional compilation selecting platform code | **Eliminated** by splitting into `io_unix.c` / `io_windows.c` |
| [tetris.h:15-16](file:///home/adler/dev/spacetetris/tetris.h#L15-L16) `#define LOCK_DELAY_FRAMES` multi-line | Uses `\` line continuation | Acceptable if it's a simple constant, but could use `enum` or `static const` instead |

After the I/O split: **zero `#ifdef` blocks remain in any `.c` file**. Only include guards in `.h` files.

> [!TIP]
> MISRA Rule 20.4 also prohibits macros with names that match C keywords. Your `#define`s are all fine (TETRIS_ROWS, etc.), but prefer `static const` or anonymous `enum` over `#define` for integer constants where possible — the compiler gives better type checking.

---

### Rule 9: Restrict Pointer Use (≤1 Dereference, No Function Pointers)

✅ **Compliant.** Only single-level pointer dereferences (`engine->field`). No function pointers. The I/O abstraction uses link-time binding, not callbacks.

> [!NOTE]
> The original paper says "no more than one level of dereferencing." `engine->active_piece.x` is a single dereference of `engine` followed by struct member access — this is compliant. `engine->grid[row][col]` is a single pointer dereference followed by array indexing — also compliant.

---

### Rule 10: Compile With All Warnings, Use Static Analysis

**Current:** No build system or compiler flags specified beyond basic `gcc tetris.c main.c`.

**Required:**
```bash
gcc -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -std=c99
```

**Recommended static analysis tools:**
- `cppcheck --enable=all --addon=misra` (free, MISRA checker)
- `splint` (free, lightweight)
- `PC-lint Plus` (commercial, gold standard for MISRA)
- `Polyspace Bug Finder` (commercial, MathWorks)

---

## 3. MISRA C:2012 — Rule-by-Rule Audit

### §8: Declarations and Definitions

| Rule | Req. | Status | Issue |
|---|---|---|---|
| 8.1 Types explicit | Req. | ✅ | All types explicitly specified |
| 8.2 Prototype form | Req. | ✅ | All functions prototyped in `tetris.h` |
| 8.4 Compatible declaration visible | Req. | ✅ | All definitions have matching declarations |
| 8.7 Internal linkage if single TU | Adv. | ❌ | `SHAPE_TABLE` is only used in `tetris.c` but declared `extern` in header — should be `static` in `tetris.c` |
| 8.8 `static` for internal linkage | Req. | ❌ | Per-state handler functions should be `static` |
| 8.9 Block scope if single function | Adv. | ❌ | `temp` in `shuffle_bag` (see JPL Rule 6 above) |
| 8.13 `const` pointer where possible | Adv. | ❌ | `render()`, `physics_get()` don't modify engine — parameter should be `const tetris_engine_t *engine` |

---

### §9: Initialization

| Rule | Req. | Status | Issue |
|---|---|---|---|
| 9.1 Read before set | Mand. | ✅ | All variables initialized before use |
| 9.3 No partial array init | Req. | ✅ | `SHAPE_TABLE` fully initialized |

---

### §10: Essential Type Model (Type Conversions)

This is a **major violation area**. MISRA's essential type model requires explicit handling of every narrowing, widening, and cross-category conversion.

| Rule | Location | Violation | Fix |
|---|---|---|---|
| **10.1** Inappropriate operand type | [tetris.c:207](file:///home/adler/dev/spacetetris/tetris.c#L207) `rand() % (i + 1)` | `rand()` returns `int`, used in modulo with `int8_t` | Replace with `prng_next()` returning `uint32_t`, use explicit casts |
| **10.3** Narrowing assignment | [tetris.c:207](file:///home/adler/dev/spacetetris/tetris.c#L207) `int8_t seed = rand() % (i + 1)` | `int` → `int8_t` narrowing without cast | `int8_t seed = (int8_t)(prng_next(&prng) % (uint32_t)(i + 1));` |
| **10.3** Narrowing assignment | [tetris.c:221](file:///home/adler/dev/spacetetris/tetris.c#L221) `engine->active_piece.shape_type = engine->bag[engine->bag_index]` | Enum category mismatch possible | Add explicit cast to `(shape_type_t)` |
| **10.4** Mixed arithmetic | [main.c:50](file:///home/adler/dev/spacetetris/main.c#L50) `(milliseconds * CLOCKS_PER_SEC) / 1000` | `int16_t × clock_t` — mixed signed widths | Explicit cast: `(clock_t)milliseconds * CLOCKS_PER_SEC` — but `clock()` is banned anyway (Rule 21.10) |
| **10.4** Mixed arithmetic | [tetris.c:84-88](file:///home/adler/dev/spacetetris/tetris.c#L84-L88) comparisons | `int16_t` compared against `#define` (which is `int`) | Use typed constants or explicit casts |
| **10.6** Composite to wider | Various loop counters | `int8_t i` used in expressions that promote to `int` | Use `int8_t` consistently with explicit casts on results |

> [!IMPORTANT]
> The **general rule**: every narrowing assignment needs an explicit cast. Every arithmetic operation between different-width types needs the types aligned first. Use `U` suffix on all unsigned constants (e.g., `0U`, `4U`, `7U`).

---

### §12: Expressions

| Rule | Location | Violation |
|---|---|---|
| 12.1 Make precedence explicit | [main.c:168](file:///home/adler/dev/spacetetris/main.c#L168) `newt.c_lflag &= ~(ICANON \| ECHO)` | ✅ Parenthesized correctly |
| 12.1 Make precedence explicit | [tetris.c:87-89](file:///home/adler/dev/spacetetris/tetris.c#L87-L89) compound conditions | Add explicit parentheses around sub-expressions for clarity |

---

### §13: Side Effects

| Rule | Status | Notes |
|---|---|---|
| 13.3 Increment/decrement | ✅ | No compound side-effect expressions |
| 13.4 Assignment result used | ✅ | No `if (x = ...)` patterns |
| 13.5 Side effects in `&&`/`||` RHS | ✅ | No function calls in logical RHS |

---

### §15: Control Flow

| Rule | Location | Violation | Fix |
|---|---|---|---|
| **15.1** No `goto` | — | ✅ Compliant | — |
| **15.4** Max one `break` per loop | [line_checker](file:///home/adler/dev/spacetetris/tetris.c#L275-L301) inner loop | `break` at L282 is the only one per loop | ✅ OK |
| **15.5** Single exit per function | Multiple functions | ❌ See table below | Use result variable pattern |
| **15.7** `if...else if` terminated with `else` | Not applicable | ✅ | — |

#### MISRA 15.5 — Functions With Multiple Returns

| Function | Return Points | Fix |
|---|---|---|
| [state_machine](file:///home/adler/dev/spacetetris/tetris.c#L62-L136) | L67, L135 | Single `result` variable |
| [call_menu](file:///home/adler/dev/spacetetris/tetris.c#L164-L197) | L189, L196 | Single `result` variable |
| [physics_get](file:///home/adler/dev/spacetetris/tetris.c#L228-L252) | L241, L246, L251 | Single `result` variable |
| [line_checker](file:///home/adler/dev/spacetetris/tetris.c#L275-L329) | L324, L328 | Single `result` variable |
| [get_raw_keypress](file:///home/adler/dev/spacetetris/main.c#L152-L187) | L157, L158, L180, L181, L185 | Single `result` variable |
| [input](file:///home/adler/dev/spacetetris/main.c#L58-L88) | L79 | Single `result` variable or remove early return |

**Pattern:**
```c
int8_t physics_get(tetris_engine_t *engine, int8_t test_x, int8_t test_y,
                   int8_t test_rotation) {
    int8_t result = 0;
    assert(engine != NULL);
    assert((test_rotation >= 0) && (test_rotation < 4));

    for (...) {
        for (...) {
            if (/* collision */) {
                result = 1;  /* instead of return 1; */
            }
        }
    }
    return result;  /* single exit point */
}
```

> [!NOTE]
> The single-exit refactoring for `physics_get` may need a `found` flag and loop-break pattern to avoid continuing iteration after collision is detected. This is fine — MISRA 15.4 allows one `break` per loop.

---

### §16: Switch Statements

| Rule | Location | Violation | Fix |
|---|---|---|---|
| **16.3** Every case ends with `break` | All switches | ✅ All cases have `break` or `return` | After 15.5 fix, all will have `break` |
| **16.4** Every switch has `default` | [tetris.c:63](file:///home/adler/dev/spacetetris/tetris.c#L63) `state_machine` switch | ❌ No `default` | Add `default: break;` |
| **16.4** Every switch has `default` | [tetris.c:304](file:///home/adler/dev/spacetetris/tetris.c#L304) `line_checker` scoring switch | ❌ No `default` | Add `default: break;` |
| **16.4** Every switch has `default` | [main.c:65](file:///home/adler/dev/spacetetris/main.c#L65) `input` switch | ✅ Has `default` | — |
| **16.6** Min 2 switch-clauses | All switches | ✅ All have ≥2 cases | — |

---

### §17: Functions

| Rule | Location | Violation |
|---|---|---|
| **17.2** No recursion | — | ✅ No recursion |
| **17.7** Return value used | See JPL Rule 7 table | ❌ All `printf`/`scanf`/`tcsetattr`/`fcntl` unchecked |
| **17.8** Don't modify parameters | — | ✅ Parameters not modified |

---

### §20: Preprocessor

| Rule | Location | Status |
|---|---|---|
| **20.1** `#include` only after preprocessor directives | — | ✅ OK |
| **20.4** No macro names matching keywords | — | ✅ OK |
| **20.5** `#undef` not used | — | ✅ OK |

After I/O split: zero `#ifdef`/`#elif`/`#else` in any `.c` file. ✅

---

### §21: Standard Library Restrictions ⚠️ CRITICAL

This is the section with the most severe violations.

| Rule | Banned Item | Current Usage | Fix |
|---|---|---|---|
| **21.3** `malloc`/`free` | Dynamic allocation | ✅ Not used | — |
| **21.8** `abort`/`exit` | Program termination | ✅ Not used | — |
| **21.10** `<time.h>` — **ENTIRE HEADER BANNED** | [tetris.h:8](file:///home/adler/dev/spacetetris/tetris.h#L8) `#include <time.h>` | ❌ Used for `srand(time(NULL))` and `clock()` in `delay_ms()` | **Remove entirely.** Seed PRNG from hardware timer or fixed seed. Move `delay_ms` to I/O layer using platform timer. |
| **21.24** `rand`/`srand` — **BANNED** | [main.c:12](file:///home/adler/dev/spacetetris/main.c#L12) `srand(time(NULL))`, [tetris.c:207](file:///home/adler/dev/spacetetris/tetris.c#L207) `rand()` | ❌ Used for bag shuffling | **Replace with xorshift32 PRNG** (see §5 below) |

> [!CAUTION]
> `<time.h>` being banned means `clock()`, `time()`, `difftime()`, `localtime()` — all of it. Your `delay_ms()` currently depends on `clock()`. This function **must** live exclusively in the I/O layer (`io_unix.c` uses `clock()` or `nanosleep()`, `io_rtos.c` uses `vTaskDelay()`, etc.). The engine must never see `<time.h>`.

After cleanup, `tetris.h` includes only:
```c
#include <stdint.h>
#include <string.h>
#include "prng.h"
```

No `<stdio.h>`, no `<stdlib.h>`, no `<time.h>`.

---

## 4. Gravity Simplification

**Current (overcomplicated):**
- `GRAVITY_FRAMES 30` — piece drops every 30 frames
- `gravity_counter` field in engine struct — frame counter

**After (simplified):**
- `TARGET_FPS` controls drop speed directly (which was your original intent)
- Remove `GRAVITY_FRAMES` define, `gravity_counter` field
- Block drops **every tick** of the cyclic executive loop

**Files affected:**
- [tetris.h:14](file:///home/adler/dev/spacetetris/tetris.h#L14) — delete `GRAVITY_FRAMES`
- [tetris.h:56](file:///home/adler/dev/spacetetris/tetris.h#L56) — delete `gravity_counter` from struct
- [tetris.c:58](file:///home/adler/dev/spacetetris/tetris.c#L58) — delete `engine->gravity_counter = 0`
- [tetris.c:77](file:///home/adler/dev/spacetetris/tetris.c#L77) — delete `engine->gravity_counter = 0`
- [tetris.c:81-95](file:///home/adler/dev/spacetetris/tetris.c#L81-L95) — simplify `block_fall` to just try gravity every tick
- [tetris.c:125](file:///home/adler/dev/spacetetris/tetris.c#L125) — delete `engine->gravity_counter = 0`

**Simplified `block_fall`:**
```c
static void handle_block_fall(tetris_engine_t *engine) {
    assert(engine != NULL);
    /* Gravity: try to move down every tick. FPS = fall speed. */
    if (physics_get(engine, engine->active_piece.x,
                    (int8_t)(engine->active_piece.y + 1),
                    engine->active_piece.rotation) == 0) {
        engine->active_piece.y++;
    } else {
        engine->current_state = block_stop;
    }
}
```

---

## 5. xorshift32 PRNG — Replacing `rand()`/`srand()`

Both `rand()` and `srand()` are **banned** by MISRA Rule 21.24. `time()` for seeding is **banned** by Rule 21.10. Replace with a deterministic, self-contained PRNG.

### `prng.h`
```c
#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

typedef struct {
    uint32_t state;
} prng_ctx_t;

void prng_init(prng_ctx_t *ctx, uint32_t seed);
uint32_t prng_next(prng_ctx_t *ctx);

#endif /* PRNG_H */
```

### `prng.c`
```c
#include "prng.h"
#include <assert.h>

void prng_init(prng_ctx_t *ctx, const uint32_t seed) {
    assert(ctx != NULL);
    /* xorshift32 is stuck at zero if state == 0; force non-zero */
    ctx->state = (seed == 0U) ? 0xA634716AU : seed;
    assert(ctx->state != 0U);  /* post-condition */
}

uint32_t prng_next(prng_ctx_t *ctx) {
    assert(ctx != NULL);
    assert(ctx->state != 0U);  /* invariant: state must never be zero */

    uint32_t x = ctx->state;
    x ^= (x << 13U);
    x ^= (x >> 17U);
    x ^= (x << 5U);
    ctx->state = x;

    assert(ctx->state != 0U);  /* post-condition: still non-zero */
    return x;
}
```

### Usage in `shuffle_bag`
```c
void shuffle_bag(tetris_engine_t *engine) {
    assert(engine != NULL);

    for (int8_t i = 0; i < 7; i++) {
        engine->bag[i] = (shape_type_t)i;
    }

    for (int8_t i = 6; i > 0; i--) {
        uint32_t raw = prng_next(&engine->prng);
        int8_t j = (int8_t)(raw % (uint32_t)((uint8_t)i + 1U));

        assert((j >= 0) && (j <= i));  /* bounds check before swap */

        shape_type_t temp = engine->bag[i];
        engine->bag[i] = engine->bag[j];
        engine->bag[j] = temp;
    }
}
```

### Seeding
Add `prng_ctx_t prng;` to the `tetris_engine_t` struct. In `main.c`:
```c
/* Platform-specific seed — io.h provides io_get_seed() */
prng_init(&engine.prng, io_get_seed());
```

Each platform implements `io_get_seed()`:
- **Linux:** read from `/dev/urandom` or use a hardware timer counter
- **Windows:** `GetTickCount()` or `__rdtsc()`  
- **RTOS:** hardware timer register value

---

## 6. Checksum/Parity Verification on Critical State

You mentioned verifying `engine->grid`, `score`, and `lines_cleared` with a checksum once per tick. This is a **defensive coding** pattern aligned with JPL's data integrity philosophy and Rule 5 (assertions).

### Approach: CRC-8 or Simple XOR Parity

Add to `tetris_engine_t`:
```c
typedef struct {
    int8_t grid[TETRIS_ROWS][TETRIS_COLS];
    active_piece_t active_piece;
    current_state_t current_state;
    int32_t score;
    int16_t lines_cleared;
    int8_t bag_index;
    shape_type_t bag[7];
    int16_t wait_counter;
    prng_ctx_t prng;

    /* Data integrity */
    uint8_t grid_checksum;     /* XOR parity over grid[][] */
    uint8_t score_checksum;    /* XOR parity over score + lines_cleared */
} tetris_engine_t;
```

### Checksum Functions
```c
static uint8_t compute_grid_checksum(const tetris_engine_t *engine) {
    assert(engine != NULL);
    uint8_t cs = 0U;
    for (int8_t row = 0; row < TETRIS_ROWS; row++) {
        for (int8_t col = 0; col < TETRIS_COLS; col++) {
            cs ^= (uint8_t)engine->grid[row][col];
        }
    }
    return cs;
}

static uint8_t compute_score_checksum(const tetris_engine_t *engine) {
    assert(engine != NULL);
    uint8_t cs = 0U;
    const uint8_t *p = (const uint8_t *)&engine->score;
    for (uint8_t i = 0U; i < (uint8_t)sizeof(engine->score); i++) {
        cs ^= p[i];
    }
    p = (const uint8_t *)&engine->lines_cleared;
    for (uint8_t i = 0U; i < (uint8_t)sizeof(engine->lines_cleared); i++) {
        cs ^= p[i];
    }
    return cs;
}
```

### Verification — Once Per Tick in Main Loop
```c
/* Inside cyclic executive, after state_machine() */
void verify_engine_integrity(tetris_engine_t *engine) {
    assert(engine != NULL);

    uint8_t grid_cs = compute_grid_checksum(engine);
    assert(grid_cs == engine->grid_checksum);  /* grid corruption detected */

    uint8_t score_cs = compute_score_checksum(engine);
    assert(score_cs == engine->score_checksum);  /* score corruption detected */
}
```

### Update — After Every State Mutation
After any function that modifies `grid`, `score`, or `lines_cleared`:
```c
engine->grid_checksum = compute_grid_checksum(engine);
engine->score_checksum = compute_score_checksum(engine);
```

Functions that need checksum updates:
- `init()` — after zeroing grid
- `physics_set()` — after stamping piece into grid
- `line_checker()` — after clearing lines and updating score

> [!WARNING]
> The `compute_score_checksum` function uses a pointer cast (`uint8_t *`) to iterate over bytes. This violates **MISRA Rule 11.3** (cast between pointer to object types). An alternative is to use bitwise decomposition:
> ```c
> cs ^= (uint8_t)(engine->score & 0xFFU);
> cs ^= (uint8_t)((engine->score >> 8U) & 0xFFU);
> cs ^= (uint8_t)((engine->score >> 16U) & 0xFFU);
> cs ^= (uint8_t)((engine->score >> 24U) & 0xFFU);
> ```
> This avoids pointer casts entirely and is fully MISRA-compliant.

---

## 7. I/O Abstraction — Removing stdio from Engine

### Current `stdio` calls in [tetris.c](file:///home/adler/dev/spacetetris/tetris.c)

| Line | Call | Where it goes |
|---|---|---|
| 73 | `printf("\033[2J\033[H")` | → `io_clear_screen()` in `io.h` |
| 140-149 | `printf(...)` in `guide()` | → `io_display_guide()` |
| 151 | `scanf(...)` in `guide()` | → `io_read_char()` |
| 155-159 | `printf(...)` in `about()` | → `io_display_about()` |
| 161 | `scanf(...)` in `about()` | → `io_read_char()` |
| 165-169 | `printf(...)` in `call_menu()` | → `io_display_menu()` |
| 172 | `scanf(...)` in `call_menu()` | → `io_read_menu_selection()` |
| 188 | `printf(...)` in `call_menu()` | → `io_display_text("Thanks...")` |

### After: `tetris.c` Header Includes

```c
#include "tetris.h"   /* engine types + constants */
#include "prng.h"     /* xorshift32 */
#include "io.h"       /* I/O abstraction for menu display */
#include <assert.h>
#include <string.h>
```

**No `<stdio.h>`. No `<stdlib.h>`. No `<time.h>`.**

---

## 8. Complete Checklist (Prioritised)

| # | Change | Standard Rule(s) | Severity | Effort |
|---|---|---|---|---|
| 1 | Replace `rand()`/`srand()` with xorshift32 PRNG | MISRA 21.24 | 🔴 Mandatory | Medium |
| 2 | Remove `#include <time.h>` — move `delay_ms`/seed to I/O | MISRA 21.10 | 🔴 Mandatory | Medium |
| 3 | Remove `#include <stdio.h>` from engine — abstract all I/O | MISRA 21.6 / JPL arch | 🔴 Required | Medium |
| 4 | Split I/O into `io_unix.c` / `io_windows.c` / `io_rtos.c` | MISRA 20.x / JPL 8 | 🟠 Required | Medium |
| 5 | Bound main loop (cyclic executive) | JPL 2 | 🔴 Required | Small |
| 6 | Bound `delay_ms()` busy-wait loop | JPL 2 | 🔴 Required | Trivial |
| 7 | Add ≥2 assertions per function (avg) | JPL 5 | 🔴 Required | Large |
| 8 | Add assertions **before every array index** | JPL 5 + 7 | 🔴 Required | Large |
| 9 | Add `default:` to all `switch` statements | MISRA 16.4 | 🔴 Required | Trivial |
| 10 | Single return per function | MISRA 15.5 | 🟠 Advisory | Medium |
| 11 | Check/`(void)`-cast all return values | MISRA 17.7 / JPL 7 | 🔴 Required | Small |
| 12 | Validate all function parameters | JPL 7 | 🔴 Required | Medium |
| 13 | Explicit casts on all narrowing assignments | MISRA 10.3 | 🔴 Required | Medium |
| 14 | `U` suffix on all unsigned constants | MISRA 10.1 | 🟠 Required | Small |
| 15 | `const` on pointer params that don't modify target | MISRA 8.13 | 🟡 Advisory | Small |
| 16 | `static` on file-local functions/objects | MISRA 8.7 / 8.8 | 🟠 Required | Small |
| 17 | Make `SHAPE_TABLE` static in `tetris.c` | MISRA 8.7 | 🟡 Advisory | Trivial |
| 18 | Remove gravity counter — simplify to 1 tick = 1 drop | Simplification | 🟢 Design | Small |
| 19 | Split `state_machine()` into per-state handlers | JPL 4 | 🟠 Required | Small |
| 20 | Move `temp` to smallest scope in `shuffle_bag` | JPL 6 / MISRA 8.9 | 🟡 Advisory | Trivial |
| 21 | Add checksum/parity on `grid`, `score`, `lines_cleared` | JPL 5 (defensive) | 🟠 Defensive | Medium |
| 22 | Verify checksums once per tick in main loop | JPL 5 (defensive) | 🟠 Defensive | Small |
| 23 | Explicit parentheses in compound conditions | MISRA 12.1 | 🟡 Advisory | Trivial |
| 24 | Compile with `-Wall -Wextra -Werror -Wconversion` | JPL 10 | 🔴 Required | Trivial |
| 25 | Run `cppcheck --addon=misra` — zero warnings | JPL 10 | 🔴 Required | Ongoing |

---

## 9. Headers After Full Compliance

### `tetris.h`
```c
#include <stdint.h>
#include <string.h>
#include "prng.h"
```

### `tetris.c`
```c
#include "tetris.h"
#include "io.h"
#include <assert.h>
```

### `io_unix.c` / `io_windows.c`
```c
#include "io.h"
#include <stdio.h>    /* printf — allowed in I/O layer only */
/* platform-specific headers */
```

### `main.c`
```c
#include "tetris.h"
#include "io.h"
#include <assert.h>
```

**Zero banned headers in engine code. `stdio.h`, `stdlib.h`, `time.h` isolated to platform I/O files.**

---

*Analysis based on MISRA C:2012 (Third Edition, incl. AMD1–AMD4), MISRA C:2012 Rule 21.24 (AMD3), and Gerard J. Holzmann, "The Power of 10: Rules for Developing Safety-Critical Code," IEEE Computer, June 2006.*

