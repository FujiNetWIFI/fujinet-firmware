# tools

Host-side helpers that are not part of either firmware build.

## Layout
| Path | Defines |
|---|---|
| `gdrive-relay/` | Node.js OAuth2 relay that holds the Google client secret so a FujiNet behind NAT can authorise the `GDRIVE` protocol; has its own [README](gdrive-relay/README.md) |
| `onedrive-relay/` | The same relay for the Microsoft identity platform and Graph, behind `ONEDRIVE`; has its own [README](onedrive-relay/README.md) |
| `mac68k/make_hd20_volume.py` | Rebuilds any HFS image into a bootable HD20 volume for the Mac 68k target; needs the `machfs` Python package |

## How it fits
- The relays are the servers that the `GDRIVE` and `ONEDRIVE` adapters in
  [lib/network-protocol/](../lib/network-protocol/) talk to, configured through the
  `[GoogleDrive]` and `[OneDrive]` sections of the configuration file.
- `mac68k/make_hd20_volume.py` is described in `docs/mac68k.md`.
