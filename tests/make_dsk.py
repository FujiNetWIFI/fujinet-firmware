#!/usr/bin/env python3
"""Generate self-identifying .DSK images for MediaTypeDSK (RS232) testing.

Each sector's first 4 bytes are a stamp: b'D', head, track & 0xFF, sector & 0xFF.
The rest of the sector is filled with the linear sector index & 0xFF. A read
addressed by CHS can then be checked against its stamp, so a wrong CHS->offset
resolution fails loudly on *content* rather than silently returning plausible
bytes. This mirrors what tests/DiskTypeDSKTests.cpp fabricates in-process; use
this script to produce standalone files for the FujiNet-PC run and the hardware
checklist.

The layout MUST mirror the firmware's dsk_formats[] region-for-region -- a
uniform format writes tracks x sectors_per_track sectors of a fixed size, while
Tarbell DD writes track 0 as 26 x 128 and tracks 1..76 as 51 x 128. If this
generator and the table disagree, the round-trip is validating the wrong image.

Usage:
    ./make_dsk.py ibm_sd out.dsk           # a full IBM 8" SD image
    ./make_dsk.py --tracks 3 ibm_sd out.dsk  # a small (fast) 3-track subset
"""

import argparse
import sys

STAMP = ord('D')


def _write_sector(f, head, track, sector, idx, secsize):
    body = bytes([idx & 0xFF]) * (secsize - 4)
    f.write(bytes([STAMP, head & 0xFF, track & 0xFF, sector & 0xFF]) + body)


def build_uniform(path, tracks, sides, spt, secsize, first=1):
    """Uniform single-region format: tracks x sides x spt sectors of secsize."""
    with open(path, "wb") as f:
        for t in range(tracks):
            for h in range(sides):
                for s in range(first, first + spt):
                    idx = (t * sides + h) * spt + (s - first)
                    _write_sector(f, h, t, s, idx, secsize)


def build_tarbell(path, tracks, first=1):
    """Tarbell DD: track 0 = 26 x 128, tracks 1.. = 51 x 128 (varies COUNT)."""
    with open(path, "wb") as f:
        idx = 0
        for t in range(tracks):
            spt = 26 if t == 0 else 51
            for s in range(first, first + spt):
                _write_sector(f, 0, t, s, idx, 128)
                idx += 1


# name -> (num_tracks, num_sides, spt, secsize) for the uniform formats; Tarbell
# is handled specially. Keep these in lockstep with dsk_formats[] in
# lib/media/rs232/diskTypeDSK.cpp.
UNIFORM = {
    "ibm_sd": (77, 1, 26, 128),   # IBM 8" SD  = 256,256 bytes
    "ibm_dd": (77, 1, 26, 256),   # IBM 8" DD  = 512,512 bytes
    "altair": (77, 1, 32, 137),   # Altair 8"  = 337,568 bytes
    "fdcplus": (2048, 1, 32, 137),  # FDC+ (Altair geom, 2048 trk) = 8,978,432 bytes
}


def main(argv):
    ap = argparse.ArgumentParser(description="Generate self-identifying .DSK images.")
    ap.add_argument("format", choices=list(UNIFORM) + ["tarbell_dd"])
    ap.add_argument("path")
    ap.add_argument("--tracks", type=int, default=None,
                    help="override track count (a small subset builds faster)")
    args = ap.parse_args(argv)

    if args.format == "tarbell_dd":
        tracks = args.tracks if args.tracks is not None else 77
        build_tarbell(args.path, tracks)
    else:
        num_tracks, sides, spt, secsize = UNIFORM[args.format]
        tracks = args.tracks if args.tracks is not None else num_tracks
        build_uniform(args.path, tracks, sides, spt, secsize)

    print(f"wrote {args.path} ({args.format}, {tracks} tracks)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
