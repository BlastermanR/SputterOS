# Contributing to SputterOS

Thank you for helping improve this project.

This repository contains the reusable SputterOS C++17 library, its host-native test suites, example projects, and supporting documentation.

## Before You Start

1. Check existing issues and PRs to avoid duplicate work.
2. For larger changes, open an issue first to discuss approach.
3. Keep PRs focused on one change set.

## Local Setup

## Prerequisites

- CMake and Ninja
- A C++17 compiler
- Git
- For firmware work: Pico SDK and ARM embedded toolchain (`arm-none-eabi-gcc`)

## Clone and Initialize

```bash
git clone <your-fork-or-repo-url>
cd SputterOS
git submodule update --init --recursive
```

## Build and Test: SputterOS

```bash
make test          # unit tests + system tests + example projects
make formalTest    # regenerate FormalTestResults.md / .txt
make lib-only      # library-only build
make exampleProjects # build and run standalone examples
make format        # clang-format all C++ sources/headers
```

## Coding Standards

- Language: C++17
- Formatting: run `make format` in the project you touched before opening a PR
- Naming conventions:
  - Interfaces start with `I` (example: `IUserApplication`)
  - Kernel task types end with `Task` (example: `ControlTask`)
  - Dummy/stub types start with `Dummy`
  - Member variables use `m_`, static members use `s_`, globals use `g_`
  - Compile-time constants use `k` prefix (example: `kQueueCapacity`)
- Time values: use `SputterMicros` instead of raw integer time values
- Architecture constraints:
  - No blocking behavior in HAL methods or task `tick()` methods
  - No dynamic allocation in kernel paths
  - Inject hardware dependencies, do not instantiate hardware directly in kernel code
- Comments: follow Doxygen style in `../docs/CommentStyle.md`

## Branch and Commit Workflow

1. Create a branch from your target base branch:

```bash
git checkout -b feat/short-description
```

2. Make changes with tests.
3. Run formatting and tests for affected project(s).
4. Commit with clear messages:

```bash
git add <files>
git commit -m "Short imperative summary"
```

## Pull Request Process

1. Push your branch to your fork/remote.
2. Open a Pull Request against the correct base branch.
3. Fill out the PR template completely.
4. Ensure all of the following are true:
   - Tests pass locally for impacted code
   - Code is formatted (`make format`)
   - Documentation is updated when behavior/API changes
   - Safety-relevant changes explain risk and mitigation
5. Address review comments with follow-up commits.

## PR Review Expectations

Reviewers will evaluate:
- Behavioral correctness and regressions
- Safety implications and fault handling
- Determinism/non-blocking behavior
- Test coverage and maintainability

## Reporting Bugs and Security Issues

- For normal bugs/features, use GitHub Issues.
- For security posture and reporting limitations, follow `SECURITY.md`.

Thank you for contributing to SputterOS.
