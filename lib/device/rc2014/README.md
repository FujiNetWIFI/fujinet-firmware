# lib/device/rc2014

The RC2014 device set for `BUILD_RC2014`. Legacy: the bus defines no `FUJI_COMMAND_PACKET`, which
`fujiDevice` requires (#1658).

## How it fits
- Devices implement `rc2014_process(commanddata, checksum)` from
  [lib/bus/rc2014bus/](../../bus/rc2014bus/) (or the unreachable
  [lib/bus/rc2014sio/](../../bus/rc2014sio/)); this directory's name matches neither bus directory.
  Media comes from [lib/media/rc2014/](../../media/rc2014/).

## Build
ESP only, `BUILD_RC2014`; not compiled on PC.

## Notes
- `rc2014Network.h`, `rc2014Network.cpp`: `rc2014Network` predates `NDevice` and still uses
  `ProtocolParser`, which no longer exists.
