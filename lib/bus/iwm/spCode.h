#ifndef SPCODE_H
#define SPCODE_H

enum spCode_t : uint8_t {
  SP_STAT_DEVICE            = 0x00,
  SP_STAT_CONTROL_BLOCK     = 0x01,
  SP_STAT_NEWLINE           = 0x02,
  SP_STAT_DIB               = 0x03,
  SP_STAT_UNIDISK           = 0x05,

  SP_CTRL_RESET             = 0x00,
  SP_CTRL_SET_DCB           = 0x01,
  SP_CTRL_SET_NEWLINE       = 0x02,
  SP_CTRL_DEV_INTERRUPT     = 0x03,
  SP_CTRL_EJECT             = 0x04, // Apple 3.5, UniDisk 3.5
  SP_CTRL_EXECUTE           = 0x05, // UniDisk 3.5
  SP_CTRL_SET_ADDRESS       = 0x06, // UniDisk 3.5
  SP_CTRL_DOWNLOAD          = 0x07, // UniDiisk 3.5
  SP_CTRL_SET_HOOK          = 0x05, // Apple 3.5
  SP_CTRL_RESET_HOOK        = 0x06, // Apple 3.5
  SP_CTRL_SET_MARK          = 0x07, // Apple 3.5
  SP_CTRL_RESET_MARK        = 0x08, // Apple 3.5
  SP_CTRL_SET_SIDES         = 0x09, // Apple 3.5
  SP_CTRL_SET_INTERLEAVE    = 0x0A, // Apple 3.5

  SP_CTRL_CLEAR_DISKII_SEEN = 0x08, // iwmFuji
  SP_STAT_GET_DISKII_SEEN   = 0x08, // iwmFuji
};

#endif /* SPCODE_H */
