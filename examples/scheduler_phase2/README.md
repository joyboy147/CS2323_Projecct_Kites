# Phase 2 Scheduler Demos

These programs exercise the **Suggestions** tab: the scheduler finds a safe
reordering, explains every move, and shows the original and suggested Kites
pipelines side by side.

| Program | Shows |
| --- | --- |
| `01_two_load_use.s` | Two load-use pairs; hoisting the second load hides both bubbles. |
| `02_alu_chain_noforward.s` | ALU chains: nothing to gain with forwarding (HF), several cycles without it (HNF). |
| `03_loop_sum.s` | A loop: the per-iteration saving is multiplied by the 8 iterations. |
| `04_must_not_move.s` | RAW/WAR/WAW and memory order block every move: "No safe reordering lowers the stall count". |
| `05_pseudo_and_layout.s` | `li` expansions stay one unit; comments and blank lines survive Apply. |
| `06_kites_hazard_quirk.s` | Kites' hazard unit compares raw register fields, so `addi x7, x0, 3` stalls behind a load of `x3`; the model knows this. |
| `07_matrix_2x2.s` | A naive 2x2 matrix multiply with fresh registers: large savings. |
| `08_dot_product.s` | A dot product that reuses `x5`/`x6`: WAR/WAW dependences keep most stalls. |

## Measured Kites cycle counts

Measured by running each program and its suggested reordering on Kites'
`RV5StageProcessorHF` / `RV5StageProcessorHNF` (the same `KitesTraceRunner`
that the Suggestions tab uses; cross-checked against `DebugRun()`). "Model
stalls" is the static model's count for the program text (each block counted
once). "Same result" means the final registers and the first 4 KiB of the data
section are identical for the original and the suggested program.

| Program | HF original | HF suggested | HNF original | HNF suggested | Model stalls HF | Model stalls HNF | Same result |
| --- | ---: | ---: | ---: | ---: | :---: | :---: | :---: |
| `01_two_load_use.s` | 11 | 9 | 15 | 12 | 2 → 0 | 6 → 3 | yes |
| `02_alu_chain_noforward.s` | 10 | 10 | 18 | 12 | 0 → 0 | 8 → 2 | yes |
| `03_loop_sum.s` | 77 | 69 | 101 | 77 | 1 → 0 | 4 → 1 | yes |
| `04_must_not_move.s` | 12 | 12 | 18 | 18 | 2 → 2 | 8 → 8 | yes |
| `05_pseudo_and_layout.s` | 14 | 12 | 20 | 16 | 2 → 0 | 8 → 4 | yes |
| `06_kites_hazard_quirk.s` | 10 | 9 | 13 | 11 | 1 → 0 | 4 → 2 | yes |
| `07_matrix_2x2.s` | 37 | 31 | 61 | 46 | 6 → 0 | 30 → 15 | yes |
| `08_dot_product.s` | 27 | 24 | 42 | 37 | 4 → 1 | 19 → 14 | yes |
| `../scheduler_phase1/load_use_original.s` | 11 | 10 | 14 | 13 | 1 → 0 | 4 → 3 | yes |

For straight-line programs (all except `03_loop_sum.s`) Kites' cycle count is
exactly `instructions + model stalls + 4`, and the automated tests check this.
In the loop, the HF saving is 1 cycle × 8 iterations = 8, and the HNF saving is
3 cycles × 8 iterations = 24.

## Full reordering flow (demo script)

1. Start Kites and open a demo (**File → Open**), e.g. `07_matrix_2x2.s`.
2. Choose the processor (**Processor** toolbar button). Pick *5-stage, hazard
   detection + forwarding*, or *hazard detection, no forwarding*.
3. Open the **Suggestions** tab in the sidebar and press **Analyze**.
4. Read the banner. It shows the real Kites cycle counts before and after, the
   stall counts, whether the model's prediction matched, and whether the final
   registers and data memory are identical.
5. Inspect the result:
   - The **Original** and **Suggested** listings scroll together. Moved rows
     are tinted and marked ↑/↓, and stall cells show the reason as a tooltip.
   - **Why these moves** has one entry per move. Click an entry to highlight
     the moved instruction and the instructions it relates to.
   - The two pipeline diagrams come from real Kites runs. Stall cells are
     red, bubbles are shown as `—`, and flushed instructions as `×`. Hover a
     stall cell to see its cause.
6. Press **Apply to editor**. The editor text is replaced by the suggested
   order. Only whole lines move, so comments and layout are kept.
   **Revert** restores the previous text.
7. Press **Run** and compare the cycle count in the Processor tab with the
   number in the banner.

Try `04_must_not_move.s` to see a program where nothing can move.
`02_alu_chain_noforward.s` is the clearest way to see why the forwarding
setting matters. Under **Model**, switch to *Custom latencies* to see a
model-only "what if" (the diagrams always show real Kites).

## Known Kites behaviour the demos work around

- **`la` is broken on the 5-stage processors.** In all `RV5Stage*`
  processors, `auipc` currently produces 0, so `la` yields a wrong address.
  The demos therefore build data addresses with `lui`/`li`. Moving an `la`
  line changes its PC-relative offset, so with this bug the moved `la` gives a
  different wrong value. The Suggestions tab detects that the final registers
  differ and disables Apply.
- **`ecall` does not stop HF/HNF runs.** The hazard-detecting processors never
  call `handle_syscall()`, so an `ecall` neither stops the run nor resets the
  state.
- **The static model counts every block once.** Loops multiply the saving
  (see `03_loop_sum.s`). A choice that only helps the loop exit can rank the
  same as one that helps every iteration.
