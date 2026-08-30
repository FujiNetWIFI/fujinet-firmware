# RS232 MediaTypeFloppy test images

Self-identifying `.DSK` images, one per firmware floppy format, for validating
`MediaTypeFloppy` on real hardware over a **local** TNFSD (no flaky remote host).

Each sector's first 4 bytes are a stamp — `'D', head, track&0xFF, sector&0xFF` —
and the rest is the linear sector index `&0xFF`. So any CHS a host addresses can
be checked against its own stamp: a wrong CHS→offset resolution fails on
*content*, not silently. Regenerate any file with `tests/make_dsk.py`.

## Files (regenerate; not committed)

| fmt | file             | geometry                         | size (bytes) |
|----:|------------------|----------------------------------|-------------:|
| 0   | `ibm_sd.dsk`     | 77 × 26 × 128, single-sided      |      256,256 |
| 1   | `ibm_dd.dsk`     | 77 × 26 × 256, single-sided      |      512,512 |
| 2   | `altair.dsk`     | 77 × 32 × 137, single-sided      |      337,568 |
| 3   | `fdcplus.dsk`    | 2048 × 1 × 4384, single-sided    |    8,978,432 |
| 4   | `tarbell_dd.dsk` | t0: 26×128, t1–76: 51×128        |      499,456 |

The `fmt` column is the low 6 bits of the `fmttype` byte in a READ/WRITE. OR in
`0x40` (`FMT_MODE_TRACK`) to transfer a whole track instead of one sector.

## Suggested mount plan (drive N = fmt N)

RS232 disks are device ids `0x31`..`0x3F` (D1..D15). Mounting each format on its
own drive lets a single host run exercise them all:

| drive | device id | image            |
|------:|----------:|------------------|
| D1    | `0x31`    | `ibm_sd.dsk`     |
| D2    | `0x32`    | `ibm_dd.dsk`     |
| D3    | `0x33`    | `altair.dsk`     |
| D4    | `0x34`    | `fdcplus.dsk`    |
| D5    | `0x35`    | `tarbell_dd.dsk` |

Note: D1 (`0x31`) currently holds the real CP/M image on the rig. If you want to
keep that mounted, shift these to D2–D6 and tell me the device ids you used.

## Regenerating

```sh
cd test-images/rs232-floppy
python3 ../../tests/make_dsk.py ibm_sd     ibm_sd.dsk
python3 ../../tests/make_dsk.py ibm_dd     ibm_dd.dsk
python3 ../../tests/make_dsk.py altair     altair.dsk
python3 ../../tests/make_dsk.py fdcplus    fdcplus.dsk
python3 ../../tests/make_dsk.py tarbell_dd tarbell_dd.dsk
```

Add `--tracks N` for a small, fast subset of any format.
