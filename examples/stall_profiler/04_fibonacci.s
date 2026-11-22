# Profiler sample 04: iterative fib(20) (= 6765 in x7) and recursive fib(8)
# (= 21 in x8). The recursion spends many cycles on jal/jalr flushes, and the
# return address is reloaded right before ret (load-use into jalr).

    li   sp, 0x10001000    # stack inside .data space
    li   x5, 0             # fib(0)
    li   x6, 1             # fib(1)
    li   x9, 19            # iterations
iterate:
    add  x7, x5, x6
    addi x5, x6, 0
    addi x6, x7, 0
    addi x9, x9, -1
    bne  x9, x0, iterate
    li   a0, 8
    jal  ra, fib
    addi x8, a0, 0
    j    done
fib:
    li   t0, 2
    blt  a0, t0, fib_base
    addi sp, sp, -12
    sw   ra, 0(sp)
    sw   a0, 4(sp)
    addi a0, a0, -1
    jal  ra, fib
    sw   a0, 8(sp)
    lw   a0, 4(sp)
    addi a0, a0, -2
    jal  ra, fib
    lw   t1, 8(sp)
    add  a0, a0, t1
    addi sp, sp, 12
    lw   ra, -12(sp)
    ret                    # load-use on ra
fib_base:
    ret
done:
    nop
