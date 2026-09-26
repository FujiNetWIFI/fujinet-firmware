#ifdef BUILD_MAC
#include "floppy.h"
#include "../../media/mac/macGCR.h"
#include "../../media/mac/mediaTypeDCD.h"
#include <esp_heap_caps.h>
#include "../bus/mac/mac_ll.h"
#include "../../include/debug.h"
#include <cstring>

#define NS_PER_BIT_TIME 125
#define BLANK_TRACK_LEN 6400

mediatype_t macFloppy::mount(FILE *f, const char *filename, uint32_t disksize,
                             disk_access_flags_t access_mode, mediatype_t disk_type)
{
  mediatype_t mt = MEDIATYPE_UNKNOWN;

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    unmount();
  }

  if (disk_type == MEDIATYPE_UNKNOWN)
    disk_type = MediaType::discover_mediatype(filename);

  if (disk_type == MEDIATYPE_SIT)
  {
    // Unpack the archive's disk image into PSRAM, then mount that like a
    // host file under the inner image's name
    _sit = std::make_unique<SitMount>();
    if (_sit->extract(f, filename).is_error())
    {
      Debug_printf("\nStuffIt mount: could not extract a disk image from '%s'\n", filename);
      _sit.reset();
      fclose(f);
      device_active = false;
      return MEDIATYPE_UNKNOWN;
    }

    bool wants_floppy = is_floppy_slot();
    bool image_is_floppy = (_sit->kind == sit_image_kind_t::FLOPPY);
    if (wants_floppy != image_is_floppy)
    {
      Debug_printf("\nStuffIt mount: '%s' -> '%s' is a %s image, slot %c is a %s slot - refusing\n",
                   filename, _sit->inner_filename,
                   image_is_floppy ? "floppy" : "HD20",
                   disk_num + 1,
                   wants_floppy ? "floppy" : "HD20");
      _sit.reset();
      fclose(f);
      device_active = false;
      return MEDIATYPE_UNKNOWN;
    }

    Debug_printf("\nStuffIt mount: '%s' -> '%s' (%u bytes, %s)\n",
                 filename, _sit->inner_filename, _sit->image_len,
                 image_is_floppy ? "floppy" : "HD20");

    fclose(f); // the image in PSRAM replaces the archive file

    mediatype_t result = mount(_sit->image_fh, _sit->inner_filename, _sit->image_len,
                                access_mode, _sit->disk_type);
    if (result == MEDIATYPE_UNKNOWN)
      _sit.reset();
    return result;
  }

  _disk_size_in_blocks = disksize / 512;
  readonly = !(access_mode & DISK_ACCESS_MODE_WRITE);

  // Sector images go to the floppy encoder when they land in the floppy
  // slot; the same file in slots 1-4 is served as an HD20 instead.
  bool floppy_sector_image = is_floppy_slot() &&
                             (disk_type == MEDIATYPE_DSK || disk_type == MEDIATYPE_DC42);

  switch (disk_type)
  {
  case MEDIATYPE_MOOF:
  case MEDIATYPE_DSK:
  case MEDIATYPE_DC42:
    if (!is_floppy_slot())
    {
      if (disk_type == MEDIATYPE_MOOF)
      {
        Debug_printf("\nMOOF images can only be mounted in slot %d (floppy), not slot %c\n",
                     MAC_FLOPPY_SLOT + 1, disk_num + 1);
        return MEDIATYPE_UNKNOWN;
      }
      goto mount_dcd;
    }
    if (floppy_sector_image)
    {
      Debug_printf("\nMounting sector image as GCR floppy");
      _disk = new MediaTypeFloppyImage();
      mt = ((MediaTypeFloppyImage *)_disk)->mount(f, disksize);
    }
    else
    {
      Debug_printf("\nMounting Media Type MOOF");
      _disk = new MediaTypeMOOF();
      mt = ((MediaTypeMOOF *)_disk)->mount(f);
    }
    if (mt == MEDIATYPE_UNKNOWN)
    {
      Debug_printf("\nFloppy mount failed");
      delete _disk;
      _disk = nullptr;
      device_active = false;
      return MEDIATYPE_UNKNOWN;
    }
    device_active = true;
    _sector_image = floppy_sector_image;
    _wcap_active = false;
    _wcap_len = 0;
    track_pos = 0;
    old_pos = 2;     // make different to force change_track buffer copy
    change_track(0); // initialize rmt buffer
    change_track(1); // initialize rmt buffer
    switch (_disk->num_sides)
    {
    case 1:
      SYSTEM_BUS.write((uint8_t)'s');
      SYSTEM_BUS.write((uint8_t)(track_pos | 128));
      _disk_inserted = true;
      break;
    case 2:
      SYSTEM_BUS.write((uint8_t)'d');
      SYSTEM_BUS.write((uint8_t)(track_pos | 128));
      _disk_inserted = true;
      break;
    default:
      break;
    }
    // a sector image mounted read/write may be written; MOOFs and read-only
    // mounts stay write protected on the Pico
    SYSTEM_BUS.write((uint8_t)((_sector_image && !readonly) ? 'u' : 'l'));
    break;
  mount_dcd:
    if (!is_dcd_slot())
    {
      Debug_printf("\nDCD (HD20) images can only be mounted in slots 1-%d, not slot %c\n",
                   MAC_DCD_SLOTS, disk_num + 1);
      return MEDIATYPE_UNKNOWN;
    }
    if (disk_type == MEDIATYPE_DC42)
    {
      Debug_printf("\nMounting Media Type DC42 for DCD");
      _disk = new MediaTypeDCD(0x54); // offset of image data in Disk Copy 4.2 file
    }
    else
    {
      Debug_printf("\nMounting Media Type DSK for DCD");
      _disk = new MediaTypeDCD();
    }
    static_cast<MediaTypeDCD *>(_disk)->set_readonly(readonly);
    mt = ((MediaTypeDCD *)_disk)->mount(f, disksize);
    if (mt == MEDIATYPE_UNKNOWN)
    {
      Debug_printf("\nDCD mount failed");
      delete _disk;
      _disk = nullptr;
      device_active = false;
      return MEDIATYPE_UNKNOWN;
    }
    // the media decides how many blocks the Mac sees (drive images only
    // expose their HFS partition, DC42 images strip their header)
    _disk_size_in_blocks = _disk->num_blocks;
    device_active = true;
    SYSTEM_BUS.add_dcd_mount(id());
    break;
  default:
    Debug_printf("\nMedia Type UNKNOWN - no mount in floppy.cpp");
    device_active = false;
    break;
  }

  return mt;
}

