# lib/device/sio

The Atari device set for `BUILD_ATARI`: every SIO device the firmware presents to the Atari,
built on the shared bases in [lib/device/](../).

## Layout
| File | Defines |
|---|---|
| `sioFuji.h`, `sioFuji.cpp` | `sioFuji : fujiDevice` (`.atr` images, Atari lobby); defines `platformFuji` and sets `theFuji`; owns the cassette and netstream devices and the `sioNetwork` instances; `setup()` registers the disks, networks, cassette, CP/M and MIDI devices on the bus |
| `disk.h`, `disk.cpp` | `sioDisk`, the `DISK_DEVICE`; `mount()` routes CAS and WAV files to the cassette and otherwise picks `MediaTypeATR`, `MediaTypeATX` or `MediaTypeXEX` |
| `sioNetwork.h`, `sioNetwork.cpp` | `sioNetwork : NDevice` |
| `sioClock.h`, `sioClock.cpp` | `sioClock : fujiClock`; defines `platformClock` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `sioPrinter` and `printerlist`; defines `fnPrinters` |
| `modem.h`, `modem.cpp` | `modem`, the 850-style R: device (Hayes commands, telnet, sniffer) |
| `cassette.h`, `cassette.cpp`, `cassetteTrail.h`, `cassetteTrail.cpp`, `cassetteRewindRequest.h` | `sioCassette` with its `softUART` bit-banger, plus the `CassetteTrail` rewind history and `CassetteRewindRequest` |
| `cassetteFSK.h`, `cassetteFSK.cpp` | `cassette_fsk_play_run()`, playing one A8CAS `fsk ` run through the RMT |
| `cassetteFSKLoader.h`, `cassetteFSKLoader.cpp` | `CassetteFSKLoader`, the task that loads an `fsk ` run progressively into PSRAM |
| `cassetteFSKRead.h`, `cassetteFSKRead.cpp` | `fsk_resilient_read()`, reading a file that can lose its position |
| `netstream.h`, `netstream.cpp` | `sioNetStream`, UDP MIDI streaming |
| `pclink.h`, `pclink.cpp` | `sioPCLink`, the PCLink file server over the SD card |
| `voice.h`, `voice.cpp` | `sioVoice`, speech through [lib/sam/](../../sam/) |
| `siocpm.h`, `siocpm.cpp` | `sioCPM`, CP/M through [lib/runcpm/](../../runcpm/) |

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/sio/](../../bus/sio/) and implements
  `sio_status()` and `sio_process()`. Disk images come from [lib/media/atari/](../../media/atari/);
  printers use [lib/printer-emulator/](../../printer-emulator/).
- `sioFuji` overrides `setup()`, `set_additional_direntry_details()`, `announce_rotation()`,
  `insert_boot_device()`, `fujicore_mount_disk_image_success()`, `fujidev_copy_file()`,
  `fujidev_set_device_fullpath()`, `appkey_read()`, `appkey_write()`, `fujicmd_net_scan_networks()`
  and `qr_encode()`. `sioNetwork` overrides the prefix, seek, tell, query and read hooks. `sioClock`
  overrides `fujidev_read_tz()`.
- `src/main.cpp` adds the Fuji, clock, MIDI, PCLink, printer, modem, voice and CP/M devices;
  `sioFuji::setup()` adds the rest.

## Build
ESP: globbed by `src/CMakeLists.txt`; compiled only under `BUILD_ATARI`, except `cassetteTrail.cpp`
and `cassetteFSKRead.cpp`, which have no guard; `cassetteFSK.cpp` and `cassetteFSKLoader.cpp` also need
`ESP_PLATFORM`. PC: `FUJINET_TARGET=ATARI` lists every file; the cassette trail, rewind request, FSK
read recovery and DSTATS logic have tests under [tests/](../../../tests/).
