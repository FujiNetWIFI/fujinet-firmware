# RS232 Firmware — `MediaTypeDSK` (CP/M 8" floppy disk images)

This document is the implementation plan for a new RS232 media type,
`MediaTypeDSK`, that serves **bit-exact raw dumps of real 8" (and 8"-class)
floppy disks** — IBM, Altair, FDC+ — to a CP/M host, addressed the way the host's
disk BIOS already thinks: by **head / track / sector**, with the disk's **format** named
explicitly on every access.

The design is pinned by these hard constraints:

- **The `.DSK` image is a pure sector dump. No header, no sidecar, no modification —
  ever.** These images are copies of physical media and must be writable back to a real
  floppy unchanged. Nothing may be prepended, appended, or stored beside them.
- **The host's CP/M BIOS is the geometry authority.** Its read/write sector routines know
  the disk's format, so every command carries `head / track / sector / fmttype`.
- **The ESP32 holds the format table.** `fmttype` indexes a firmware-baked table of known
  floppy geometries; the FujiNet turns CHS into a byte offset into the raw dump. Geometry
  is **never inferred** from the image (not from a header, not from size, not at mount).
- **`MediaTypeImg` is not touched.** `MediaTypeDSK` is a new `MediaType` subclass beside
  `MediaTypeImg` and `MediaTypeROM` — it inherits the base `MediaType`, not `MediaTypeImg`,
  so none of the flat-512 behavior comes along. The one shared device edit (§6) is
  behavior-preserving for `MediaTypeImg` by construction.

The design body (§1–12) is the plan of record. Reference material this design draws on —
the existing RS232 disk layer (Appendix A), prior-art geometry-aware media types elsewhere
in the codebase (Appendix B), and the design alternatives these constraints rule out
(mount-time geometry, `.cfg` sidecars, size-guessing — Appendix C) — is collected in the
appendices at the end rather than in the body.

All paths are relative to the repo root.

---

## 1. Why a new type (and not `MediaTypeImg`)

`MediaTypeImg` treats every disk as a flat array of 512-byte LBA sectors
(`diskTypeImg.cpp`): `_sector_to_offset(n) = n * 512`, `mount()` sets
`_disk_num_sectors = size / 512`, and it inherits the base `sector_size()` that returns a
flat `512`. That is correct for the images it serves and must stay exactly as is.
(Appendix A.2 breaks that path down in full.)

CP/M 8" floppies don't fit that model:

- Geometry is **tracks × sectors-per-track**, and sector sizes are **not 512** — 128, 256,
  137 bytes across the five initial formats. The 137-byte Altair sector doesn't
  even tile into 512.
- Forcing those onto 512-byte LBA blocks means **deblocking** on the 8-bit host: a 512-byte
  block buffer in scarce RAM plus arithmetic to split/merge sectors across block
  boundaries — code a boot ROM can't spare.

So `MediaTypeDSK` is a new subclass of the base `MediaType`, constructed by its own
`case` in the mount switch exactly like `MediaTypeROM`. It never shares offset math,
sizing, or sector counts with `MediaTypeImg`; the two share only the base-class plumbing all
media types share.

---

## 2. The layers (only the media layer is new)

```
Host computer (CP/M BIOS builds head/track/sector/fmttype)
   │  (FujiBusPacket over the RS232 link)
   ▼
lib/bus/rs232/rs232.cpp        systemBus  — transport, transaction_* primitives
   │
   ▼
lib/device/rs232/disk.cpp      rs232Disk  — command dispatch, buffer plumbing
   │  _disk->read(sector,…) / _disk->write(sector,…)
   ▼
lib/media/rs232/diskType*.cpp  MediaType  — image-format logic (NEW: MediaTypeDSK)
```

- **Bus** (`lib/bus/rs232/`): owns the link and the `transaction_*` primitives. Commands
  arrive as a `FujiBusPacket` with a `fujiCommandID_t command()` and typed params
  `param(0..n)`. **No bus change** — it moves whatever byte count the device hands it.
  (The primitives are listed in Appendix A.3.)
- **Device** (`lib/device/rs232/disk.cpp`): class `rs232Disk`, holds a single
  `MediaType *_disk`. **One behavior-preserving edit** (§6): the device extracts the
  packet's typed params into a bus-agnostic array and passes them to the media type's
  addressing hook, instead of hard-wiring `param(0)`. The media layer never sees a
  `FujiBusPacket` — it stays independent of the bus protocol.
- **Media** (`lib/media/rs232/`): base `MediaType` (`diskType.h/.cpp`), plus
  `MediaTypeImg`, `MediaTypeROM`, and the new **`MediaTypeDSK`**. (The base interface is
  reproduced in Appendix A.1.)

---

## 3. The two hooks a media type must get right

The device layer is format-agnostic. It only ever asks a media type two things:

1. **`sector_size(sectornum)`** — how many bytes this sector holds. The device uses it to
   size the host transfer (`disk.cpp:49`, in `rs232_write`).
2. **`read()` / `write()`** — map an addressed sector to a byte offset in the backing file
   and transfer `sector_size` bytes into/out of `_disk_sectorbuff`.

Read path — `rs232Disk::rs232_read()` (`disk.cpp:20`):

```cpp
SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
uint32_t readcount;
bool err = _disk->read(sector, &readcount);
SYSTEM_BUS.transaction_send(_disk->sector_buffer(), readcount, err);   // ← accessor, §3
```

Write path — `rs232Disk::rs232_write()` (`disk.cpp:41`):

```cpp
SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
uint16_t sectorSize = _disk->sector_size(sector);          // ← how many bytes to expect
memset(_disk->sector_buffer(), 0, _disk->sector_buffer_size());
if (SYSTEM_BUS.transaction_get(_disk->sector_buffer(), sectorSize)) {
    if (_disk->write(sector, verify) == false) {
        SYSTEM_BUS.transaction_success();
        return;
    }
}
SYSTEM_BUS.transaction_error();
```

### The staging buffer — per-type, not one size for all

The largest single sector is IBM 8" DD's **256 bytes**, but **track mode** (§4.3) transfers a
whole track at once — up to an IBM 8" DD track's 26 × 256 = **6656 bytes** — so the staging
buffer must hold the larger of the two, i.e. the largest track.
But the base-class member `_disk_sectorbuff[DISK_SECTORBUF_SIZE]` (`diskType.h:72`) is
inherited by **every** RS232 media type, so simply raising `DISK_SECTORBUF_SIZE` (512) would
force a 6.5 KB buffer onto `MediaTypeImg` and `MediaTypeROM`, which never need more than 512.
It would also enlarge the `uint8_t buf[DISK_SECTORBUF_SIZE]` stack temp in
`MediaTypeROM::push_stream()` (`diskTypeROM.cpp:65`). Neither should pay for the floppy track.

Instead, **leave `DISK_SECTORBUF_SIZE` at 512 and expose the buffer through a virtual
accessor**, so each type supplies a buffer of the size *it* needs. The device uses the
accessor rather than reaching into the member directly:

```cpp
// base MediaType (diskType.h) — default: the existing 512-byte member
virtual uint8_t *sector_buffer()       { return _disk_sectorbuff; }
virtual uint32_t sector_buffer_size()  { return DISK_SECTORBUF_SIZE; }   // 512
```

```cpp
// MediaTypeDSK — its own buffer, sized for the largest transfer; nobody else carries it
#define DSK_MAX_SECTOR_SIZE 256                     // IBM 8" DD single sector
#define DSK_MAX_TRACK_SIZE  6656                    // IBM 8" DD track: 26 x 256 (track mode)
#define DSK_BUFFER_SIZE     DSK_MAX_TRACK_SIZE   // holds a sector OR a whole track
uint8_t  _dsk_buff[DSK_BUFFER_SIZE];
uint8_t *sector_buffer()       override { return _dsk_buff; }
uint32_t sector_buffer_size()  override { return sizeof(_dsk_buff); }
```

For `MediaTypeImg` / `MediaTypeROM` the accessor returns the same base member at the same 512
size, so their behavior is byte-identical and **neither `diskTypeImg.cpp` nor
`diskTypeROM.cpp` changes at all** — `DISK_SECTORBUF_SIZE` stays 512, and the ROM stack temp
stays 512. `MediaTypeDSK` uses its own `_dsk_buff` in `read()`/`write()`. The device's
switch from `_disk->_disk_sectorbuff` to `_disk->sector_buffer()` is part of the §6 device
edit and is behavior-preserving for the flat path.

---

## 4. Addressing — `head / track / sector / fmttype`

The host's CP/M BIOS holds head, track, sector, and knows the disk's format. It sends all
four; the FujiNet does the geometry. This is the substantive difference from `MediaTypeImg`
(which forces the host to compute a single linear LBA).

### 4.1 Why not LBA

On an 8080/Z80 host, LBA addressing is expensive and awkward:

- Building `track * sectors_per_track + sector` is a multiply/add the BIOS would rather not
  do — and for FDC+ (2048 tracks) it's a genuinely wide multiply.
- The BIOS already holds head/track/sector from its FDC state. Sending them verbatim
  removes the multiply and keeps the BIOS **stateless** — it names its format on every
  access and never negotiates geometry.

### 4.2 The four parameters

