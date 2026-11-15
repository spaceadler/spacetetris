#!/usr/bin/env bash
# tools/check.sh: the spacetetris compliance and build test.
#
# compiles every configuration with each compiler's strictest flags, runs
# both static analyzers, the MISRA C:2012 and Power of 10 checkers, the
# project's own rule tools, and behaviour tests: the old (v1) and current
# engine compared tick by tick, sanitizers, planted faults, and the real game
# played in a terminal. it also plants faults to prove the checkers can fail.
#
# exits 0 only when everything passed. findings that were reviewed and
# documented in COMPLIANCE.md are allowed by name (rule + file) below;
# anything else fails. a missing optional tool is a SKIP, never a pass.
#
# usage: tools/check.sh [--quick]    --quick skips the ~40 s terminal game
# needs: gcc or clang, python3, git. optional: clang, cppcheck (with its
# misra addon), Cobra (COBRA_HOME, default ~/.local/src/Cobra).

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PY="${PYTHON:-python3}"
COBRA_HOME="${COBRA_HOME:-$HOME/.local/src/Cobra}"
V1_COMMIT=033d184 # the original code, before the v2 refactor
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

PASS=0
FAIL=0
SKIP=0
pass() { printf '  PASS  %s\n' "$1"; PASS=$((PASS + 1)); }
fail() {
  printf '  FAIL  %s\n' "$1"
  if [ -n "${2:-}" ] && [ -s "$2" ]; then head -20 "$2" | sed 's/^/          /'; fi
  FAIL=$((FAIL + 1))
}
skip() { printf '  SKIP  %s\n' "$1"; SKIP=$((SKIP + 1)); }
section() { printf '\n== %s\n' "$1"; }
have() { command -v "$1" > /dev/null 2>&1; }
cfg_files() { echo "tetris.c prng.c assert_handler.c io_$1.c main.c"; }

# replace the first occurrence of $2 with $3 in file $1 (for planted faults)
replace_once() {
  "$PY" -I - "$1" "$2" "$3" << 'PYEOF'
import sys
path, old, new = sys.argv[1:4]
s = open(path).read()
if old not in s:
    sys.exit("planted-fault pattern not found in " + path)
open(path, "w").write(s.replace(old, new, 1))
PYEOF
}

