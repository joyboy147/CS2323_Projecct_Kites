# Kites: RISC-V Simulator with Code-Reordering Tools

This repository is an extension of the [Kites RISC-V simulator](https://github.com/hghit-harshit/Kites), originally created by Atharva Gajendra Lohare and Harshit Daheriya. It retains the original MIT license and extends Kites for the CS2323 Computer Architecture project by Nandan Nihal Chanda (CS25BTECH11014).

Kites is a C++20 and Qt 6 RISC-V simulator and assembly editor. It supports the I, M, F, and D extensions in single-cycle mode, and I/M execution in configurable five-stage pipeline modes.

## Project additions

This extension adds tools to identify, explain, and reduce pipeline stalls:

- A clearer processor selector: single-cycle or five-stage pipeline, with hazard detection and forwarding controls enabled only for the five-stage model.
- Basic-block instruction scheduling using a dependency DAG and Kites-aware list scheduling.
- A Suggestions tab that compares original and reordered code, explains each safe move, and lets a user apply or reject suggestion blocks.
- Stall profiling with per-line lost-cycle ranking, data-hazard causes, branch/jump flushes, and suggested fixes where applicable.
- Conservative loop unrolling for eligible simple counted loops, with Off, x2, and x4 options.
- Scheduler, profiler, loop-unrolling, and processor validation tests.

The original simulator remains the foundation of this project. The code-reordering tools are designed for the five-stage processor configurations; the single-cycle RVSS model is used as a correctness reference where applicable.

## Build

Install CMake, Ninja, a C++20 compiler, and Qt 6.9.2 or newer with the `Core`, `Widgets`, `Network`, and `Concurrent` modules.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x.x/gcc_64"
cmake --build build --parallel
./build/Kites
```

On Ubuntu, the usual prerequisites are:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools
```

## Tests

Enable and run the test suite with:

```bash
cmake -S . -B build/tests -G Ninja -DENABLE_TESTS=ON \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x.x/gcc_64"
cmake --build build/tests --parallel
ctest --test-dir build/tests --output-on-failure
```

See [the testing guide](docs/TESTING.md) for further details.

## CLI

See [the CLI guide](docs/CLI.md) for building and using the headless command-line executable.

## Credits and license

- Original Kites project: [hghit-harshit/Kites](https://github.com/hghit-harshit/Kites)
- Project extension: [joyboy147/CS2323_Projecct_Kites](https://github.com/joyboy147/CS2323_Projecct_Kites)
- License: [MIT](LICENSE.md)
