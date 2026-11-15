"""Power of 10 rules 1 and 3 from the compiled code: builds a config with
gcc -fcallgraph-info, merges the per-file call graphs, and reports

  - recursion: any cycle in the call graph (direct or indirect), checked on
    a -O0 build. rule 1 is about the source, and at -O2 gcc can inline one
    function into another and turn the call into a jump, which erases a
    recursive cycle from the compiled graph (this tool once missed planted
    mutual recursion that way)
  - the worst-case stack from main(), summing frame sizes on the deepest path:
    for the -O2 strict build (what ships) and for -O0
  - frames that aren't a fixed size (alloca, variable-length arrays)
  - allocator symbols (malloc, free, ...) the object files import, via nm

usage: python3 tools/callgraph.py [--check] [unix] [rtos] [windows]   (default: unix rtos)
  --check  exit 1 on recursion, a frame that isn't a fixed size, an allocator
           symbol, or a worst-case stack over STACK_BUDGET bytes

needs gcc 10 or newer. the windows config only builds where conio.h exists.
at -O2 gcc folds small static functions into their callers, so fewer
frames appear than functions exist. libc's own frames (printf, ...) aren't
included: only this repo's code.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CONFIGS = {
    "unix": ["tetris.c", "prng.c", "assert_handler.c", "io_unix.c", "main.c"],
    "windows": ["tetris.c", "prng.c", "assert_handler.c", "io_windows.c", "main.c"],
    "rtos": ["tetris.c", "prng.c", "assert_handler.c", "io_rtos.c", "main.c"],
}
STACK_BUDGET = 1024  # bytes, worst-case path from main(), -O2 build
ALLOCATORS = {"malloc", "calloc", "realloc", "free", "alloca", "aligned_alloc",
              "posix_memalign"}


def build(files, out, opt):
    """compile each file at `opt` with call-graph output into `out`; return the .o paths"""
    out.mkdir()
    objs = []
    for f in files:
        obj = out / (Path(f).stem + ".o")
        subprocess.run(["gcc", "@" + str(REPO / "gcc-strict.flags"), opt,
                        "-fcallgraph-info=su,da", "-c", str(REPO / f), "-o", str(obj)],
                       check=True)
        objs.append(obj)
    return objs


def parse(ci_files):
    """frames[fn] = (bytes, kind) for our functions; calls[fn] = callees"""
    frames, calls, seen = {}, {}, set()
    for ci in ci_files:
        text = ci.read_text()
        for m in re.finditer(r'node:\s*\{\s*title:\s*"([^"]+)"\s*label:\s*"([^"]*)"', text):
            fn = m.group(1).split(":")[-1]
            seen.add(fn)
            size = re.search(r"(\d+) bytes \((static|dynamic|dynamic,bounded)\)", m.group(2))
            if size:  # defined in this file, so gcc measured its frame
                frames[fn] = (int(size.group(1)), size.group(2))
        for m in re.finditer(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s*targetname:\s*"([^"]+)"', text):
            calls.setdefault(m.group(1).split(":")[-1], set()).add(m.group(2).split(":")[-1])
    return frames, calls, sorted(seen - set(frames))


def find_cycles(frames, calls):
    state, cycles = {}, []

    def visit(fn, path):
        state[fn] = "open"
        for callee in sorted(calls.get(fn, ())):
            if callee not in frames:
                continue
            if state.get(callee) == "open":
                cycles.append(path[path.index(callee):] + [callee])
            elif callee not in state:
                visit(callee, path + [callee])
        state[fn] = "done"

    for fn in sorted(frames):
        if fn not in state:
            visit(fn, [fn])
    return cycles


def deepest(fn, frames, calls, memo):
    """(total bytes, path) of the deepest stack below and including fn"""
    if fn not in memo:
        best = (0, [])
        for callee in calls.get(fn, ()):
            if callee in frames:
                best = max(best, deepest(callee, frames, calls, memo))
        memo[fn] = (frames[fn][0] + best[0], [fn] + best[1])
    return memo[fn]


def report(name):
    """print the report; return a list of problems for --check"""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        objs = build(CONFIGS[name], Path(tmp) / "O2", "-O2")
        frames, calls, outside = parse(sorted((Path(tmp) / "O2").glob("*.ci")))
        build(CONFIGS[name], Path(tmp) / "O0", "-O0")
        frames0, calls0, _ = parse(sorted((Path(tmp) / "O0").glob("*.ci")))
        imported = set()
        if shutil.which("nm"):
            for obj in objs:
                syms = subprocess.run(["nm", "-u", str(obj)], capture_output=True,
                                      text=True, check=True).stdout.split()
                imported |= ALLOCATORS & set(syms)

    print(f"== {name}: {len(frames0)} functions at -O0, {len(frames)} frames left at -O2")
    cycles = find_cycles(frames0, calls0)
    print(f"   rule 1, recursion (-O0 graph): {cycles or 'none'}")
    if cycles:
        problems.append(f"{name}: recursion {cycles}")
    sized = [fn for fn, (_, kind) in {**frames0, **frames}.items() if kind != "static"]
    print(f"   frames not a fixed size: {sized or 'none'}")
    if sized:
        problems.append(f"{name}: frames not a fixed size {sized}")
    if not cycles and "main" in frames:
        total, path = deepest("main", frames, calls, {})
        print(f"   worst-case stack from main, -O2 strict build: {total} bytes")
        if total > STACK_BUDGET:
            problems.append(f"{name}: worst-case stack {total} > {STACK_BUDGET} bytes")
        print("      " + " -> ".join(f"{fn}({frames[fn][0]})" for fn in path))
        total0, _ = deepest("main", frames0, calls0, {})
        print(f"   worst-case stack from main, -O0: {total0} bytes")
    largest = sorted(frames, key=lambda fn: -frames[fn][0])[:3]
    print("   largest frames: " + ", ".join(f"{fn} {frames[fn][0]} B" for fn in largest))
    print(f"   calls leaving this repo's code: {', '.join(outside) or 'none'}")
    if shutil.which("nm"):
        print(f"   rule 3, allocator symbols imported: {', '.join(sorted(imported)) or 'none'}")
        if imported:
            problems.append(f"{name}: allocator symbols {sorted(imported)}")
    else:
        print("   rule 3: nm not found, allocator scan skipped")
    return problems


def main():
    args = sys.argv[1:]
    check = "--check" in args
    names = [a for a in args if a != "--check"] or ["unix", "rtos"]
    problems = []
    for name in names:
        if name not in CONFIGS:
            sys.exit(f"unknown config '{name}': pick from {', '.join(CONFIGS)}")
        problems += report(name)
    if check:
        for p in problems:
            print(f"   PROBLEM {p}")
        sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
