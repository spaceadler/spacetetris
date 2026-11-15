# Compliance Record: MISRA C:2012 and JPL Power of 10

> What was checked, with which tool, what it found, what was fixed, and what is deliberately not followed (with the reason). The audit and the plan live in [REFACTOR-GOALS.md](REFACTOR-GOALS.md); the change-by-change walkthrough from v1 is [REFACTOR-REPORT.md](REFACTOR-REPORT.md); this file is the evidence. Everything here is re-checked by one command: `tools/check.sh` (section 7).

Recorded 2026-11-15 on the Raspberry Pi (aarch64, Debian 12) with:

| Tool | Version | Used for |
|---|---|---|
| gcc | 12.2.0 | strictest build, `-fanalyzer`, call graph + stack sizes |
| clang | 14.0.6 | `-Weverything` build, static analyzer |
| cppcheck | 2.10 + `misra.py` addon | MISRA C:2012 rules, scope and style checks |
| Cobra | 5.4 (`nimble-code/Cobra` @ `e307aed`) | Power of 10 (`main/p10`, and `tools/p10_rule5.cobra`), MISRA cross-check (`main/misra2012`), `scope_check` |

Every check runs on each **build configuration** separately, i.e. the files that link into one program:

| Config | Files |
|---|---|
| unix | `tetris.c prng.c assert_handler.c io_unix.c main.c` |
| windows | `tetris.c prng.c assert_handler.c io_windows.c main.c` (no MinGW here: compiled with `-fsyntax-only` against a stand-in `conio.h`) |
| rtos | `tetris.c prng.c assert_handler.c io_rtos.c main.c` |

## Summary