# a scratch copy of the sources, for planting faults in
scratch_copy() {
  rm -rf "$1" && mkdir -p "$1/tools" &&
    cp "$REPO"/*.c "$REPO"/*.h "$REPO"/*.flags "$1/" && cp "$REPO"/tools/*.py "$1/tools/"
}

cd "$REPO" || exit 1
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) HOST=windows ;; *) HOST=unix ;; esac
CONFIGS="$HOST rtos"

# ---------------------------------------------------------------------------
section "1. compile: strictest flags, zero warnings (JPL rule 10)"
COMPILERS=""
for cc in gcc clang; do
  if have "$cc"; then COMPILERS="$COMPILERS $cc"; else skip "$cc not installed"; fi
done
for cc in $COMPILERS; do
  for cfg in $CONFIGS; do
    for opt in -O2 -O0; do
      log="$TMP/build.$cc.$cfg$opt.txt"
      # shellcheck disable=SC2046 # word splitting of the file list is intended
      if "$cc" "@$cc-strict.flags" "$opt" $(cfg_files "$cfg") -o "$TMP/st_${cfg}_$cc$opt" > "$log" 2>&1 &&
        [ ! -s "$log" ]; then
        pass "$cc $cfg $opt"
      else
        fail "$cc $cfg $opt" "$log"
      fi
    done
  done
  if [ "$HOST" = unix ]; then
    mkdir -p "$TMP/winstub" && printf 'int _kbhit(void);\nint _getch(void);\n' > "$TMP/winstub/conio.h"
    log="$TMP/build.$cc.windows.txt"
    if "$cc" "@$cc-strict.flags" -fsyntax-only -isystem "$TMP/winstub" io_windows.c > "$log" 2>&1 &&
      [ ! -s "$log" ]; then
      pass "$cc windows port (syntax only: stand-in conio.h, no MinGW here)"
    else
      fail "$cc windows port" "$log"
    fi
  fi
done
CC1="${COMPILERS# }"
CC1="${CC1%% *}"
BIN="$TMP/st_${HOST}_$CC1-O2"
BIN_RTOS="$TMP/st_rtos_$CC1-O2"

# ---------------------------------------------------------------------------
section "2. static analyzers (JPL rule 10)"
if have gcc; then
  log="$TMP/fanalyzer.txt"
  : > "$log"
  for f in tetris.c prng.c assert_handler.c "io_$HOST.c" io_rtos.c main.c; do
    gcc @gcc-strict.flags -fanalyzer -Wanalyzer-too-complex \
      --param=analyzer-max-enodes-per-program-point=200 \
      --param=analyzer-bb-explosion-factor=200 -c "$f" -o /dev/null >> "$log" 2>&1
  done
  if [ ! -s "$log" ]; then pass "gcc -fanalyzer: complete analysis, no findings"; else fail "gcc -fanalyzer" "$log"; fi
else
  skip "gcc -fanalyzer (no gcc)"
fi
if have clang; then
  log="$TMP/clang_analyze.txt"
  : > "$log"
  for f in tetris.c prng.c assert_handler.c "io_$HOST.c" io_rtos.c main.c; do
    clang --analyze -std=c99 -Xclang -analyzer-checker=core,unix,deadcode,security,nullability,optin.portability,alpha.core,alpha.security,alpha.unix,valist \
      -o /dev/null "$f" >> "$log" 2>&1
  done
  if ! grep -q warning "$log"; then pass "clang --analyze (incl. alpha checkers): no findings"; else fail "clang --analyze" "$log"; fi
else
  skip "clang --analyze (no clang)"
fi

# ---------------------------------------------------------------------------
section "3. MISRA C:2012: cppcheck + misra addon"
if have cppcheck; then
  for cfg in $CONFIGS $([ "$HOST" = unix ] && echo windows); do
    raw="$TMP/cppcheck.$cfg.txt"
    # shellcheck disable=SC2046
    cppcheck --addon=misra --enable=all --inconclusive --std=c99 --platform=unix64 \
      --suppress=missingIncludeSystem -q --template='{file}:{line}:{id}' \
      $(cfg_files "$cfg") 2> "$raw"
    # the reviewed findings (COMPLIANCE.md), by file and rule:
    #   D1  misra-c2012-21.6   <stdio.h> in the two terminal ports
    #   D2  misra-c2012-21.10  <time.h> in the two terminal ports
    #   rtos only: unusedFunction cell_filled (the stub renderer draws nothing yet)
    # (section 6 proves the addon really runs: it has to flag a planted goto)
    grep -vE '^io_(unix|windows)\.c:[0-9]+:misra-c2012-21\.(6|10)$' "$raw" |
      { if [ "$cfg" = rtos ]; then grep -vE '^tetris\.c:[0-9]+:unusedFunction$'; else cat; fi; } > "$TMP/unexpected.$cfg.txt"
    if [ -s "$TMP/unexpected.$cfg.txt" ]; then
      fail "cppcheck MISRA $cfg: findings beyond the reviewed deviations" "$TMP/unexpected.$cfg.txt"
    else
      pass "cppcheck MISRA $cfg: only the reviewed deviations"
    fi
  done
else
  skip "cppcheck not installed (apt install cppcheck)"
fi

# ---------------------------------------------------------------------------
section "4. Power of 10 and MISRA: Cobra (Holzmann's checker)"
COBRA="$COBRA_HOME/src/cobra"
if [ -x "$COBRA" ]; then
  export C_BASE="$COBRA_HOME/rules"
  export PATH="$COBRA_HOME/src_app:$PATH"
  for cfg in $CONFIGS; do
    paths=""
    for f in $(cfg_files "$cfg") tetris.h prng.h io.h assert_handler.h; do paths="$paths $REPO/$f"; done
    # cobra writes its detail files into the current directory: run it in $TMP
    # shellcheck disable=SC2086
    (cd "$TMP" && "$COBRA" -f main/p10 $paths) > "$TMP/p10.$cfg.txt" 2>&1
    # reviewed: P10_5 (the stock rule only counts tokens spelled `assert`,
    # so TETRIS_ASSERT is invisible to it; its formula runs just below on
    # TETRIS_ASSERT) and P10_pp (an intermediate set, macro definitions)
    grep -oE '^(<[0-9]+>)?[[:space:]]*[0-9]+: [A-Za-z0-9_]+' "$TMP/p10.$cfg.txt" | sed -E 's/.*: //' |
      grep -vxE 'P10_5|P10_pp' > "$TMP/p10.unexpected.$cfg.txt"
    if ! grep -q 'P10_pp' "$TMP/p10.$cfg.txt"; then
      fail "cobra p10 $cfg: no results (did cobra run?)" "$TMP/p10.$cfg.txt"
    elif [ -s "$TMP/p10.unexpected.$cfg.txt" ]; then
      fail "cobra p10 $cfg: unreviewed findings" "$TMP/p10.unexpected.$cfg.txt"
    else
      pass "cobra p10 $cfg: no findings beyond the reviewed P10_5 blind spot"
    fi
    # rule 5 by the stock P10_5 formula, counting TETRIS_ASSERT, on the .c files
    # shellcheck disable=SC2046
    (cd "$REPO" && "$COBRA" -f tools/p10_rule5.cobra $(cfg_files "$cfg")) > "$TMP/rule5.$cfg.txt" 2>&1
    if grep -q '^rule 5: PASS' "$TMP/rule5.$cfg.txt"; then
      pass "cobra rule 5 $cfg (P10_5 formula on TETRIS_ASSERT): $(grep -oE '[0-9]+ assertions for [0-9]+ statements' "$TMP/rule5.$cfg.txt")"
    else
      fail "cobra rule 5 $cfg" "$TMP/rule5.$cfg.txt"
    fi
    # shellcheck disable=SC2086
    (cd "$TMP" && "$COBRA" -f main/misra2012 $paths) > "$TMP/cmisra.$cfg.txt" 2>&1
    # nothing is allowlisted by group, only by exact line (COMPLIANCE.md):
    #   M12_R4_6 (Dir 4.6) marks every basic-type keyword with no exceptions;
    #     the two left are the ones Dir 4.6 itself grants: `int main(void) {`
    #     (C requires it) and `typedef char char_t;` (the one place char is
    #     named)
    #   M12_R20_7 is broken in Cobra 5.4: it flags the textbook-compliant
    #     #define SQ(x) ((x) * (x)) and passes the violation #define BAD(x)
    #     x * x. allowed on TETRIS_ASSERT's definition, the one macro with a
    #     parameter; a match anywhere else still fails
    grep -oE '^(<[0-9]+>)?[[:space:]]*[0-9]+: [A-Za-z0-9_]+' "$TMP/cmisra.$cfg.txt" | sed -E 's/.*: //' |
      grep -vxE 'M12_R4_6|M12_R20_7' > "$TMP/cmisra.unexpected.$cfg.txt"
    awk '/^=== /{sec = ($0 ~ /typedefs that indicate size/) ? "R4_6" : (($0 ~ /expansion of macro parameters/) ? "R20_7" : "")}
         sec != "" && /^>? *[0-9]+  /{code = $0; sub(/^>? *[0-9]+  +/, "", code); print sec "|" code}' \
      "$TMP/_Misra2012_.txt" 2> /dev/null |
      grep -vxF -e 'R4_6|int main(void) {' -e 'R4_6|typedef char char_t;' \
        -e 'R20_7|#define TETRIS_ASSERT(cond) ((cond) ? true : assert_fail(__FILE__, __LINE__))' \
        >> "$TMP/cmisra.unexpected.$cfg.txt"
    if ! grep -q 'int main(void) {' "$TMP/_Misra2012_.txt" 2> /dev/null; then
      fail "cobra misra2012 $cfg: no detail file (did cobra run?)" "$TMP/cmisra.$cfg.txt"
    elif [ -s "$TMP/cmisra.unexpected.$cfg.txt" ]; then
      fail "cobra misra2012 $cfg: findings beyond the three reviewed lines" "$TMP/cmisra.unexpected.$cfg.txt"
    else
      pass "cobra misra2012 $cfg: only the three reviewed lines (int main, char_t, TETRIS_ASSERT)"
    fi
  done
  # rule 6 via scope_check, on copies without #include lines: it counts
  # header names like `time` in <time.h> as globals (see COMPLIANCE.md)
  mkdir -p "$TMP/noinc"
  for f in tetris.c prng.c assert_handler.c io_unix.c io_windows.c io_rtos.c main.c; do
    grep -vE '^[[:space:]]*#[[:space:]]*include' "$f" > "$TMP/noinc/$f"
  done
  for cfg in $CONFIGS; do
    # shellcheck disable=SC2046
    counts=$(cd "$TMP/noinc" && scope_check $(cfg_files "$cfg") 2>&1 | grep -oE 'only: *[0-9]+' | grep -oE '[0-9]+' | tr '\n' ' ')
    if [ "$counts" = "0 0 " ]; then pass "cobra scope_check $cfg (rule 6): no data wider than needed"; else fail "cobra scope_check $cfg: counts $counts"; fi
  done
else
  skip "Cobra not found at $COBRA_HOME (github.com/nimble-code/Cobra, make all in src/ and src_app/)"
fi

# ---------------------------------------------------------------------------
section "5. project rules: preprocessor, banned names, function size, checks, call graph"
log="$TMP/pp.txt"
grep -nE '^[[:space:]]*#[[:space:]]*(define|if|ifdef|ifndef|elif|else)' ./*.c > "$log"
if [ ! -s "$log" ]; then pass "no #define and no conditional compilation in any .c file (rule 8)"; else fail "preprocessor in .c files" "$log"; fi
log="$TMP/banned.txt"
for f in tetris.c tetris.h prng.c prng.h assert_handler.c assert_handler.h io.h main.c; do
  sed 's://.*$::' "$f" | grep -nE '#include <(stdio|stdlib|time|signal|assert|setjmp)\.h>|\b(rand|srand|malloc|calloc|realloc|free|abort|exit|printf|scanf|assert|setjmp|longjmp)[[:space:]]*\(|\bgoto\b' |
    sed "s|^|$f:|"
done > "$log"
if [ ! -s "$log" ]; then pass "engine, main and handler: no banned headers, calls or goto"; else fail "banned names in engine code" "$log"; fi
if "$PY" -I tools/p10_metrics.py --check > "$TMP/metrics.txt" 2>&1; then
  pass "rules 4 and 5: no function over 60 lines; every unchecked function over 10 lines is a reviewed exception"
else
  fail "tools/p10_metrics.py --check" "$TMP/metrics.txt"
fi
# shellcheck disable=SC2086
if "$PY" -I tools/callgraph.py --check $CONFIGS > "$TMP/callgraph.txt" 2>&1; then
  pass "rules 1 and 3: no recursion, fixed-size frames, no allocators, stack within budget"
else
  fail "tools/callgraph.py --check" "$TMP/callgraph.txt"
fi

# ---------------------------------------------------------------------------
section "6. the checkers catch planted faults (a check that can't fail proves nothing)"
P="$TMP/plant_tools"
scratch_copy "$P"
{
  echo "int8_t planted_long(int8_t x);"
  echo "int8_t planted_long(int8_t x) {"
  for i in $(seq 1 66); do echo "  x = (int8_t)(x + $((i % 3)));"; done
  echo "  return x;"
  echo "}"
  echo "int8_t planted_ping(int8_t n);"
  echo "int8_t planted_pong(int8_t n);"
  echo "int8_t planted_ping(int8_t n) { int8_t r = 0; if (n > 0) { r = planted_pong((int8_t)(n - 1)); } return r; }"
  echo "int8_t planted_pong(int8_t n) { int8_t r = 1; if (n > 0) { r = planted_ping((int8_t)(n - 1)); } return r; }"
} >> "$P/tetris.c"
if ! "$PY" -I "$P/tools/p10_metrics.py" --check > "$TMP/plant_m.txt" 2>&1 && grep -q 'PROBLEM.*planted_long' "$TMP/plant_m.txt"; then
  pass "p10_metrics.py catches a 69-line function"
else
  fail "p10_metrics.py missed the planted 69-line function" "$TMP/plant_m.txt"
fi
if ! "$PY" -I "$P/tools/callgraph.py" --check rtos > "$TMP/plant_c.txt" 2>&1 && grep -q 'PROBLEM.*planted_ping' "$TMP/plant_c.txt"; then
  pass "callgraph.py catches mutual recursion (it hides at -O2; checked at -O0)"
else
  fail "callgraph.py missed the planted recursion" "$TMP/plant_c.txt"
fi
P="$TMP/plant_cc"
scratch_copy "$P"
printf 'int8_t planted_narrow(int16_t wide);\nint8_t planted_narrow(int16_t wide) { int8_t narrow = wide; return narrow; }\n' >> "$P/tetris.c"
# shellcheck disable=SC2046
if ! (cd "$P" && "$CC1" "@$CC1-strict.flags" -c tetris.c -o /dev/null) > "$TMP/plant_cc.txt" 2>&1 &&
  grep -q 'conversion' "$TMP/plant_cc.txt"; then
  pass "the strict build rejects an implicit narrowing (int16_t -> int8_t)"
else
  fail "the strict build accepted a planted narrowing" "$TMP/plant_cc.txt"
fi
if [ -x "$COBRA" ] && have git && git -C "$REPO" cat-file -e "$V1_COMMIT:tetris.c" 2> /dev/null; then
  mkdir -p "$TMP/v1rule5"
  git -C "$REPO" show "$V1_COMMIT:tetris.c" > "$TMP/v1rule5/tetris.c"
  git -C "$REPO" show "$V1_COMMIT:main.c" > "$TMP/v1rule5/main.c"
  (cd "$TMP/v1rule5" && C_BASE="$COBRA_HOME/rules" "$COBRA" -f "$REPO/tools/p10_rule5.cobra" tetris.c main.c) > "$TMP/v1rule5/out.txt" 2>&1
  if grep -q '^rule 5: FAIL' "$TMP/v1rule5/out.txt"; then
    pass "cobra rule 5 fails v1 ($V1_COMMIT): $(grep -oE '[0-9]+ assertions for [0-9]+ statements' "$TMP/v1rule5/out.txt")"
  else
    fail "cobra rule 5 passed v1, which has no assertions" "$TMP/v1rule5/out.txt"
  fi
else
  skip "cobra rule 5 against v1 (needs Cobra, git and commit $V1_COMMIT)"
fi
P="$TMP/plant_static"
scratch_copy "$P"
replace_once "$P/tetris.h" '#define SHAPE_COUNT ((int8_t)7)' '#define SHAPE_COUNT ((int8_t)8)'
# a plain build, no strict flags and no -Werror: the static check has to
# stop it on its own
if ! (cd "$P" && "$CC1" -std=c99 -c tetris.c -o /dev/null) > "$P/out.txt" 2>&1 &&
  grep -q 'negative' "$P/out.txt"; then
  pass "the static check stops a build with 8 shapes (even without -Werror)"
else
  fail "a build with 8 shapes compiled" "$P/out.txt"
fi
scratch_copy "$P"
replace_once "$P/tetris.h" '  ZULU,' '  ZULU,
  PLANTED_EXTRA,'
if ! (cd "$P" && "$CC1" -std=c99 -c tetris.c -o /dev/null) > "$P/out.txt" 2>&1 &&
  grep -q 'negative' "$P/out.txt"; then
  pass "the static check stops a build with a shape inserted before TANGO"
else
  fail "a build with an inserted shape compiled" "$P/out.txt"
fi
if have cppcheck; then
  mkdir -p "$TMP/plant_misra"
  printf 'void planted(void);\nvoid planted(void) {\n  goto done;\ndone:\n  return;\n}\n' > "$TMP/plant_misra/planted.c"
  (cd "$TMP/plant_misra" && cppcheck --addon=misra --std=c99 -q --template='{id}' planted.c) > "$TMP/plant_misra/out.txt" 2>&1
  if grep -qx 'misra-c2012-15.1' "$TMP/plant_misra/out.txt"; then
    pass "the cppcheck misra addon runs: it flags a planted goto (rule 15.1)"
  else
    fail "the cppcheck misra addon missed a planted goto" "$TMP/plant_misra/out.txt"
  fi
fi

# ---------------------------------------------------------------------------
section "7. behaviour"
H="$TMP/harness"
mkdir -p "$H/v1"
if have gcc && have git && git -C "$REPO" cat-file -e "$V1_COMMIT:tetris.c" 2> /dev/null; then
  git -C "$REPO" show "$V1_COMMIT:tetris.c" > "$H/v1/tetris.c"
  git -C "$REPO" show "$V1_COMMIT:tetris.h" > "$H/v1/tetris.h"
  if gcc -O2 -DOLD -I"$H/v1" -I"$REPO" tools/trace_harness.c "$H/v1/tetris.c" prng.c assert_handler.c -o "$H/old" > "$H/old.build" 2>&1 &&
    gcc -O2 -I"$REPO" tools/trace_harness.c tetris.c prng.c assert_handler.c io_rtos.c -o "$H/new" > "$H/new.build" 2>&1; then
    TRACE="$H/old.txt" "$H/old" > /dev/null 2> "$H/old.report"
    TRACE="$H/new.txt" "$H/new" > /dev/null 2> "$H/new.report"
    if [ -s "$H/old.txt" ] && cmp -s "$H/old.txt" "$H/new.txt"; then
      pass "v1 ($V1_COMMIT) and current engine: identical traces, $(wc -l < "$H/new.txt" | tr -d ' ') lines ($(grep -o '[0-9]* games' "$H/new.report"))"
    else
      fail "engine traces differ from v1" "$H/new.report"
    fi
    if gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -I"$REPO" tools/trace_harness.c tetris.c prng.c assert_handler.c io_rtos.c -o "$H/san" > "$H/san.build" 2>&1; then
      if TRACE="$H/san.txt" "$H/san" > /dev/null 2> "$H/san.report" && cmp -s "$H/san.txt" "$H/old.txt"; then
        pass "same run under AddressSanitizer + UBSan: clean, identical trace"
      else
        fail "sanitizer run" "$H/san.report"
      fi
    else
      skip "sanitizers not available"
    fi
    for what in grid score; do
      "$H/new" "$what" > "$H/fault.$what.txt" 2>&1
      rc=$?
      want="$(grep -n "TETRIS_ASSERT(${what}_now == engine->${what}_checksum)" tetris.c | cut -d: -f1)"
      if [ "$rc" -eq 3 ] && grep -q "caught at tetris.c:$want\$" "$H/fault.$what.txt"; then
        pass "corrupted $what caught by its checksum check (tetris.c:$want)"
      else
        fail "corrupted $what went unnoticed (exit $rc)" "$H/fault.$what.txt"
      fi
    done
  else
    fail "trace harness build" "$H/new.build"
  fi
else
  skip "v1 trace comparison (needs gcc, git and commit $V1_COMMIT)"
fi

if [ -x "$BIN_RTOS" ]; then
  t0=$(date +%s)
  "$BIN_RTOS" > /dev/null 2>&1
  rc=$?
  if [ "$rc" -eq 0 ]; then
    pass "headless rtos build runs out its bounded loop (2,160,000 ticks, $(($(date +%s) - t0)) s), exit 0"
  else
    fail "headless rtos build exit $rc"
  fi
fi

if [ -x "$BIN" ]; then
  "$BIN" < /dev/null > "$TMP/closed.out" 2> "$TMP/closed.err"
  rc=$?
  if [ "$rc" -eq 0 ] && grep -q 'Thanks for playing' "$TMP/closed.out" && ! grep -q 'SAFE MODE' "$TMP/closed.err"; then
    pass "stdin closed at start: says goodbye, exit 0"
  else
    fail "stdin closed at start (exit $rc)" "$TMP/closed.err"
  fi
  printf '3\n\n9\n4\n' | "$BIN" > "$TMP/menu.out" 2> "$TMP/menu.err"
  rc=$?
  if [ "$rc" -eq 0 ] && grep -q 'a spaceadler production' "$TMP/menu.out" &&
    grep -q 'Unknown option' "$TMP/menu.out" && grep -q 'Thanks for playing' "$TMP/menu.out" &&
    ! grep -q 'SAFE MODE' "$TMP/menu.err"; then
    pass "menu: about, bare Enter back, invalid option, exit"
  else
    fail "menu run (exit $rc)" "$TMP/menu.err"
  fi
fi

# safe mode, both paths, through the real binaries
P="$TMP/plant_shape"
scratch_copy "$P"
replace_once "$P/tetris.c" '{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},' \
  '{{0, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},'
# shellcheck disable=SC2046
if (cd "$P" && "$CC1" "@$CC1-strict.flags" $(cfg_files "$HOST") -o planted) > "$P/build.txt" 2>&1; then
  "$P/planted" < /dev/null > "$P/out.txt" 2> "$P/err.txt"
  rc=$?
  want="$(grep -n 'TETRIS_ASSERT(cells == 4)' "$P/tetris.c" | cut -d: -f1)"
  if [ "$rc" -eq 1 ] && grep -q "check failed at tetris.c:$want\$" "$P/err.txt" && [ ! -s "$P/out.txt" ]; then
    pass "safe mode at power-on: a 5-cell tetromino stops the start at tetris.c:$want, exit 1"
  else
    fail "self-test plant (exit $rc)" "$P/err.txt"
  fi
else
  fail "self-test plant build" "$P/build.txt"
fi
P="$TMP/plant_cell"
scratch_copy "$P"
replace_once "$P/tetris.c" 'engine->grid[board_y][board_x] = 1;' 'engine->grid[board_y][board_x] = 2;'
# shellcheck disable=SC2046
if (cd "$P" && "$CC1" "@$CC1-strict.flags" $(cfg_files rtos) -o planted) > "$P/build.txt" 2>&1; then
  "$P/planted" > /dev/null 2>&1
  rc=$?
  if [ "$rc" -eq 1 ]; then
    pass "safe mode in play: an illegal cell value stops the game at the first lock, exit 1"
  else
    fail "in-game plant (exit $rc, expected 1)"
  fi
else
  fail "in-game plant build" "$P/build.txt"
fi

if [ "$QUICK" -eq 1 ]; then
  skip "terminal game test (--quick)"
elif [ "$HOST" != unix ] || ! "$PY" -I -c 'import pty' 2> /dev/null; then
  skip "terminal game test (needs a POSIX pty)"
elif [ -x "$BIN" ]; then
  if "$PY" -I tools/pty_game_test.py "$BIN" > "$TMP/pty.txt" 2>&1; then
    pass "real terminal: two games, 'y', 'n', exit 0, terminal restored"
  else
    fail "terminal game test" "$TMP/pty.txt"
  fi
fi

# ---------------------------------------------------------------------------
printf '\n== result: %d passed, %d failed, %d skipped\n' "$PASS" "$FAIL" "$SKIP"
[ "$FAIL" -eq 0 ]
