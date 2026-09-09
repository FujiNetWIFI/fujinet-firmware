#ifdef BUILD_APPLE

#include "iwmNetwork.h"

iwm_device_status_block_t iwmNetwork::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_WRITE_ALLOWED | STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmNetwork::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "NETWORK");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET_NETWORK;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET_NETWORK;
  dib.version = 0x0100;

  return dib;
}

void iwmNetwork::iwm_status(const iwm_decoded_cmd_t &cmd)
{
#ifdef DEBUG
  {
    uint8_t pcmd = (uint8_t) cmd.command();
    Debug_printf("\r\n[NETWORK] Device %02x Status Code %02x('%c') net_unit %02x\r\n",
                 id(), pcmd, isprint(pcmd)
                 ? (char) pcmd : '.', cmd.unit());
  }
#endif

    // Let the base class handle standard commands
    if (NDevice::processCommand(cmd))
        return;
}

void iwmNetwork::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
  {
    uint8_t pcmd = (uint8_t) cmd.command();
    Debug_printf("\r\n[NETWORK] Device %02x Control Code %02x('%c') net_unit %02x",
                 id(), pcmd, isprint(pcmd)
                 ? (char)pcmd : '.', cmd.unit());
  }

    // Let the base class handle standard commands
    if (NDevice::processCommand(cmd))
        return;

    switch (cmd.command())
    {
    case CMD::NET_SET_UNIT:
      SYSTEM_BUS.setDefaultNetworkUnit(cmd.param(0));
      // control command still needs a bus reply or the host times out
      SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
      SYSTEM_BUS.transaction_success();
      break;
    default:
      SYSTEM_BUS.transaction_error();
      break;
    }
}

void iwmNetwork::iwm_write(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\n[NETWORK] Device %02x Write %04x bytes, net_unit %02x\n",
                 id(), cmd.frame.char_rw.length, cmd.unit());

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (cmd.frame.char_rw.length && NDevice::fujicore_write(cmd.data().value()).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

void iwmNetwork::iwm_read(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\n[NETWORK] Device %02x Read %04x bytes, net_unit %02x\n",
                 id(), cmd.frame.char_rw.length, cmd.unit());

    ByteBuffer buffer;
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (NDevice::fujicore_read(buffer, cmd.frame.char_rw.length).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_send(buffer);
}

#endif /* BUILD_APPLE */
