#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "iwmClock.h"

#include "fujiCommandID.h"
#include "../../include/debug.h"

iwmClock platformClock;

iwmClock::iwmClock()
{
}

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

std::optional<std::string> iwmClock::read_tz()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    const auto &d = _packet->data();
    if (!d.has_value())
    {
        Debug_printv("ERROR: No timezone sent");
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
        return std::nullopt;
    }

    std::string tz(reinterpret_cast<const char *>(d->data()), d->size());
    SYSTEM_BUS.transaction_success();
    return tz;
}

// Alternate timezone is selected by command byte case, never by parameter.
bool iwmClock::alt_requested()
{
    return false;
}

void iwmClock::send_string(const std::string &s)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(s);
}

void iwmClock::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
#ifdef DEBUG
    Debug_printf("[CLOCK] Device %02x Control Code %02x('%c')\r\n", id(), cmd.command(), isprint(cmd.command()) ? (char) cmd.command() : '.');
#endif

    _packet = &cmd;

    switch (cmd.command())
    {
    case APETIMECMD_SETTZ_ALT2:
        set_fn_tz();
        break;
    case APETIMECMD_SETTZ_ALT:
        set_alternate_tz();
        break;
    default:
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
        break;
    }

    _packet = nullptr;
}

void iwmClock::iwm_status(const iwm_decoded_cmd_t &cmd)
{
    bool use_alt = false;

#ifdef DEBUG
    Debug_printf("[CLOCK] Device %02x Status Code %02x('%c')\r\n", id(), cmd.command(), isprint(cmd.command()) ? (char)cmd.command() : '.');
#endif

    _packet = &cmd;

    // Uppercase = system timezone, lowercase = alternate.
    switch (cmd.command())
    {
    case APETIMECMD_SETTZ_ALT2:
    case APETIMECMD_SETTZ_ALT:
        use_alt = cmd.command() == APETIMECMD_SETTZ_ALT;
        get_simple(use_alt);
        break;
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS:
        get_simple_hundredths(false);
        break;
    case APETIMECMD_GET_PRODOS:
    case APETIMECMD_GET_PRODOS_ALT:
        use_alt = cmd.command() == APETIMECMD_GET_PRODOS_ALT;
        get_prodos(use_alt);
        break;
    case APETIMECMD_GET_SOS:
    case APETIMECMD_GET_SOS_ALT:
        use_alt = cmd.command() == APETIMECMD_GET_SOS_ALT;
        get_sos(use_alt);
        break;
    case APETIMECMD_GET_ISO_LOCAL:
    case APETIMECMD_GET_ISO_LOCAL_ALT:
        use_alt = cmd.command() == APETIMECMD_GET_ISO_LOCAL_ALT;
        get_iso_local(use_alt);
        break;
    case APETIMECMD_GET_ISO_UTC:
    case APETIMECMD_GET_ISO_UTC_ALT:
        get_iso_utc();
        break;
    case APETIMECMD_GET_ATARI:
    case APETIMECMD_GET_ATARI_ALT:
        use_alt = cmd.command() == APETIMECMD_GET_ATARI_ALT;
        get_apetime(use_alt);
        break;
    case APETIMECMD_GET_GENERAL:
        get_general_tz();
        break;
    default:
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
        break;
    }

    _packet = nullptr;
}

void iwmClock::iwm_open(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\nClock: Open\n");
    SYSTEM_BUS.transaction_success();
}

void iwmClock::iwm_close(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\nClock: Close\n");
    SYSTEM_BUS.transaction_success();
}

void iwmClock::shutdown()
{
}

#endif /* BUILD_APPLE */
