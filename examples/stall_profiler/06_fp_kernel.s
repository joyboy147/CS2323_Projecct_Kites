# Profiler sample 06: single-precision dot product of 4 floats (= 11.0, stored
# with fsw). Each fmul reads an FP register loaded just above it (FPR load-use
# with forwarding), and each fadd reads the product just computed (FPR RAW
# without forwarding). fmul.s + fadd.s are used because Kites' fmadd.s does not
# write its destination register (pre-existing bug, also on RVSS).
# The base register is set two instructions before the first flw: Kites does
# not forward a GPR into an FP load's base register.

.data
xs:  .float 1.0, 2.0, 3.0, 4.0
ys:  .float 0.5, 0.25, 2.0, 1.0
out: .float 0.0

.text
    li     x10, 0x10000000
    li     x11, 0
    li     x12, 0
    flw    f1, 0(x10)
    flw    f2, 16(x10)
    fmul.s f0, f1, f2
    flw    f1, 4(x10)
    flw    f2, 20(x10)
    fmul.s f3, f1, f2
    fadd.s f0, f0, f3
    flw    f1, 8(x10)
    flw    f2, 24(x10)
    fmul.s f3, f1, f2
    fadd.s f0, f0, f3
    flw    f1, 12(x10)
    flw    f2, 28(x10)
    fmul.s f3, f1, f2
    fadd.s f0, f0, f3
    fsw    f0, 32(x10)
