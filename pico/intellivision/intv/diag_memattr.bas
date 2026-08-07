' Diagnostic: identical to booted.bas but with the MEMATTR window declared,
' to isolate whether declaring the $8000-$9FFF RAM window (with no actual
' polling loop touching it) is itself what breaks on real hardware.
    ASM MEMATTR $8000, $9FFF, "+RWN"
    PRINT "MEMATTR OK"
halt:
    WAIT
    GOTO halt
