# FujiNet

A multi-function peripheral built on ESP32 hardware being developed for the multiple 8-bit systems

### Please see the [GitHub wiki](https://github.com/FujiNetWIFI/fujinet-platformio/wiki) for documentation and additional details.

### A dedicated web site is also available at https://fujinet.online/

MAJOR ANNOUNCEMENT FOR ANYONE WORKING ON FIRMWARE CODE:

Fujinet-platformio has been ported forward to the new PlatformIO Esp32 3.0. The changes made mean that it no longer builds under 1.12.x or 2.0, so you must upgrade in order to work on this firmware.

To upgrade:

* Select Platforms from PIO Home in Quick Access

* You should see an upgrade notice for Espressif 32. Upgrade it. After the upgrade, you will move from 1.12.x or 2.0 to 3.0.

* Once this is done, delete your .vscode and .pio folders, and re-start vs.code.

-------------------------------------------------

## Generating platformio configuration for your board

See [build.sh documentation](build-sh.md) for full documentation on using build.sh to configure your platformio ini files.

## ATARI

FujiNet currently provides the following devices for the Atari 8-bit system:

* D: for disk emulation, allowing disk images to be read or written to on SD cards or TNFS servers over the local network or Internet.
* P: for printing emulation, providing printer emulation for various types of popular printers, including Atari-branded, Epson, and other alternatives.
* R: for RS-232 emulation, providing a Wi-Fi modem that can be used by existing communications programs that work with an Atari 850 interface.
* N: providing a network adapter that can talk TCP, HTTP, UDP, and other protocols to other TCP/IP hosts.

This is the primary ESP32 firmware project. In addition, there are several related Github projects:

* fujinet-config: Atari 8bit program to configure FujiNet
* fujinet-nhandler: Atari 8bit "N:" device handler
* fujinet-config-tools: Additional Atari 8bit programs to directly control FujiNet
* fujinet-hardware: Schematics and design files for FujiNet hardware

## Coleco ADAM

* Disk and Tape emulation, allowing disk images to be read or written to on SD cards or TNFS servers over the local network or Internet.
* Printing emulation, providing printer emulation for various types of popular printers, including Atari-branded, Epson, and other alternatives.
* A network adapter that can talk TCP, HTTP, UDP, and other protocols to other TCP/IP hosts.

## Apple

* Floppy and Hard drive emulation
* Printing emulation
* An N device that allows Applesoft to use a network adapter that can talk TCP, HTTP, UDP, and other protocols to other TCP/IP hosts.

## Other works in progress

Atari Lynx, IEC devices (Commodore 64, Commander X16), RC2014, S100 and more are coming all the time

### How to contact us outside of GitHub:

There is active discussion and work on Discord: https://discord.gg/7MfFTvD

There are two active threads on the AtariAge forums:  
* Software development discussion in
["#FujiNet - a WIP SIO Network Adapter for the Atari 8-bit"](https://atariage.com/forums/topic/298720-fujinet-a-wip-sio-network-adapter-for-the-atari-8-bit/)  
* Hardware discussion in
["#FujiNet Hardware Discussion"](https://atariage.com/forums/topic/306728-fujinet-hardware-discussion/)