| Check | unix | windows | rtos |
|---|---|---|---|
| gcc strict build (`@gcc-strict.flags`), `-O2` and `-O0` | 0 warnings | 0 (syntax) | 0 warnings |
| clang `-Weverything` (`@clang-strict.flags`), `-O2` and `-O0` | 0 warnings | 0 (syntax) | 0 warnings |
| gcc `-fanalyzer` / clang `--analyze` | 0 / 0 | n/a | 0 / 0 |
| cppcheck MISRA findings | D1, D2 | D1, D2 | none (1 `unusedFunction`, see §3) |
| Cobra `main/misra2012` | 3 reviewed lines | n/a | 3 reviewed lines |
| Cobra `main/p10` | only P10_5 (its stock rule can't see `TETRIS_ASSERT`; its formula passes, §4) | n/a | only P10_5 |
| Power of 10 | all 10 rules met (§4) | same | same |
| `tools/check.sh` | 44 passed, 0 failed, 0 skipped | | |

---

## 1. Strictest build

### What "strictest" means here

- **clang:** `-Weverything`, which turns on every warning clang has.
- **gcc:** has no such switch, so `gcc-strict.flags` turns on every C warning that `gcc -Q --help=warnings` reports as disabled under `-Wall -Wextra` (64 in gcc 12), with every leveled warning at its maximum (`-Wformat=2`, `-Wstrict-overflow=5`, `-Wimplicit-fallthrough=5`, `-Wstringop-overflow=4`, `-Wshift-overflow=2`, `-Warray-bounds=2`, `-Wunused-const-variable=2`, `-Wuse-after-free=3`, `-Wstrict-aliasing=1`, `-Wbidi-chars=any`).
- **Both:** `-std=c99 -pedantic-errors -Werror -O2`. `-O2` because several warnings (`-Wnull-dereference`, `-Wstrict-overflow`, the stack checks) only see what the optimizer works out. The build is also clean at `-O0`.
- **Budgets** (flight-software habits): every stack frame at most 512 bytes (`-Wstack-usage=512`, `-Wframe-larger-than=512`), no object larger than 1 KB (`-Wlarger-than=1024`), no variable-length arrays or `alloca` (`-Wvla`, `-Walloca`).

`gcc-strict.flags` and `clang-strict.flags` are response files: the compiler reads its options from them (`gcc @gcc-strict.flags ...`), so it's still a direct compiler call with no build system. Response files can't hold comments, so the explanations are here.

### Warnings switched off, and why

Each one is listed as an explicit `-Wno-...` at the bottom of its flags file, so "deliberately off" can't be mistaken for "forgotten".

| Warning | Compiler | Why it's off |
|---|---|---|
| `-Wcovered-switch-default` | clang | Contradicts MISRA 16.4 (every `switch` needs a `default`), and gcc's `-Wswitch-default` (on) demands the opposite. No code can satisfy both. Clang's concern, a `default` hiding a newly added enum value, is already covered by `-Wswitch-enum` (on in both), which warns about a missing enum case even when a `default` exists. |
| `-Wpadded` | both | Reports the compiler's alignment padding (1 byte in `active_piece_t`, 1 + 3 in `tetris_engine_t`). The only way to silence it is dead `reserved` members. Padding only matters if a struct's raw bytes leave memory (a wire, DMA, a checksum over raw bytes). `tetris_engine_t` never does, and its CRCs read the fields, not the raw bytes. |
| `-Wdeclaration-after-statement` | both | A C90 rule. Meeting it would mean dropping `const` from locals or reading `engine->...` before checking `engine != NULL`. It wouldn't make the code C90 anyway (`//` comments, `for (int8_t i = ...)`, `<stdint.h>` are all C99 and this warning ignores them). JPL rule 13 and Power of 10 rule 6 favour declaring at first use. |
| `-Wsuggest-attribute=pure`, `=const` | gcc | The fix is `__attribute__((pure))`, a GCC extension (MISRA 1.2), and other compilers would need it hidden behind `#ifdef`s, which rule 8 removed. `=noreturn`, `=format`, `=malloc`, `=cold` stay on. |
| `-Wtraditional`, `-Wtraditional-conversion` | gcc | Compare against pre-1989 K&R C. The second flags every call made through a prototype, which MISRA 8.2 requires. |
| `-Wc90-c99-compat` | gcc | Flags every C99 feature; the target is C99. |
| `-Wabi` | gcc | C++ ABI warning. In a C build its only output is a notice that it has nothing to warn about, which `-Werror` would turn into a failure. |
| `-Wsystem-headers` | gcc | Reports problems inside glibc's own headers, not this code. |

`-Wc++-compat` is **on and clean**: with every conversion explicit, the C is also valid C++.

### What the strict build found and what changed

| Finding | Fix |
|---|---|
| `-Wstrict-overflow` in `io_delay_ms` (both ports) | `(clock_t)ms * CLOCKS_PER_SEC` overflows a 32-bit signed `clock_t` (32-bit Linux, where `CLOCKS_PER_SEC` is 1000000) for any delay over ~2.1 s, and signed overflow is undefined behaviour. The target is now calculated in `uint64_t`, and elapsed time is subtracted in unsigned too, so a clock that wraps around ends the delay early instead of overflowing. |
| everything else | the `-Wpadded`, `-Wdeclaration-after-statement`, `-Wsuggest-attribute` and `-Wcovered-switch-default` hits are the exclusions above |

---

## 2. Static analyzers (JPL rule 2, Power of 10 rule 10)

| Analyzer | Result |
|---|---|
| clang `--analyze`, checkers `core, unix, deadcode, security, nullability, optin.portability, valist`, plus `alpha.core, alpha.security, alpha.unix` | 0 findings |
| gcc `-fanalyzer`, with `--param=analyzer-max-enodes-per-program-point=200 --param=analyzer-bb-explosion-factor=200` and `-Wanalyzer-too-complex` on | 0 findings, and the analysis completes on every file |

What the analyzers caught on the way: the first version of the check handler declared `assert_fail()` in the header and defined it in `assert_handler.c`. Neither analyzer can see into another file, so both had to assume `assert_fail()` might return true, i.e. that a guard like `if (TETRIS_ASSERT(engine != NULL))` might let a NULL `engine` through. clang reported those paths as NULL dereferences and out-of-bounds reads; gcc ran out of exploration budget on them. `assert_fail()` is now a `static inline` function in the header that visibly returns `false` (MISRA 8.10 wants inline functions `static`). Both analyzers are clean, and gcc completes at its original limit.

---

## 3. MISRA C:2012

### Coverage: what the tools can and can't see

- cppcheck 2.10's `misra.py` implements **130 of the rules** (cppcheck's own core checks cover a few more) and **none of the directives**. Directives are reviewed by hand below. It also doesn't implement rule 21.24 (`rand`/`srand`): v1's use of them was found by reading the code.
- Cobra's `main/misra2012` rule set was run as an independent second opinion. Several of its patterns are reused from MISRA 1997 (see below).
- Neither tool is a certified MISRA checker. A clean result means "no violation these tools can detect", which is strong evidence but not proof of compliance.
- Debian doesn't ship the MISRA rule texts (licensed by MISRA), so cppcheck reports rule numbers only; the descriptions below are paraphrased.

