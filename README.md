# 7sh

A minimal, POSIX-compliant custom shell written in C, featuring a native line editor and UTF-8 support built from scratch.

> **Note:** This is a hobby project built by a single Arabic developer, created to explore low-level systems programming and terminal behavior without complex dependencies.
## Features

| Feature | Description |
| :--- | :--- |
| **Custom Line Editor** | Implemented using raw terminal mode (`termios`) without `readline` or `ncurses`. |
| **UTF-8 Support** | Accurate visual width calculation and byte backtracking for multi-byte characters. |
| **Shortcuts** | Supports `Ctrl+W` (word deletion), `Ctrl+Left/Right` (word navigation), and history tracking. |
| **Autocomplete** | Tab-completion for directories and files using `dirent.h`. |

## Building and Running

### Prerequisites
- `gcc`
- `make`

### Installation
```bash
make
./bin/7sh
