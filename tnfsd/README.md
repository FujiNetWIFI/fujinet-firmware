tnfsd-fujinet
=============

This is the fork of tnfsd with extensions to help the #FujiNet project.

Building the tnfs daemon
------------------------

Use the command 'make OS=osname'.
The following is valid:

make OS=LINUX       All versions of Linux
make OS=BSD         Use this also for Mac OSX. Has been tested on OpenBSD.
make OS=WINDOWS     All versions of Windows (with MinGW)

If using Windows with cygwin, it's probable you'll need to use
make OS=LINUX instead since Cygwin looks more like Linux than Windows.
You might have to remove the -DENABLE_CHROOT from the Makefile, though
since I'm not sure chrooting is supported under Cygwin.

To make a debug version, use 'make OS=osname DEBUG=yes'. This will add
some extra debugging messages and add the -g flag to the compilation 
options.

