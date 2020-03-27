#fujinet   
=========

A multi-function peripheral being developed for the Atari 8-bit systems, built upon ESP8266 and ESP32 hardware.

It currently provides the following devices:

* D: for disk emulation, allowing disk images to be read or written to on TNFS servers over the local network or internet.
* P: for printing emulation, providing printer emulation for Atari 820, 822 and 1027 printers, outputting to a PDF stream that can be output to modern printers...
* R: for RS-232 emulation, providing a Wi-Fi modem that can be used by existing communications programs that work with an Atari 850 interface.
* N: providing a network adapter that can talk TCP, HTTP, UDP, and other protocols to other TCP/IP hosts.

The current Filesystem can be divided into the following areas:

* esp32 - The ESP32 tests are going here, written in Arduino.
* platformio - This is where the production firmware is being developed. It is almost ready.
* config - Source code for the CONFIG program that is stored in SPIFFS, to allow the Atari to configure #FujiNet.
* build-tools - Source code for various tools used by the maintainers.
* diagnostic-tools - Source code used for various diagnostic tools used by maintainers.

How to contact us, outside of GitHub:

There is active discussion and work in twitter, and discord: https://discord.gg/hhKm9AH

There is also information at AtariAge: https://atariage.com/forums/topic/298720-fujinet-a-wip-sio-network-adapter-for-the-atari-8-bit/

