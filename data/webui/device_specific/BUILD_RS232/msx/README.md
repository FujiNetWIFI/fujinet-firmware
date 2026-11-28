# msx

The files that NEW DISK puts on a formatted MSX disk image. They are copied unchanged from the
[Nextor](https://github.com/Konamiman/Nextor) releases, and `LICENSE.md` must ship with them.

- `bootsect.bin`: sector 0 of `nextor.dsk` from "Tools disk image and ZIP file for Nextor 3.0".
  The firmware rewrites its BPB and volume serial for each image. Its boot code loads
  `MSXDOS.SYS` on an MSX-DOS 1 disk ROM. Nextor and MSX-DOS 2 kernels load their own system
  file.
- `msxdos/`: `MSXDOS.SYS` and `COMMAND.COM`, release `msxdossys-v1.03-commandcom-v1.11`.
- `nextor/`: `NEXTOR.SYS` and `COMMAND3.COM`, release `commandcom-nextorsys-v3.0`. A Nextor
  disk also gets the MSX-DOS 1 files, which Nextor boots in MSX-DOS 1 mode on a machine
  without a memory mapper.
