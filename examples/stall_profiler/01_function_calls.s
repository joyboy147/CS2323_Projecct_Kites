# Profiler sample 01: function calls.
# main calls square() three times. Every jal and every return (jalr) flushes
# 2 younger instructions, and square's mul uses the value loaded just above it
# (load-use, charged to the mul line).

.data
args:    .word 3, 5, 7
results: .word 0, 0, 0

.text
main:
    li   s0, 0x10000000    # &args
    addi a1, s0, 0
    jal  ra, square
    sw   a0, 12(s0)
    addi a1, s0, 4
    jal  ra, square
    sw   a0, 16(s0)
    addi a1, s0, 8
    jal  ra, square
    sw   a0, 20(s0)
    j    done
square:
    lw   a0, 0(a1)         # argument
    mul  a0, a0, a0        # load-use on a0
    ret
done:
    nop
