"""Power of 10 rules 4 and 5, measured per function for each build config.

  rule 4: no function longer than 60 lines (signature through closing brace)
  rule 5: assertion density, averaged over every function in the config;
          plus the JPL C standard's restatement, "every function longer than
          10 lines has at least one assertion"

usage: python3 tools/p10_metrics.py [-v] [--check]
  -v       list every function
  --check  exit 1 on a rule 4 violation, or on a function over 10 lines with
           no assertion that isn't one of the reviewed exceptions in
           JPL_EXCEPTIONS. (the per-function average is reported, not gated:
           the paper says it "should" average two, Cobra measures rule 5 by
           statements, see tools/p10_rule5.cobra)

the parser leans on this repo's formatting: a definition starts at column 0,
its body opens with '{' at the end of the signature, and closes with '}'
alone at column 0 (or the whole function sits on one line).
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CONFIGS = {
    "unix": ["tetris.c", "prng.c", "assert_handler.c", "io_unix.c", "main.c"],
    "windows": ["tetris.c", "prng.c", "assert_handler.c", "io_windows.c", "main.c"],
    "rtos": ["tetris.c", "prng.c", "assert_handler.c", "io_rtos.c", "main.c"],
}
ENGINE = ("tetris.c", "prng.c", "main.c")  # everything except the port and handler
MAX_LINES = 60
JPL_LINES = 10  # functions longer than this need at least one assertion
# reviewed deviations from JPL D-60411 rule 16's "should" clause (functions
# over 10 lines have an assertion), each with its justification (COMPLIANCE.md)
JPL_EXCEPTIONS = {
    "crc8_update": "every input byte is valid; engine_self_test() checks the result",
    "io_get_seed": "any value is a valid seed (0 is replaced); a missing /dev/urandom falls back to the clock",
    "read_line_first_char": "every character is valid input, the loop is bounded, EOF and read errors share one handled path",
    "io_display_fault": "the fault report itself: a failure to report can't be reported",
    "main": "only sequences self-test, loop and safe mode; each decision comes from a checked function",
}
SIG = re.compile(r"^(static\s+)?[A-Za-z_][\w\s\*]*?\b(\w+)\s*\(")
# TETRIS_ASSERT is the project's check; plain assert() counts too (v1, other code)
ASSERT = re.compile(r"\b(?:TETRIS_ASSERT|assert)\s*\(")


def functions(path):
    """(name, first line, length in lines, assertion count) per definition"""
    lines = path.read_text().split("\n")
    found, i = [], 0
    while i < len(lines):
        m = SIG.match(lines[i])
        if m and not lines[i].startswith(("typedef", "#", "//")):
            j = i  # the signature may wrap: find the line that ends it
            while j < len(lines) and not re.search(r"([;{]|\})\s*(//.*)?$", lines[j]):
                j += 1
            if j == len(lines):
                break
            if re.search(r"\{.*\}\s*(//.*)?$", lines[j]):  # one-line function
                found.append((m.group(2), i + 1, j - i + 1, len(ASSERT.findall(lines[j]))))
                i = j
            elif re.search(r"\{\s*(//.*)?$", lines[j]):  # body follows
                k = j
                while k < len(lines) and lines[k] != "}":
                    k += 1
                body = "\n".join(lines[j:k + 1])
                found.append((m.group(2), i + 1, k - i + 1, len(ASSERT.findall(body))))
                i = k
            # else: a prototype (ends in ';'), skip it
        i += 1
    return found


def main():
    verbose = "-v" in sys.argv[1:]
    check = "--check" in sys.argv[1:]
    problems = []
    for name, files in CONFIGS.items():
        fns = [(f, *fn) for f in files for fn in functions(REPO / f)]
        total = sum(fn[4] for fn in fns)
        longest = max(fns, key=lambda fn: fn[3])
        over = [f"{f}:{n} ({length})" for f, n, _, length, _ in fns if length > MAX_LINES]
        bare = [n for _, n, _, _, a in fns if a == 0]
        jpl = [f"{n} ({length})" for _, n, _, length, a in fns if a == 0 and length > JPL_LINES]
        print(f"== {name}: {len(fns)} functions, {total} assertions, "
              f"average {total / len(fns):.2f} per function")
        print(f"   rule 4: longest {longest[1]}() at {longest[3]} lines; "
              f"over {MAX_LINES}: {', '.join(over) or 'none'}")
        print(f"   rule 5: {len(bare)} functions with no assertion: {', '.join(bare) or 'none'}")
        print(f"   rule 5, JPL reading: over {JPL_LINES} lines with no assertion: {', '.join(jpl) or 'none'}")
        engine = [fn for fn in fns if fn[0] in ENGINE]
        e_total = sum(fn[4] for fn in engine)
        print(f"   engine + main.c only: {len(engine)} functions, {e_total} assertions, "
              f"average {e_total / len(engine):.2f}")
        problems += [f"{name}: over {MAX_LINES} lines: {o}" for o in over]
        problems += [f"{name}: unreviewed function over {JPL_LINES} lines with no assertion: {j}"
                     for j in jpl if j.split(" ")[0] not in JPL_EXCEPTIONS]
        if verbose:
            for f, n, line, length, a in fns:
                print(f"     {f}:{line:<4} {n:<26} {length:>3} lines  {a} assert")
    if check:
        for p in problems:
            print(f"   PROBLEM {p}")
        sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
