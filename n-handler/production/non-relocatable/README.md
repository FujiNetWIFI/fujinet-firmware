n-handler
=========

This directory contains the N: handler. It is currently written in Atari Macro Assembler (AMAC), and is non-relocatable.

building
========

Convert to ATASCII and use AMAC to assemble, putting disk in D2: with the following options

```
D2:NDEV.ASM,H=NDEV.COM,L=P:,R=F
```

TBD
===

* make relocatable, based on @damosan's work.
* implement burst mode
* figure out how to make it play nice in dos 2 so DUP doesn't write over it.