// void macFloppy::init()
// {
//   track_pos = 80;
//   old_pos = 0;
//   device_active = false;
// }

/* MCI/DCD signals

 * 800 KB GCR Drive
CA2         CA1     CA0     SEL     RD Output       PIO
Low         Low     Low     Low     !DIRTN          latch
Low         Low     Low     High          !CSTIN          latch
Low         Low     High          Low       !STEP           latch
Low         Low     High          High    !WRPROT         latch
Low         High          Low       Low     !MOTORON        latch
Low         High    Low     High    !TK0            latch
Low         High          High    Low       SWITCHED        latch
Low         High          High    High    !TACH           tach
High      Low       Low     Low     RDDATA0         echo
High      Low       Low     High          RDDATA1         echo
High      Low       High          Low       SUPERDRIVE      latch
High      Low       High          High    +               latch
High      High    Low       Low     SIDES           latch
High      High    Low       High          !READY          latch
High      High    High    Low       !DRVIN          latch
High      High    High    High    REVISED         latch
+ TODO

Signal Descriptions
Signal Name     Description
!DIRTN        Step direction; low=toward center (+), high=toward rim (-)
!CSTIN        Low when disk is present
!STEP           Low when track step has been requested
!WRPROT       Low when disk is write protected or not inserted
!MOTORON            Low when drive motor is on
!TK0            Low when head is over track 0 (outermost track)
SWITCHED            High when disk has been changed since signal was last cleared
!TACH           Tachometer; frequency reflects drive speed in RPM
INDEX           Pulses high for ~2 ms once per rotation
RDDATA0       Signal from bottom head; falling edge indicates flux transition
RDDATA1       Signal from top head; falling edge indicates flux transition
SUPERDRIVE        High when a Superdrive (FDHD) is present
MFMMODE       High when drive is in MFM mode
SIDES           High when drive has a top head in addition to a bottom head
!READY        Low when motor is at proper speed and head is ready to step
!DRVIN        Low when drive is installed
REVISED       High for double-sided double-density drives, low for single-sided double-density drives
PRESENT/!HD       High when a double-density (not high-density) disk is present on a high-density drive
DCDDATA       Communication channel from DCD device to Macintosh
!HSHK           Low when DCD device is ready to receive or wishes to send

*/

