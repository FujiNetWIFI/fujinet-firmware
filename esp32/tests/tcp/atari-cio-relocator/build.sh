ca65 -l driver.lst -t atari driver.s
cl65 -l reloc.lst -m reloc.map -t atari -C atari.cfg -Osir -o reloc.xex reloc.c driver.o

rm distrib.atr

cp dos.atr distrib.atr
./atr distrib.atr put reloc.xex AUTORUN.SYS

