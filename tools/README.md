# tools

Host-side helpers that are not part of either firmware build.

## How it fits
- The relays are the servers that the `GDRIVE` and `ONEDRIVE` adapters in
  [lib/network-protocol/](../lib/network-protocol/) talk to, configured through the
  `[GoogleDrive]` and `[OneDrive]` sections of the configuration file.
- `mac68k/make_hd20_volume.py` is described in `docs/mac68k.md`.

## Notes
- [gdrive-relay/](gdrive-relay/) and [onedrive-relay/](onedrive-relay/) each have their own README.
- `mac68k/make_hd20_volume.py` needs the `machfs` Python package.