void macFloppy::unmount()
{
  // anything mounted in slots 1-4 is an HD20, archive-backed or not
  bool was_dcd = (_disk != nullptr) && is_dcd_slot();

  if (_disk != nullptr)
  {
    _disk->unmount();
    delete _disk; // virtual destructor, handles MOOF track buffers
    _disk = nullptr;
  }

  if (_sit != nullptr)
  {
    _sit->image_fh = nullptr; // already closed by _disk->unmount()
    _sit.reset();
  }

  if (was_dcd)
    SYSTEM_BUS.rem_dcd_mount(id());
  else if (is_floppy_slot() && _disk_inserted)
  {
    // only once, and only if the Pico was told a disk was inserted
    floppy_ll.stop();
    SYSTEM_BUS.write((uint8_t)'r');
    _disk_inserted = false;
  }
  device_active = false;
}

void macFloppy::flush_if_idle()
{
  if (_disk != nullptr && is_dcd_slot())
    static_cast<MediaTypeDCD *>(_disk)->flush_if_idle();
}

int IRAM_ATTR macFloppy::step()
{
  // done - todo: move head by 2 steps (keep on even track numbers) for side 0 of disk
  // done - todo: change_track() should copy both even and odd tracks from SPRAM to DRAM
  // todo: the next_bit() should pick from even or odd track buffer based on HDSEL

  if (!device_active)
    return -1;

  old_pos = track_pos;
  track_pos += 2 * head_dir;
  if (track_pos < 0)
  {
    track_pos = 0;
  }
  else if (track_pos > MAX_TRACKS - 2)
  {
    track_pos = MAX_TRACKS - 2;
  }
  // change_track(0);
  // change_track(1);
  return (track_pos / 2);
}

void macFloppy::update_track_buffers()
{
  if (device_active && _disk != nullptr)
    act_reads++;
  change_track(0);
  change_track(1);
}

// copy the current cylinder into the RMT buffers unconditionally (after a
// sector of it was rewritten)
void macFloppy::reload_track_buffers()
{
  if (!device_active || _disk == nullptr)
    return;
  for (int side = 0; side < 2; side++)
  {
    int tp = track_pos + ((_disk->num_sides == 1) ? 0 : side);
    if (_disk->trackmap(tp) != 255)
      floppy_ll.copy_track(_disk->get_track(tp), side, _disk->track_len(tp), _disk->num_bits(tp),
                           NS_PER_BIT_TIME * _disk->optimal_bit_timing);
  }
}

#define WCAP_BYTES 16384 // 131072 bits: more than the longest track (74432 bits)