### cppcheck: findings and fixes

| Rule | Where | Fix |
|---|---|---|
| 4.1 (Req): octal/hex escapes must be terminated | `"\033[H"`, `"\033[J"`, `"\033[2J\033[H"` | The escape sits in its own literal: `"\033" "[H"`. Adjacent literals join into one string, and each escape now ends where its literal ends. (The danger is real with hex: `"\x1BAB"` is a single character, `\x1BAB`.) |
| 10.4 (Req): operands of the same essential type category | `ch == '\n'` etc. in `read_line_first_char` | `getchar()`'s result is tested against `EOF` first, then converted once, and characters are compared with characters. |
| 10.3 (Req): no assignment to a different category | `result = IO_INPUT_CLOSED` (`'\x04'`, an `int`) | `#define IO_INPUT_CLOSED ((char_t)0x04)` |
| 10.8 (Req): no casting a composite expression to another category | `(int8_t)(raw % (i + 1U))` in `shuffle_bag` | The `%` result is held in a `uint32_t`, then the variable is cast, so the width of the arithmetic is visible. |
| 8.9 (Adv): an object used in one function belongs inside it | `SHAPE_TABLE` | Now a `static const` inside `shape_cell()`, the only function that reads it. Nothing else in the file *can* index the table without going through that function's bounds checks. |
| 20.10 (Adv): no `#` operator | `#cond` in `TETRIS_ASSERT` | The macro no longer turns the condition into text: the safe-mode report names `file:line`, which points at the exact check. As a side effect ~40 condition strings left the binary (16,301 → 13,064 bytes of code and constants, together with the checks removed in the same pass). |
| 2.4, 10.6, from a first form of the compile-time check (an anonymous `enum`) | `tetris.c` | The check moved into the size of `SHAPE_TABLE`, an object the code uses (§4, compile-time checks). |
| cppcheck `variableScope` (JPL rule 13) | `running` in `main`, `lines_cleared_now`/`row` in `line_checker`, after the check handler wrapped their uses in guards | declared inside the guarded blocks |

After these, the engine (`tetris.c`, `prng.c`), the handler and `main.c` have **no MISRA findings** in any configuration. What remains:

- **21.6 and 21.10** in the two terminal ports: deviations D1 and D2.
- **`unusedFunction: cell_filled`** in the rtos configuration only: the stub renderer draws nothing yet. It clears when `io_rtos.c`'s `io_render` TODO is implemented.

### Cobra's MISRA set: every match rewritten away or pinned to one line

Power of 10 rule 10's rationale: "If the compiler or analyzer gets confused, the code causing the confusion should be rewritten." So each of Cobra's matches was either rewritten away or, where no rewrite exists, allowed on one exact line (`tools/check.sh` fails on any match elsewhere).

