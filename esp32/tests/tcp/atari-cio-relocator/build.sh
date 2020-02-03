cl65 -m reloc.map -t atari -C atari.cfg -Osir -o reloc.xex reloc.c rel.s

rm distrib.atr

cp dos.atr distrib.atr
./atr distrib.atr put reloc.xex AUTORUN.SYS

