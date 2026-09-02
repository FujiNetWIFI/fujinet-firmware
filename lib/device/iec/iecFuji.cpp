#ifdef BUILD_IEC

#include "iecFuji.h"
#include "iecNetwork.h"
#include "iecClock.h"
#include "fsFlash.h"
#include "fnSystem.h"

#include <sstream>

#define IMAGE_EXTENSION ".d64"

iecFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

// iecNetwork sioNetDevs[MAX_NETWORK_DEVICES];

bool _validate_host_slot(uint8_t slot, const char *dmsg = nullptr);
bool _validate_device_slot(uint8_t slot, const char *dmsg = nullptr);

bool _validate_host_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_HOSTS)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid host slot %hu\r\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid host slot %hu @ %s\r\n", slot, dmsg);
    }

    return false;
}

bool _validate_device_slot(uint8_t slot, const char *dmsg)
{
    if (slot < MAX_DISK_DEVICES)
        return true;

    if (dmsg == NULL)
    {
        Debug_printf("!! Invalid device slot %hu\r\n", slot);
    }
    else
    {
        Debug_printf("!! Invalid device slot %hu @ %s\r\n", slot, dmsg);
    }

    return false;
}

static std::string dataToHexString(uint8_t *data, size_t len)
{
  std::string res;
  char buf[10];

  for(size_t i=0; i<len; i++)
    {
      sprintf(buf, "%02X ", data[i]);
      res += buf;
    }

  return res;
}


// Constructor
iecFuji::iecFuji() : fujiDevice(MAX_DISK_DEVICES, IMAGE_EXTENSION, std::nullopt)
{
    // Helpful for debugging
    for (int i = 0; i < MAX_HOSTS; i++)
        _fnHosts[i].slotid = i;

    state = DEVICE_IDLE;
}

// Initializes base settings and adds our devices to the SIO bus
void iecFuji::setup()
{
    //Debug_printf("iecFuji::setup()\r\n");

    populate_slots_from_config();

    FileSystem *ptrfs = fnSDFAT.running() ? (FileSystem *)&fnSDFAT : (FileSystem *)&fsFlash;
//    iecPrinter::printer_type ptype = Config.get_printer_type(0);
    iecPrinter::printer_type ptype = iecPrinter::printer_type::PRINTER_COMMODORE_MPS803; // temporary
    Debug_printf("Creating a default printer using %s storage and type %d\r\n", ptrfs->typestring(), ptype);
    iecPrinter *ptr = new iecPrinter(4, ptrfs, ptype);
    fnPrinters.set_entry(0, ptr, ptype, Config.get_printer_port(0));

    // 04-07 Printers / Plotters
    if (SYSTEM_BUS.attachDevice(ptr))
        Debug_printf("Attached printer device #%d\r\n", 4);

    // 08-15 Drives
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].disk_dev.setDeviceNumber(BUS_DEVICEID_DISK+i);
        if (SYSTEM_BUS.attachDevice(&_fnDisks[i].disk_dev))
            Debug_printf("Attached drive device #%d\r\n", BUS_DEVICEID_DISK+i);
    }

    // 16-19 Network Devices
    if (SYSTEM_BUS.attachDevice(new iecNetwork(16)))     // 16-19 Network Devices
        Debug_printf("Attached network device #%d\r\n", 16);

    //Serial.print("CPM "); SYSTEM_BUS.addDevice(new iecCpm(), 20);             // 20-29 Other
    if (SYSTEM_BUS.attachDevice(new iecClock(29)))
        Debug_printf("Attached clock device #%d\r\n", 29);

    // FujiNet
    setDeviceNumber(30);
    if (SYSTEM_BUS.attachDevice(this))
        Debug_printf("Attached Meatloaf device #%d\r\n", 30);
}

void logResponse(const void* data, size_t length)
{
    // ensure we don't flood the logs with debug, and make it look pretty using util_hexdump
    uint8_t debug_len = length;
    bool is_truncated = false;
    if (debug_len > 64) {
        debug_len = 64;
        is_truncated = true;
    }

    std::string msg = util_hexdump(data, debug_len);
    Debug_printf("Sending:\r\n%s\r\n", msg.c_str());
    if (is_truncated) {
        Debug_printf("[truncated from %d]\r\n", length);
    }

    // Debug_printf("  ");
    // // ASCII Text representation
    // for (int i=0;i<length;i++)
    // {
    //     char c = petscii2ascii(data[i]);
    //     Debug_printf("%c", c<0x20 || c>0x7f ? '.' : c);
    // }

}


void iecFuji::talk(uint8_t secondary)
{
  // only talk on channel 15
  if( (secondary & 0x0F)==15 )
    {
      state = DEVICE_TALK;
    }
}


void iecFuji::listen(uint8_t secondary)
{
  // only listen on channel 15
  if( (secondary & 0x0F)==15 )
    {
      state = DEVICE_LISTEN;
      _payload.clear();
    }
}


void iecFuji::untalk()
{
  state = DEVICE_IDLE;
}


void iecFuji::unlisten()
{
  if( state == DEVICE_LISTEN )
    state = DEVICE_ACTIVE;
}


