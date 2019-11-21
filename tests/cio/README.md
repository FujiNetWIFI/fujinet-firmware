CIO
===

This is a test to see:

* If a CIO handler could be written in C.
* To see what would be involved in doing a simple XIO operation.

Usage
=====

1. Use Sketch Data Upload tool to upload autorun.atr to SPIFFS
2. Flash cio.ino to device.
3. boot Atari
4. Type in the following BASIC code to see the result:

```
XIO #1,254,0,0,"N:"

FOR X=1536 to 1555:? CHR$(PEEK(X));:NEXT X
```

If all goes well, you should see:

```
Hello from Fujinet!
```

followed by a READY prompt.