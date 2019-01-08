# Small Shell

This program is a simple shell coded in C. Small Shell is capable of handling redirections, supporting foreground and background processes, creating and handling child processes (including reaping finished background processes), and handling interrupts. Small Shell has the following built-in commands: `exit`, `cd`, and `status`. All other unix commands are handled via calls to `execvp()`.

## Instructions
1. Run `make` to compile program.
2. Run `smallsh.exe` to run shell.

