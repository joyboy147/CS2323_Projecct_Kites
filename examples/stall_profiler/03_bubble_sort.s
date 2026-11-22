# Profiler sample 03: bubble sort of 8 words (7 passes x 7 comparisons).
# The comparison branch reads x6 right after it is loaded: a load-use stall on
# every comparison with forwarding.

.data
array: .word 7, 3, 8, 1, 6, 2, 5, 4

.text
    li   x10, 0x10000000
    li   x11, 7            # passes left
outer:
    addi x12, x10, 0       # element pointer
    li   x13, 7            # comparisons left
inner:
    lw   x5, 0(x12)
    lw   x6, 4(x12)
    bge  x6, x5, noswap    # load-use on x6
    sw   x6, 0(x12)
    sw   x5, 4(x12)
noswap:
    addi x12, x12, 4
    addi x13, x13, -1
    bne  x13, x0, inner
    addi x11, x11, -1
    bne  x11, x0, outer
