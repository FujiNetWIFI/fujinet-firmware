cio3
====
This particular example demonstrates TCP reading, writing, server, and connect usage.

It implements all of the commands from cio2, as well as:

* 'l' to listen to a connection
* 'a' to accept a pending listened connection
* 'r' to read from a TCP socket.

To use
======
* Flash onto your device
* Flash autorun.atr onto your SPIFFS
* Boot device

To use as a client
==================
See '''cio2''' for setup instructions, but now you can also read from a socket using INPUT, after typing something on other end:


```
DIM A$(99);
INPUT #1,A$
? A$

```

To use as a server
==================

First tell the #FujiNet that it needs to listen on a specific port, such as port 2000.

```
OPEN #1,12,128,"N:2000"
```

At this point, a connection can be made by another host.

Then you must tell the system to accept the connection. (There is a bug where if the accept times out, the fujinet crashes until reset):

```
XIO 97,#1,12,128,"N:"
```

The 97 is equivalent to ASCII 'a.'

Once the system reports READY, you will be able to send and receive to and from the socket:

```
PRINT #1;"Hello to whoever connected!<ctrl-m><ctrl-j>"
```

If you type something on the other end, you can get a status report on the incoming # of bytes:

```
STATUS #1,A
```

A will contain the current error code (which right now is 1), and four bytes will be written to DVSTAT.

```
? PEEK(746):REM THE LOW BYTE ON # OF BYTES AVAILABLE
? PEEK(747):REM THE HIGH BYTE ON # OF BYTES AVAILABLE
? PEEK(748):REM THE NETWORK CONNECTION STATUS - 3 if WIFI IS CONNECTED
? PEEK(749):REM TBD
```

If there are bytes waiting, you can get them either with:

```
GET #1,A:REM NEXT BYTE IN A
```

or

```
INPUT #1,A$:REM NEXT EOL TERMINATED STRING IN A$
```

ERROR- 136 will be reported if there are no more bytes left in buffer on an attempted GET or INPUT.

As with CIO2, closing the IOCB will terminate the TCP connection.

```
CLOSE #1
```