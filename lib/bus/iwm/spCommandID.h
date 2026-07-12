#ifndef SPCOMMANDID_H
#define SPCOMMANDID_H

#include <cstdint>

enum spCommandID_t : uint8_t {
  SP_CMD_STATUS         = 0x00,
  SP_CMD_READBLOCK      = 0x01,
  SP_CMD_WRITEBLOCK     = 0x02,
  SP_CMD_FORMAT         = 0x03,
  SP_CMD_CONTROL        = 0x04,
  SP_CMD_INIT           = 0x05,
  SP_CMD_OPEN           = 0x06,
  SP_CMD_CLOSE          = 0x07,
  SP_CMD_READ           = 0x08,
  SP_CMD_WRITE          = 0x09,
  SP_ECMD_STATUS        = 0x40,
  SP_ECMD_READBLOCK     = 0x41,
  SP_ECMD_WRITEBLOCK    = 0x42,
  SP_ECMD_FORMAT        = 0x43,
  SP_ECMD_CONTROL       = 0x44,
  SP_ECMD_INIT          = 0x45,
  SP_ECMD_OPEN          = 0x46,
  SP_ECMD_CLOSE         = 0x47,
  SP_ECMD_READ          = 0x48,
  SP_ECMD_WRITE         = 0x49,
};

#endif /* SPCOMMANDID_H */
