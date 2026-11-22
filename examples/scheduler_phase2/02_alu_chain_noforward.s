# Demo 02: two independent ALU dependency chains.
# With forwarding (HF) nothing stalls, so there is nothing to gain.
# Without forwarding (HNF) every link waits for write-back; interleaving
# the two chains lets each instruction's producer finish in the meantime.

    addi x5, x0, 12
    add  x6, x5, x5
    add  x7, x6, x6
    addi x8, x0, 20
    add  x9, x8, x8
    add  x18, x9, x9
