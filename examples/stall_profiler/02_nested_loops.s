# Profiler sample 02: 4 x 5 nested counted loops summing i*j (= 150).
# The inner bne is taken 4 times per outer iteration and the outer bne 3 times:
# 19 taken branches, 3 flushed cycles each.

    li   x5, 4             # i
    li   x7, 0             # sum
outer:
    li   x6, 5             # j
inner:
    mul  x8, x5, x6
    add  x7, x7, x8
    addi x6, x6, -1
    bne  x6, x0, inner
    addi x5, x5, -1
    bne  x5, x0, outer
    li   x9, 0x10000000
    sw   x7, 0(x9)
