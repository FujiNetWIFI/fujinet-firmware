#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "iwmClock.h"

#include "fujiCommandID.h"
#include "../../include/debug.h"

iwmClock platformClock;

iwm_device_status_block_t iwmClock::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmClock::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "FN_CLOCK");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET_CLOCK;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET_CLOCK;
  dib.version = 0x0100;

  return dib;
}

// Lowercase asks for the alternate timezone, and ApeTime is 'A' rather than
// the legacy opcode the shared table indexes it by.
fujiCommandID_t iwmClock::fujidev_canonical_command(fujiCommandID_t command, bool &use_alt)
{
    uint8_t c = (uint8_t) command;

    use_alt = c >= 'a' && c <= 'z';
    if (use_alt)
        c -= 'a' - 'A';

    return c == (uint8_t) CMD::APETIME_GET_ATARI ? CMD::APETIME_GETTIME : (fujiCommandID_t) c;
}

#endif /* BUILD_APPLE */
