# Demo 08: 4-element dot product that reuses x5/x6 for every pair.
# Register reuse creates WAR/WAW dependences: the next pair's loads may not
# move above the multiply that still reads x5/x6, so most stalls remain.
# Compare with demo 07, which uses a fresh register per value.

.data
vec_x:  .word 1, 2, 3, 4
vec_y:  .word 5, 6, 7, 8
dot:    .word 0

.text
    li   x10, 0x10000000   # &vec_x
    addi x11, x10, 16      # &vec_y
    lw   x5, 0(x10)
    lw   x6, 0(x11)
    mul  x7, x5, x6
    lw   x5, 4(x10)
    lw   x6, 4(x11)
    mul  x8, x5, x6
    add  x7, x7, x8
    lw   x5, 8(x10)
    lw   x6, 8(x11)
    mul  x8, x5, x6
    add  x7, x7, x8
    lw   x5, 12(x10)
    lw   x6, 12(x11)
    mul  x8, x5, x6
    add  x7, x7, x8
    addi x12, x10, 32      # &dot
    sw   x7, 0(x12)
