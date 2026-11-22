# Demo 05: pseudo-instructions, comments and blank lines survive "Apply".
# li with a large immediate expands to lui + addi; the pair stays one unit
# and only whole source lines are permuted. (la is avoided on purpose: the
# Kites 5-stage processors currently compute auipc as 0, so la gives a wrong
# address there. li of the data address works on every processor.)

.data
values: .word 10, 20, 30

.text
main:
    li   x10, 0x10000000   # &values (lui)

    lw   x5, 0(x10)        # first value
    add  x6, x5, x5        # load-use on x5
    li   x7, 0x12345       # lui + addi, independent

    lw   x8, 4(x10)        # second value
    add  x9, x8, x6        # load-use on x8
    li   x12, 1000         # independent
