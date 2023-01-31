#ifdef BUILD_APPLE
#include "disk2.h"

#include "fnSystem.h"
#include "led.h"
#include "fuji.h"
#include "fnHardwareTimer.h"

// const int8_t phases_lut [16][16] = {
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 2, 1, 0, 0, 0, 0,-2,-1, 0, 0, 0, 0, 0, 0},
// { 0,-2, 0,-1, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0,-1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0, 0, 0, 0},
// { 0, 0,-2, 0, 0, 0,-1, 0, 2, 0, 0, 0, 1, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0,-1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 2, 0, 0,-2, 0, 0, 0, 0, 1, 0, 0,-1, 0, 0, 0},
// { 0, 1, 0, 0, 0, 0, 0, 0,-1, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0,-1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
// { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
// };

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

mediatype_t iwmDisk2::mount(FILE *f)//, const char *filename), uint32_t disksize, mediatype_t disk_type)
{

  mediatype_t mt = MEDIATYPE_UNKNOWN;
  mediatype_t disk_type = MEDIATYPE_WOZ;

  // Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    delete _disk;
    _disk = nullptr;
  }

    // Determine MediaType based on filename extension
    // if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
    //     disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_WOZ:
        Debug_printf("\nMounting Media Type WOZ");
        device_active = true;
        _disk = new MediaTypeWOZ();
        mt = ((MediaTypeWOZ *)_disk)->mount(f);
        change_track(0); // initialize spi buffer
        break;
    default:
        Debug_printf("\nMedia Type UNKNOWN - no mount");
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

    //phases_lut[oldphases][newphases];
    old_pos = track_pos;
    track_pos += delta;
    if (track_pos < 0)
    {
      track_pos = 0;
    }
    else if (track_pos > MAX_TRACKS-1)
    {
      track_pos = MAX_TRACKS-1;
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
  if ( ((MediaTypeWOZ *)_disk)->trackmap(old_pos) == ((MediaTypeWOZ *)_disk)->trackmap(track_pos) )
    return;

  if (((MediaTypeWOZ *)_disk)->trackmap(track_pos) != 255)
    // need to tell diskii_xface the number of bits in the track
    // and where the track data is located so it can convert it
    diskii_xface.encode_spi_packet(
        ((MediaTypeWOZ *)_disk)->get_track(track_pos),
        ((MediaTypeWOZ *)_disk)->track_len(track_pos),
        ((MediaTypeWOZ *)_disk)->num_bits(track_pos),
        indicator);
}

void IRAM_ATTR iwmDisk2::refresh_track()
{
  // rewrite the track data to update the MC3470 random bits

  if (!device_active)
    return;

  if (old_pos != track_pos)
    return;

  if (((MediaTypeWOZ *)_disk)->trackmap(track_pos) != 255)
    // need to tell diskii_xface the number of bits in the track
    // and where the track data is located so it can convert it
    diskii_xface.encode_spi_packet(
        ((MediaTypeWOZ *)_disk)->get_track(track_pos),
        ((MediaTypeWOZ *)_disk)->track_len(track_pos),
        ((MediaTypeWOZ *)_disk)->num_bits(track_pos),
        0);
}

#endif /* BUILD_APPLE */