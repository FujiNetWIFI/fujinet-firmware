tnfsBoot
========

This was the first serious test for what was still called #AtariWiFi. It mounts the TNFS server specified in the firmware, and
opens the file /autorun.atr, ready to serve it to the device when requested. The autorun.atr provided is a copy of "Jumpman."

To use
======

1. modify the SSID and PASSWORD for your network here: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsBoot/tnfsBoot.ino#L677
2. modify the address for your TNFS server here: https://github.com/tschak909/atariwifi/blob/master/tests/tnfsBoot/tnfsBoot.ino#L9
3. Set up your tnfs server, if not already done so, and place a copy of autorun.atr in its data directory.
4. Flash the device. The device should immediately attempt to connect to your network and mount the TNFS server. If you are running a copy of tnfsd with debugging enabled (such as the windows version), you will see the mount attempt in the console.
5. plug device into Atari
6. Boot.

