tnfsDirList
===========

This test sends the / directory of your TNFS server over to the Atari, one directory entry at a time. The test program will read these
entries and print them, one per line, with a max size of 36 characters, so it fits on one line.

To use:
=======

1. Modify SSID and Password in tnfsDirList.ino: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsDirList/tnfsDirList.ino#L641
2. Also modify the TNFS server entry: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsDirList/tnfsDirList.ino#L9
3. Set up TNFS server, if you haven't already done so.
4. Copy a bunch of atr images into the root (or other files, really, doesn't matter.)
5. Use the ESP8266 Sketch Data Upload to upload the tnfsDirList test program onto the SPIFFS flash
6. Flash the firmware onto the device. It should attempt to connect to your network and mount the TNFS server, immediately.
7. Attach to Atari
8. Boot. You should see a list of files come across the display, one per line, followed by a jump to DOSVEC (so either BASIC or self-test)
