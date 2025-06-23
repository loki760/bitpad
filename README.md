# Kilo — A Minimal Text Editor in C

This is a small, fully functional text editor for the terminal, inspired by [kilo](https://viewsourcecode.org/snaptoken/kilo/index.html) by Salvatore Sanfilippo. It is built **from scratch in C**, using **raw terminal input**, with no dependencies other than the standard library and POSIX system calls.

---

## Features

- Raw mode terminal input (handles Ctrl keys, disables echo, etc.)
- Cursor movement with arrow keys (WSAD-like navigation)
- Page up/down, home/end support
- Open and display files (line-by-line memory allocation)
- Vertical scrolling and viewport control
- Clean redraw and screen refresh with escape sequences
- Exit with `Ctrl + Q`
- Syntax highlighting and saving **not yet implemented**

---

## Tech Details

- Written in pure C (C89/C99 compatible)
- Uses low-level POSIX APIs:
  - `termios.h` for terminal control
  - `read()` / `write()` for input/output
  - `ioctl()` for screen size
- Manual memory management (`malloc`, `free`)
- No external libraries or dependencies



