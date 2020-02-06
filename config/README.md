config
======

This program is provided to the Atari by #FujiNet when it is cold started, and allows the user to configure their networking parameters, and assign host and drive slots.

How to use
==========

Upon cold start of #FujiNet, either by power-on or pressing the RESET button, CONFIG will boot automatically.

Navigation
----------

The following keys work consistenly throughout the program:

* - (UP ARROW) - move the selection bar up.
* = (DOWN ARROW) - move the selection bar down.
* RETURN - Select an item.
* 1-8 - Quickly select a drive slot.
* SHIFT 1-8 - Quickly select a host slot.
* OPTION to boot, see "Booting" below.

Setting up Network
==================

If the network configuration hasn't been completed, if the network connection fails, or if SELECT is pressed on the Atari during boot, then the network configuration screen will be shown, allowing you to select the desired wireless network (SSID), and specify a password. The navigation keys mentioned above can quickly be used to move the selection bar for the network. If the desired network is not displayed in the list, it can be manually specified by pressing 'X', a cursor will appear, allowing you to input the name of the network.

Once a network is selected, the status message will indicate that it is attempting to connect to the selected network. The color of the selection bar will also change to indicate status, typically:

* Red - no connection yet
* Pink - Attempting to connect
* Green - Connection successful

TNFS Host/Device Slots
======================

To facilitate connecting ATR disk images on TNFS servers, CONFIG provides Host Slots and Device Slots, eight of each.

Host Slots allow you to quickly connect to one of eight possible TNFS servers, by host name.

Adding / Editing a Host Slot
----------------------------

You can quickly edit a host slot by selecting it with the arrow keys, or by pressing SHIFT and a number between 1 and 8, followed by pressing E to edit. A cursor will then display, allowing you to edit the entry. Pressing RETURN will commit the edit.

Deleting a Host Slot
--------------------
If a previously filled slot is deleted completely with the DELETE BACK SPACE key, CONFIG will indicate that the device slot is empty.

Selecting a Host Slot
---------------------
Using the navigation keys, you can select a host slot and press ENTER, which will proceed to the Disk Image Selection screen, showing you the disk images available on the selected Host Slot.

Some example TNFS Hosts
-----------------------

While it is easy [to set up a TNFS server](http://spectrum.alioth.net/doc/index.php/TNFS_server). You can use these publically available TNFS servers, as well:

* fujinet.online
* tnfs.atari8bit.net
* irata.online

Device Slots
------------

Device Slots are ATR disk images that are assigned to one of the eight possible Atari disk drives.

### Relationship to Atari Devices

Device slots 1-8 correspond directly to Atari devices D1: to D8:. If a device slot is marked as empty, #FujiNet is not using it, and another disk drive can safely use this device number.

Toggling Between Device and Host Slots
--------------------------------------

Pressing D will switch the navigation bar to the Device Slots, so you can manipulate them. Pressing 1-8 will also quickly switch to Device Slots, and position the navigation bar to the desired slot. Pressing H will toggle the navigation bar back to the host slots. Pressing SHIFT 1-8 will also toggle back to the host slots, as well as place the navigation bar to the desired host slot.

Ejecting a Device Slot
----------------------

When the navigation bar is toggled to Device Slots, pressing an E will eject the currently selected device slot. This will close the ATR disk image on the TNFS server, and FujiNet will no longer control the device slot, allowing it to be used with other Atari disk drives.

Disk Image Selection
====================

Once a Host Slot is selected, the disk images available on that server will be shown. Use the navigation bar to select the desired disk image, followed by RETURN to commit the selection.

The ESC key can be used to abort this screen, and return to the Host/Device Slot selection screen.

Slot Selection
==============

Select desired slot
-------------------

Once a disk image is selected, the Slot Selection screen appears to assign the selected disk image to a given device slot. Use the navigation keys to move the navigation bar to the desired device slot, and press ENTER.

Select desired Read/Write Mode
------------------------------

Disk images can be mounted in one of two modes, by pressing either R or W:

* R, for read only, in this mode, writes are silently ignored, and the disk image can not be altered. Many potential users can open a disk in read-only mode.
* W, for read/write, in this mode, writes are written to the disk image over the network back to the server. Only one user can open a disk in read/write mode; emitting an error if someone already has a disk image opened read/write.

Once the read/write mode has been selected, CONFIG will return to the Host Slots/Device Slots selection screen.

Booting
=======

Once you have made your selections, the OPTION key will mount all of the disk images on the respective host slots into their respective device slots, and cold start the system.

XL/XE systems and BASIC
-----------------------
If you are using an XL/XE system, the OPTION key will of course affect whether the built-in BASIC is enabled or disabled. If you wish to enable the built-in BASIC, release the OPTION key immediately after pressing.

Note to Maintainers
===================

To build:

* run make
* run ./make_disk.sh
* Copy autorun.atr to SPIFFS so it can be dumped to flash.