| param      | meaning     | width           | notes                                            |
|------------|-------------|-----------------|--------------------------------------------------|
| `param(0)` | **head**    | `uint8_t`       | 0-based side. `0` for all five initial formats.  |
| `param(1)` | **track**   | **`uint16_t`**  | 0-based. Must be ≥16-bit: FDC+ has 2048 tracks.  |
| `param(2)` | **sector**  | `uint8_t`       | physical sector ID; numbering base is per-format.|
| `param(3)` | **fmttype** | `uint8_t`       | format index (bits 0-3) + access-mode bit (bit 6); §4.3, §5.2. `0x0F` = custom. |

`FujiBusPacket::param(i)` returns a `uint32_t` regardless of the wire width
(`FujiBusPacket.h:75`), so the ESP side reads all four uniformly; the widths above only
govern how the BIOS packs the bytes.

**Wire cost.** The FujiBus serializer packs same-width params under one 3-bit descriptor
(`FujiBusPacket.cpp:34`); the first descriptor rides inside the 6-byte header
(`FujiBusPacket.cpp:16`). Because `track` is 16-bit and the rest are 8-bit, the four params
span two descriptor groups rather than one, costing one extra descriptor byte over a pure
same-width set — a handful of bytes after the header, still with **no host-side multiply**.
For a hand-coded 8080 BIOS, sending all four as `uint16_t` (uniform width, trivial to emit)
is a fine alternative — a few more bytes, no correctness difference on the ESP side.
Whichever the BIOS chooses, it must match `FujiBusPacket`'s descriptor encoding
(`FujiBusPacket.cpp`).

> `fmttype` is sent on **every** access, not just cold boot. That is what keeps the BIOS
> stateless and lets the FujiNet resolve geometry per command without ever consulting the
> image — exactly what the "no header, no sidecar" constraint demands.

### 4.3 Access mode — single sector or whole track

A read or write addresses **either one sector or an entire track**. Because a track is a
run of consecutive sectors and the dump stores them in ascending order with no interleave
(§5.2), a whole track is just a contiguous byte range — reading or writing it is the same
seek-and-transfer as a sector, only longer. Track mode lets a host move a track in one
command instead of 26–51 (a real win for an 8080/Z80 BIOS), and it is the natural transfer
unit for a future track-oriented FORMAT (§5.6).

The mode rides in a **spare bit of `fmttype`** — no extra wire param, no new opcode, no
device-layer change. The byte carries a 4-bit format index, one access-mode bit, and
reserved bits:

```cpp
//   bits 0-3  format index (16 formats; 0x0F = custom, host-supplied geometry — §5.8)
//   bits 4-5  reserved (must be 0)
//   bit  6    access mode: 0 = single sector, 1 = whole track
//   bit  7    reserved (must be 0)
#define FMT_INDEX_MASK    0x0F   // low 4 bits: format index
#define FMT_CUSTOM        0x0F   //   index 15: geometry set via CMD::DISK_SET_GEOMETRY (§5.8)
#define FMT_MODE_TRACK    0x40   // bit 6: whole-track access (clear = single sector)
#define FMT_RESERVED_MASK 0xB0   // bits 4,5,7 -- must be zero
```

The index shrank from 6 bits to 4 (16 formats is ample headroom over the five baked
formats, and `FMT_CUSTOM` removes any pressure to pre-bake more); the freed bits, plus the
old second mode bit, become reserved. The host ORs the mode into the index on every access
(e.g. `FMT_IBM_SD | FMT_MODE_TRACK`). In track mode the **sector** param is ignored; `head`
and `track` are still bounds-checked. Any **reserved bit set** is refused, never guessed —
same discipline the old design applied to unknown mode values, now enforced on the whole
byte (§5.2).

