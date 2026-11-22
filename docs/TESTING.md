# Running Tests

Kites uses [GoogleTest](https://github.com/google/googletest) for unit testing, integrated with CMake/CTest. Two test targets are available:

- **`kites_test`** — plain test build, no instrumentation. Use this for everyday local testing.
- **`kites_test_cov`** — same test suite, built with coverage instrumentation (`--coverage`), plus a `coverage` target that generates an HTML report via [`gcovr`](https://gcovr.com/).

Both targets are produced from a single configure step; you choose which one to build/run afterward.

## Prerequisites

- CMake 3.24+
- Ninja
- A GCC- or Clang-style compiler (coverage instrumentation requires this — see [Notes](#notes))
- [`gcovr`](https://gcovr.com/) on your `PATH` (only required if configuring with `ENABLE_COVERAGE=ON`)

## Configure

Both test targets live under the same configure preset:

```sh
cmake --preset test
```

## Running tests (no coverage)

```sh
cmake --build --preset test-only
ctest --preset test-only
```

This builds and runs `kites_test` only — the fast path for everyday local test runs.

## Running tests (with coverage)

```sh
cmake --build --preset test-coverage
ctest --preset test-coverage
```

This builds and runs `kites_test_cov`, whose CTest test names are prefixed with `cov_` so they don't collide with the plain suite's test names.

### Generating the HTML coverage report

The `coverage` target runs the instrumented test suite via CTest and then invokes `gcovr` to produce an HTML report:

```sh
cmake --build build/coverage --target coverage
```

The report is written to `build/coverage/coverage/index.html`.

## Build options

Set via `-D<OPTION>=ON|OFF` at configure time, or in `CMakePresets.json`:

| Option              | Default | Description                                      |
|---------------------|---------|---------------------------------------------------|
| `DISABLE_GUI`        | `ON`    | Disable GUI components during tests               |
| `VM_DEBUG_PRINTS`     | `OFF`   | Enable debug prints in the VM during tests        |
| `ENABLE_COVERAGE`     | `OFF`   | Build the `kites_test_cov` target and `coverage` target |

## Notes

- Coverage instrumentation (`--coverage`) requires GCC or Clang; configuring with `ENABLE_COVERAGE=ON` on another compiler will fail with a clear error.
- If you're on Windows with a Qt-bundled MinGW toolchain, confirm it actually ships `libgcov.a` before relying on coverage builds — some trimmed-down distributions omit it. You can check with:
  ```sh
  g++ -print-file-name=libgcov.a
  ```
  A real path back means it's present; the bare filename echoed back means it isn't found.
## Scheduler and stall-profiler tests

| File | What it checks |
| --- | --- |
| `test/basic_block_scheduler_test.cpp` | Phase-1 scheduler behaviour (kept unchanged). |
| `test/scheduler_core_test.cpp` | Qt-free scheduler core: DAG legality, Kites hazard model, block choice, LCS moves, explanations, rewriting, diagrams. |
| `test/scheduler_kites_validation_test.cpp` | Model cycles == real Kites cycles on examples and random programs (HF/HNF), equivalence of suggestions, apply round-trip. |
| `test/stall_profiler_test.cpp` | Qt-free stall profiler on hand-written stage snapshots (identity, causes, flushes, truncation). |
| `test/stall_profiler_kites_test.cpp` | Profiler identity `cycles = retired + stall + flush + fill + truncated` with zero unclassified cycles, on every example and 60 random control-flow programs in HF, HNF, NH_F and NH_NF; sample-program properties; cycle limit, cancel and ecall stop. |
| `test/selection_evaluator_test.cpp` | Per-block selection: every subset of accepted blocks re-evaluated, rewritten, verified and run on Kites. |

These tests read example programs relative to `test/` (CTest's working directory).

## Running the suite without Qt

`tools/headless/` builds the non-GUI sources against small Qt stand-ins (see its README):

```sh
cmake -S tools/headless -B build-headless -G Ninja
cmake --build build-headless
(cd test && ../build-headless/kites_headless_test)
```
