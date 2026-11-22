# Profiler sample 10: stalls caused only by immediate bits.
# Kites' hazard unit compares the raw rs1/rs2 fields of the next instruction:
#  - addi x7, x0, 3 has 3 in its rs2 field (I-type immediate);
#  - lui x9, 0x18 has 3 in its rs1 field (U-type immediate bits 19:15).
# Neither reads x3, but both stall behind "lw x3".

    li   x10, 0x10000000
    lw   x3, 0(x10)
    addi x7, x0, 3
    lw   x3, 4(x10)
    lui  x9, 0x18
    add  x12, x3, x3
