# Demo 04: every stall here is real, but nothing may move to hide it.
# Expected: "No safe reordering lowers the stall count".

    lui  x10, 0x10000      # every memory access needs x10 (RAW)
    lw   x5, 0(x10)
    add  x6, x5, x5        # needs x5 from the load (RAW): stalls
    sw   x6, 4(x10)        # needs x6 (RAW)
    lw   x7, 8(x10)        # may not pass the store above (memory order)
    addi x5, x7, 1         # needs x7 (RAW); also rewrites x5, so it must stay
                           # after the add that reads x5 (WAR) and after the
                           # load that wrote it (WAW)
