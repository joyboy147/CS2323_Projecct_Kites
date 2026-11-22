# Demo 03: sum an 8-word array in a loop, then store the result.
# Inside the loop the add uses the value loaded right above it. Moving the
# pointer increment into that gap saves one cycle on every iteration, so
# Kites saves 8 cycles in total even though the static model counts 1.

.data
array:  .word 1, 2, 3, 4, 5, 6, 7, 8
result: .word 0

.text
    lui  x10, 0x10000      # x10 = &array (start of .data)
    addi x11, x0, 8        # iterations left
    addi x6, x0, 0         # running sum
loop:
    lw   x5, 0(x10)
    add  x6, x6, x5        # load-use on x5
    addi x10, x10, 4
    addi x11, x11, -1
    bne  x11, x0, loop
    sw   x6, 0(x10)        # x10 now points at result
