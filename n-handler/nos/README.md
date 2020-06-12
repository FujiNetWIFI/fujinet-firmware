nos
===

A proof of concept which takes the n-handler, and turns it into a boot disk.

The CP needs to be written.

How CP needs to work
====================

Prompt:

```
N1: <the output of NPWD> <EOL>
[] <-- cursor
```

A mock image has been made (everything after DOS):
![NOS Mock-up](https://i.imgur.com/3NoZt05.jpg)

Commands (three character minimum, any other characters ignored)

* CAR: Jump back to cartridge.
* CWD: Change Directory (call SIO command $2C)
* COP: Copy File (call SIO command $32)
* DIR: Open path directory via CIO, append /* to filename, return output until EOF.
* DEL: Delete File (Call SIO command $21)
* LOA: Call a standard binary loader given filename. (needs to be added)
* LOC: LOCK a file (Call SIO command $23)
* MKD: Make directory (Call SIO command $2A)
* RMD: Remove directory (Call SIO command $2B)
* UNL: UNLOCK a file (Call SIO command $24)

Any parameters AFTER the command need to be copied into the 256 byte rx/tx buffer, as is, to be passed as parameters to the command.

If a command is not recognized, it is assumed to be an extrensic command ending in .COM, and loader should be called.

If anyone can help, jump into nos.s label GODOS, and please help flesh this out.
-Thom
