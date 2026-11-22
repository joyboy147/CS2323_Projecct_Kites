# Demo 06: Kites' hazard unit compares raw register fields.
# addi x7, x0, 3 does not read x3, but its immediate 3 sits in the rs2 bits,
# so Kites stalls it behind the load of x3. The exact model knows this and
# picks addi x8, x0, 5 (immediate 5, no alias) as the filler instead.

    lui  x10, 0x10000
    lw   x3, 0(x10)
    addi x7, x0, 3         # spurious stall: "reads" x3 through its rs2 bits
    addi x8, x0, 5
    add  x9, x3, x3
