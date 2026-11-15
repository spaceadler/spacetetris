# Refactor Report: spacetetris v1 → v2

> Every change between the original code (**v1**, commit `033d184`) and the refactored code (**v2**, the working tree as of 2026-11-15), shown before and after, with the rule each one serves. The numbers below are measured, not estimated: the same compilers, analyzers and checkers were run on both versions. Companion to [REFACTOR-GOALS.md](REFACTOR-GOALS.md) (the plan) and [COMPLIANCE.md](COMPLIANCE.md) (the evidence, deviations and gaps). `tools/check.sh` re-runs all of it.

## 1. At a glance

| | v1 | v2 |
|---|---|---|
| Code | 3 files, 617 lines | 12 files, 1,807 lines, plus `tools/` |
| Binary, `gcc -std=c99 -O2` (code + constants) | 8,317 bytes | 13,064 bytes: the checks, CRC integrity, self-test and handler cost ~4.7 KB |
| gcc, strictest flags (`gcc-strict.flags`) | 25 warnings | 0 |
| clang `-Weverything` (`clang-strict.flags`) | 5 warnings | 0 |
| Static analyzers (gcc `-fanalyzer`, clang `--analyze`) | not run | 0 findings, complete analysis |
| cppcheck MISRA C:2012 | 67 findings across 15 rules | 2, both documented deviations (D1 stdio, D2 `<time.h>`, only in the terminal ports) |
| Cobra MISRA set | not run | 3 reviewed lines (`int main`, the `char_t` typedef, `TETRIS_ASSERT`'s definition) |
| Cobra Power of 10 | P10_8 (platform `#ifdef`s) | none; its stock rule 5 can't see `TETRIS_ASSERT` |
| Rule 5 by Cobra's own formula (≥ 1 assertion per 50 statements) | fails: 0 for 207 statements | passes: 69 for 430 (unix) |
| `rand()` / `srand()` (MISRA 21.24) | yes | no: xorshift32 |
| Banned headers in the engine | `<stdio.h>`, `<stdlib.h>`, `<time.h>` | none |
| Text and screen handling in the engine | menu `printf`/`scanf`, two messages, a screen clear, a pause | none: the engine names a screen, the port draws it |
| Conditional compilation in `.c` files | 5 lines (`#ifdef _WIN32` ...) | 0: link-time ports |
| Unbounded loops | 2 (`while (1)`, the `clock()` spin) | 0 |
| Functions over 60 lines | 2 (`state_machine` 75, `render` 61) | 0 (longest `shape_cell`, 57) |
| Run-time checks | 0 in 15 functions | 69 in 45 functions |
| Compile-time checks | 0 | 1 (exactly 7 shapes, `TANGO` the 7th) |
| What happens when a check fails | (no checks) | local safe default, then safe mode: report + exit 1 |
| Return values ignored silently | 21 (MISRA 17.7) | 0: checked, or discarded with an explicit `(void)` where failure changes nothing (D3) |
| Recursion / heap | none / none | none / none |
| Worst-case stack (`-O2`, from `main`) | 464 bytes | 560 bytes |
| Behaviour | | identical to v1, tick for tick, over 500,004 traced ticks |

Function lengths here are counted by `tools/p10_metrics.py` (signature through closing brace); REFACTOR-GOALS.md counted `state_machine` as 74 and `render` as 60.

## 2. Files, before and after

```
v1                                     v2
tetris.h   engine interface            tetris.h / tetris.c   engine: state machine, collision, line
           (+ stdio, stdlib, time)                           clears, checksums, power-on self-test
tetris.c   engine + menu screens       prng.h / prng.c       xorshift32
           (printf / scanf)            assert_handler.h/.c   TETRIS_ASSERT and the fault latch
main.c     loop, input, render,        char_t.h              char_t: the one place `char` is named
           delay, keyboard             io.h                  the platform interface
           (#ifdef per platform)       io_unix.c             terminal port: termios, ANSI, clock()
                                       io_windows.c          console port: conio.h (from io_unix.c)
                                       io_rtos.c             embedded stub, a TODO per function
                                       main.c                cyclic executive, safe mode
                                       gcc-strict.flags      the strictest warning set, per compiler
                                       clang-strict.flags
                                       tools/                check.sh, p10_metrics.py, callgraph.py,
                                                             p10_rule5.cobra, trace_harness.c,
                                                             pty_game_test.py
                                       COMPLIANCE.md         evidence, deviations, gaps
                                       REFACTOR-REPORT.md    this file
```

## 3. The changes, step by step

### Step 1: the platform, and the representation, out of the engine

*JPL rule 8 / Power of 10 rule 8, MISRA 21.6 / 21.10, the core/port separation*

**Before:** `main.c` chose the platform with conditional compilation, and the engine's interface pulled in the standard I/O and time headers.

```c
// main.c (v1)
#ifdef _WIN32
#include <conio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

// tetris.h (v1)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
```

The engine also did its own presentation: it printed the menus, held the "Thanks for playing" and "Unknown option" texts, cleared the screen at game start, and called `delay_ms()` (defined in `main.c`, the one dependency direction the architecture forbids) so a message could be read.

**After:** `io.h` declares the platform interface once. Each port implements all of it, and the build picks the platform by which file it names:

```bash
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_unix.c    main.c   # Linux / macOS
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_windows.c main.c   # Windows
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_rtos.c    main.c   # embedded stub
```

The engine says *what* happens and never *how* it looks. Five display functions became one call with a list of screens:

```c
// tetris.c (v2): the engine names a screen
io_show(IO_SCREEN_BAD_CHOICE);

// io_unix.c (v2): the terminal port decides what that looks like
case IO_SCREEN_BAD_CHOICE:
  put("\nUnknown option. Please try again.\n");
  io_delay_ms(1500U); // long enough to read before the menu redraws
  break;
```

`tetris.c` contains no text, no screen clearing and no pauses; an LCD port would draw panels for the same screen ids. No `.c` file contains an `#ifdef`, and the engine includes only `<stdbool.h>`, `<stddef.h>` and `<stdint.h>`.

### Step 2: `rand()` → xorshift32

*MISRA 21.24, determinism across platforms*

**Before:**

```c
srand(time(NULL));                 // main.c
int8_t seed = rand() % (i + 1);    // tetris.c, shuffle_bag()
```

`rand()`'s algorithm and range differ between C libraries, so the same seed dealt different pieces on glibc, MSVC and newlib.

**After:** `prng.c`, three shifts and three XORs, identical everywhere. The seed comes from `/dev/urandom` (`io_get_seed()`), and a zero seed is replaced, because xorshift32 stays at zero forever.

```c
x ^= (x << 13U);
x ^= (x >> 17U);
x ^= (x << 5U);
```

### Step 3: every loop has a bound you can prove

*Power of 10 rule 2, JPL rule 3, MISRA 14.2*

**Before:** two unbounded loops, plus a loop counter modified inside its own body.

```c
while (1) { ... break; ... }                         // main.c: the game loop
while ((clock() - start_time) < target_ticks) { }    // main.c: delay_ms()

for (int8_t row = TETRIS_ROWS - 1; row >= 0; row--) {  // tetris.c: line_checker()
  ...
        row++; // Re-check this row
```

**After:**

```c
for (uint32_t tick = 0U; (tick < MAX_GAME_TICKS) && (running != 0); tick++)  // 10 h at 60 FPS
for (guard = 0U; guard < MAX_SPIN_ITERATIONS; guard++)                       // 10^8 spins
for (int8_t pass = 0; pass < LINE_CHECK_PASSES; pass++)                      // 40 passes
```

`line_checker` now counts passes. Each pass either clears the current row (the row above falls into it, so it's checked again) or moves up one. There are at most 20 of each, so 40 passes always reach the top, and a check after the loop confirms it. The headless rtos build demonstrates the game loop's bound: with no input it plays one game, then runs out its 2,160,000 ticks and exits.

### Step 4: functions under 60 lines

*Power of 10 rule 4, JPL rule 25*

**Before:** `state_machine()` (75 lines) held every state's logic; `render()` (61) drew the HUD, the board and the game-over screen.

**After:** `state_machine()` is a dispatch `switch` over six `handle_<state>()` functions. Rendering became `render_hud()`, `render_board()` and `render_game_over()`. `line_checker()` gained `row_is_full()`, `remove_row()` and `points_for_lines()`. The longest function is now `shape_cell()` at 57 lines, 43 of them its shape table.

### Step 5: one exit per function, and loops that always run to the end

*MISRA 15.5, 15.4*

**Before:**

```c
// physics_get() (v1)
if (board_x < 0 || board_x >= TETRIS_COLS || board_y >= TETRIS_ROWS) {
  return 1;
}
if (board_y >= 0 && engine->grid[board_y][board_x] != 0) {
  return 1;
}
```

**After:** a `result` variable, and the loop checks all 16 cells every time, so the worst-case time is the usual time (the property EAGLE's WCET work will measure):

```c
if ((board_x < 0) || (board_x >= TETRIS_COLS) || (board_y >= TETRIS_ROWS)) {
  result = 1;
} else if ((board_y >= 0) && (engine->grid[board_y][board_x] != 0)) {
  result = 1;
} else {
  // free cell
}
```

### Step 6: types and conversions

*MISRA 10.x, 7.2, 12.1, Dir 4.6; JPL rules 17 and 18*

**Before:** implicit narrowing and enum mix-ups, mostly invisible to gcc's default warnings, and basic types everywhere.

```c
engine->bag[i] = i;                      // int8_t into an enum (found by clang)
temp = engine->bag[i];                   // enum into an int8_t
int8_t board_x = test_x + col;           // int arithmetic into int8_t
test_rot = (test_rot + 1) % 4;           // same
clock_t target_ticks = (milliseconds * CLOCKS_PER_SEC) / 1000;
```

**After:**

- Constants carry their type (`#define TETRIS_ROWS ((int8_t)20)`), every narrowing is an explicit cast that a check has made safe, unsigned constants have `U`, shifts happen only on unsigned values, and `printf` uses `PRId32` (on arm-none-eabi `int32_t` is `long`, so `%d` would be wrong).
- Text is `char_t` (`char_t.h`, the name MISRA's own examples use) and integers are `<stdint.h>` types; the only basic types left named are `int main(void)` and the `char_t` typedef itself, both exceptions MISRA Dir 4.6 grants.
- Chained conditions are paired, `(shape_ok && rotation_ok) && (row_ok && col_ok)`, which also removed Cobra's precedence matches.

The last line of the "before" block was a real bug. On 32-bit Linux, `clock_t` is a 32-bit signed `long` and `CLOCKS_PER_SEC` is 1,000,000, so any delay over ~2.1 s overflowed: undefined behaviour. gcc's `-Wstrict-overflow` pointed at it. v2 calculates in `uint64_t`:

```c
const uint64_t target_ticks =
    ((uint64_t)milliseconds * (uint64_t)CLOCKS_PER_SEC) / 1000U;
```

### Step 7: return values and input

*MISRA 17.7, Dir 4.7; Power of 10 rule 7; JPL rules 14 and 15*

**Before:** 21 ignored return values, including every terminal-mode call and every `scanf`.

```c
tcgetattr(STDIN_FILENO, &oldt);              // get_raw_keypress() (v1)
newt.c_lflag &= ~(ICANON | ECHO);
tcsetattr(STDIN_FILENO, TCSANOW, &newt);

char selection;                              // call_menu() (v1)
scanf(" %c", &selection);
```

**After:** every call whose failure changes what should happen is checked, and failure takes a defined path:

```c
if (tcgetattr(STDIN_FILENO, &oldt) == 0) {   // not a terminal: report no key, touch nothing
  ...
  const int32_t restored = tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  (void)TETRIS_ASSERT(restored == 0);        // a failed restore leaves the terminal without echo
```

Terminal writes and `fclose` are discarded with an explicit `(void)`: a failed write has no better response than carrying on, which is the case Power of 10 rule 7's rationale names ("This is often the case with calls to printf and close"). That's deviation D3.

`scanf` is gone, replaced by `read_line_first_char()`, which reads one bounded line; end of input and read errors both come back as `IO_INPUT_CLOSED`, and the menu quits. Three v1 bugs disappeared with these changes (section 5).

### Step 8: control-flow hygiene

*MISRA 16.4, 15.7, 15.6*

**Before:** `state_machine()`'s `switch` and the scoring `switch` had no `default`, and two `if ... else if` chains had no final `else`.

**After:** every `switch` has a `default`, every `else if` chain ends in `else` (with a comment saying what that case means), and every body is braced.

### Step 9: scope and visibility

*MISRA 8.7, 8.8, 8.9, 8.13; Power of 10 rules 6 and 8; JPL rules 13 and 20*

**Before:**

```c
extern const int16_t SHAPE_TABLE[7][4][4][4];   // tetris.h: anyone could index it
#define LOCK_DELAY_FRAMES                                                      \
  15 // brief pause after a piece locks before next spawn
```

and every engine function, including internal helpers, was public.

**After:**

- `SHAPE_TABLE` is a `static const` inside `shape_cell()`, the only function that can read it, and it checks all four indices first. The renderer asks `cell_filled(engine, row, col)` instead of knowing what a tetromino is.
- File-local functions are `static`; the public engine API is `init`, `state_machine`, `physics_get`, `cell_filled`, `verify_engine_integrity` and `engine_self_test`.
- Pointers the function doesn't write through are `const`.
- No `#define` in any `.c` file: file-local constants are typed `static const` objects, declared inside the function that uses them.

### Step 10: strings and escapes

*MISRA 4.1, format-string safety*

**Before:**

```c
printf("\033[H");          // the octal escape runs straight into '['
printf("\033[2J\033[H");
```

**After:**

```c
static const char_t ANSI_HOME[] = "\033" "[H";   // each escape ends where its literal ends
put(ANSI_HOME);
```

Text passed in as a parameter is written with `fputs`, never used as a `printf` format.

### Step 11: checks that recover, and safe mode

*Power of 10 rule 5, JPL rule 16, MISRA 21.8, the spec's `assert_handler.h`*

**Before:** no assertions at all.

**After (first pass):** 61 standard `assert()`s. These abort the process on failure (fail-stop, not recovery) and disappear entirely under `-DNDEBUG`.

**After (final):** `TETRIS_ASSERT`, shaped like Holzmann's `c_assert` (JPL D-60411 rule 16):

```c
#define TETRIS_ASSERT(cond) ((cond) ? true : assert_fail(__FILE__, __LINE__))
```

It's an expression, true when the condition holds, so the code around it can recover:

```c
int8_t physics_get(const tetris_engine_t *engine, ...) {
  int8_t result = 1; // a position that can't be checked counts as blocked
  const bool engine_ok = TETRIS_ASSERT(engine != NULL);
  ...
  if ((engine_ok && rotation_ok) && (x_ok && y_ok)) {
    ...
```

- **Local recovery:** guards take a safe default. A shape cell that can't be looked up reads as solid, a move that can't be checked is blocked, a corrupted bag index forces a fresh bag, and a row that can't be checked is never removed.
- **Safe mode:** a failed check latches a fault. `main()` stops the game at the end of that tick, never draws a state that failed a check, reports where the first failure sits on `stderr`, and exits with status 1:

  ```
    === SAFE MODE ===
    check failed at tetris.c:608
    the game was stopped to protect its state.
  ```

- **No `abort()`, no `NDEBUG`:** checks are always compiled in.
- **`assert_fail()` is a `static inline` function in the header that visibly returns `false`.** That way both static analyzers can see a failed check is false. When it lived in `assert_handler.c`, clang reported NULL dereferences inside guarded blocks, because for all it knew `assert_fail()` returned true.
- **The condition isn't turned into text.** An earlier version stringified it (`#cond`) for the report; that took the `#` operator (MISRA 20.10) and stored ~40 condition strings in the binary. `file:line` points at the exact check.
- **Side-effect-free conditions** (rule 5): values are worked out first, then checked. `assert(physics_get(...) == 0)` became `const int8_t overlaps = physics_get(...); (void)TETRIS_ASSERT(overlaps == 0);`.
- **Only checks that change something.** A round of checks added to raise the per-function average (every terminal write, `fclose`, `ferror`, a duplicate score check, a summary assert in `main`) was taken out again: none of them changed what the program would do.

### Step 12: data integrity, the power-on self-test and the compile-time check

*Power of 10 rule 5 (defensive), JPL rule 16, REFACTOR-GOALS.md §6*

**Before:** nothing checked the engine's own memory or its tables.

**After:**

- **CRC-8 checksums** over the grid and over score + lines are refreshed on every write and verified once per tick. It's CRC rather than XOR because the cells are only ever 0 or 1, so an XOR over them collapses to a single parity bit. The bytes come out by shifting unsigned copies, not by casting `&score` to a byte pointer (MISRA 11.3).
- **Every grid cell must be 0 or 1.** The CRC catches *changes*; this catches a *value no code path should write*, even through a legitimate path. A planted bug (`physics_set` writing 2) is caught at the first lock, which the CRC alone would never do.
- **A power-on self-test** checks known answers before anything relies on them:
  - every shape in every rotation has exactly 4 cells (`SHAPE_TABLE` typos, flash corruption);
  - CRC-8 of `"123456789"` is `0xF4` (the published check value);
  - xorshift32 from seed 1 first returns 270369 (worked by hand: 1 → 8193 → 8193 → 270369).
- **A compile-time check** (JPL rule 16's "static assertions"): `SHAPE_TABLE` is sized by `SHAPE_COUNT_CHECKED`, which is -1, an array size no compiler accepts, unless there are exactly 7 shapes and `TANGO` is the 7th. The build stops, with or without `-Werror`, at no run-time cost. It's the Apollo lesson in one line: the case that "can't happen" is the one worth a check.

### Step 13: naming

**Before:** the lock-delay state was `wait`, which clashes with POSIX `pid_t wait(int *)`. Any file including both `<sys/wait.h>` and `tetris.h` fails to compile, and RTOS or SDK headers that declare `wait` would break the build.

**After:** `tetriswait`, and its handler `handle_tetriswait()`.

### Step 14: build and verification

**Before:** `gcc tetris.c main.c -o spacetetris`, with default warnings.

**After:**

- `gcc-strict.flags` / `clang-strict.flags`: every warning each compiler has, with the few exceptions explained in COMPLIANCE.md §1.
- `tools/`: `p10_metrics.py` (rules 4 and 5), `callgraph.py` (rules 1 and 3, worst-case stack), `p10_rule5.cobra` (rule 5 by Cobra's own formula), `trace_harness.c` (v1 against v2, tick for tick), `pty_game_test.py` (the real game in a pseudo-terminal), and `check.sh`, which runs everything.
- Code that confused an analyzer was rewritten, as Power of 10 rule 10's rationale asks, except where the analyzer's pattern is provably broken (COMPLIANCE.md §3).

## 4. Behaviour changes a player can notice

Everything else is identical: the trace comparison replays v1 and v2 on the same seed and scripted input, and every one of 500,004 trace lines matches.

| Situation | v1 | v2 |
|---|---|---|
| "press Enter to return" on the guide or about screen, pressing just Enter | nothing happens; you have to type a character first | returns to the menu |
| Bare Enter at the main menu | silently keeps waiting | "Unknown option. Please try again." |
| stdin closed (end of piped input, terminal gone) | loops forever on an uninitialized menu choice | says goodbye and exits |
| stdin piped (not a terminal) | the key poll swallows the first character | the key poll leaves stdin alone |
| A check fails | (no checks) | game stops, SAFE MODE report with `file:line` on stderr, exit status 1 |
| Exit status | always 0 | 0 normally, 1 after safe mode |

## 5. Bugs found and fixed along the way

| Bug | How it was found |
|---|---|
| `scanf(" %c")` skips newlines, so a bare Enter never returned from the guide or about screen | reading the code; confirmed in a pseudo-terminal (v1 fails the test, v2 passes) |
| Closed stdin: `scanf` failed unchecked, `selection` was read uninitialized (undefined behaviour), and the menu looped forever | reading the code; confirmed (v1 killed by a timeout, v2 exits 0) |
| Piped stdin: v1's key poll ran even though `tcgetattr()` had failed, and swallowed the first character | while verifying section 4: `printf '4\n' \| v1` hangs, `printf '\n4\n' \| v1` quits |
| Delays over ~2.1 s overflowed on 32-bit Linux | gcc `-Wstrict-overflow` in the strictest build |
| The `wait` state clashed with POSIX `wait()` | a test harness that included `<sys/wait.h>` failed to compile |
| `int8_t` ↔ enum conversions in `shuffle_bag` | clang `-Wsign-conversion`; REFACTOR-GOALS.md had pointed at a different line |

## 6. Not changed, or still open

- **Power of 10 rule 5 is recorded as met** on its author's own checker (Cobra's `P10_5` formula, `tools/p10_rule5.cobra`) and on JPL D-60411 rule 16, whose 10-line clause is a *should* rule with five justified deviations. The paper's sentence, "should average to minimally two assertions per function", comes to 1.53 program-wide (1.97 in the engine and `main.c`). See COMPLIANCE.md §4.
- **The engine still owns the menu keys** (`'1'`..`'4'`): the input-side counterpart of `io_show()` would be a list of choices the port maps keys or buttons onto.
- **The terminal is reconfigured on every key poll** (~60 times a second). Moving the setup to start/exit needs a Ctrl-C-safe exit path, and MISRA 21.5 bans `<signal.h>`.
- **During the lock delay, the locked piece is drawn twice.** After a line clear it shows at its pre-clear position for ~250 ms. This is inherited from v1's `render()`; the fix is one line in `cell_filled()`.
- **`io_windows.c` has not been built with real MinGW.**
- **`io_rtos.c` is a stub:** no timer, buttons, display or UART fault log yet.
- **Deviations D1-D4** (stdio and `<time.h>` in the terminal ports, unchecked terminal writes and `fclose`, the `TETRIS_ASSERT` macro) are recorded with their reasons in COMPLIANCE.md §3.

## 7. Verification

`tools/check.sh`, run on the final code: **44 passed, 0 failed, 0 skipped.**

| Section | What it proves |
|---|---|
| 1. compile | gcc and clang, strictest flags, `-O2` and `-O0`, linux and rtos (windows: syntax only): zero warnings |
| 2. static analyzers | gcc `-fanalyzer` (complete analysis) and clang `--analyze` (with alpha checkers): no findings |
| 3. MISRA, cppcheck | per configuration, only the documented deviations |
| 4. Cobra | Power of 10 and MISRA rule sets: only reviewed findings, the MISRA ones pinned to exact lines; rule 5 by Cobra's own formula passes; `scope_check`: no data wider than needed |
| 5. project rules | no `#define` or `#if` in `.c` files; no banned headers, calls or `goto` in the engine; function size and reviewed exceptions; no recursion, fixed frames, no allocators, stack within budget |
| 6. planted faults | the metrics, call-graph and strict-build checks each catch a planted violation; the rule 5 formula fails v1; the compile-time check stops 8 shapes and an inserted shape; the MISRA addon flags a planted `goto` |
| 7. behaviour | identical traces vs v1; sanitizers clean; corrupted state caught at its exact check; bounded headless run; piped menus; safe mode at power-on and in play, each at its exact check; the real game in a terminal |
