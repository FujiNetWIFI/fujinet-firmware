' Diagnostic: same MEMATTR window as Test B, but this time actually POKE and
' PEEK one address in it -- the simplest possible "touch the window" test,
' isolating whether ANY access to $8000-$9FFF breaks things on real
' hardware, independent of the mailbox protocol's loop/handshake logic.
    ASM MEMATTR $8000, $9FFF, "+RWN"
    PRINT "BEFORE POKE"
    POKE $9800,42
    x=PEEK($9800)
    PRINT AT 20,"AFTER: "
    PRINT AT 27,<3>x
halt:
    WAIT
    GOTO halt
