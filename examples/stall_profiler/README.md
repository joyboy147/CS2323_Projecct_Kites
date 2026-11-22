# Stall Profiler Samples

These programs exercise the stall profiler in the **Profiler** tab. For every program and every
5-stage mode, the profiler explains every cycle:

`cycles = retired + stall + flush + fill (4) + truncated`

The automated tests (`test/stall_profiler_kites_test.cpp`) check this identity against
Kites' own counters, with zero unclassified cycles, together with the properties listed below.

| File | Shows |
| --- | --- |
| `01_function_calls.s` | `jal`/`ret` flushes (2 cycles each), and a `lw → mul` load-use inside the callee. |
| `02_nested_loops.s` | 4 × 5 nested loops: 19 taken branches, 3 flushed cycles each. The inner `bne` is the top line. |
| `03_bubble_sort.s` | A load-use into the comparison branch on every comparison. The array ends up sorted. |
| `04_fibonacci.s` | Iterative fib(20) = 6765 and recursive fib(8) = 21: many `jal`/`jalr` flushes, and `lw ra → ret` load-use. |
| `05_memcpy.s` | A naive copy (16 load-use stalls: load → store data) next to a paired copy (0 stalls). |
| `06_fp_kernel.s` | FPR load-use with forwarding, and FPR RAW stalls without it (dot product = 11.0). |
| `07_fp_alias_quirk.s` | The Kites quirk where `fadd.s f5` looks like a write of `x5`: a spurious stall in HNF. |
| `08_muldiv.s` | Single-cycle M operations: no stalls with forwarding, RAW stalls without. |
| `09_rv64.s` | 64-bit `ld`/`sd`, `mulw`, `divw`, `sext.w`. `ld → mulw` is a load-use. |
| `10_spurious_imm.s` | Stalls caused only by immediate bits: an I-type `rs2` field and a U-type `rs1` field. |

## Measured cycles and causes

Measured with `tools/headless/kites_profile <file> --table` (real Kites processors, cycle limit
2,000,000). The "causes" column lists the cycles lost to each cause other than the 4 fill
cycles, for HF and HNF.

| Program | HF | HNF | NH_F | NH_NF | Causes (HF / HNF) | Identity |
| --- | ---: | ---: | ---: | ---: | --- | :---: |
| `01_function_calls.s` | 42 | 47 | 39 | 39 | HF: load-use 3, jal flush 8, jalr flush 6 / HNF: RAW no-fwd d=1 8, jal flush 8, jalr flush 6 | yes |
| `02_nested_loops.s` | 157 | 256 | 157 | 230 | HF: branch flush 57 / HNF: RAW no-fwd d=1 98, spurious field alias 1, branch flush 57 | yes |
| `03_bubble_sort.s` | 651 | 820 | 619 | 2,000,000* | HF: load-use 49, branch flush 240 / HNF: RAW no-fwd d=1 210, d=2 8, branch flush 240 | yes |
| `04_fibonacci.s` | 1263 | 1688 | 2,000,000* | 182 | HF: load-use 99, branch 156, jal 136, jalr 134 / HNF: RAW d=1 502, d=2 20, spurious 2, same flushes | yes |
| `05_memcpy.s` | 254 | 330 | 238 | 170 | HF: load-use 16, branch flush 66 / HNF: RAW no-fwd d=1 84, d=2 8, branch flush 66 | yes |
| `06_fp_kernel.s` | 27 | 45 | 23 | 23 | HF: load-use 4 / HNF: RAW no-fwd d=1 16, spurious FP alias 6 | yes |
| `07_fp_alias_quirk.s` | 9 | 13 | 9 | 9 | HF: none / HNF: spurious field alias 2, spurious FP alias 2 | yes |
| `08_muldiv.s` | 15 | 22 | 15 | 15 | HF: none / HNF: RAW no-fwd d=1 6, d=2 1 | yes |
| `09_rv64.s` | 17 | 29 | 16 | 16 | HF: load-use 1 / HNF: RAW no-fwd d=1 12, d=2 1 | yes |
| `10_spurious_imm.s` | 12 | 16 | 10 | 10 | HF: spurious field alias 2 / HNF: RAW no-fwd d=1 2, spurious field alias 4 | yes |

\* Without hazard detection, Kites reads stale registers. The loop counters of these two programs
never reach their exit condition, so the run hits the cycle limit. The profiler reports it as a
truncated run, and the identity still holds. The NH columns are therefore only meaningful for
programs written for that mode, and the profiler lists the unprotected hazards it found.

## Kites behaviour these samples work around

- `la` / `auipc` produce 0 on the 5-stage processors, so data addresses are built with `li`.
- `fmadd.s` (and the other FMA instructions) do not write their destination register, even on
  RVSS. `06_fp_kernel.s` therefore uses `fmul.s` + `fadd.s`.
- `addw`, `subw` and `addiw` do not parse. `09_rv64.s` uses `mulw`, `divw` and `sext.w`.
- With forwarding, Kites forwards an FP result into an integer instruction that reads the
  register with the same number (`07_fp_alias_quirk.s`, HF). The value the add computes is wrong.
