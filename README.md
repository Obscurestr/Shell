# MiniShell (minish)

A small POSIX-compliant command-line shell written in C.  
Supports pipelines, redirection, background processes, and basic built-in commands.

## Features

- Execute external programs using `execvp`.
- Pipelines (`|`) between commands.
- Input/output redirection: `<`, `>`, `>>`.
- Background execution with `&`.
- Built-in commands:
  - `cd [dir]` – change directory
  - `pwd` – print current working directory
  - `exit` – exit the shell
- Forwarding of `SIGINT` (Ctrl+C) to foreground processes.
- Basic job handling (background processes notification).

## Requirements

- Linux or macOS (POSIX-compliant system)
- GCC or Clang compiler

## Compilation

```bash
gcc -Wall -Wextra -O2 -o minish minish.c

## Usage
./minish
