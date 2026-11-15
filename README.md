# spacetetris

> A from-scratch Tetris engine written in C that is fully MISRA C:2012 and JPL Power of 10 compliant. Built as a practical bridge from VHDL hardware design to low-level embedded systems and RTOS development.

![Language](https://img.shields.io/badge/Language-Embedded%20C-blue)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20RTOS-green)
![Architecture](https://img.shields.io/badge/Architecture-Finite%20State%20Machine-orange)

## Overview


The main objective for me with this project was to learn strict memory management, deterministic state machine architecture, and HAL's in a baremetal style C environment, much like those used in flight systems.

## Architecture

```
┌────────────────────────────────────────────────────────┐
│                         main.c                         │
│            (Cyclic Executive, bounded loop)            │
│          power-on self-test, then every tick:          │
│       key ─► state_machine() ─► verify ─► render       │
├────────────────────────────┬───────────────────────────┤
│    tetris.h / tetris.c     │            io.h           │
│ (Hardware-Agnostic Engine) │    (Platform Interface)   │
│                            │                           │
│  state_machine()           │  io_unix.c     termios    │
│  physics_get()             │  io_windows.c  conio.h    │
│  cell_filled()             │  io_rtos.c     stub       │
│  verify_engine_integrity() │                           │
│  engine_self_test()        │  one port is linked in,   │
├────────────────────────────┤                           │
│      prng.h / prng.c       │  no #ifdef anywhere       │
│        (xorshift32)        │                           │
├────────────────────────────┴───────────────────────────┤
│                 assert_handler.h / .c                  │
│      (TETRIS_ASSERT: checks, recovery, safe mode)      │
└────────────────────────────────────────────────────────┘
```

The codebase is split into an engine, a platform interface with one implementation per platform, a loop that drives them, and the check handler they all share:

- **`tetris.h` / `tetris.c`** These form the core engine, containing the master state machine, collision detection, line-clearing, and 7-bag piece randomizer. It operates purely on memory arrays.
- **`prng.h` / `prng.c`** A xorshift32 random number generator. Main reason is because MISRA bans `rand()`.
- **`io.h`** The platform interface: keyboard, rendering, timing, seeding and screens. The engine says *what* happens; each port decides *how* it looks.
- **`main.c`** The organizer, it does a power-on self-test, then a counted loop with a hard 10-hour bound that reads a key, advances the state machine one step, verifies the engine's checksums, renders, and waits out the frame.
- **`assert_handler.h` / `assert_handler.c`** `TETRIS_ASSERT(cond)`, the project's checkers. It is true when `cond` holds, so the code around it can recover when it doesn't. A failed check latches a fault and `main.c` stops the game at the end of that tick in **safe mode**, reports where the first failed check sits (`check failed at <file>:<line>`), and exits with status 1.
- **`char_t.h`** `char_t`, plain `char` under a name: the one place the code spells out a basic C type (MISRA Dir 4.6, JPL rule 17).

## State Machine

The game loop is lead by a deterministic finite state machine. Gravity and lock delays are frame-counted, which keeping the main loop free to poll hardware inputs every cycle.

```
                 ┌─────────────────────────────────────────────┐
                 ▼                                             │
    MENU ──► GAME_START ──► BLOCK_FALL ──► BLOCK_STOP          │
                                ▲              │               │
                                │              ▼               │
                       TETRISWAIT ◄─────── LINE_CHECK          │
                        (spawn)                │               │
                                               ▼               │
                                          GAME_OVER ───────────┘
                                           (restart)
```

## Technical Details

- **Decoupled Collision Radar:** Invalid state overlaps are avoided by testing side movement and rotations against the physics engine before rendering.
- **Frame-Based Gravity:** Gravity ticks every 30 frames (~0.5s at 60 FPS) via an internal counter which keeps the game playable and the timing adjustable.
- **7-Bag Randomizer:** Fisher-Yates shuffle across all 7 tetrominoes, driven by xorshift32. Each piece appears exactly once per bag before reshuffling.
- **Classic Scoring:** 100 / 300 / 500 / 800 points for 1 / 2 / 3 / 4 simultaneous line clears.
- **Data Types:** Strict use of explicit integer widths (`int8_t`, `int16_t`, `int32_t`) to prevent signed/unsigned underflow and minimize memory footprint.
- **Bounded Loops:** Every loop has a fixed upper bound you can read off its header, including the game loop itself (JPL Rule 2).
- **Self-Checking State:** Checks guard every pointer, array index and state invariant, and CRC-8 checksums over the grid and score are verified once per tick (JPL Rule 5). At power-on a self-test checks the shape table, the CRC and the PRNG against known answers.

## Controls

| Key | Action |
|---|---|
| `a` | Move left |
| `d` | Move right |
| `s` | Soft drop |
| `w` | Rotate clockwise |

## Building and Running

Requires a standard C compiler (GCC / Clang). The build uses the strictest warning set each compiler has, and every warning is an error (JPL Rule 10). The flags live in `gcc-strict.flags` / `clang-strict.flags`, which the compiler reads directly via `@file`. [COMPLIANCE.md](COMPLIANCE.md) explains every flag and the few that are switched off.

**Linux / macOS**
```bash
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_unix.c main.c -o spacetetris
./spacetetris
```

**Windows (MinGW / GCC)**
```bash
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_windows.c main.c -o spacetetris.exe
spacetetris.exe
```

**RTOS stub (headless, builds and runs anywhere)**
```bash
gcc @gcc-strict.flags tetris.c prng.c assert_handler.c io_rtos.c main.c -o spacetetris-headless
```

With no keys, screen or timer it plays one silent game to game over, then the bounded game loop runs out its ticks and exits.

Any of these also builds with `clang @clang-strict.flags` in place of `gcc @gcc-strict.flags`.

## Testing

```bash
tools/check.sh            # everything, about 5 minutes on a Raspberry Pi
tools/check.sh --quick    # skips the ~40 s game played in a real terminal
```

The script compiles every configuration with both compilers, runs both static analyzers, the MISRA C:2012 checks (cppcheck) and the Power of 10 checks (Cobra and `tools/`), replays the original v1 engine against this one tick by tick, plants faults to prove every checker can fail, and plays the real game through a pseudo-terminal. It exits non-zero on any finding that isn't a reviewed one in [COMPLIANCE.md](COMPLIANCE.md). [REFACTOR-REPORT.md](REFACTOR-REPORT.md) walks through every change from v1, before and after.

---
*powered by logic, coffee, and many sleepless nights*
