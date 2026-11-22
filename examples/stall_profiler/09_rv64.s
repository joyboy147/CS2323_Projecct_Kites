# Profiler sample 09: 64-bit values with ld/sd, mulw, divw and sext.w
# (addw/subw/addiw are avoided: the Kites assembler does not parse them).

    li     x10, 0x10000000
    li     x5, 0x123456789
    sd     x5, 0(x10)
    ld     x6, 0(x10)      # load-use into mulw
    mulw   x7, x6, x6
    li     x8, 3
    divw   x9, x7, x8
    sext.w x11, x6
    sd     x9, 8(x10)
