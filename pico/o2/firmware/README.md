# `pico/o2/firmware` — the RP2040 cartridge

Under construction. What is here today is the part that does **not** depend on
the cartridge bus: the FujiBus wire codec and the cartridge-image mapper. Both
are plain C with no pico-sdk and no TinyUSB, both have desktop regression tests,
and both are compiled into the o2em model as well — so the emulator and the real
cartridge cannot drift on the two things hardest to diagnose on real hardware.

| File | Role |
|---|---|
| `include/fuji_mailbox.h` | the mailbox address map; single source of truth |
| `include/fujibus.h`, `src/fujibus.c` | SLIP + FujiBus codec, shared with the Intellivision cart |
| `include/o2map.h`, `src/o2map.c` | how a raw cart image maps into the program window |
| `host_test/` | desktop gates for both |

```sh
cd host_test
gcc -Wall -Wextra -I.. -I../include -o /tmp/test_fujibus test_fujibus.c ../src/fujibus.c && /tmp/test_fujibus
gcc -Wall -Wextra -I../include     -o /tmp/test_o2map   test_o2map.c   ../src/o2map.c   && /tmp/test_o2map
# and against real images:
/tmp/test_o2map ../../build/*.bin
```

## Still to come

The bus loop, TinyUSB CDC transport and board integration. That part starts from
`wilco2009/Videopac-micro-SD-Cart` or PicoPAC — see the licensing section of the
top-level README; PicoPAC needs a grant from its author for his own work, and the
GPL-3.0/LGPL-3.0 material it vendors comes in under its own terms.

The o2em model in `../emu/` is the working specification for that half: its
`fujinet.c` already implements the mailbox register file, the SEQ/ACKSEQ
interlock, the DBC push receiver and the boot swap against real traffic.