| Cobra group | What it really checks | Outcome |
|---|---|---|
| `M12_R12_1` | MISRA **1997** rule 34 reused under a 2012 label (Cobra's own source: "not a very good match of the rule"): flags a bare identifier between two operators, i.e. chains like `a && b && c` | **Rewritten:** chains paired, `(shape_ok && rotation_ok) && (row_ok && col_ok)`. Gone. |
| `M12_R14_4` | flags an `if` whose condition is a bare variable, even a `bool` (it can't see types) | **Rewritten:** `if (self_test_passed)` became `if (engine_self_test())`. Gone. |
| `M12_R15_7`, and `P10_9` in the Power of 10 set | `else { if ... }` read as an else-if chain; `typedef struct { const char *...; }` read as a typedef hiding a pointer | **Rewritten:** branches swapped; the struct got a tag with the typedef on its own line. Gone. |
| `M12_R4_6` (Dir 4.6, Adv) | marks every `char`, `int`, `short`, `long`, `float`, `double` keyword, with none of the directive's own exceptions | **Rewritten:** plain `char` is `char_t` (`char_t.h`, the name MISRA's own examples use; JPL rule 17 asks the same), library return values are held in `int32_t`. From 50 matches to 2, both exceptions Dir 4.6 grants: `int main(void)` (C requires it) and `typedef char char_t;` (a typedef naming a basic type). **Allowed on those two lines.** |
| `M12_R20_7` | MISRA 1997 rule 96 reused: macro parameters in parentheses | **Cobra's pattern is broken.** It flags the textbook-compliant `#define SQ(x) ((x) * (x))` and `#define ADD(a, b) ((a) + (b))`, and passes the textbook violation `#define BAD(x) x * x` (tested 2026-11-15). Every spelling of `TETRIS_ASSERT` was flagged, including one with a single, parenthesized use of its parameter. No correct function-like macro satisfies it, so no rewrite exists short of dropping the macro, which JPL D-60411's own `c_assert` pattern recommends. **Allowed on `TETRIS_ASSERT`'s definition line only.** |

### Deviation records

MISRA Compliance:2020 allows a Required or Advisory rule to be broken with a recorded justification (Mandatory rules can't be).

| ID | Rule | Scope | Justification | Controls |
|---|---|---|---|---|
| D1 | 21.6 (Req): standard library I/O | `io_unix.c`, `io_windows.c` | These files *are* the terminal ports, and the C standard I/O library is the portable way to talk to a terminal. Keeping stdio out of everything else is the point of the port layer: the engine, the handler and `main.c` don't include it, and the rtos configuration has no 21.6 finding. | Every format string is a literal, checked by `-Wformat=2 -Wformat-signedness`. No `scanf`: input is read with `getchar()` in a loop bounded at 256 characters. |
| D2 | 21.10 (Req): `<time.h>` | `io_unix.c`, `io_windows.c` | `clock()` drives the PC frame delay (the busy-wait REFACTOR-GOALS.md specifies), and `time()` is the seed fallback on Unix and the seed on Windows. The rtos port uses neither. | The delay is done in unsigned 64-bit arithmetic, bounded by a guard counter, and a check fires if the guard ever runs out. On POSIX, `clock()` measures CPU time; that equals wall time only because the loop spins. `nanosleep()` would end that dependency and stop burning a core. |
| D3 | Dir 4.7 (Req): test returned error information | the terminal ports' output calls (`fputs` through `put()`, the two `printf`s, `fflush`, the safe-mode `fprintf`) and `fclose` of `/dev/urandom` | A failed terminal write, or a failed close of a read-only stream, has no better response than carrying on. Holzmann's Power of 10 rule 7 rationale names exactly these: "if the response to an error would be no different than the response to success, there is little point in explicitly checking a return value. This is often the case with calls to printf and close"; JPL D-60411 rule 14 allows "explicitly cast to (void) if irrelevant". Every result is discarded with an explicit `(void)`. | Every call whose failure *does* change what should happen is tested: `tcgetattr`, both `tcsetattr`s (a failed restore leaves the terminal without echo: safe mode), both `fcntl`s, `fopen`, `fread`, `getchar`, `clock`. |
| D4 | Dir 4.9 (Adv): function-like macros | `TETRIS_ASSERT` in `assert_handler.h` | Only a macro can record where a check sits (`__FILE__`, `__LINE__`). Holzmann's `c_assert` in JPL D-60411 rule 16 is the same kind of macro. | One function-like macro. Its parameter is evaluated exactly once, it uses no `#` or `##`, and it's used only with side-effect-free conditions (values are worked out before they're checked). |

### Directives, reviewed by hand

| Dir | | Status |
|---|---|---|
| 1.1 (Req) | implementation-defined behaviour documented | Plain `char` signedness (unsigned on ARM, signed on x86): never relied on, characters are compared only with character constants. Enum size: range checks cast to `uint32_t` and never depend on the width. Right shifts happen only on unsigned values. Narrowing casts to `int8_t` happen only where a check has already bounded the value. `clock_t` width: handled in `uint64_t`. |
| 2.1 (Req) | compiles without errors | Yes, both compilers, strictest flags, every configuration. |
| 3.1 (Req) | traceable to requirements | No formal requirements baseline; REFACTOR-GOALS.md and the README are the specification. |
| 4.1 (Req) | run-time failures minimised | Checks on every pointer, index and invariant, each with a recovery action; CRC integrity checks; a compile-time check; a power-on self-test; ASan/UBSan runs (section 5). |
| 4.2, 4.3 | assembly | None used. |
| 4.4 (Adv) | no commented-out code | None. The TODOs in `io_rtos.c` name the API a real port will call (`vTaskDelay`, GPIO, UART); they describe code still to be written, they don't disable code. |
| 4.5 (Adv) | typographically unambiguous identifiers | No look-alike names (`l`/`1`, `O`/`0`) in any scope. |
| 4.6 (Adv) | sized typedefs | Text is `char_t`, integers are `<stdint.h>` types. The only basic types named are `int main(void)` and `typedef char char_t;`, both exceptions the directive grants. |
| 4.7 (Req) | test error information | All tested, except deviation D3. |
| 4.8 (Adv) | hide structs a file never dereferences | Applies only to the rtos stub, which receives a `const tetris_engine_t *` it doesn't use yet. `io.h` needs the full type for the terminal ports. |
| 4.9 (Adv) | function-like macros | One, `TETRIS_ASSERT`: deviation D4. |
| 4.10 (Req) | include guards | All five headers. |
| 4.11 (Req) | valid arguments to library functions | Literal formats checked by `-Wformat=2`. `fread` is passed an exact size and count. |
| 4.12 (Req) | no dynamic memory | None. Verified by symbol scan (section 4, rule 3). |
| 4.13 (Adv) | resource operations in order | `fopen` → `fread` → `fclose`. Every terminal-mode change is restored on every path, and each restore is checked. |
| 4.14 (Req) | validate external input | Keys pass through a `switch` with a `default`. Menu lines are bounded at 256 characters; end of input and read errors share one handled path (the menu quits). Any 32-bit seed is valid (0 is replaced). |

---

## 4. JPL Power of 10

| # | Rule | Evidence | Result |
|---|---|---|---|
| 1 | Simple control flow: no `goto`, `setjmp`/`longjmp`, recursion | Cobra P10_1: none. `tools/callgraph.py`: the `-O0` call graph (gcc `-fcallgraph-info`, all 46 functions of the unix build, merged across files) has no cycles. It checks at `-O0` because at `-O2` gcc can merge one function into another and turn the call into a jump, erasing a recursive cycle from the compiled graph: planted mutual recursion was invisible in the `-O2` graph and caught in the `-O0` one. The handler can't create a cycle: nothing it calls contains a check. | **Met** |
| 2 | Fixed upper bound on every loop | Cobra P10_2: none. Every loop is a counted `for` with a constant bound: the game loop (`MAX_GAME_TICKS`, 10 h at 60 FPS), the delay (guard of 10⁸), `line_checker` (40 passes, worst case proved in its comment), the line reader (256 characters). The headless rtos build demonstrates termination: no input, and it exits on the bound after 2,160,000 ticks. | **Met** |
| 3 | No dynamic allocation after initialization | Cobra P10_3: none. `tools/callgraph.py`: no object file imports `malloc`, `calloc`, `realloc`, `free` or `alloca`, and every stack frame is a fixed size (no VLA, no `alloca`). | **Met** |
| 4 | At most 60 lines per function | Cobra checks a 75-line limit: none over. Counted at 60 by `tools/p10_metrics.py`: longest is `shape_cell` at 57 (43 of them its shape table). | **Met** |
| 5 | Assertion density, side-effect-free Boolean checks, an explicit recovery action, no provably-true checks | **Recovery: met.** Every check is a `TETRIS_ASSERT` that never aborts: guards take a safe default locally, and any failure latches a fault that stops the game at the end of its tick in safe mode, reports where the first failed check sits, and exits 1. This is the pattern JPL D-60411 recommends (`if (!c_assert(p >= 0) == true) { return ERROR; }`). **Side-effect free and Boolean: met.** Values are worked out before they're checked. **No provably-true checks: met.** **Density, by Holzmann's own checker:** Cobra's `P10_5` formula (fail below one assertion per 50 statements) passes: unix 69 assertions for 430 statements, rtos 59 for 346 (`tools/p10_rule5.cobra`, the stock formula counting `TETRIS_ASSERT`, because the stock rule counts only tokens spelled `assert`); the same formula fails v1 (0 for 207). **Density, by JPL D-60411 rule 16:** its 10-line clause is a *should* rule, which the standard defines as allowing deviations "provided that an adequate justification is given for each deviation". Five, below. **The paper's own sentence**, "should average to minimally two assertions per function", counted by `tools/p10_metrics.py`: 69 in 45 functions = 1.53 program-wide, 57 in 29 = 1.97 in the engine and `main.c`. | **Met** by its author's checker and JPL rule 16 |
| 6 | Data at the smallest scope | No global variables apart from the handler's fault latch, which is `static` to `assert_handler.c`; other file-scope objects are `static const`. Cobra `scope_check`: 0 once its `#include` artifact is removed (it counts header names such as `time` in `#include <time.h>` as globals; confirmed with a 2-line test file). cppcheck `variableScope`: none (it caught two the handler introduced; both fixed). | **Met** |
| 7 | Check return values and parameters | Cobra P10_7: none. cppcheck MISRA 17.7: none. Every pointer parameter is checked. Every return value whose failure changes what should happen is checked; output calls and `fclose` are explicitly discarded (D3), as the paper's rationale allows: "explicitly casting the function return value to (void) can be acceptable". | **Met** |
| 8 | Preprocessor: includes and simple macros only | No `#if`/`#ifdef` in any `.c` file; the only conditionals are header include guards. No `#define` in any `.c` file: Cobra's rule is "defines only in `.h` files", and file-local constants are block-scoped `static const` objects. One function-like macro, `TETRIS_ASSERT` (D4), the same shape as Holzmann's `c_assert`, with no `#` or `##`. Cobra P10_8a: none. Cobra's P10_8b pattern doesn't parse in 5.4 (`expr: syntax error`), so `tools/check.sh` does the same check with grep. | **Met** |
| 9 | One level of dereferencing, no function pointers | Cobra P10_9: none. No function pointers; port selection happens at link time. | **Met** |
| 10 | All warnings on, zero warnings, static analysis | Sections 1 and 2: zero warnings under both strictest builds, zero analyzer findings. Code that confused Cobra was rewritten (§3), except where its pattern is provably broken. | **Met** |

### JPL rule 16: the five justified deviations

"All functions of more than 10 lines *should* have at least one assertion." These five don't, each for a reason (also listed in `tools/p10_metrics.py`, whose `--check` fails on any unlisted one):

| Function | Lines | Justification |
|---|---|---|
| `crc8_update` | 12 | Every input byte is valid; `engine_self_test()` checks its output against the published CRC-8 check value. |
| `io_get_seed` (unix) | 17 | Any value is a valid seed (0 is replaced by `prng_init()`); a missing `/dev/urandom` already falls back to the clock. |
| `read_line_first_char` (unix, windows) | 28 | Every character is valid input, the loop is bounded, and end of input and read errors share one handled path. |
| `io_display_fault` (unix, windows) | 12 | It *is* the fault report: a failure to report can't be reported. |
| `main` | 35 | It only sequences self-test, game loop and safe mode; every decision it acts on comes from a function that checks. |

### Compile-time checks (JPL rule 16's "static assertions")

`SHAPE_COUNT_CHECKED` in `tetris.h` is `SHAPE_COUNT` while there are exactly 7 shapes and `TANGO`, the last in `shape_type_t`, is the 7th; otherwise it's -1. It sizes `SHAPE_TABLE`, so a false condition is an array of negative size, which no C compiler accepts: the build stops with or without `-Werror`, at no run-time cost. `tools/check.sh` plants both failures (8 shapes; a shape inserted before `TANGO`) and requires a plain `gcc -std=c99` build to stop. It doesn't catch a shape *appended* after `TANGO`, which can't do harm (the bag only deals the first `SHAPE_COUNT` shapes); catching that would need a count entry in `shape_type_t`.

### Worst-case stack (the reason rule 1 exists)

An acyclic call graph means the deepest stack can be calculated. `tools/callgraph.py` adds up gcc's per-function frame sizes along the deepest path from `main` (this repo's code only; libc's own frames are not included):

| Config | `-O2` strict build | `-O0` | Path at `-O2` |
|---|---|---|---|
| unix | 560 bytes | 624 bytes | `main` (352, holds the 260-byte engine) → `state_machine` (96) → `physics_get` (112) → `assert_record` |
| rtos | 560 bytes | 624 bytes | the same path |

v1's was 464 bytes (`main` → `get_raw_keypress`). As a FreeRTOS task, 560 bytes is the floor, before interrupt margin.

---

## 5. Behaviour

All of these run in `tools/check.sh`, section 7.

| Test | Result |
|---|---|
| v1 (rebuilt from commit `033d184`) vs this engine: same seed, same 500,000 scripted ticks (313 games) plus 1/2/3/4-line set-ups, one trace line per tick | identical, 500,004 lines; scoring 100/300/500/800 |
| Same run under AddressSanitizer + UndefinedBehaviorSanitizer | clean, identical trace |
| Corrupting a grid cell or the score behind the engine's back | caught by `verify_engine_integrity()`, each at its own checksum check (`tetris.c:642`, `:643`) |
| Planted: a 5-cell tetromino in `SHAPE_TABLE` | the power-on self-test stops the start: `check failed at tetris.c:681` (its shape-cell check), exit 1, no menu drawn |
| Planted: `physics_set` writing 2 instead of 1 (a legitimate code path, so the CRC alone wouldn't catch it) | the cell-value check stops the game at the first lock, exit 1, and the bad state is never drawn |
| Headless rtos build | runs out 2,160,000 ticks, exits 0 |
| Pseudo-terminal: two games, `y`, `n` | all pass; exit 0; terminal restored |
| Piped input: about, bare Enter, invalid option, Exit; stdin closed at start | all pass, exit 0 (v1 hangs on closed stdin) |
| The checkers themselves, against planted faults: a 69-line function, mutual recursion, an implicit narrowing, v1 against the rule 5 formula, 8 shapes, an inserted shape, a `goto` for the MISRA addon | each one caught |

## 6. Known gaps

- **Rule 5, the paper's literal average:** 1.53 assertions per function program-wide (1.97 in the engine and `main.c`). Rule 5 is recorded as met on the author's own measure (Cobra's formula) and on JPL rule 16; raising the per-function average to 2 would take checks in functions that have nothing to check, which rule 5's own text forbids.
- **`io_windows.c` has not been built with real MinGW.**
- **The rtos port is a stub**: no timer, buttons, display or fault log yet. Its `io_display_fault` TODO is where the spec's "logs to UART/flash" lands, and its `io_show` TODO is where the screens get drawn.
- **Tool limits:** cppcheck covers 130 rules, no directives and not 21.24; Cobra's stock rule 5 counts only `assert`; its rule 8b pattern doesn't parse; `scope_check` miscounts `#include` names; its MISRA set reuses 1997 patterns, one of them (20.7) inverted. Each is worked around above.

## 7. Rerunning the checks

```bash
tools/check.sh            # everything: about 5 minutes on the Pi
tools/check.sh --quick    # skips the ~40 s game played in a real terminal
```

It exits 0 only when every check passes. Reviewed findings (D1, D2 in cppcheck; the rtos `unusedFunction`; Cobra's three pinned MISRA lines; its P10_5 blind spot; the five JPL deviations) are allowed by rule and file or exact line; anything new fails. cppcheck and Cobra are optional: when one is missing, its checks show as SKIP, not PASS. Cobra is looked for in `$COBRA_HOME` (default `~/.local/src/Cobra`, built with `make all` in `src/` and `src_app/`).

The individual tools:

```bash
python3 tools/p10_metrics.py [-v] [--check]      # rules 4 and 5, per function
python3 tools/callgraph.py [--check] [unix rtos]  # rules 1 and 3, worst-case stack
python3 tools/pty_game_test.py ./spacetetris      # the real game in a pseudo-terminal
C_BASE=~/.local/src/Cobra/rules ~/.local/src/Cobra/src/cobra \
    -f tools/p10_rule5.cobra tetris.c prng.c assert_handler.c io_unix.c main.c   # rule 5, Cobra's formula
```

## 8. Sources

- G. J. Holzmann, "The Power of 10: Rules for Developing Safety-Critical Code", *IEEE Computer*, June 2006, pp. 93-95. Rule 5, rule 7's rationale on `(void)` casts and rule 10's rationale on rewriting code that confuses an analyzer are quoted from it. Read from a copy at [web.eecs.umich.edu/~imarkov/10rules.pdf](http://web.eecs.umich.edu/~imarkov/10rules.pdf).
- *JPL Institutional Coding Standard for the C Programming Language*, JPL DOCID D-60411, version 1.0, 2009-03-03 (the version cleared for external distribution, CL#09-0763). Rules 14 and 16 and the *shall* / *should* definitions (p. 6) are quoted from it. Read from a mirror at [yurichev.com/mirrors/C/JPL_Coding_Standard_C.pdf](https://yurichev.com/mirrors/C/JPL_Coding_Standard_C.pdf); the document itself says the current version is the one in JPL's LaRS DocuShare library.
- Cobra, `rules/main/p10.def` (`def P10_5`), `rules/misra/misra2012.def` and `misra1997.def`, [github.com/nimble-code/Cobra](https://github.com/nimble-code/Cobra) @ `e307aed`.
