# Profiler sample 08: M-extension chains on loaded operands.
# Kites executes mul/div in one cycle, so mul -> use is an ordinary RAW:
# no stall with forwarding, 2 / 1 cycles without.

.data
operands: .word 1000, 7, -45

.text
    li    x10, 0x10000000
    lw    x5, 0(x10)       # 1000
    lw    x6, 4(x10)       # 7
    lw    x7, 8(x10)       # -45
    mul   x8, x5, x6       # 7000
    mulh  x9, x8, x8       # 0
    div   x11, x8, x6      # 1000
    rem   x12, x5, x6      # 6
    divu  x13, x5, x6      # 142
    mul   x14, x7, x6      # -315
    div   x15, x14, x7     # 7
