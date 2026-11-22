# Profiler sample 07: a documented Kites quirk (profiler only).
# Kites' control unit sets reg_write for FP opcodes, so "fadd.s f5" looks like a
# write of x5 to the hazard unit. Without forwarding (HNF) the add below stalls
# although it does not depend on the fadd. With forwarding (HF) there is no
# stall, but Kites forwards the FP result into the integer add (wrong value).

    li     x5, 7
    li     x1, 1
    li     x2, 2
    fadd.s f5, f1, f2
    add    x6, x5, x1
