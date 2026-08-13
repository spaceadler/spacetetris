# spacetetris

> A custom-built Tetris engine written in C. Designed as a practical bridge from VHDL hardware design to low-level embedded systems and RTOS development.

![Language](https://img.shields.io/badge/Language-Embedded%20C-blue)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20RTOS-green)
![Architecture](https://img.shields.io/badge/Architecture-Finite%20State%20Machine-orange)

## Overview

A from-scratch Tetris engine I made with strict separation between the hardware-agnostic game logic and the platform-specific I/O layer. 

The main objective is to learn strict memory management, deterministic state machine architecture, and hardware abstractions in a bare-metal-style C environment used in embedded firmware.

## Architecture

```
┌──────────────────────────────────────────────────┐
│                    main.c                        │
│           (Platform-Specific I/O Port)            │
│                                                  │
│  get_raw_keypress()  render()  delay_ms()        │
│  ┌──────────┐  ┌──────────────┐  ┌───────────┐   │
│  │ conio.h  │  │ ANSI Escape  │  │ CPU Busy  │   │
│  │ termios.h│  │ Sequences    │  │ Wait      │   │
│  └──────────┘  └──────────────┘  └───────────┘   │
├──────────────────────────────────────────────────┤
│                   tetris.h                       │
│              (Interface Contract)                │
├──────────────────────────────────────────────────┤
│                   tetris.c                       │
│          (Hardware-Agnostic Engine)              │
│                                                  │
│  state_machine()   physics_get()   line_checker()│
│  spawn_piece()     physics_set()   shuffle_bag()   │
└──────────────────────────────────────────────────┘
```

The codebase is decoupled into two compilation units:

- **`tetris.h` / `tetris.c`** These form the core engine. They contain the master state machine, 2D collision detection, the line-clearing system, and 7-bag piece randomizer. It operates purely on memory arrays without any dependencies on screens, keyboards, or standard I/O.
- **`main.c`** This is the "PC port". It handles OS-specific keyboard polling (`<conio.h>` on Windows, `<termios.h>` on Linux), escape sequence rendering, and the main game loop. This file can be edited (and will be edited) to support further platforms. RTOS port soon.

## State Machine

The game loop is lead by a deterministic finite state machine. Gravity and lock delays are frame-counted, which keeping the main loop free to poll hardware inputs every cycle.

```
                 ┌─────────────────────────────────────────────┐
                 ▼                                             │
    MENU ──► GAME_START ──► BLOCK_FALL ──► BLOCK_STOP          │
                                ▲              │               │
                                │              ▼               │
                             WAIT ◄─────── LINE_CHECK          │
                             (spawn)           │               │
                                               ▼               │
                                          GAME_OVER ───────────┘
                                           (restart)
```

## Technical Details

- **Decoupled Collision Radar:** Invalid state overlaps are avoided by testing side movement and rotations against the physics engine before rendering.
- **Frame-Based Gravity:** Gravity ticks every 30 frames (~0.5s at 60 FPS) via an internal counter which keeps the game playable and the timing adjustable.
- **7-Bag Randomizer:** Fisher-Yates shuffle across all 7 tetrominoes. Each piece appears exactly once per bag before reshuffling.
- **Classic Scoring:** 100 / 300 / 500 / 800 points for 1 / 2 / 3 / 4 simultaneous line clears.
- **Data Types:** Strict use of explicit integer widths (`int8_t`, `int16_t`, `int32_t`) to prevent signed/unsigned underflow and minimize memory footprint.

## Controls

| Key | Action |
|---|---|
| `a` | Move left |
| `d` | Move right |
| `s` | Soft drop |
| `w` | Rotate clockwise |

## Building and Running

Requires a standard C compiler (GCC / Clang).

**Linux / macOS**
```bash
gcc tetris.c main.c -o spacetetris
./spacetetris
```

**Windows (MinGW / GCC)**
```bash
gcc tetris.c main.c -o spacetetris.exe
spacetetris.exe
```

---
*powered by logic, coffee, and many sleepless nights*
