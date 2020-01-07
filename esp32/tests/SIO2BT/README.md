# Bluetooth with Fujinet

ESP32 can act as a Bluetooth transceiver.
It supports Serial Port Profile (SPP) and can run as a master.

SIO devices are emulated by the [SIO2BT Android app](https://play.google.com/store/apps/details?id=org.atari.montezuma.sio2bt&hl=en) or [RespeQt PC software](https://github.com/jzatarski/RespeQt/releases) (Windows/Linux/Mac).

Before connecting to the ESP32, devices have to paired. In Android - go to settings, search for Bluetooth devices and select "ATARI Fujinet". Then start the SIO2BT app, which will let you connect to the ESP32.

Pairing on the PC will result in a virtual serial port beeing created. Select this serial port in RespeQT settings and change handshake to "SIO2BT". Opening the port initiates a Bluetooth connection.


You can watch the [video](https://www.youtube.com/watch?v=-43HCN8lWMo) to see it live.
