# Demo 07: 2x2 matrix multiply C = A * B, written naively: every product
# loads its second operand on the line just above it.

.data
mat_a: .word 1, 2, 3, 4
mat_b: .word 5, 6, 7, 8
mat_c: .word 0, 0, 0, 0

.text
    li   x10, 0x10000000   # &mat_a
    addi x11, x10, 16      # &mat_b
    addi x12, x10, 32      # &mat_c
    lw   x5, 0(x10)        # a00
    lw   x7, 0(x11)        # b00
    mul  x13, x5, x7
    lw   x6, 4(x10)        # a01
    lw   x8, 8(x11)        # b10
    mul  x14, x6, x8
    add  x15, x13, x14
    sw   x15, 0(x12)       # c00
    lw   x16, 4(x11)       # b01
    mul  x18, x5, x16
    lw   x17, 12(x11)      # b11
    mul  x19, x6, x17
    add  x20, x18, x19
    sw   x20, 4(x12)       # c01
    lw   x21, 8(x10)       # a10
    mul  x23, x21, x7
    lw   x22, 12(x10)      # a11
    mul  x24, x22, x8
    add  x25, x23, x24
    sw   x25, 8(x12)       # c10
    mul  x26, x21, x16
    mul  x27, x22, x17
    add  x28, x26, x27
    sw   x28, 12(x12)      # c11
