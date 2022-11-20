#ifdef BUILD_APPLE
#include "disk2.h"

#include "fnSystem.h"
#include "led.h"
#include "fuji.h"

#define MAX_TRACKS 140

const int8_t phases_lut [16][16] = {
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 2, 1, 0, 0, 0, 0,-2,-1, 0, 0, 0, 0, 0, 0},
{ 0,-2, 0,-1, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0,-1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0, 0, 0, 0},
{ 0, 0,-2, 0, 0, 0,-1, 0, 2, 0, 0, 0, 1, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0,-1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 2, 0, 0,-2, 0, 0, 0, 0, 1, 0, 0,-1, 0, 0, 0},
{ 0, 1, 0, 0, 0, 0, 0, 0,-1, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0,-1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

iwmDisk2::~iwmDisk2()
{
}

void iwmDisk2::shutdown()
{
}

iwmDisk2::iwmDisk2()
{
  Debug_printf("\r\nNew Disk ][ object");
}

mediatype_t iwmDisk2::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{

  mediatype_t mt = MEDIATYPE_UNKNOWN;

  Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    delete _disk;
    _disk = nullptr;
  }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_PO:
        Debug_printf("\r\nMedia Type PO");
        device_active = true;
        _disk = new MediaTypePO();
        mt = _disk->mount(f, disksize);
        //_disk->fileptr() = f;
        // mt = MEDIATYPE_PO;
        break;
    default:
        Debug_printf("\r\nMedia Type UNKNOWN - no mount");
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

bool iwmDisk2::phases_valid(uint8_t phases)
{
  switch (phases) // lazy brute force way
  {
  case 1:
  case 2:
  case 3:
  case 4:
  case 6:
  case 8:
  case 9:
  case 12:
    return true;
  default:
    break;
  }
  return false;
}

bool iwmDisk2::move_head()
{
  int delta;
  uint8_t newphases = smartport.iwm_phase_vector(); // could access through IWM instead
  if (phases_valid(newphases))
  {
    delta = phases_lut[oldphases][newphases];
    track_pos += delta;
    if (track_pos < 0)
    {
      track_pos = 0;
    }
    else if (track_pos > MAX_TRACKS)
    {
      track_pos = MAX_TRACKS;
    }
    oldphases = newphases;
  }
  return (delta != 0);
}

void iwmDisk2::process()
{
  if (move_head())
  {
    // set up new track to output
  }
}

#endif /* BUILD_APPLE */