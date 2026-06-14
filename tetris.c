/*
The Engineering Paradigm

1. The Rendering Trick: ANSI Escape Codes

        Up until now:
                * Every time you used `printf`, the terminal printed the text
and scrolled downward.
                * If you print your Tetris board, update it, and print it again,
you will just get a massive scrolling list of boards.

        The Solution: You use standard ANSI escape sequences to take control of
the terminal cursor.
                * Instead of scrolling, you print a special hidden string like
`\033[H`.
                * This tells the terminal, "Instantly teleport the invisible
cursor back to the top-left corner of the screen."
                * Every frame of your game, you teleport the cursor to the top,
print your 2D array, and overwrite the previous frame.
                * This creates the illusion of a static screen and smooth
animation.

2. The Input Problem: Non-Blocking I/O

        * If you use `fgetc` or `scanf` to ask the user to move a Tetris piece,
the program completely pauses and waits for them to type something and hit
`Enter`.
        * But in Tetris, gravity doesn't wait. The block must fall even if the
player touches nothing.

        The Solution: You have to disable the terminal's "Canonical Mode."
        * In a Linux environment, you will use a built-in library called
`<termios.h>`.
        * This allows you to rewrite the rules of the terminal so that it passes
keystrokes (like the arrow keys) to your program instantly.

---

The Architecture of Terminal Tetris

1. Start Menu: Make a start menu that either starts the game or prints
information about the project
2. Initialization: Set up a 2D array of integers to represent the grid (e.g.,
`int board[20][10];`). Turn off terminal Canonical Mode.
3. The `while (1)` Loop (The Engine):
        * Input: Check if the user pressed a key. If yes, update the block's X/Y
coordinates in memory.
        * Logic (Gravity & Collision): Has enough time passed? If yes, move the
block down by 1. Does it hit the bottom or another block? Lock it in place and
spawn a new one.
        * Render: Send the ANSI teleport code to the screen. Loop through the 2D
array and `printf` either `#` or `[]` for every `1`, and a space ` ` for every
`0`.
        * Select: Monitor your keyboard (Standard Input) for a specific amount
of time so the loop runs at a steady framerate instead of 5 million times a
second.
*/

#include <stdio.h>
#include <termios.h>

int main(int argc, char *argv[]) {
  int board[40][20];

  if (argv[1] != "info" || argv[1] != "i" || argv[1] != "play" ||
      argv[1] != "p") {
    printf("Supported commands: p/play or i/info\n");
  }

  else if (argv[1] == "info" || argv[1] == "i") {
    printf("Terminal Tetris is a game created by'spaceadler' to better "
           "understand the C programming languange in his journey to master "
           "low-level and embedded C development.\n");
  }

  else if (argv[1] == "play" || argv[1] == "p") {
    // turn off terminal canonical mode
    // load board on screen

    while (1) {
    }
  }
  return 0;
}