void macFloppy::write_capture_data(const uint8_t *p, size_t n)
{
  if (!_wcap_active)
  {
    if (_wcap == nullptr)
    {
      _wcap = (uint8_t *)heap_caps_malloc(WCAP_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
      if (_wcap == nullptr)
      {
        Debug_printf("\nFloppy write: no memory for capture buffer");
        return;
      }
    }
    _wcap_active = true;
    _wcap_overflow = false;
    _wcap_len = 0;
    _wcap_side = floppy_ll.mac_headsel_val() ? 1 : 0; // SEL is steady through a write
  }
  if (_wcap_len + n > WCAP_BYTES)
  {
    _wcap_overflow = true;
    n = WCAP_BYTES - _wcap_len;
  }
  memcpy(_wcap + _wcap_len, p, n);
  _wcap_len += n;
}

void macFloppy::write_capture_end()
{
  if (!_wcap_active)
    return;
  _wcap_active = false;

  Debug_printf("\nFloppy write: %u bytes captured at cyl %d side %d%s", (unsigned)_wcap_len,
               track_pos / 2, _wcap_side, _wcap_overflow ? " (overflow)" : "");

  if (_disk == nullptr || !_sector_image || readonly)
  {
    Debug_printf("\nFloppy write: ignored (%s)", _disk == nullptr ? "no disk" : !_sector_image ? "not a sector image" : "read-only mount");
    return;
  }

  static mac_gcr_written_sector *ws = nullptr;
  if (ws == nullptr)
    ws = (mac_gcr_written_sector *)heap_caps_malloc(sizeof(mac_gcr_written_sector) * MAC_GCR_MAX_SECTORS,
                                                    MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
  if (ws == nullptr)
    return;

  int n = mac_gcr_decode_capture(_wcap, _wcap_len * 8, ws, MAC_GCR_MAX_SECTORS);
  int written = 0;
  for (int i = 0; i < n; i++)
  {
    int cyl = ws[i].cyl >= 0 ? ws[i].cyl : track_pos / 2;
    int side = ws[i].side >= 0 ? ws[i].side : _wcap_side;
    if (_disk->num_sides == 1)
      side = 0; // a 400K disk has one side whatever the head-select line says
    if (ws[i].cyl >= 0 && (ws[i].cyl != track_pos / 2 || ws[i].side != _wcap_side))
      Debug_printf("\nFloppy write: header says C%d H%d, head is at C%d H%d", ws[i].cyl, ws[i].side,
                   track_pos / 2, _wcap_side);
    if (!ws[i].checksum_ok)
    {
      Debug_printf("\nFloppy write: bad checksum for C%d H%d S%d, not stored", cyl, side, ws[i].sector);
      continue;
    }
    if (((MediaTypeFloppyImage *)_disk)->write_sector(cyl, side, ws[i].sector, ws[i].data))
      written++;
  }
  if (n == 0)
    Debug_printf("\nFloppy write: no data field found in capture");
  act_writes += written;
  act_errors += (n > written) ? n - written : 0;
  if (written)
    reload_track_buffers();
}

void IRAM_ATTR macFloppy::change_track(int side)
{
  if (!device_active || _disk == nullptr)
    return;

  // a single-sided disk serves its one track whichever head is selected
  int ds = (_disk->num_sides == 1) ? 0 : side;
  int tp = track_pos + ds;
  int op = old_pos + ds;

  if (op == tp)
    return;

  // should only copy track data over if it's changed
  if (_disk->trackmap(op) == _disk->trackmap(tp))
    return;

  // need to tell diskii_xface the number of bits in the track
  // and where the track data is located so it can convert it
  if (_disk->trackmap(tp) != 255)
    floppy_ll.copy_track(
        _disk->get_track(tp),
        side,
        _disk->track_len(tp),
        _disk->num_bits(tp),
        NS_PER_BIT_TIME * _disk->optimal_bit_timing);
  else
    floppy_ll.copy_track(
        nullptr,
        side,
        BLANK_TRACK_LEN,
        BLANK_TRACK_LEN * 8,
        NS_PER_BIT_TIME * _disk->optimal_bit_timing);
  // Since the empty track has no data, and therefore no length, using a fake length of 51,200 bits (6400 bytes) works very well.
}

void macFloppy::dcd_status(uint8_t* payload)
{
   const uint8_t icon[] = {0b11111111, 0b11111110, 0b11111111, 0b11111111,
                          0b11111111, 0b11111110, 0b11111111, 0b11111111,
                          0b11111111, 0b11111110, 0b11111111, 0b11111111,
                          0b11111111, 0b11111000, 0b00111111, 0b11111111,
                          0b11111111, 0b11110000, 0b00011111, 0b11111111,
                          0b11111110, 0b11100000, 0b00001110, 0b11111111,
                          0b11111110, 0b11100000, 0b00001110, 0b11111111,
                          0b11111000, 0b00000000, 0b00000000, 0b00011111,
                          0b11111110, 0b11100000, 0b00001110, 0b11111111,
                          0b11111110, 0b11100000, 0b00001110, 0b11111111,
                          0b11111110, 0b11110000, 0b00011110, 0b11111111,
                          0b11111000, 0b00111000, 0b00111000, 0b00111111,
                          0b11110000, 0b00010011, 0b10010000, 0b00011111,
                          0b11100000, 0b00000111, 0b11000000, 0b00001111,
                          0b11100000, 0b00001111, 0b11100000, 0b00001111,
                          0b00000000, 0b00001111, 0b11100000, 0b00000111,
                          0b11100000, 0b00001111, 0b11100000, 0b00001111,
                          0b11100000, 0b00000111, 0b11000000, 0b00001111,
                          0b11110000, 0b00010011, 0b10010000, 0b00011111,
                          0b11111000, 0b00111000, 0b00111000, 0b00111111,
                          0b11111110, 0b11111110, 0b11110000, 0b00011111,
                          0b11111110, 0b11111110, 0b11100000, 0b00001111,
                          0b11111110, 0b11111110, 0b11100000, 0b00001111,
                          0b11111110, 0b00000000, 0b00000000, 0b00000011,
                          0b11111111, 0b11111110, 0b11100000, 0b00001111,
                          0b11111111, 0b11111110, 0b11100000, 0b00001111,
                          0b11111111, 0b11111110, 0b11110000, 0b00011111,
                          0b11111111, 0b11111110, 0b11111000, 0b00111111,
                          0b11111111, 0b11111110, 0b11111110, 0b11111111,
                          0b11111111, 0b11111110, 0b11111110, 0b11111111,
                          0b11111111, 0b11111000, 0b00000000, 0b00000000,
                          0b11111111, 0b11111110, 0b11111110, 0b11111111};

   const uint8_t numset[4][6] = {
    {0b00011000,
     0b00111000,
     0b00011000,
     0b00011000,
     0b00011000,
     0b01111110},

    {0b00111100,
     0b01100110,
     0b00001100,
     0b00011000,
     0b00110000,
     0b01111110},

    {0b01111110,
     0b00001100,
     0b00011000,
     0b00001100,
     0b01100110,
     0b00111100},

    {0b00001100,
     0b00011100,
     0b00111100,
     0b01101100,
     0b01111110,
     0b00001100}
    };

  /*
    DCD Device:
    Offset      Value   Sample Value from HD20
    0   0x83
    1   0x00
    2-5 Status
    6-7 Device type     0x0001
    8-9 Device manufacturer     0x0001
    10  Device characteristics bit field (see below)    0xE6
    11-13       Number of blocks        0x009835
    14-15       Number of spare blocks  0x0045
    16-17       Number of bad blocks    0x0001
    18-69       Manufacturer reserved
    70-197      Icon (see below)
    198-325     Icon mask (see below)
    326 Device location string length
    327-341     Device location string
    342 Checksum

    The device characteristics bit field is defined as follows:
    Value       Meaning
    0x80        Mountable
    0x40        Readable
    0x20        Writable
    0x10        Ejectable (see below)
    0x08        Write protected
    0x04        Icon included
    0x02        Disk in place (see below)

    The "ejectable" and "disk in place" bits are ostensibly intended to support removable media, however no mechanism for ejecting disks is known to exist in the protocol.
*/
  payload[7] = 1;
  payload[9] = 1;
  payload[10] = 0x80 | 0x40 | 0x20 | 0x04 | 0x02; // real HD20 says 0xe6, which is same;
  if (readonly) {payload[10] |= 0x08;};
  // no way to eject DCD's, so 0x10 and 0x02 cannot change.

  payload[11] = (_disk_size_in_blocks >> 16) & 0xff;
  payload[12] = (_disk_size_in_blocks >> 8) & 0xff;
  payload[13] = _disk_size_in_blocks & 0xff;

  memcpy(&payload[70], icon, sizeof(icon));
  for (int i = 0 ; i < 6 ; i++)
  {
    payload[70+96+4*i]=~numset[(get_disk_number()-'0') & 3][i];
  }
  memset(&payload[198], 0xff, 128);
  payload[326] = 10; // seems to be limited to 12 chars
  strcpy((char*)&payload[327],"FujiNet_D");
  payload[336] = get_disk_number()+1;
}

void macFloppy::process(mac_cmd_t cmd)
{
  uint32_t sector_num;
  uint8_t buffer[512];
  uint8_t s[3];

  switch (cmd)
  {
  case 'R':
    SYSTEM_BUS.read_exact(s, 3);
    if (_disk == nullptr)
    {
      Debug_printf("\nDCD read with no media mounted");
      memset(buffer, 0, sizeof(buffer));
      SYSTEM_BUS.write(buffer, sizeof(buffer));
      break;
    }
    sector_num = ((uint32_t)s[0] << 16) + ((uint32_t)s[1] << 8) + (uint32_t)s[2];
    Debug_printf("\nDCD sector request: %06lx", sector_num);
    if (_disk->read(sector_num, buffer))
    {
      Debug_printf("\nError Reading Sector %06lx",sector_num);
      act_errors++;
    }
    else
      act_reads++;
    // todo: error handling
    SYSTEM_BUS.write(buffer, sizeof(buffer));
    break;
  case 'T':
    act_status++;
    // flush the write cache before the Mac looks at status
    if (_disk != nullptr && is_dcd_slot())
      static_cast<MediaTypeDCD *>(_disk)->flush();
    memset(buffer,0,sizeof(buffer));
    dcd_status(buffer);
    Debug_printf("\nSending STATUS block");
    SYSTEM_BUS.write(&buffer[6], 336); // status info block is 336 char's without header and checksum
    break;
  case 'W':
    // code on PICO:
    // uart_putc_raw(UART_ID, 'W');
    // uart_putc_raw(UART_ID, (sector >> 16) & 0xff);
    // uart_putc_raw(UART_ID, (sector >> 8) & 0xff);
    // uart_putc_raw(UART_ID, sector & 0xff);
    // sector++;
    // uart_write_blocking(UART_ID, &payload[26], 512);
    SYSTEM_BUS.read_exact(s, 3);
    SYSTEM_BUS.read_exact(buffer, sizeof(buffer));
    sector_num = ((uint32_t)s[0] << 16) + ((uint32_t)s[1] << 8) + (uint32_t)s[2];
    Debug_printf("\nDCD sector write: %06lx", sector_num);
    if (_disk == nullptr || readonly || _disk->write(sector_num, buffer))
    {
      Debug_printf("\nError Writing Sector %06lx", sector_num);
      act_errors++;
      SYSTEM_BUS.write((uint8_t)'e');
    }
    else
    {
      act_writes++;
      SYSTEM_BUS.write((uint8_t)'w');
    }
    break;
  default:
    break;
  }
}

#endif // BUILD_MAC
