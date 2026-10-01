# lib/device/fujiClock

The shared clock device: APETime-compatible time and time-zone commands, plus the formatters that
turn the current time into the layouts the host programs expect.

## Layout

| File | Defines |
|---|---|
| `fujiClock.h`, `fujiClock.cpp` | `fujiClock`; static formatters `fujicore_time_iso`, `fujicore_time_simple`, `fujicore_time_prodos`, `fujicore_time_apetime`, `fujicore_time_simple_hundredths`, `fujicore_time_sos`; `valid_timezone`, `system_tz`, `tz_to_use` |

## How it works

- `processCommand()` dispatches `APETIME_*` commands. Buses with a single command namespace call
  `dispatch()`; SmartPort, whose status and control calls are separate, uses `dispatch_read()` and
  `dispatch_write()`.
- `fujidev_read_tz()` is the one pure virtual: how the platform reads the requested time zone
  from the packet. `fujidev_alt_requested()`, `fujidev_canonical_command()`, `reject_command()`
  and `string_needs_null()` are optional hooks.

Subclasses: `sioClock`, `iwmClock`, `drivewireClock`, `rs232Clock` and `adamClock`, each defining
the global `platformClock` in its bus directory under [lib/device/](../). The IEC clock is a bare
`IECDevice` and does not use this class.

## How it fits

- Registered on `SYSTEM_BUS` with the `CLOCK` device ID by `main_setup()` in [src/](../../../src/)
  or by the platform's `<bus>Fuji::setup()`; the DriveWire bus calls `platformClock` directly.
- Time-zone strings and civil-time math come from `fn_time` in [lib/utils/](../../utils/); the
  default zone is `Config.get_general_timezone()`.
- Covered by the `no_build_ifdefs_in_fujidevice` ctest.

## Build

Compiled into every ESP target and every PC target.
