# Instruction Scheduling and Stall Profiling in Kites

Kites has two tools for understanding and reducing pipeline stalls on the 5-stage processors:

- **Suggestions tab**: finds a safe reordering of the editor program that removes stalls,
  explains every move, shows the original and suggested pipelines side by side (real Kites
  runs) and can apply the new order to the editor.
- **Stall profiler** (in the Profiler tab): re-runs the program on a private Kites processor
  and attributes every lost cycle to a source line and a hazard cause.

## 1. User guide

### Suggestions tab

1. Write or open a program, pick the processor, open **Suggestions** and press **Analyze**.
2. The banner shows the real Kites cycle counts before and after, the stall counts, whether
   the static model's prediction matched, and whether the final registers and data memory
   are identical.
3. The **Original** and **Suggested** listings scroll together. Moved rows are tinted and
   marked ↑/↓. Stall cells give their reason as a tooltip.
4. **Why these moves** explains each move. Click an entry to highlight the instructions
   involved.
5. The two pipeline diagrams come from real Kites runs: stalls in the stall colour, bubbles
   as `—`, flushed instructions as `×`.
6. Each explanation has a check box for its block ("Block N · …"). Unticking a block keeps it
   in its original order: the banner shows the new model cycle count at once, and about
   300 ms after the last toggle the suggested program is re-run on Kites (Apply stays disabled
   until then). **Select all** / **Select none** set every block. A selection that would create
   a new Kites quirk pair across a block boundary is refused and the check boxes snap back.
7. **Apply to editor** rewrites the editor (only whole lines move; comments and layout are
   kept). **Revert** undoes it.

### Stall profiler

1. Press **Run**. With "Profile after each Run" ticked (the default), the profile is computed
   in the background when the run ends. Or press **Profile** at any time. The mode combo
   profiles the current processor, or a chosen 5-stage model.
2. The summary line and the coloured bar split the lost cycles by cause, and confirm that the
   numbers match Kites' own counters (and your run, when it completed).
3. The table lists the lines that lost cycles, sorted by cycles lost: the dominant hazard (the
   tooltip gives the full breakdown and an explanation), the top producer line and the
   execution count.
   - **Click** a row to scroll the profiler editor to that line.
   - **Double-click** a row to jump to the line in the Editor tab.
   - **Suggest fix** opens the Suggestions tab on that line's block.
4. The profiler editor gets a heat gutter with the cycles lost per line. It (and the row-click
   scroll) is shown only while the profiler editor displays the profiled text; after editing,
   press **Run** to refresh both.
5. In modes without hazard detection, stalls are 0 by construction. The profiler then lists
   *unprotected hazards*: places where Kites read a register before its producer wrote it.

Demo script:
1. Open `examples/stall_profiler/03_bubble_sort.s`, choose the HF processor and press **Run**.
2. In the Profiler tab, the top data-stall row is the `bge` load-use. Click it, then
   double-click it.
3. Press **Suggest fix** → Suggestions → **Apply** → **Run** again.
4. The profiler shows fewer load-use cycles.

## 2. Hazard taxonomy

| Cause | When Kites produces it | Example |
| --- | --- | --- |
| load-use | Forwarding on: the producer in EX is a load whose rd matches a source field of the instruction in ID. 1 cycle. | `lw x5,0(x1)` → `add x6,x5,x2` |
| RAW no-fwd d=1 | Forwarding off: the producer is in EX. 2 cycles. | `addi x5,x0,1` → `add x6,x5,x5` |
| RAW no-fwd d=2 | Forwarding off: the producer is in MEM. 1 cycle. | `addi x5,…`; `nop`; `add x6,x5,x5` |
| spurious field alias | The hazard unit matched raw `rs1`/`rs2` bits that the consumer does not read (an I-type immediate, a U/J immediate). | `lw x3,…` → `addi x7,x0,3` |
| spurious FP alias | An F/D producer's rd is treated as a GPR write (the control unit sets `reg_write` for FP opcodes), or a store's immediate as an FPR destination. | `fadd.s f5,…` → `add x6,x5,x1` (HNF) |
| branch flush | A taken B-type is resolved in MEM: 3 cycles, charged to the branch line. | loop `bne` |
| jal / jalr flush | A jump redirects in EX: 2 cycles, charged to the jump line. | `jal ra, f`, `ret` |
| pipeline fill | The first instruction needs 4 cycles to reach WB. | every run |
| truncated | Cycles after the last retirement when the run stopped (cycle limit, NH_NF `ecall`). | infinite loop |

