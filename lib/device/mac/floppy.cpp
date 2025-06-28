#ifdef BUILD_MAC
#include "floppy.h"
#include "../bus/mac/mac_ll.h"
#include <cstring>

#define NS_PER_BIT_TIME 125
#define BLANK_TRACK_LEN 6400

mediatype_t macFloppy::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{

  mediatype_t mt = MEDIATYPE_UNKNOWN;
  // mediatype_t disk_type = MEDIATYPE_WOZ;

  // Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    delete _disk;
    _disk = nullptr;
  }

  if (disk_type == MEDIATYPE_UNKNOWN)
    disk_type = MediaType::discover_mediatype(filename);

  _disk_size_in_blocks = disksize/512;

  switch (disk_type)
  {
  case MEDIATYPE_MOOF:
    Debug_printf("\nMounting Media Type MOOF");
    // init();
    device_active = (id() == '4');
    _disk = new MediaTypeMOOF();
    mt = ((MediaTypeMOOF *)_disk)->mount(f);
    track_pos = 0;
    old_pos = 2; // makde different to force change_track buffer copy
    change_track(0); // initialize rmt buffer
    change_track(1); // initialize rmt buffer
    switch (_disk->num_sides)
    {
    case 1:
      fnUartBUS.write('s');
      fnUartBUS.write(track_pos | 128);
      break;
    case 2:
      fnUartBUS.write('d');
      fnUartBUS.write(track_pos | 128);
    default:
      break;
    }
    break;
  case MEDIATYPE_DSK:
    Debug_printf("\nMounting Media Type DSK for DCD");
    device_active = true;
    _disk = new MediaTypeDCD();
    mt = ((MediaTypeDCD *)_disk)->mount(f);
    MAC.add_dcd_mount(id());
    break;
  case MEDIATYPE_DC42:
    Debug_printf("\nMounting Media Type DC42 for DCD");
    device_active = true;
    _disk = new MediaTypeDCD(0x54); // offset of image data in Disk Copy 4.2 file
    mt = ((MediaTypeDCD *)_disk)->mount(f);
    MAC.add_dcd_mount(id());
    break;
  // case MEDIATYPE_DSK:
  //   Debug_printf("\nMounting Media Type DSK");
  //   device_active = true;
  //   _disk = new MediaTypeDSK();
  //   mt = ((MediaTypeDSK *)_disk)->mount(f);
  //   change_track(0); // initialize spi buffer
  //   break;
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
CA2	    CA1	    CA0	    SEL	    RD Output       PIO
Low	    Low	    Low	    Low	    !DIRTN          latch
Low	    Low	    Low	    High	  !CSTIN          latch
Low	    Low	    High	  Low	    !STEP           latch
Low	    Low	    High	  High	  !WRPROT         latch
Low	    High	  Low	    Low	    !MOTORON        latch
Low	    High    Low     High    !TK0            latch
Low	    High	  High	  Low	    SWITCHED        latch
Low	    High	  High	  High	  !TACH           tach
High	  Low	    Low	    Low	    RDDATA0         echo
High	  Low	    Low	    High	  RDDATA1         echo
High	  Low	    High	  Low	    SUPERDRIVE      latch
High	  Low	    High	  High	  +               latch
High	  High	  Low	    Low	    SIDES           latch
High	  High	  Low	    High	  !READY          latch
High	  High	  High	  Low	    !DRVIN          latch
High	  High	  High	  High	  REVISED         latch
+ TODO

Signal Descriptions
Signal Name 	Description
!DIRTN	      Step direction; low=toward center (+), high=toward rim (-)
!CSTIN	      Low when disk is present
!STEP	        Low when track step has been requested
!WRPROT	      Low when disk is write protected or not inserted
!MOTORON	    Low when drive motor is on
!TK0	        Low when head is over track 0 (outermost track)
SWITCHED	    High when disk has been changed since signal was last cleared
!TACH	        Tachometer; frequency reflects drive speed in RPM
INDEX	        Pulses high for ~2 ms once per rotation
RDDATA0	      Signal from bottom head; falling edge indicates flux transition
RDDATA1	      Signal from top head; falling edge indicates flux transition
SUPERDRIVE	  High when a Superdrive (FDHD) is present
MFMMODE	      High when drive is in MFM mode
SIDES	        High when drive has a top head in addition to a bottom head
!READY	      Low when motor is at proper speed and head is ready to step
!DRVIN	      Low when drive is installed
REVISED	      High for double-sided double-density drives, low for single-sided double-density drives
PRESENT/!HD	  High when a double-density (not high-density) disk is present on a high-density drive
DCDDATA	      Communication channel from DCD device to Macintosh
!HSHK	        Low when DCD device is ready to receive or wishes to send

*/

void macFloppy::unmount()
{
  // todo - check device type and call correct unmount() 
  // ((MediaTypeMOOF *)_disk)->unmount();
  if (disktype() == mediatype_t::MEDIATYPE_MOOF)
    ((MediaTypeMOOF *)_disk)->unmount();
  else if (_disk != nullptr)
    _disk->unmount();
  if (_disk != nullptr)
    free(_disk);

  _disk = nullptr;

  MAC.rem_dcd_mount(id());
  device_active = false;
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
  change_track(0);
  change_track(1);
}

void IRAM_ATTR macFloppy::change_track(int side)
{
  int tp = track_pos + side;
  int op = old_pos + side;

  if (!device_active)
    return;

  if (op == tp)
    return;

  // should only copy track data over if it's changed
  if (((MediaTypeMOOF *)_disk)->trackmap(op) == ((MediaTypeMOOF *)_disk)->trackmap(tp))
    return;

  // need to tell diskii_xface the number of bits in the track
  // and where the track data is located so it can convert it
  if (((MediaTypeMOOF *)_disk)->trackmap(tp) != 255)
    floppy_ll.copy_track(
        ((MediaTypeMOOF *)_disk)->get_track(tp), 
        side,
        ((MediaTypeMOOF *)_disk)->track_len(tp),
        ((MediaTypeMOOF *)_disk)->num_bits(tp),
        NS_PER_BIT_TIME * ((MediaTypeMOOF *)_disk)->optimal_bit_timing);
  else
    floppy_ll.copy_track(
        nullptr,
        side,
        BLANK_TRACK_LEN,
        BLANK_TRACK_LEN * 8,
        NS_PER_BIT_TIME * ((MediaTypeMOOF *)_disk)->optimal_bit_timing);
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
    Offset	Value	Sample Value from HD20
    0	0x83
    1	0x00
    2-5	Status
    6-7	Device type	0x0001
    8-9	Device manufacturer	0x0001
    10	Device characteristics bit field (see below)	0xE6
    11-13	Number of blocks	0x009835
    14-15	Number of spare blocks	0x0045
    16-17	Number of bad blocks	0x0001
    18-69	Manufacturer reserved
    70-197	Icon (see below)
    198-325	Icon mask (see below)
    326	Device location string length
    327-341	Device location string
    342	Checksum

    The device characteristics bit field is defined as follows:
    Value	Meaning
    0x80	Mountable
    0x40	Readable
    0x20	Writable
    0x10	Ejectable (see below)
    0x08	Write protected
    0x04	Icon included
    0x02	Disk in place (see below)

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
    payload[70+96+4*i]=~numset[get_disk_number()-'0'][i];
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
  char s[3];

  switch (cmd)
  {
  case 'R':
    fnUartBUS.readBytes(s, 3);
    sector_num = ((uint32_t)s[0] << 16) + ((uint32_t)s[1] << 8) + (uint32_t)s[2];
    Debug_printf("\nDCD sector request: %06lx", sector_num);
    if (_disk->read(sector_num, buffer))
      Debug_printf("\nError Reading Sector %06lx",sector_num);
    // todo: error handling
    fnUartBUS.write(buffer, sizeof(buffer));
    break;
  case 'T':
    memset(buffer,0,sizeof(buffer));
    dcd_status(buffer);
    Debug_printf("\nSending STATUS block");
    fnUartBUS.write(&buffer[6], 336); // status info block is 336 char's without header and checksum
    break;
  case 'W':
    // code on PICO:
    // uart_putc_raw(UART_ID, 'W');
    // uart_putc_raw(UART_ID, (sector >> 16) & 0xff);
    // uart_putc_raw(UART_ID, (sector >> 8) & 0xff);
    // uart_putc_raw(UART_ID, sector & 0xff);
    // sector++;
    // uart_write_blocking(UART_ID, &payload[26], 512);
    fnUartBUS.readBytes(s, 3);
    fnUartBUS.readBytes(buffer, sizeof(buffer));
    sector_num = ((uint32_t)s[0] << 16) + ((uint32_t)s[1] << 8) + (uint32_t)s[2];
    Debug_printf("\nDCD sector write: %06lx", sector_num);
    if (_disk->write(sector_num, buffer))
    {
      Debug_printf("\nError Writing Sector %06lx", sector_num);
      fnUartBUS.write('e');
    }
    else
    {
      fnUartBUS.write('w');
    }
    break;
  default:
    break;
  }
}

#endif // BUILD_MAC

#if 0
#include "disk2.h"

#include "fnSystem.h"
#include "fuji.h"
#include "fnHardwareTimer.h"

#define NS_PER_BIT_TIME 125
#define BLANK_TRACK_LEN 6400

const int8_t phase2seq[16] = {-1, 0, 2, 1, 4, -1, 3, -1, 6, 7, -1, -1, 5, -1, -1, -1};
const int8_t seq2steps[8] = {0, 1, 2, 3, 0, -3, -2, -1};

iwmDisk2::~iwmDisk2()
{
}

void iwmDisk2::shutdown()
{
}

iwmDisk2::iwmDisk2()
{
  track_pos = 80;
  old_pos = 0;
  oldphases = 0;
  Debug_printf("\nNew Disk ][ object");
  device_active = false;
}

void iwmDisk2::init()
{
  track_pos = 80;
  old_pos = 0;
  oldphases = 0;
  device_active = false;
}

mediatype_t iwmDisk2::mount(FILE *f, mediatype_t disk_type) //, const char *filename), uint32_t disksize, mediatype_t disk_type)
{

  mediatype_t mt = MEDIATYPE_UNKNOWN;
  // mediatype_t disk_type = MEDIATYPE_WOZ;

  // Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    delete _disk;
    _disk = nullptr;
  }

  switch (disk_type)
  {
  case MEDIATYPE_WOZ:
    Debug_printf("\nMounting Media Type WOZ");
    device_active = true;
    _disk = new MediaTypeWOZ();
    mt = ((MediaTypeWOZ *)_disk)->mount(f);
    change_track(0); // initialize spi buffer
    break;
  case MEDIATYPE_DSK:
    Debug_printf("\nMounting Media Type DSK");
    device_active = true;
    _disk = new MediaTypeDSK();
    mt = ((MediaTypeDSK *)_disk)->mount(f);
    change_track(0); // initialize spi buffer
    break;
  default:
    Debug_printf("\nMedia Type UNKNOWN - no mount in disk2.cpp");
    device_active = false;
    break;
  }

  return mt;
}

