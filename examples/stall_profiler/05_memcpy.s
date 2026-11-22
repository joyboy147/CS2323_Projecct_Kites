# Profiler sample 05: copy 16 words twice.
# copy_naive stores each word right after loading it (16 load-use stalls with
# forwarding); copy_paired loads two words before storing them (no stalls).

.data
src:  .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
dst1: .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
dst2: .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

.text
    li   a0, 0x10000000    # src
    li   a1, 0x10000040    # dst1
    li   a2, 16
copy_naive:
    lw   t0, 0(a0)
    sw   t0, 0(a1)         # load-use on t0 (store data)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -1
    bne  a2, x0, copy_naive
    li   a0, 0x10000000
    li   a1, 0x10000080    # dst2
    li   a2, 8
copy_paired:
    lw   t0, 0(a0)
    lw   t1, 4(a0)
    sw   t0, 0(a1)
    sw   t1, 4(a1)
    addi a0, a0, 8
    addi a1, a1, 8
    addi a2, a2, -1
    bne  a2, x0, copy_paired
