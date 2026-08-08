# spacetetris

A custom-built Tetris engine written in C. This project serves as a practical bridge for transitioning from VHDL hardware design to low-level embedded systems and RTOS development. 

The primary objective of this project is to enforce strict memory management, state machine architecture, and hardware abstraction in a bare-metal-style C environment.

## Architecture

The codebase is decoupled to separate the pure physics engine from hardware-specific I/O. The goal is mimicing professional embedded firmware design, allowing the core logic to run on (mostly) anything without modifying the game logic.

*   `tetris.h` / `tetris.c`: The hardware-agnostic core. Contains the 2D Cartesian physics radar, line-clearing algorithms, and the master state machine. It operates purely on memory arrays and has no dependencies on screens, keyboards, or standard I/O libraries.
*   `pc-port.c`: The hardware wrapper for Windows and Linux. Handles standard library includes, OS-specific non-blocking keyboard polling (`<termios.h>` / `<conio.h>`), CPU delay cycles, and ANSI escape sequence rendering.

## Technical Details

*   **Decoupled Collision Radar:** Lateral movement and rotations are calculated by testing coordinate variants against a unified memory grid before committing to memory writes, preventing invalid state overlaps.
*   **State Machine Execution:** Gravity, block locking, and line clearing are handled as discrete non-blocking states, ensuring deterministic execution and freeing the main loop to poll hardware inputs.
*   **Data Types:** Strict adherence to explicit integer widths (`int8_t`, `uint8_t`) to prevent signed/unsigned underflow bugs and minimize memory footprint.

## Building and Running

The current PC port requires a standard C compiler (GCC/Clang).

**Linux / macOS**
```bash
gcc tetris.c pc-port.c -o spacetetris
./spacetetris
```
**Windows (MinGW/GCC)**
```DOS
gcc tetris.c pc-port.c -o spacetetris.exe
spacetetris.exe
```

---
*made with logic, coffee, and many sleepless nights*
