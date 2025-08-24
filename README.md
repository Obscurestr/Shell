# Shell (Mini Unix-Like Shell in C)
A simple yet functional Unix-like shell written in C, supporting command execution, piping, redirection, background jobs, and a few built-in commands.
## Features
- Command parsing with support for:
  - Multiple commands separated by pipes ( | )
  - Input (<) and output (>) redirection
  - Output append (>>)
  - Background execution (&)
- Built-in commands:
  - Ctrl+C support to interrupt foreground processes
  - Background job completion notifications
- Custom prompt showing the current working directory
- Robust tokenization and memory management
## Requirements
- A POSIX-compatible system (Linux, macOS, or WSL on Windows)
- GCC or Clang compiler supporting C99 or later
## Building
Clone the repository and compile:
```bash
git clone https://github.com/Obscurestr/Shell.git
cd Shell
gcc -Wall -Wextra -o shell Source.cpp
```
## Usage
Run the shell executable:
```bash
./shell
```
Example commands:
```bash
ls -l
cat file.txt | grep "keyword"
sort < unsorted.txt > sorted.txt
echo "Append me" >> log.txt
sleep 10 &
pwd
cd /path/to/dir
exit
```
## Code Structure
- Tokenization: Splits user input into tokens.
- Parsing: Builds command pipelines.
- Execution: Handles forking, piping, and redirection.
- Builtins: Executes simple commands internally.
- Signal handling: Properly manages interrupts and child process cleanup.
 
