# SputterOS Development Dependencies

All host-native development tools required to build, test, analyze, and document SputterOS.

## Required (Build & Test)

| Package | Version | Purpose | Install |
|---|---|---|---|
| CMake | ≥ 3.14 | Build system generator | `apt install cmake` / `pacman -S mingw-w64-x86_64-cmake` |
| Ninja | ≥ 1.10 | Build backend | `apt install ninja-build` / `pacman -S mingw-w64-x86_64-ninja` |
| GCC or Clang | C++17 support | Compiler | `apt install g++` / `pacman -S mingw-w64-x86_64-gcc` |
| GNU Make | ≥ 4.0 | Makefile runner | `apt install make` / `pacman -S make` |
| GoogleTest | v1.15.2 | Unit & system tests | *Fetched automatically by CMake* |

## Required (Code Quality)

| Package | Version | Purpose | Install |
|---|---|---|---|
| clang-format | ≥ 17 | Code formatting (`make format`) | `pip install clang-format==21.1.8` / `apt install clang-format` |
| clang-tidy | ≥ 17 | Static analysis (`make tidy`) | `apt install clang-tidy` / `pacman -S mingw-w64-x86_64-clang-tools-extra` |

## Required (Documentation)

| Package | Version | Purpose | Install |
|---|---|---|---|
| Doxygen | ≥ 1.9 | API documentation (`make docs`) | `apt install doxygen` / `pacman -S mingw-w64-x86_64-doxygen` |
| Graphviz | ≥ 2.40 | Class/call/dependency graphs | `apt install graphviz` / `pacman -S mingw-w64-x86_64-graphviz` |

## Optional (Coverage)

| Package | Version | Purpose | Install |
|---|---|---|---|
| Clang | ≥ 17 | Coverage-instrumented build | `apt install clang` / `pacman -S mingw-w64-x86_64-clang` |
| llvm-profdata | ≥ 17 | Profile data merging | `apt install llvm` / `pacman -S mingw-w64-x86_64-llvm` |
| llvm-cov | ≥ 17 | Coverage report generation | *(same as above)* |

## Quick Install

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ make ninja-build clang clang-tidy clang-format \
    doxygen graphviz llvm
```

### MSYS2 (MinGW64)

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc make \
    mingw-w64-x86_64-clang-tools-extra mingw-w64-x86_64-doxygen mingw-w64-x86_64-graphviz \
    mingw-w64-x86_64-clang mingw-w64-x86_64-llvm
```

### macOS (Homebrew)

```bash
brew install cmake ninja gcc make llvm doxygen graphviz
# clang-tidy and clang-format are included in the llvm package
```

## Python Tools (CLI / Testing)

| Package | Purpose | Install |
|---|---|---|
| sputterctl | Serial/TCP REPL and metrics CLI | `pip install -e tools/cli` |
| clang-format (pip) | Pinned formatter version for CI | `pip install clang-format==21.1.8` |
