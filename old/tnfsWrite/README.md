tnfsWrite
=========

This was the second amazing moment, seeing reading and writing working to a disk image mounted on a TNFS server. This test has a DOS 2.5
image, that can have files read to and written to, e.g. from BASIC or elsewhere. It is almost a complete disk implementation, only
really missing formatting.

To use
======

1. Modify tnfsWrite.ino to change SSID and Password: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsWrite/tnfsWrite.ino#L857
2. Change TNFS server address: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsWrite/tnfsWrite.ino#L9
3. Set up TNFS server, if needed. Copy the autorun.atr from data/ into your TNFS server's serving root.
4. Flash to your device. It should immediately connect to your wireless network, and mount the TNFS server, and open autorun.atr
5. Boot the Atari, it should boot into DOS 2.5
6. Read and write to the virtual disk, e.g. save a BASIC program, use duplicate disk to replace the disk img, whatever.
7. Freak out that this is happening over a network.

