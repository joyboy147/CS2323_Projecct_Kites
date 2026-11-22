# Headless (Qt-free) build of the Kites core

Builds the non-GUI Kites sources (assembler, processors, caches, scheduler and stall profiler)
against small Qt stand-ins, so the gtest suite and the analysis tools run without Qt.
The main Kites build never sees this directory, because it only globs `src/`.

```bash
cmake -S tools/headless -B build-headless -G Ninja          # add -DGTEST_SRC=/path/to/googletest offline
cmake --build build-headless
(cd test && ../build-headless/kites_headless_test)          # tests read files relative to test/
```

| Target | Purpose |
| --- | --- |
| `kites_headless_test` | All `test/*.cpp` except `pseudo_inst_test.cpp`, which needs real Qt (QFile/QJson). |
| `kites_cycles <file.s> <hf\|hnf\|nhf\|nhnf\|rvss> [--trace]` | Run a program and print cycles, registers and an optional per-cycle stage trace. |
| `kites_suggest <file.s> [hf\|hnf] [--write out.s] [--table]` | The Suggestions-tab flow: analyze, rewrite, verify, run both versions, compare. |
| `kites_profile <file.s> <hf\|hnf\|nhf\|nhnf\|--table>` | The stall profiler: accounting identity, causes and per-line table. |
| `kites_fuzz <n> [first] [--fp] [--profile\|--select\|--emit] [--dump prefix]` | Differential fuzzer: suggestions vs RVSS and Kites; `--profile` checks the stall-profiler identity in all 4 modes (and per-line stalls vs the model on straight-line code); `--select` applies every subset of accepted blocks and checks verify, RVSS, Kites state and model == Kites cycles; `--emit` only writes the programs. |

## Stubs

- `qtstub/` holds minimal QtCore stand-ins. Signals become empty functions
  (`qt_signal_stubs.cpp`), and `connect()` type-checks its arguments the way Qt's
  pointer-to-member overload does.
- `qtstub_widgets/` holds Qt 6 widget signatures, for syntax checks of UI code only:

```bash
g++ -std=c++20 -fsyntax-only -Wall -Wextra -pedantic -Itools/headless/qtstub_widgets \
    -Itools/headless/qtstub -Isrc src/ui/suggestions_tab/suggestionstab.cpp
```
