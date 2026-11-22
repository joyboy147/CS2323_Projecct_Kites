# Demo 01: two independent load-use pairs.
# Each add reads the register loaded by the line just above it, so the
# forwarding pipeline (HF) stalls once per pair. Hoisting the second load
# between the first load and its use hides both bubbles.

.data
values: .word 5, 7

.text
    lui  x10, 0x10000      # x10 = start of .data (0x10000000)
    lw   x5, 0(x10)
    add  x6, x5, x5        # load-use on x5
    lw   x7, 4(x10)
    add  x8, x7, x7        # load-use on x7
