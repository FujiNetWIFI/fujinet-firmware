#FujiNet   
=========

A multi-function peripheral built on ESP32 hardware being developed for the Atari 8-bit systems

It currently provides the following devices:

* D: for disk emulation, allowing disk images to be read or written to on SD cards or TNFS servers over the local network or Internet.
* P: for printing emulation, providing printer emulation for various types of popular printers, including Atari-branded, Epson, and other alternatives.
* R: for RS-232 emulation, providing a Wi-Fi modem that can be used by existing communications programs that work with an Atari 850 interface.
* N: providing a network adapter that can talk TCP, HTTP, UDP, and other protocols to other TCP/IP hosts.

This the primary ESP32 firmware project. In addition, there are several closely-related Github projects:

* fujinet-config: Atari 8bit program to configure FujiNet
* fujinet-nhandler: Atari 8bit "N:" device handler
* fujinet-config-tools: Additional Atari 8bit programs to directly control FujiNet
* fujinet-hardware: Schematics and design files for FujiNet hardware

How to contact us outside of GitHub:

There is active discussion and work in Twitter and Discord: https://discord.gg/hhKm9AH

There is also information at AtariAge: https://atariage.com/forums/topic/298720-fujinet-a-wip-sio-network-adapter-for-the-atari-8-bit/