Track mode is what sets the staging-buffer size: the buffer must hold the largest whole
track (IBM 8" DD, 6656 bytes for the baked formats), not just the largest sector — see §3,
§5.7. A custom format may declare a larger sector or track (§5.8); the buffer is grown to
fit when the custom geometry is set.

---

## 5. `MediaTypeDSK` — the class

New files `lib/media/rs232/diskTypeDSK.h/.cpp`, modeled on `diskTypeImg.*`.

### 5.1 The format table (ESP32-side, firmware-baked)

The whole geometry authority on the FujiNet side is a static table. Each format is a list of
**regions** so a single `fmttype` can describe a disk whose tracks aren't uniform. Four of
the five initial formats are single-sided and uniform, so each is a **single-region** entry;
the fifth, **Tarbell Double Density**, is the real mixed-density case — an IBM 8" SD-style
track 0 (26 × 128-byte sectors) followed by tracks 1–76 with **51** 128-byte sectors each.
The sector *size* is a constant 128 bytes; what varies is the sector *count* per track. It's
a **two-region** entry that exercises the general offset walk and per-region sector bounds.

```cpp
struct DSKRegion
{
    uint16_t first_track;   // inclusive
    uint16_t last_track;    // inclusive
    uint8_t  head_mask;     // sides this applies to: bit0=side0, bit1=side1; 0xFF = all
    uint8_t  sectors;       // sectors per track (per side) in this region
    uint16_t sector_size;   // bytes
    uint8_t  first_sector;  // sector-number of the first sector on a track
};

struct DSKFormat
{
    const char         *name;
    uint16_t            num_tracks;
    uint8_t             num_sides;
    uint8_t             num_regions;
    const DSKRegion *regions;
    bool                head_major;   // false = interleaved (T0H0,T0H1,T1H0,...)
                                       // true  = sequential  (all H0 tracks, then all H1)
};

// --- uniform single-sided formats (one region each) ---
static const DSKRegion R_IBM_SD[]  = {{0,   76, 0xFF, 26,  128, 1}};
static const DSKRegion R_IBM_DD[]  = {{0,   76, 0xFF, 26,  256, 1}};
static const DSKRegion R_ALTAIR[]  = {{0,   76, 0xFF, 32,  137, 1}};
static const DSKRegion R_FDCPLUS[] = {{0, 2047, 0xFF, 32,  137, 1}};

// --- Tarbell Double Density: 26 sectors on track 0, 51 on tracks 1-76;
//     128-byte sectors throughout (varies sector COUNT, not size) ---
static const DSKRegion R_TARBELL_DD[] = {
    {0,  0, 0xFF, 26, 128, 1},   // track 0     : 26 x 128-byte sectors
    {1, 76, 0xFF, 51, 128, 1},   // tracks 1-76 : 51 x 128-byte sectors
};

enum dsk_fmt_t
{
    FMT_IBM_SD = 0,   // IBM 8" SD  : 1 side, 77 trk, 26 spt, 128B      = 256,256 bytes
    FMT_IBM_DD,       // IBM 8" DD  : 1 side, 77 trk, 26 spt, 256B      = 512,512 bytes
    FMT_ALTAIR,       // Altair 8"  : 1 side, 77 trk, 32 spt, 137B      = 337,568 bytes
    FMT_FDCPLUS,      // FDC+       : 1 side, 2048 trk, 32 spt, 137B    = 8,978,432 bytes
    FMT_TARBELL_DD,   // Tarbell DD : 1 side, trk0 26x128 + trk1-76 51x128  = 499,456 bytes
    FMT_COUNT         // baked-format count; indices 5-14 reserved, 15 (FMT_CUSTOM) is custom
};

static const DSKFormat dsk_formats[FMT_COUNT] = {
    {"IBM 8\" SD",  77,   1, 1, R_IBM_SD,     false},
    {"IBM 8\" DD",  77,   1, 1, R_IBM_DD,     false},
    {"Altair 8\"",  77,   1, 1, R_ALTAIR,     false},
    {"FDC+",        2048, 1, 1, R_FDCPLUS,    false},
    {"Tarbell DD",  77,   1, 2, R_TARBELL_DD, false},
};
```

All five baked formats are single-sided, so `head_major` is moot for them (both layouts
reduce to the same offset) and is set `false` for clarity. It matters only for a
double-sided **custom** format (§5.8). The byte totals in the comments are documentation
only — a mount-time sanity check that the image size is a whole multiple of the format,
never a source of geometry.

`FMT_CUSTOM` (index 15) is not in this table: its `DSKFormat` is built at runtime from
host-supplied geometry (§5.8), and `locate()` routes index 15 to that runtime slot instead
of `dsk_formats[]`.

### 5.2 `decode_sector()` — CHS + fmttype → byte offset

`decode_sector()` is the addressing hook. It reads the four params, looks up the format, and
resolves the **byte offset** and **sector size** directly (no LBA intermediate — that
abstraction is lossy once regions vary). It stashes both plus a validity flag; `read()` /
`write()` / `sector_size()` consume the stash.

```cpp
uint32_t MediaTypeDSK::decode_sector(const uint32_t *params, unsigned count)
{
    // A floppy host always sends all four params; a short access cannot resolve
    // geometry and is refused (see §8).
    if (count < 4)
    {
        _cur_valid = false;   // no fmttype -> cannot resolve geometry
        return 0;
    }

    uint8_t  head    = (uint8_t)params[0];
    uint16_t trk     = (uint16_t)params[1];
    uint16_t sec     = (uint16_t)params[2];
    uint8_t  fmttype = (uint8_t)params[3];

    if (fmttype & FMT_RESERVED_MASK)
    {
        _cur_valid = false;   // a reserved bit is set -> refuse, don't guess (§4.3)
        return trk;
    }

    uint8_t index      = fmttype & FMT_INDEX_MASK;      // bits 0-3: format (0x0F = custom)
    bool    track_mode = (fmttype & FMT_MODE_TRACK);    // bit 6: sector or track (§4.3)

    _cur_valid = locate(index, head, trk, sec, track_mode, &_cur_offset, &_cur_xfer_size);
    return trk;   // returned for logging/bounds only; read()/write() use the stash
}

// Resolve (fmt, head, trk, sec) -> file offset + transfer size. In track_mode
// the result spans the whole track and `sec` is ignored; otherwise it is a
// single sector. Returns false if the address is out of range for the format.
bool MediaTypeDSK::locate(uint8_t fmt, uint8_t head, uint16_t trk, uint16_t sec,
                             bool track_mode, uint32_t *offset, uint16_t *size)
{
    // Select the baked table entry, or the runtime custom slot (§5.8).
    const DSKFormat *fp;
    if (fmt == FMT_CUSTOM)      { if (!_custom_valid) return false; fp = &_custom_format; }
    else if (fmt < FMT_COUNT)   { fp = &dsk_formats[fmt]; }
    else                        { return false; }
    const DSKFormat &f = *fp;
    if (trk >= f.num_tracks || head >= f.num_sides) return false;

    // Fast path: a uniform (single-region) format is a straight multiply. head_major
    // (§5.8) selects the side layout: all of head 0's tracks then head 1's, vs. the two
    // heads interleaved per cylinder. Single-sided formats reduce to the same value.
    if (f.num_regions == 1)
    {
        const DSKRegion &r = f.regions[0];
        uint32_t track_index = f.head_major
            ? ((uint32_t)head * f.num_tracks + trk)   // sequential sides
            : ((uint32_t)trk * f.num_sides + head);   // interleaved
        uint32_t track_start = track_index * r.sectors;
        if (track_mode)                                   // whole track
        {
            *offset = track_start * r.sector_size;
            *size   = (uint16_t)(r.sectors * r.sector_size);
            return true;
        }
        if (sec < r.first_sector || sec >= r.first_sector + r.sectors) return false;
        *offset = (track_start + (sec - r.first_sector)) * r.sector_size;
        *size   = r.sector_size;
        return true;
    }

    // General path: sum each preceding (track, side) slot's OWN region bytes, in the order
    // the layout stores them, then add the sector offset within the target slot. Because
    // every slot contributes its own region's size, the two sides of a cylinder may carry
    // different geometry (e.g. a 128-byte SD boot side 0 and a 256-byte side 1 on track 0)
    // and the offsets still come out right (§5.8). visit(t,h) below is the per-slot body;
    // head_major just changes the iteration order.
    uint32_t off = 0;
    auto slot_bytes = [&](uint16_t t, uint8_t h) -> int32_t {   // -1 = no region (refuse)
        const DSKRegion *r = region_for(f, t, h);
        return r ? (int32_t)((uint32_t)r->sectors * r->sector_size) : -1;
    };

    if (f.head_major)                                   // all of side 0, then side 1, ...
    {
        for (uint8_t h = 0; h < head; h++)
            for (uint16_t t = 0; t < f.num_tracks; t++)
            {
                int32_t b = slot_bytes(t, h);
                if (b < 0) return false;
                off += (uint32_t)b;
            }
        for (uint16_t t = 0; t < trk; t++)
        {
            int32_t b = slot_bytes(t, head);
            if (b < 0) return false;
            off += (uint32_t)b;
        }
    }
    else                                                // cylinder-major: both sides per cyl
    {
        for (uint16_t t = 0; t < trk; t++)
            for (uint8_t h = 0; h < f.num_sides; h++)
            {
                int32_t b = slot_bytes(t, h);
                if (b < 0) return false;
                off += (uint32_t)b;
            }
        for (uint8_t h = 0; h < head; h++)
        {
            int32_t b = slot_bytes(trk, h);
            if (b < 0) return false;
            off += (uint32_t)b;
        }
    }

    const DSKRegion *r = region_for(f, trk, head);   // the target slot
    if (r == nullptr) return false;
    if (track_mode)                                     // whole track
    {
        *offset = off;
        *size   = (uint16_t)(r->sectors * r->sector_size);
        return true;
    }
    if (sec < r->first_sector || sec >= r->first_sector + r->sectors) return false;
    *offset = off + (uint32_t)(sec - r->first_sector) * r->sector_size;
    *size   = r->sector_size;
    return true;
}
```

`region_for(f, t, h)` returns the region whose `[first_track, last_track]` contains `t` and
whose `head_mask` includes `h` (`nullptr` if none — a malformed table or address). The
regions must **tile every `(track, head)`** in range; a slot with no region refuses the
address rather than guessing.

Two correctness notes tied to the "raw physical dump" constraint:

- **No interleave translation.** The image stores sectors in ascending sector-ID order per
  track, and the BIOS addresses by physical sector ID — so sector *N* maps straight to slot
  `N - first_sector`. CP/M's logical↔physical skew lives in the host's sector-translate
  table, not here. (This also keeps the sequential-read optimization valid.)
- **Side ordering is a declared property**, not an assumption. `DSKFormat.head_major`
  selects between cylinder-major/interleaved (`c0h0, c0h1, c1h0, …`) and head-major/
  sequential (all of side 0, then side 1). The baked formats are single-sided (moot); a
  double-sided **custom** format declares its convention in `SET_GEOMETRY` (§5.8), and the
  `locate()` walk above honors it in both the fast and general paths.

### 5.3 `sector_size()`, `read()`, `write()`

```cpp
uint16_t MediaTypeDSK::sector_size(uint32_t /*sectornum*/)
{
    return _cur_xfer_size;   // stashed by decode_sector() for this access
}
```

`read()` / `write()` follow `MediaTypeImg::read`/`write` (`diskTypeImg.cpp:23`, `:62`) with
these changes:

- gate on `_cur_valid` — a bad `fmt`/track/sector from the host returns an error instead of
  seeking out of range (tighter than IMG's `sectornum > _disk_num_sectors`);
- seek to `_cur_offset` (the stashed byte offset) rather than `sector * 512`;
- transfer `_cur_xfer_size` bytes into/out of `_dsk_buff` (the type's own buffer, §3),
  not the base `_disk_sectorbuff`.

**Writes are supported.** `write()` `fwrite`s `_cur_xfer_size` bytes at `_cur_offset` and
`fflush`es (matching IMG's sync-on-write). This changes sector *contents*, never the image
*layout* — the dump stays bit-for-bit re-writable to physical media, satisfying the
constraint. The sequential-write seek-skip optimization is valid because physical sectors
are contiguous in the dump (no interleave, §5.2).

**Writes extend the file — by design.** The `_cur_valid` geometry check is the *only* bound;
there is deliberately no actual-file-size check. A write to any in-range address is allowed
even when its offset lies past the current end of file — the seek-past-EOF + `fwrite`
extends the image. This is how a **blank 0-byte `.DSK` gets formatted**: mount an empty file,
have the host write each valid track, and the file grows to the format's full size. Because
`locate()` caps every address to `num_tracks × sectors × sector_size`, a valid write can
never grow the file beyond that maximum — the geometry bound *is* the size limit. This is
exactly why Altair (77 trk) and FDC+ (2048 trk) stay separate formats (§5.1): each must cap
extension at its own medium's track count.

### 5.4 `mount()` — open the file, nothing else

Because the host names its format on every access and the image carries no geometry,
`mount()` has almost nothing to do:

```cpp
mediatype_t MediaTypeDSK::mount(fnFile *f, uint32_t disksize, fujiHost*, const char*)
{
    _disk_fileh      = f;
    _disk_image_size = disksize;
    _disktype        = MEDIATYPE_DSK;
    // No header parse, no sidecar, no geometry inference. (disksize is available
    // only as an optional whole-multiple sanity check against a known format.)
    return _disktype;
}
```

There is deliberately **no** `.cfg` sidecar and **no** mount-time geometry: the constraints
forbid the sidecar and make it unnecessary, since `fmttype` supplies geometry per access.
`host`/`filename` are ignored (they exist only for `MediaTypeROM`'s sibling-`.cfg` lookup,
`diskType.h:76`). Appendix C lays out the alternatives (sidecar, mount-time selection) and
why per-command `fmttype` is preferred.

### 5.5 `status()` and PERCOM

`MediaTypeDSK` doesn't know the disk's format until the first access, so it has no
geometry to report to `CMD::DISK_PERCOM_READ` / `status()` beforehand. A pure CP/M CHS boot
path never issues those, so this is a non-issue in practice; implement `status()` as a
minimal clear/ready response (mirror `MediaTypeImg::status()`, `diskTypeImg.cpp:105`, but
without the density/side inference), and leave `_percomBlock` zeroed. If a future host needs
PERCOM, remember the last `fmttype` seen and report from `dsk_formats[]`.

### 5.6 `format()` — interim stub; Write-Track is future work

Formatting is wanted, but the real mechanism is non-trivial: floppy controllers like the
Western Digital 179x expose a **Write Track** command that streams an entire raw track
(sync, gaps, ID address marks, and data) in one operation. Turning that stream into a clean
sector dump means parsing the track format — its own project.

- **Interim:** implement `format()` as a documented stub. If a host's format utility needs a
  non-error response, have it fill the addressed track (or whole image) for the given
  `fmttype` with CP/M's empty byte `0xE5` rather than reject — but write **no** structural
  bytes, preserving the raw-dump invariant.
- **Whole-track data transfer already exists (§4.3).** Track mode reads/writes a track's
  worth of *sector data* in one command, and the staging buffer is already sized to the
  largest track (6656 B). That covers a format utility that lays down sector contents (e.g.
  fill a track with `0xE5`) — it just writes the track in track mode.
- **Future (flagged for investigation):** true low-level formatting — likely a new
  `CMD::DISK_WRITE_TRACK` carrying `track` + a full WD179x-style track *stream* (sync, gaps,
  ID address marks, data) — that the media type parses per `fmttype` into the right sector
  offsets. This is distinct from track mode: track mode moves the clean sector-data range,
  whereas Write-Track must strip the structural bytes. The buffer is already track-sized,
  so no further sizing is needed; the work is the parser — a decision local to
  `MediaTypeDSK`, still costing IMG/ROM nothing (§3).

### 5.7 Header

```cpp
#ifndef _MEDIATYPE_DSK_RS232
#define _MEDIATYPE_DSK_RS232

#include "diskType.h"   // MediaType — bus-agnostic, no FujiBusPacket dependency

class MediaTypeDSK : public MediaType
{
private:
    // The default staging buffer, sized to the largest baked transfer — a whole track in
    // track mode (6656 B), not just the largest baked sector (IBM 8" DD, 256 B); §3, §4.3.
    // A custom format may need more (§5.8): _buff/_buff_size point here by default and are
    // repointed at a heap block when the custom geometry is larger. Private to
    // MediaTypeDSK so MediaTypeImg/MediaTypeROM keep their 512-byte base buffer.
    uint8_t  _dsk_buff[DSK_BUFFER_SIZE];   // DSK_BUFFER_SIZE == 6656
    uint8_t *_buff        = _dsk_buff;         // active staging buffer (may be heap, §5.8)
    uint32_t _buff_size   = DSK_BUFFER_SIZE;
    uint8_t *_heap_buff   = nullptr;              // owned overflow buffer, freed on unmount/dtor

    uint32_t _cur_offset    = 0;      // byte offset of the addressed sector/track
    uint16_t _cur_xfer_size = 0;      // bytes to transfer: one sector, or a whole track
    bool     _cur_valid     = false;  // false if the last address was out of range

    // Runtime custom format (FMT_CUSTOM), built by set_geometry() from host-supplied
    // regions (§5.8). Baked formats live in dsk_formats[]; this is the index-15 slot.
    DSKRegion _custom_regions[DSK_MAX_CUSTOM_REGIONS];
    uint8_t      _custom_region_count = 0;
    DSKFormat _custom_format {};
    bool         _custom_valid = false;

    bool locate(uint8_t fmt, uint8_t head, uint16_t trk, uint16_t sec,
                bool track_mode, uint32_t *offset, uint16_t *size);
    bool ensure_buffer(uint32_t bytes);   // grow the staging buffer to hold `bytes` (§5.8)

public:
    uint32_t      decode_sector(const uint32_t *params, unsigned count) override;
    void          set_geometry(const uint32_t *params, unsigned count) override; // FMT_CUSTOM (§5.8)

    // Staging buffer overrides — hand the device this type's active buffer (§3, §5.8).
    uint8_t      *sector_buffer()      override { return _buff; }
    uint32_t      sector_buffer_size() override { return _buff_size; }

    mediatype_t   mount(fnFile *f, uint32_t disksize,
                        fujiHost *host = nullptr, const char *filename = nullptr) override;
    error_is_true read (uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;
    uint16_t      sector_size(uint32_t sectornum) override;
    void          status(uint8_t statusbuff[4]) override;
    error_is_true format(uint32_t *responsesize) override;
    ~MediaTypeDSK() override;      // frees _heap_buff
};

#endif // _MEDIATYPE_DSK_RS232
```

---

## 5.8 Custom formats — host-supplied geometry (`FMT_CUSTOM`)

The baked table covers formats a constrained 8080 boot ROM can name with a single index.
But a capable host or controller — the FujiNet-PC/hardware **disk-controller emulators**
that drive the same `.DSK` corpus (WD177x/FD1771-class), and any BIOS that formats its own
media — needs to serve **arbitrary** geometry: 512- or 1024-byte sectors, odd sector
counts, mixed density, double-sided, per-side differences. Rather than pre-bake every such
shape, index **`0x0F` (`FMT_CUSTOM`)** resolves against a geometry the host **declares once**
and then addresses with `FMT_CUSTOM` on every access — the same "host is the geometry
authority" principle as `fmttype` itself (§4), extended from *naming* a format to *defining*
one. No sidecar, no header, no image change: the custom geometry lives in RAM on the ESP,
tied to the mount, and is re-sent by the host after a remount.

### 5.8.1 `CMD::DISK_SET_GEOMETRY` — declaring the geometry

A new device command, `CMD::DISK_SET_GEOMETRY = 0x47` ('G'), carries **one region** per call
as params (no data-frame handshake, trivial for an 8080 to emit — the same param model as
CHS+fmttype). The `append` flag builds a multi-region format from several calls:

| param | field | default | scope |
|-------|-------|---------|-------|
| `param(0)` | first_track (u16, inclusive) | — | region |
| `param(1)` | last_track (u16, inclusive)  | — | region |
| `param(2)` | sectors_per_track (u8)       | — | region |
| `param(3)` | sector_size (u16)            | — | region |
| `param(4)` | first_sector (u8)            | — | region |
| `param(5)` | head_mask (u8; bit0=side0, bit1=side1) | `0xFF` | region |
| `param(6)` | num_sides (u8)               | `1`    | format |
| `param(7)` | flags (u8)                   | `0`    | format/ctl |

```
flags: bit0 head_major (0 = interleaved, 1 = head 1 follows head 0)
       bit1 append     (0 = fresh: reset the builder; 1 = add this region)
       2-7  reserved (0)
```

Trailing params default, so the **uniform single-sided** case stays 5 params
(`first_track=0, last_track=N-1, spt, size, first`). A **uniform double-sided** disk adds
`head_mask`/`num_sides` (and `head_major` in flags). A **mixed** disk (different geometry
per track range, or per side of a track) sends a fresh call for region 0 and an `append`
call for each further region:

```
# Tarbell DD, rebuilt as a custom format (equals dsk_formats[FMT_TARBELL_DD]):
SET_GEOMETRY  0,  0, 26, 128, 1                 flags=0x00   # fresh:  track 0, 26x128
SET_GEOMETRY  1, 76, 51, 128, 1, 0xFF, 1, 0x02  flags=append # tracks 1-76, 51x128

# Track 0 side 0 is a 128-byte SD boot track; side 1 and tracks 1-76 are 256-byte:
SET_GEOMETRY  0,  0, 26, 128, 1, 0x01, 2, 0x00  # fresh:  T0 side 0 only
SET_GEOMETRY  0,  0, 26, 256, 1, 0x02, 2, 0x02  # append: T0 side 1 only
SET_GEOMETRY  1, 76, 26, 256, 1, 0xFF, 2, 0x02  # append: T1-76 both sides
```

Because `region_for()` matches on both the track range **and** `head_mask`, and the
`locate()` general walk sums each `(track, head)` slot's *own* region bytes (§5.2),
per-side differences resolve correctly — the two sides of a cylinder may carry different
sector sizes or counts. The regions must **tile every `(track, head)`** in range; a gap
refuses the address.

### 5.8.2 `set_geometry()` — accumulating regions

```cpp
#define DSK_MAX_CUSTOM_SECTOR   1024   // cap a single custom sector
#define DSK_MAX_CUSTOM_REGIONS  8      // Tarbell=2; per-side special tracks fit comfortably

void MediaTypeDSK::set_geometry(const uint32_t *params, unsigned count)
{
    if (count < 5) { _custom_valid = false; return; }

    uint16_t first_track = (uint16_t)params[0];
    uint16_t last_track  = (uint16_t)params[1];
    uint8_t  spt         = (uint8_t)params[2];
    uint16_t sec_size    = (uint16_t)params[3];
    uint8_t  first_sec   = (uint8_t)params[4];
    uint8_t  head_mask   = (count >= 6) ? (uint8_t)params[5] : 0xFF;
    uint8_t  num_sides   = (count >= 7) ? (uint8_t)params[6] : 1;
    uint8_t  flags       = (count >= 8) ? (uint8_t)params[7] : 0;
    bool     head_major  = (flags & 0x01) != 0;
    bool     append      = (flags & 0x02) != 0;

    if (!append) { _custom_region_count = 0; _custom_valid = false; }   // fresh: reset builder

    if (!spt || !sec_size || sec_size > DSK_MAX_CUSTOM_SECTOR ||
        last_track < first_track || !num_sides ||
        _custom_region_count >= DSK_MAX_CUSTOM_REGIONS)
    { _custom_valid = false; return; }

    _custom_regions[_custom_region_count++] =
        { first_track, last_track, head_mask, spt, sec_size, first_sec };

    // Derive the format header + size the staging buffer for the largest track over regions.
    uint16_t max_track = 0;
    uint32_t max_tbytes = 0;
    for (uint8_t i = 0; i < _custom_region_count; i++)
    {
        const DSKRegion &r = _custom_regions[i];
        if (r.last_track > max_track) max_track = r.last_track;
        uint32_t tb = (uint32_t)r.sectors * r.sector_size;
        if (tb > max_tbytes) max_tbytes = tb;
    }
    _custom_format = { "custom", (uint16_t)(max_track + 1), num_sides,
                       _custom_region_count, _custom_regions, head_major };
    _custom_valid  = ensure_buffer(max_tbytes);
}
```

`num_sides`/`head_major` are format-level; on an `append` call the host re-sends them
consistently (a mismatched value simply takes effect — the host owns the geometry).
Addressing `FMT_CUSTOM` before any successful `set_geometry` leaves `_custom_valid` false,
so `locate()` refuses it rather than resolving against an empty slot.

### 5.8.3 Buffer sizing — custom can exceed the baked bound

The baked staging buffer is 6656 B (largest baked track). A custom **sector** can be up to
1024 B (fits easily), but a custom **track** in track mode — `spt × sector_size` — can
exceed 6656 (e.g. 26 × 1024 ≈ 26 KB). `ensure_buffer()` keeps the static `_dsk_buff` for
anything that fits and repoints `_buff` at a right-sized heap block only when a custom
geometry needs more, so the common path pays nothing and `MediaTypeImg`/`MediaTypeROM` are
untouched:

```cpp
bool MediaTypeDSK::ensure_buffer(uint32_t bytes)
{
    if (bytes <= DSK_BUFFER_SIZE) { _buff = _dsk_buff; _buff_size = DSK_BUFFER_SIZE; return true; }
    if (bytes > _buff_size || _heap_buff == nullptr)
    {
        uint8_t *p = (uint8_t *)realloc(_heap_buff, bytes);
        if (p == nullptr) return false;   // keep the old buffer; custom set fails cleanly
        _heap_buff = p;
    }
    _buff = _heap_buff;
    _buff_size = bytes;
    return true;
}
```

`~MediaTypeDSK()` frees `_heap_buff`. Sector mode always fits (≤1024 ≤ 6656), so the heap
path is reached only by a large-track custom format that also uses track mode.

---

## 6. The one shared edit — route the packet's params through `decode_sector()`

Today the device hard-wires `param(0)` as the sector (`disk.cpp:264`). Addressing is a
property of the format, so it moves into the media type. Crucially, the media type stays
**bus-agnostic**: the device parses the `FujiBusPacket` and hands the media type only plain
values (`const uint32_t *params, unsigned count`) — the image representation never depends
on a bus protocol. The change is small and, for `MediaTypeImg`, exactly behavior-preserving.

**Step 1 — base-class defaults keep today's behavior.** In `lib/media/rs232/diskType.h`,
add to `class MediaType` (no new include — the media layer takes plain params, not a
packet):

```cpp
// Default addressing: params[0] is a linear sector (LBA). Override for CHS.
virtual uint32_t decode_sector(const uint32_t *params, unsigned count)
{ return count ? params[0] : 0; }

// Default geometry declaration: ignore. MediaTypeDSK overrides it for FMT_CUSTOM
// (§5.8); Img/ROM have fixed geometry and want the no-op.
virtual void     set_geometry(const uint32_t *params, unsigned count)
{ (void)params; (void)count; }

// Default staging buffer: the existing 512-byte member. Override to supply a
// larger one (MediaTypeDSK does, for a whole 6656-byte track — §3).
virtual uint8_t *sector_buffer()      { return _disk_sectorbuff; }
virtual uint32_t sector_buffer_size() { return DISK_SECTORBUF_SIZE; }
```

`MediaTypeImg` and `MediaTypeROM` inherit all three unchanged — no behavior change for any
existing image, and no growth of the base buffer.

**Step 2 — the device extracts the params and calls the hook.** In
`lib/device/rs232/disk.cpp`, a small `packet_params()` helper copies the packet's typed
params into a bus-agnostic array; `rs232_read`/`rs232_write` resolve the sector through the
media type from that array. The `CMD::DISK_SET_GEOMETRY` case (a control command — no payload
back, just an ACK) does the same and is a no-op for Img/ROM via the base virtual:

```cpp
static unsigned packet_params(const FujiBusPacket &pkt, uint32_t *out, unsigned max)
{
    unsigned n = pkt.paramCount();
    if (n > max) n = max;
    for (unsigned i = 0; i < n; i++) out[i] = pkt.param(i);
    return n;
}

case CMD::DISK_READ:  rs232_read(packet);         return;
case CMD::DISK_PUT:   rs232_write(packet, false); return;
case CMD::DISK_STATUS:
case CMD::DISK_WRITE: rs232_write(packet, true);  return;
case CMD::DISK_SET_GEOMETRY:
{
    uint32_t p[8]; unsigned n = packet_params(packet, p, 8);
    if (_disk != nullptr) { _disk->set_geometry(p, n); SYSTEM_BUS.transaction_success(); }
    else                    SYSTEM_BUS.transaction_error();
    return;
}
```

```cpp
void rs232Disk::rs232_read(const FujiBusPacket &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (_disk == nullptr) { SYSTEM_BUS.transaction_error(); return; }

    uint32_t params[8];
    unsigned nparams = packet_params(packet, params, 8);
    uint32_t sector  = _disk->decode_sector(params, nparams); // LBA for Img; CHS->offset for Floppy
    uint32_t readcount;
    bool err = _disk->read(sector, &readcount);
    SYSTEM_BUS.transaction_send(_disk->sector_buffer(), readcount, err);
}

void rs232Disk::rs232_write(const FujiBusPacket &packet, bool verify)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    if (_disk != nullptr)
    {
        uint32_t params[8];
        unsigned nparams    = packet_params(packet, params, 8);
        uint32_t sector     = _disk->decode_sector(params, nparams); // sets _cur_offset/_cur_xfer_size
        uint16_t sectorSize = _disk->sector_size(sector);            // Floppy returns stashed size
        memset(_disk->sector_buffer(), 0, _disk->sector_buffer_size());
        if (SYSTEM_BUS.transaction_get(_disk->sector_buffer(), sectorSize))
            if (_disk->write(sector, verify) == false)
            {
                SYSTEM_BUS.transaction_success();
                return;
            }
    }
    SYSTEM_BUS.transaction_error();
}
```

Ordering matters: `decode_sector()` runs before `sector_size()`, so the per-access size is
stashed when the device sizes the write. Update the `rs232_read`/`rs232_write` declarations
in `lib/device/rs232/disk.h` from `(uint32_t)` to `(const FujiBusPacket &)`. Also swap the
one remaining `_disk->_disk_sectorbuff` in `rs232_format` (`disk.cpp:135`) to
`_disk->sector_buffer()` so every device access goes through the accessor. All three swaps
are behavior-preserving for `MediaTypeImg`/`MediaTypeROM` (same member, same 512 size); the
`transaction_*` primitives are untouched.

---

## 7. Registering the type

### Step 1 — enum

`lib/media/rs232/diskType.h` (`diskType.h:37`):

```cpp
enum mediatype_t
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_IMG,
    MEDIATYPE_ROM,
    MEDIATYPE_DSK,   // ← new
    MEDIATYPE_COUNT
};
```

### Step 2 — route `.DSK`

`lib/media/rs232/diskType.cpp`, in `discover_mediatype()` (`diskType.cpp:60`):

```cpp
if (strcasecmp(ext, "DSK") == 0)
    return MEDIATYPE_DSK;
```

### Step 3 — construct it at mount

`lib/device/rs232/disk.cpp`, in the `mount()` switch (`disk.cpp:202`):

```cpp
case MEDIATYPE_DSK:
    device_active = true;
    _mount_time   = time(NULL);
    _disk = new MediaTypeDSK();
    return _disk->mount(f, disksize);   // host/filename unused (no sidecar)
```

### Step 4 — include the header

`lib/media/media.h`, under `BUILD_RS232` (`media.h:11`):

```cpp
# include "rs232/diskTypeDSK.h"   // ← new
```

### Step 5 — build wiring

- Guard `diskTypeDSK.cpp` with `#ifdef BUILD_RS232`. PlatformIO globs it for ESP; the PC
  build globs `lib/media/rs232/` in `fujinet_pc.cmake`.
- Build both: `./build.sh -s <rs232-board> -cb` and `./build.sh -p RS232 -g`.

---

## 8. The `.DSK` extension — collision check

Today `.DSK` is **not** matched by `discover_mediatype()` and falls through to
`MediaTypeImg` as flat 512-byte LBA (`disk.cpp:210`). Routing `.DSK` to `MediaTypeDSK`
re-interprets it — a regression **only if** a pre-existing corpus of flat-LBA `.DSK` images
is being served by this firmware. For the RS232/CP/M use case `.DSK` is unambiguously the
raw floppy format, so this is likely moot — but verify against the deployed corpus before
shipping. Cheap insurance if any doubt: the `count < 4` branch in `decode_sector()`
(§5.2) can fall back to a flat single-param read instead of erroring, keeping a legacy
single-param host readable. Legacy CP/M hosts here always send `fmttype`, so the fallback is
optional.

---

## 9. Regression analysis (flat-IMG path)

By construction the flat-IMG path is untouched:

- **`MediaTypeImg` is not modified.** Its offset math, `mount()`, inherited `sector_size()`,
  `read`/`write`, and `status()` are byte-for-byte as today.
- **The base `decode_sector()` default returns `params[0]`** — the same linear sector the
  device resolved before (`disk.cpp:264`). `MediaTypeImg` inherits it unchanged.
- **No wire-format change.** A single-param read/write serializes and parses as before; the
  four-param CHS form is produced only by a CP/M host against a mounted floppy.
- **No mount/UI/config change.** `store_mount()`, the `[MountN]` schema, and the web UI are
  as-is. New behavior is reached only when a `.DSK` is mounted and a host sends CHS+fmttype.
- **No shared buffer growth.** `DISK_SECTORBUF_SIZE` stays 512; the 6656-byte buffer is
  private to `MediaTypeDSK` (`_dsk_buff`, §3). `MediaTypeImg`/`MediaTypeROM` keep their
  512-byte base buffer, and `diskTypeImg.cpp`/`diskTypeROM.cpp` are not edited at all.
- **The device buffer-accessor swap is behavior-preserving.** Replacing
  `_disk->_disk_sectorbuff` with `_disk->sector_buffer()` in the device returns the identical
  base member at the identical 512 size for IMG/ROM.
- **The `decode_sector()` routing edit is behavior-preserving for IMG** — the resolved
  sector equals `params[0]`. A single regression test (mount IMG, read/write a known sector
  before and after) pins both device edits down.

Residual risk is localized to the new type: the CHS→offset math in `locate()`, with the
media layer kept free of any bus-protocol dependency. Neither can regress the flat-IMG path.

---

## 10. Testing with real `.DSK` images

Claude has no RS232 board and no CP/M host in the loop, so testing is layered: everything
verifiable in software runs on the **FujiNet-PC** build against real `.DSK` files; the
hardware step is a prepared checklist for the user. The seam under test —
`decode_sector()`/`locate()` → `read()`/`write()` — is reachable without the bus or a host:
the media hooks take plain params, so a test just builds the `uint32_t` array the device
would extract from a packet, and on PC `fnio` wraps host `stdio`, so a `.DSK` is just a
file.

### 10.1 Layer 1 — `MediaTypeDSK` unit tests (doctest, primary)

Add a doctest target modeled on `fujibuspacket_tests` in `tests/CMakeLists.txt`:

```cmake
add_executable(diskdsk_tests
    DiskTypeDSKTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskTypeDSK.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskType.cpp)
target_compile_definitions(diskdsk_tests PRIVATE BUILD_RS232)
target_include_directories(diskdsk_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/ ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/)
add_test(NAME diskdsk_tests COMMAND diskdsk_tests)
```

> Keep `locate()` free of file I/O (pure arithmetic over the static table) so the offset
> math tests with no `fnio` at all. Build the CHS params exactly as the device extracts
> them from a packet: `uint32_t p[4] = {head, track, sector, fmt}; decode_sector(p, 4);`.
> The media layer takes plain params, so the test needs no `FujiBusPacket`.

Offset math is the crown jewel — assert `locate()` against hand-computed offsets for all five
formats:

| Case | Assertion |
|---|---|
| **IBM SD** (77×26×128) | `(0,0,1)→0`; `(0,0,26)→3200`; `(0,1,1)→3328` (=26×128); last `(0,76,26)` = 256256−128 |
| **IBM DD** (×256) | same indices scaled by 256; `(0,1,1)→6656` |
| **Altair** (77×32×137) | non-power-of-two size: `(0,0,1)→0`, `(0,0,2)→137`, `(0,1,1)→4384` (=32×137) |
| **FDC+** (2048×32×137) | Altair geometry, 2048 tracks: `(0,t,1)→t×4384`; `(0,0,32)→4247`; `(0,2047,1)→8974048`; **track > 255 resolves** (u16) |
| **Sector base** | `sector < first_sector` and `sector > first+sectors−1` → out-of-range error |
| **Bounds** | `fmt ≥ FMT_COUNT`, `track ≥ num_tracks` → `_cur_valid=false` → `read`/`write` error, no I/O |
| **Size cap** | 6656-byte IBM DD track fits `_dsk_buff` (`DSK_BUFFER_SIZE`); base `DISK_SECTORBUF_SIZE` unchanged at 512 |
| **Tarbell DD** (mixed count, 2-region) | 128B throughout. Track 0 has 26 sectors: `(0,0,1)→0`, `(0,0,26)→3200`, `(0,0,27)`=out-of-range. **Boundary** — track 1 starts after track 0's 26×128: `(0,1,1)→3328`; track 1 has 51 sectors: `(0,1,51)→9728`, `(0,2,1)→9856` (=3328+51×128) |

### 10.2 Layer 2 — file-backed round-trip on real `.DSK` files

Fabricate images with a **self-identifying fill** so a correct read is unambiguous — stamp
each sector's first bytes with its `(head, track, sector)` and fill the rest with a
per-sector byte. A small Python generator (repo convention) writes them from the same
geometry the table encodes:

```python
# make_dsk.py — sector's first 4 bytes = b'D', head, track&0xFF, sector&0xFF
def build(path, tracks, sides, spt, secsize, first=1):
    with open(path, "wb") as f:
        for t in range(tracks):
            for h in range(sides):
                for s in range(first, first + spt):
                    idx = (t * sides + h) * spt + (s - first)
                    f.write(bytes([ord('D'), h, t & 0xFF, s & 0xFF]) +
                            bytes([idx & 0xFF]) * (secsize - 4))
# IBM SD/DD, Altair, and a small FDC+ (few tracks) for speed. Tarbell DD needs a
# region-aware variant: emit track 0 as 26x128, then tracks 1-76 as 51x128 — the
# generator must mirror dsk_formats[] region-for-region, or the round-trip is
# validating the wrong layout.
```

Then mount the file through `fnio` and, for representative `(h,t,s)`, issue the CHS read and
assert `sector_buffer()[0..3] == {'D', h, t&0xFF, s&0xFF}` — a wrong `locate()` fails loudly
because the check is on *content addressed by CHS*. For writes: write a distinct pattern to
`(h,t,s)`, then **diff the whole file against a Python-recomputed expected image** —
byte-for-byte equality proves the write hit the right offset/length and touched nothing
else, and confirms the layout is unaltered (the raw-dump invariant). Check in the generator,
not the binaries.

### 10.3 Layer 3 — running FujiNet-PC

Build/launch the PC target (`./build.sh -p RS232 -g`, then `cd build/dist && ./run-fujinet`)
and mount a fabricated `.DSK` through the normal `/mount` path to confirm the `.DSK`
extension routes to `MediaTypeDSK` and `mount()` succeeds (watch `Debug` output for the
mount type). **Honest limit:** the PC build has no CP/M host issuing `CMD::DISK_READ`/`WRITE`,
so wire traffic isn't exercised here — Layers 1–2 cover the media layer that a host would
drive. If the device plumbing itself is in doubt, a PC-only harness that builds
`FujiBusPacket`s and calls `rs232Disk::rs232_process()` against a fake `systemBus` capturing
`transaction_send` output closes that gap.

### 10.4 Layer 4 — hardware validation (user-run checklist)

The step Claude can't perform; Claude prepares the images and the checklist:

1. Flash firmware (`./build.sh -s <rs232-board> -cbu`).
2. Copy the fabricated `.DSK` images to the SD card / TNFS host (no sidecars — by design).
3. Mount a `.DSK` in the web UI; confirm the log shows `MediaTypeDSK`.
4. From the CP/M host, boot / read known sectors with `head/track/sector/fmttype` and confirm
   the boot sector and a data sector read correctly; write a sector and re-read it.
5. Return the serial `Debug` log; Claude diffs the on-SD image against the expected
   Python-generated image to confirm the write landed and the layout is intact.

### 10.5 Regression guard for the flat-IMG path

Independently, add the §9 test: mount an **IMG**, read/write a known sector, and assert the
resolved sector and bytes are identical **before and after** the `decode_sector()` routing
edit. Run all with `cd build && ctest --output-on-failure` (or `ctest -R diskdsk_tests`).

---

## 11. File reference (quick index)

| Concern | File | Symbol / line |
|---|---|---|
| Command dispatch | `lib/device/rs232/disk.cpp` | `rs232_process` :257 (edit cases :263–271) |
| Read / Write (device) | `lib/device/rs232/disk.cpp` | `rs232_read` :20, `rs232_write` :41 (take packet) |
| Type selection / construct | `lib/device/rs232/disk.cpp` | `mount` :182, switch :202 (add `MEDIATYPE_DSK`) |
| Base interface + enum | `lib/media/rs232/diskType.h` | `enum mediatype_t` :37, `class MediaType` :45 |
| New `decode_sector()` + buffer-accessor virtuals | `lib/media/rs232/diskType.h` | add to `class MediaType` (`decode_sector`, `sector_buffer`, `sector_buffer_size`) |
| Staging buffer (unchanged) | `lib/media/rs232/diskType.h` | `DISK_SECTORBUF_SIZE` :12 stays 512; Floppy owns `_dsk_buff[6656]` |
| Flat base `sector_size` | `lib/media/rs232/diskType.cpp` | `sector_size` :20 (returns 512; unchanged) |
| Extension → type | `lib/media/rs232/diskType.cpp` | `discover_mediatype` :60 (add `.DSK`) |
| IMG (unchanged reference) | `lib/media/rs232/diskTypeImg.cpp` | `_sector_to_offset` :17, `mount` :155, `read`/`write` :23/:62 |
| ROM `.cfg` sidecar pattern (NOT used) | `lib/media/rs232/diskTypeROM.cpp` | `mount` :137 |
| Header include list | `lib/media/media.h` | `BUILD_RS232` block :11 |
| Bus primitives | `lib/bus/rs232/rs232.h` | `transaction_*` :171–176 |
| Packet params | `lib/bus/rs232/FujiBusPacket.h` | `param`/`paramCount` :75–81 |
| Descriptor encoding | `lib/bus/rs232/FujiBusPacket.cpp` | `fieldSizeTable`/`numFieldsTable` :34, `fujibus_header` :16 |
| Disk command opcodes | `include/fujiCommandID.h` | `CMD::DISK_*` |

---

## 12. Implementation checklist

1. `diskType.h`: add `MEDIATYPE_DSK`; add four virtuals to `class MediaType` —
   `decode_sector()` (default `return count ? params[0] : 0;`), `set_geometry()` (default
   no-op, §6), `sector_buffer()` (default `_disk_sectorbuff`), `sector_buffer_size()`
   (default `DISK_SECTORBUF_SIZE`). The virtuals take plain `(const uint32_t *params,
   unsigned count)` — **no `FujiBusPacket` include**, so the media layer stays bus-agnostic.
   **`DISK_SECTORBUF_SIZE` stays 512.** No edit to `diskTypeImg.cpp` or `diskTypeROM.cpp`.
2. New `diskTypeDSK.h/.cpp` (`#ifdef BUILD_RS232`): `DSK_BUFFER_SIZE` (6656) +
   `_dsk_buff`/`_buff` and the `sector_buffer*` overrides; the `FMT_INDEX_MASK` (0x0F,
   `FMT_CUSTOM` 0x0F) / `FMT_MODE_TRACK` / `FMT_RESERVED_MASK` bits (§4.3); the
   `DSKRegion`/`DSKFormat` (with `head_major`) table + five baked formats (four uniform
   + Tarbell DD, 2-region); `decode_sector()` (reserved-bit guard, splits mode from index) /
   `locate()` (custom slot, `head_major`, `track_mode`); the custom slot + `set_geometry()`
   (append-region builder) + `ensure_buffer()` (heap grow) + dtor (§5.8); `read()`/`write()`
   (using `sector_buffer()`); `sector_size()`; `mount()` (open+size only); `status()`; stub
   `format()`.
3. `diskType.cpp`: `discover_mediatype()` maps `.DSK` → `MEDIATYPE_DSK`.
4. `disk.cpp`: `MEDIATYPE_DSK` case in `mount()`; change `rs232_read`/`rs232_write` to
   take `const FujiBusPacket &`, extract the params via `packet_params()`, and call
   `decode_sector()` with the plain array; add the `CMD::DISK_SET_GEOMETRY`
   dispatch case (§6); swap the three `_disk->_disk_sectorbuff` device accesses to
   `sector_buffer()` / `sector_buffer_size()`; update dispatch and `disk.h`.
   `include/fujiCommandID.h`: add `CMD::DISK_SET_GEOMETRY = 0x47`.
5. `media.h`: include `rs232/diskTypeDSK.h`.
6. Build ESP (`./build.sh -s <rs232-board> -cb`) and PC (`./build.sh -p RS232 -g`).
7. Tests (§10): `diskdsk_tests` doctest + offset matrix for all five baked formats (incl.
   the Tarbell DD region boundary), the custom-format cases (512-B and 1024-B uniform;
   interleaved-vs-head-major double-sided offsets; per-side geometry on track 0; Tarbell-via-
   `SET_GEOMETRY`-append equals the baked entry), `make_dsk.py` + file round-trips, flat-IMG
   regression test; `ctest --output-on-failure`.
8. Mount a fabricated `.DSK` in the running PC build (§10.3).
9. Hand the hardware checklist (§10.4) and generated images to the user.

### Open / future items

- **FORMAT via Write-Track** (§5.6) — parse a WD179x-style whole-track stream per `fmttype`;
  needs its own buffer sizing (a full IBM-DD track is 6656 bytes). Distinct from `SET_GEOMETRY`,
  which declares geometry but does not lay down structural bytes.
- **`fmttype` index assignment** — the baked `dsk_fmt_t` values (0–4) are provisional;
  confirm the numbers the host emits match, and that it sets `FMT_MODE_TRACK`/reserved bits
  per §4.3. Hosts that need arbitrary geometry use `FMT_CUSTOM` + `SET_GEOMETRY` (§5.8) and
  avoid the shared-numbering concern entirely.
- **Multi-region head-major** — `locate()` honors `head_major` in the general walk (§5.2);
  worth a targeted test for the rare double-sided *and* mixed-density custom case.
- **Index headroom** — the format index is 4 bits (16 slots): 0–4 baked, 5–14 reserved,
  15 custom. Reserved `fmttype` bits (4, 5, 7) leave room for a future access mode or flag.

---

# Appendices — reference material

The appendices document the *existing* RS232 disk layer the design sits on, the prior-art
geometry-aware media types elsewhere in the codebase, and the addressing alternatives the
constraints in the intro rule out. None of it is new work; it is background the body assumes.

## Appendix A — Reference: the existing RS232 disk layer

§2 (layers) and §3 (the two hooks) already sketch the device/media split and the read/write
paths. This appendix keeps the fuller reference the design draws on: the base `MediaType`
interface in full, the flat `MediaTypeImg` it sits beside, and the bus primitives beneath.

`packet.param(0)` is the **sector number** as a raw `uint32_t` (`FujiBusPacket.h:75`) — a
*linear/logical* sector index with no track/side information. The host addresses the disk
purely by logical sector; geometry, if any, is the media layer's business. That is the
assumption `MediaTypeDSK` replaces with per-command CHS (§4).

### A.1 The base `MediaType` interface (`lib/media/rs232/diskType.h`)

```cpp
class MediaType
{
protected:
    fnFile   *_disk_fileh       = nullptr;
    uint32_t  _disk_image_size  = 0;
    uint32_t  _disk_num_sectors = 0;
    uint32_t  _disk_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;   // 128
    int32_t   _disk_last_sector = INVALID_SECTOR_VALUE;
    uint8_t   _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

public:
    struct {                       // _percomBlock — 12-byte geometry descriptor
        uint8_t num_tracks;
        uint8_t step_rate;
        uint8_t sectors_per_trackH, sectors_per_trackL;
        uint8_t num_sides;
        uint8_t density;
        uint8_t sector_sizeH, sector_sizeL;
        uint8_t drive_present;
        uint8_t reserved1, reserved2, reserved3;
    } _percomBlock;

    uint8_t     _disk_sectorbuff[DISK_SECTORBUF_SIZE];   // 512-byte staging buffer
    mediatype_t _disktype = MEDIATYPE_UNKNOWN;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize,
                              fujiHost *host = nullptr,
                              const char *filename = nullptr) = 0;   // pure
    virtual void        unmount();

    virtual error_is_true format(uint32_t *responsesize);           // default: not impl
    virtual error_is_true read (uint32_t sectornum, uint32_t *readcount) = 0;  // pure
    virtual error_is_true write(uint32_t sectornum, bool verify);   // default: not impl

    virtual uint16_t sector_size(uint32_t sectornum);   // BASE returns 512 flat
    virtual void     status(uint8_t statusbuff[4]) = 0; // pure

    static mediatype_t discover_mediatype(const char *filename);
    void dump_percom_block();
    void derive_percom_block(uint32_t numSectors);
    virtual ~MediaType();
};
```

- The `_percomBlock` struct already models tracks / sectors-per-track / sides / sector-size.
  It is round-tripped to the host by `CMD::DISK_PERCOM_READ` / `CMD::DISK_PERCOM_WRITE`
  (`disk.cpp:139`, `:157`). RS232's `derive_percom_block()` (`diskType.cpp:40`) is an empty
  stub, so the block is left zeroed for `MediaTypeImg` — and, deliberately, for
  `MediaTypeDSK` (§5.5).
- The base `sector_size()` (`diskType.cpp:20`) returns a flat **512**. The Atari version
  special-cases the first three sectors as 128 bytes (Appendix B); RS232 dropped that.
- `mediatype_t` (`diskType.h:37`) gains `MEDIATYPE_DSK` for the new type (§7 Step 1).

### A.2 `MediaTypeImg` — the flat 512-byte LBA path (unchanged)

The type responsible for "everything is a 512-byte LBA block." `lib/media/rs232/diskTypeImg.cpp`:

```cpp
uint32_t MediaTypeImg::_sector_to_offset(uint32_t sectorNum)   // :17
{
    return (uint32_t)sectorNum * 512;     // ← hard-coded block size
}

mediatype_t MediaTypeImg::mount(fnFile *f, uint32_t disksize, fujiHost*, const char*)  // :155
{
    _disk_fileh       = f;
    _disk_num_sectors = disksize / 512;   // ← image size / 512
    _disktype         = MEDIATYPE_IMG;
    return _disktype;
}
```

`MediaTypeImg` does *not* override `sector_size()`, so every sector is the inherited flat 512.
The whole "LBA, 512-byte block, no tracks" behavior is exactly three things:

| Concern            | Where                                            | Value          |
|--------------------|--------------------------------------------------|----------------|
| bytes per sector   | inherited `MediaType::sector_size()`             | `512` (flat)   |
| sector → offset    | `MediaTypeImg::_sector_to_offset()`              | `sector * 512` |
| sector count       | `MediaTypeImg::mount()`                           | `size / 512`   |

There is no track concept anywhere in `MediaTypeImg`; the host's logical sector maps linearly
onto the file, and `_percomBlock` stays zeroed. `MediaTypeDSK` overrides all three, so
none of this hard-coding applies to it — the reason the two never share offset math (§1).

> 1-based vs 0-based subtlety: `_sector_to_offset(sector) = sector * 512` makes sector 0 →
> offset 0. The comment at `diskTypeImg.cpp:16` says "1-based" but the math is effectively
> 0-based (no `- 1`). `MediaTypeDSK` instead addresses by physical sector ID with an
> explicit `first_sector` per region (§5.1), so it never inherits this ambiguity.

### A.3 The bus transaction primitives

Declared in `lib/bus/rs232/rs232.h:171`, implemented in `rs232.cpp`:

- `transaction_accept(TRANS_STATE)` — acknowledge the command; `NO_GET` = send only,
  `WILL_GET` = a data frame will arrive from the host.
- `transaction_get(void *data, size_t len)` — pull `len` bytes from the host into `data`.
- `transaction_send(const void *data, size_t len, bool is_error=false)` — ship `len` bytes
  (with completion/error status) back to the host.
- `transaction_success()` / `transaction_error()` — terminal ACK/NAK, no payload.

These are format-agnostic byte movers keyed off the `len` the device passes — which the
device derives from `sector_size()`. Nothing here assumes 512; the 512 assumption lives
entirely in the media layer, which is why `MediaTypeDSK` needs no bus change.

## Appendix B — Prior art: geometry-aware media types elsewhere

`lib/media/atari/diskType.cpp` is the un-flattened ancestor of the RS232 media layer and
shows the geometry pattern. Two methods matter:

`MediaType::sector_size()` (`atari/diskType.cpp:20`) — per-sector sizing:

```cpp
uint16_t MediaType::sector_size(uint16_t sectornum)
{
    // 512-byte-sectored images use 128 for the first 3 boot sectors, else _disk_sector_size
    return (_disk_sector_size == 512) ? _disk_sector_size
                                      : (sectornum <= 3 ? 128 : _disk_sector_size);
}
```

`MediaType::derive_percom_block(numSectors)` (`atari/diskType.cpp:43`) fills `num_tracks`,
`sectors_per_track{H,L}`, `num_sides`, `density`, `sector_size{H,L}` from geometry (40 trk ×
18 spt SS/SD, 26 spt enhanced density, 80 trk, 8" formats, etc.). RS232's copy is an empty
stub; `MediaTypeDSK` deliberately leaves it stubbed (§5.5) because `fmttype` carries
geometry per access.

Genuinely track/geometry-aware types worth studying for a richer model:

| Media type            | Path                                  | Geometry model |
|-----------------------|---------------------------------------|----------------|
| `MediaTypeATR`        | `lib/media/atari/diskTypeAtr.*`       | ATR header + PERCOM, per-sector size, boot sectors |
| `MediaTypeATX`        | `lib/media/atari/diskTypeAtx.*`       | full track images with timing/weak bits |
| `MediaTypeWOZ`        | `lib/media/apple/mediaTypeWOZ.*`      | track-based flux/bit images |
| `MediaTypeDSK` (Apple)| `lib/media/apple/mediaTypeDSK.*`      | 35 tracks × 16 sectors, sector skew |
| `MediaTypeMOOF`       | `lib/media/mac/mediaTypeMOOF.*`       | track-based (WOZ successor) |

`MediaTypeATX`/`MediaTypeWOZ` are the closest existing models for "tracks, sectors per track,
variable/odd sector sizes." `MediaTypeDSK` differs by keeping the image a **pure sector
dump** and pushing geometry to the host's per-command `fmttype`, rather than embedding it in
a container header.

## Appendix C — Design alternatives considered (and why per-command `fmttype` won)

Extension alone can't tell an SSDD `.DSK` from a DSSD `.DSK`, and **neither can total size** —
the same byte count factors into different `sides × tracks × spt × sector_size` geometries. A
bare `.DSK` is a raw sector dump with no geometry of its own. Guessing from size is the
anti-pattern `discover_mediatype()` already warns against (`diskType.cpp:70` documents
retiring an old `disksize == 8192 || 16384 || 32768` heuristic because "it misfired on any
file of those exact sizes"). Four options can pin down geometry; the design chooses option 1
and treats the rest as fallbacks.

1. **The host declares it (`fmttype` in the packet) — chosen (§4).** The geometry authority
   is the host that formatted the disk, not the FujiNet. The image stays a pure
   geometry-agnostic dump. Decisive advantages:
   - **Needs no second artifact and no human** — no sidecar to keep in sync, no header to
     wrap bare dumps in, no per-slot config. Drop the `.DSK` on the card and it works.
   - **Correct when one host uses several geometries** — a BIOS reading a 128-byte-sector
     track 0 and 256-byte data tracks, or a utility mounting SSDD then DSSD. A single
     mount-time geometry can't express that; a per-command `fmttype` can.
   - **Two identically-sized `.DSK` files are never confused, because the FujiNet never
     decides** — the reader states the format each time.

   Its one gap: geometry isn't known at *mount* time, only at first access, so anything the
   FujiNet must answer before the host asks (`CMD::DISK_PERCOM_READ`, `status()`, a web-UI
   geometry display) has nothing to report yet (§5.5). A pure CHS CP/M boot never issues
   those, so it doesn't matter in practice; when it does, pair with a mount-time default from
   option 2 or 4 — `fmttype` still overrides per access.

2. **A sidecar `.cfg` file — precedent already in the codebase.** `MediaTypeROM` derives a
   same-named sibling and reads geometry from it (`diskTypeROM.cpp:137-207`); the
   `mount(f, disksize, host, filename)` signature carries `host`+`filename` precisely so a
   type can open that sibling (`diskType.h:76`). Resolves geometry **at mount time**, so
   PERCOM/status report correctly before the first read. **Ruled out by the "no sidecar"
   constraint** — nothing may be stored beside a `.DSK`.

3. **A self-describing container header.** ATR/WOZ/MOOF embed geometry in the file. Robust,
   but means wrapping raw dumps in a new format. **Ruled out by the "pure sector dump, no
   header" constraint.**

4. **Explicit selection at mount.** `rs232Disk::mount()` already accepts a `mediatype_t
   disk_type` override that wins over extension detection (`disk.cpp:183, 198`). In principle
   the web UI or a per-slot config entry could pass the chosen geometry.

   > **No mount-time format selector exists today** — a real obstacle, not a missing
   > convenience. The mount path carries no notion of disk geometry: the web UI builds only
   > `/mount?hostslot=<h>&deviceslot=<d>&filename=<f>&mode=<1|2>`
   > (`httpServiceBrowse.cpp:205`); `mount_params` holds host/device slot, mode, filename and
   > nothing else (`httpServiceBrowse.h:28`); `store_mount()` persists only host + path + mode
   > into `[MountN]` — `mount_info` has no type field (`fnConfig.h:416`, `fnc_save.cpp:98`),
   > and `mount_type_t` is `DISK` vs `TAPE`, not geometry. Deeper still, **`mediatype_t` is a
   > detection _output_, not a mount _input_**: `fujicore_mount_disk_image_success()` gets the
   > type back as a return value (`fujiDevice.cpp:640`); RS232's override hard-codes
   > `MEDIATYPE_UNKNOWN` on the way in (`rs232Fuji.h:19`). So every disk mount is auto-detected
   > by extension and no build feeds a user-chosen type through the UI. Reaching a new type by
   > option-4 selection would require new plumbing end to end (a slot-picker control, a `/mount`
   > query param, a `mount_params` field, threading the type through `mount_file` →
   > `fujicore_mount_disk_image_success` → `mount_media`, a widened `mount_info`/`[MountN]`
   > schema, and the same param mirrored in the REST and Mongoose mount entry points). This is
   > the concrete reason option 1 is preferred: per-command `fmttype` needs none of it — the
   > image mounts exactly as today and the host supplies geometry at access time.

In short: **lead with option 1**; add option 2 or 4 only if a mount-time default is ever
needed for PERCOM/status/web-UI, with `fmttype` still overriding per access. What every case
avoids is inferring geometry from image size.