void iwmDisk2::unmount()
{
}

bool iwmDisk2::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
  return false;
}

bool IRAM_ATTR iwmDisk2::phases_valid(uint8_t phases)
{
  return (phase2seq[phases] != -1);
}

bool IRAM_ATTR iwmDisk2::move_head()
{
  int delta = 0;
  uint8_t newphases = smartport.iwm_phase_vector(); // could access through IWM instead
  if (phases_valid(newphases))
  {
    int idx = (phase2seq[newphases] - phase2seq[oldphases] + 8) % 8;
    delta = seq2steps[idx];

    // phases_lut[oldphases][newphases];
    old_pos = track_pos;
    track_pos += delta;
    if (track_pos < 0)
    {
      track_pos = 0;
    }
    else if (track_pos > MAX_TRACKS - 1)
    {
      track_pos = MAX_TRACKS - 1;
    }
    oldphases = newphases;
  }
  return (delta != 0);
}

void IRAM_ATTR iwmDisk2::change_track(int indicator)
{
  if (!device_active)
    return;

  if (old_pos == track_pos)
    return;

  // should only copy track data over if it's changed
  if (((MediaTypeWOZ *)_disk)->trackmap(old_pos) == ((MediaTypeWOZ *)_disk)->trackmap(track_pos))
    return;

  // need to tell diskii_xface the number of bits in the track
  // and where the track data is located so it can convert it
  if (((MediaTypeWOZ *)_disk)->trackmap(track_pos) != 255)
    diskii_xface.copy_track(
        ((MediaTypeWOZ *)_disk)->get_track(track_pos),
        ((MediaTypeWOZ *)_disk)->track_len(track_pos),
        ((MediaTypeWOZ *)_disk)->num_bits(track_pos),
        NS_PER_BIT_TIME * ((MediaTypeWOZ *)_disk)->optimal_bit_timing);
  else
    diskii_xface.copy_track(
        nullptr,
        BLANK_TRACK_LEN,
        BLANK_TRACK_LEN * 8,
        NS_PER_BIT_TIME * ((MediaTypeWOZ *)_disk)->optimal_bit_timing);
  // Since the empty track has no data, and therefore no length, using a fake length of 51,200 bits (6400 bytes) works very well.
}

#endif /* BUILD_APPLE */