## 3. Accounting identity

For every profiled run:

`cycles = retired + stall + flush + fill + truncated`

This is checked against Kites' own `cycle_s_`, `instructions_retired_` and
`branch_mispredictions_`, with zero unclassified cycles. MEM and WB never stall in Kites, so
the gap between two consecutive retirements is made of exactly two things:
- the younger instruction's repeated ID cycles (a stall);
- cycles lost to a redirect by the older instruction (3 for a taken branch, 2 for jal/jalr).

Each stall is classified by replaying Kites' own hazard-detection function
(`check_data_hazard`) on the instructions that were in EX and MEM in that cycle. The tests
check the identity on every example and on 60 random programs with branches, loops and
calls, in HF, HNF, NH_F and NH_NF (328 runs).

## 4. Limits and Kites quirks

- **Kites forwarding quirks:**
  - **Q1:** FP results are forwarded into integer instructions that read the register with
    the same number.
  - **Q2:** the GPR base of an FP load or store is not forwarded.

  The scheduler never creates a new quirk pair, and the profiler reports spurious stalls.
- **Kites bugs, not fixed:**
  - `auipc` (so also `la`) produces 0 on the 5-stage processors;
  - FMA instructions do not write their destination register;
  - `addw`, `subw` and `addiw` do not parse.
- `ecall` is ignored by HF/HNF. Only NH_NF stops on it, and then resets itself; the profiler
  reports the tail as truncated.
- FP instructions are scheduling barriers by default.
- Computed jumps (a `jalr` to an address built by arithmetic) are invisible to the scheduler.
- The static model counts every block once. Loops multiply the saving, which the real Kites
  numbers in the banner show.
- Pre-existing: the Profiler's per-line execution counts are fed by one queued signal per
  cycle, which is slow for very long user runs. The stall profiler does not use them.

## 5. Developer guide

### Module map

- **Qt-free core** (`src/scheduler/`):
  - `instruction_facts`, `kites_hazard_model` (HDU replica), `basic_block_scheduler`;
  - `suggestion_engine`, `source_rewriter`, `selection_evaluator`;
  - `pipeline_trace` (diagram), `pipeline_instance_tracker`, `stall_profiler`.
- **Runtime** (`src/scheduler/runtime/kites_trace_runner`, QtCore): steps private
  `RV5Stage*` processors one cycle at a time. `prepare()` runs on the GUI thread;
  `runPrepared()` and `profilePrepared()` may run on a worker.
- **UI:**
  - `src/ui/suggestions_tab/` (tab and diagram view);
  - `src/ui/profiler_tab/stall_profiler_widget`, `cause_bar`, `stall_heat_gutter_column.h`;
  - `src/ui/common/theme_colors`.

### Data flow

- **Suggestions:** editor text → `assemble` → `SuggestionEngine::analyze` (scheduler +
  explanations) → `rewriteSource` → `assemble` → `verifyRewrittenProgram` →
  `KitesTraceRunner::run` on both versions → `buildPipelineDiagram` → widgets.
- **Profiler:**
  - *GUI thread:* `prepare()` constructs the processor (its circuit scene) and loads the
    program.
  - *Worker (`QtConcurrent::run`):* the `Step()` loop feeds `CycleSnapshot`s into a
    `StallProfiler`, which uses the `PipelineInstanceTracker` events and replays the HDU;
    `finish()` returns the `StallProfile`.
  - *Back on the GUI thread:* `QFutureWatcher::finished` → tables, bar and gutter.

### Adding a hazard cause

1. Add an enum value to `LostCycleCause` before `PipelineFill` (data causes come first), with
   its `causeLabel` and `causeExplanation`.
2. Classify it in `StallProfiler::classifyStall`.
3. Pick a colour in `stall_profiler_widget.cpp` (`causeColor`).
4. Add a sample program and a property test. The identity test catches any cycle you forget.

### Tests

- On macOS: `cmake -S . -B build -DENABLE_TESTS=ON`, then `(cd test && ../build/test/kites_test)`.
- Without Qt: see `tools/headless/README.md`.
- The test files are listed in `docs/TESTING.md`.
