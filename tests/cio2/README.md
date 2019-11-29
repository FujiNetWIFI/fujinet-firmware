cio2
====

This particular example demonstrates writing to a TCP socket from SIO.

It implements the needed read-only disk commands to boot everything, as well as:

* 'c' to connect to a TCP socket
* 'w' to write to a TCP socket
* 'd' to disconnect from a TCP socket.

To use:
=======

* Flash onto your device
* Flash autorun.atr onto your SPIFFS
* Boot device.

Set up a listening socket somewhere, e.g. using netcat
======================================================

```
nc -vkl 2000
```

From BASIC
==========

```
OPEN #1,12,0,"N:www.xxx.yyy.zzz:pppp"
```

where:
* www.xxx.yyy.zzz is an IP address of your listening server
* pppp is the port number listened to above

If all is well, the computer will reply back with:

```
READY
```

You can then PRINT to the listening server:

```
PRINT #1;"HELLO WORLD!<ctrl-m><ctrl-j>";
```

or send single bytes with PUT

```
PUT #1,123
```

You can then close the socket with:

```
CLOSE #1
```

