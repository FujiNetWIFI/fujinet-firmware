tnfs
====

This was an early test to understand the TNFS protocol. There is no Atari side to this test, it simply connects to the specified TNFS server
and immediately attempts to read a file called autorun.atr, and dump its sectors to the debug terminal.

To use
======

1. Unpack a TNFS server: http://spectrum.alioth.net/doc/index.php/TNFS_server
2. make a directory called data where you unpacked tnfs
3. copy the jumpman.xfd from the tests/tnfs/data folder to where you placed your data folder
4. run "tnfsd data" in a console window.
5. flash tnfs.ino to your device. It should start immediately, showing sector data being dumped to the serial monitor.