int8_t iecFuji::canWrite()
{
  return state==DEVICE_LISTEN ? 1 : 0;
}


int8_t iecFuji::canRead()
{
  return SYSTEM_BUS.iecCanRead(state);
}


void iecFuji::write(uint8_t data, bool eoi)
{
  _payload.push_back(data);
}


uint8_t iecFuji::read()
{
    return SYSTEM_BUS.iecRead();
}


void iecFuji::task()
{
  // this gets called whenever the IEC bus is NOT in a time-sensitive state.
  // Any possibly time-comsuming tasks should be processed within here.

  // first call the underlying class task function
  IECDevice::task();

  if( state==DEVICE_ACTIVE )
    {
      if( _payload.size()>0 ) process_cmd();
      state = DEVICE_IDLE;
    }
}


void iecFuji::reset()
{
  IECDevice::reset();
  state = DEVICE_IDLE;
}

void iecFuji::process_cmd()
{
  if (!_activePacket) {
    if (_payload.size() != 2
        || (_payload[0] == OPCODE_NO_PAYLOAD && _payload[0] == OPCODE_HAS_PAYLOAD))
      return;

    Debug_printv("RAW command: %s",
                 dataToHexString((uint8_t *) _payload.data(), _payload.size()).c_str());

    auto packet = std::make_unique<FujiIECPacket>(m_devnr, (fujiCommandID_t) _payload[1]);
    _activePacket = std::move(packet);

    if (_payload[0] == OPCODE_HAS_PAYLOAD)
      return;
  }
  else
    _activePacket->setPayload(_payload);

  SYSTEM_BUS._activePacket = _activePacket.get();
  processCommand(*_activePacket);
  _activePacket = NULL;
}

bool iecFuji::processCommand(const FUJI_COMMAND_PACKET &packet)
{
  bool handled = false;

  // Let the base class handle standard commands
  if (fujiDevice::processCommand(packet)) {
    handled = true;
    goto done;
  }

  switch (packet.command()) {
  default:
    handled = false;
    break;
  }

 done:
  return handled;
}

// COMMODORE SPECIFIC CONVENIENCE COMMANDS /////////////////////

bool iecFuji::is_supported(const FujiIECPacket &packet)
{
    bool result = true;

    // Let the base class validate standard commands
    if (!fujiDevice::recognizesCommand(packet))
      switch (packet.command())
      {
      default:
        result = false;
        break;
      }

    Debug_printf("Fuji Command 0x%02x recognized: %s\n", packet.command(),
                 result ? "YES" : "NO");
    return result;
}

std::pair<std::string, std::string> split_at_delim(const std::string& input, char delim) {
    // Find the position of the first occurrence of delim in the string
    size_t pos = input.find(delim);

    std::string firstPart, secondPart;
    if (pos != std::string::npos) {
        firstPart = input.substr(0, pos);
        // Check if there's content beyond the delim for the second part
        if (pos + 1 < input.size()) {
            secondPart = input.substr(pos + 1);
        }
    } else {
        // If delim is not found, the entire input is the first part
        firstPart = input;
    }

    // Remove trailing slash from firstPart, if present
    if (!firstPart.empty() && firstPart.back() == '/') {
        firstPart.pop_back();
    }

    return {firstPart, secondPart};
}

/* @brief Tokenizes the payload command and parameters.
 Example: "COMMAND:Param1,Param2" will return a vector of [0]="COMMAND", [1]="Param1",[2]="Param2"
 Also supports "COMMAND,Param1,Param2"
*/
std::vector<std::string> iecFuji::tokenize_basic_command(std::string command)
{
    Debug_printf("Tokenizing basic command: %s\r\n", command.c_str());

    // Replace the first ":" with "," for easy tokenization.
    // Assume it is fine to change the payload at this point.
    // Technically, "COMMAND,Param1,Param2" will work the smae, if ":" is not in a param value
    size_t endOfCommand = command.find(':');
    if (endOfCommand != std::string::npos)
        command.replace(endOfCommand,1,",");

    std::vector<std::string> result =  util_tokenize(command, ',');
    return result;

}

size_t iecFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                uint8_t maxlen)
{
    struct {
        dirEntryTimestamp modified;
        uint16_t size;
        uint8_t flags;
        uint8_t mediatype;
    } __attribute__((packed)) custom_details;
    dirEntryDetails details;

    details = _additional_direntry_details(f);
    custom_details.modified = details.modified;
    custom_details.size = htole16(details.size);
    custom_details.flags = details.flags;
    custom_details.mediatype = details.mediatype;

    maxlen -= sizeof(custom_details);
    // Subtract a byte for a terminating slash on directories
    if (custom_details.flags & DET_FF_DIR)
        maxlen--;

    if (strlen(f->filename) >= maxlen)
        custom_details.flags |= DET_FF_TRUNC;
    memcpy(dest, &custom_details, sizeof(custom_details));
    return sizeof(custom_details);
}

void iecFuji::update_firmware()
{
    fnSystem.update_firmware();
    set_fuji_iec_status(0, "");
}


#endif /* BUILD_IEC */

/*
  Local Variables:
  mode: c++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-file-offsets: ((substatement-open . 0))
  End:
*/
