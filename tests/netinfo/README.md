netinfo
=======

Netinfo connects to the SSID specified in the firmware, and attempts to read network information using a custom command.

To use
======

1. Modify this line, to change SSID and password: https://github.com/tschak909/atariwifi/blob/master/tests/netinfo/netinfo.ino#L371
2. Flash firmware to unit
3. Update SPIFFS using Sketch Data Upload Tool, to upload the ATR image
4. Boot
