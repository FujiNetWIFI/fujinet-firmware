#ifdef BUILD_IEC

#include "iecFuji.h"
#include "network.h"
#include "clock.h"
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
  case FUJICMD_GET_SCAN_RESULT:
    net_scan_result_raw(packet);
    break;
  case FUJICMD_SET_SSID:
    net_set_ssid_raw();
    break;
  case FUJICMD_MOUNT_HOST:
    mount_host_raw(packet);
    break;
  case FUJICMD_MOUNT_IMAGE:
    mount_disk_image_raw(packet);
    break;
  case FUJICMD_OPEN_DIRECTORY:
    open_directory_raw(packet);
    break;
  case FUJICMD_READ_DIR_ENTRY:
    read_directory_entry_raw(packet);
    break;
  case FUJICMD_WRITE_DEVICE_SLOTS:
    write_device_slots_raw();
    break;
  case FUJICMD_UNMOUNT_IMAGE:
    unmount_disk_image_raw(packet);
    break;
  case FUJICMD_UNMOUNT_HOST:
    unmount_host_raw(packet);
    break;
  case FUJICMD_SET_DIRECTORY_POSITION:
    set_directory_position_raw(packet);
    break;
  case FUJICMD_SET_DEVICE_FULLPATH:
    set_device_filename_raw(packet);
    break;
  case FUJICMD_GET_DEVICE_FULLPATH:
    get_device_filename_raw(packet);
    break;
  case FUJICMD_CONFIG_BOOT:
    set_boot_config_raw(packet);
    break;
  case FUJICMD_SET_BOOT_MODE:
    set_boot_mode_raw(packet);
    break;
  case FUJICMD_WRITE_HOST_SLOTS:
    write_host_slots_raw();
    break;
  case FUJICMD_GET_HOST_PREFIX:
    fujicmd_get_host_prefix(packet.param(0));
    break;
  case FUJICMD_SET_HOST_PREFIX:
    fujicmd_set_host_prefix(packet.param(0));
    break;

  case FUJICMD_RESET:
    fujicmd_reset();
    break;
  case FUJICMD_GET_SSID:
    net_get_ssid_raw();
    break;
  case FUJICMD_SCAN_NETWORKS:
    net_scan_networks_raw();
    break;
  case FUJICMD_GET_WIFISTATUS:
    net_get_wifi_status_raw();
    break;
  case FUJICMD_GET_WIFI_ENABLED:
    net_get_wifi_enabled_raw();
    break;
  case FUJICMD_CLOSE_DIRECTORY:
    close_directory_raw();
    break;
  case FUJICMD_READ_HOST_SLOTS:
    read_host_slots_raw();
    break;
  case FUJICMD_READ_DEVICE_SLOTS:
    read_device_slots_raw();
    break;
  case FUJICMD_GET_ADAPTERCONFIG:
    get_adapter_config_raw();
    break;
  case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
    get_adapter_config_extended_raw();
    break;
  case FUJICMD_GET_DIRECTORY_POSITION:
    get_directory_position_raw();
    break;
  case FUJICMD_STATUS:
    get_status_raw();
    break;
  case FUJICMD_MOUNT_ALL:
    fujicmd_mount_all_success();
    break;
  case FUJICMD_GENERATE_GUID:
    fujicmd_generate_guid();
    break;
  case FUJICMD_UPDATE_FIRMWARE:
    update_firmware();
    break;

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
    bool result = false;

    // Let the base class validate standard commands
    if (fujiDevice::recognizesCommand(packet))
        return true;

    switch (packet.command())
    {
    case FUJICMD_CLOSE_DIRECTORY:
    case FUJICMD_CONFIG_BOOT:
    case FUJICMD_GET_ADAPTERCONFIG_EXTENDED:
    case FUJICMD_GET_ADAPTERCONFIG:
    case FUJICMD_GET_DEVICE_FULLPATH:
    case FUJICMD_GET_DIRECTORY_POSITION:
    case FUJICMD_GET_SCAN_RESULT:
    case FUJICMD_GET_SSID:
    case FUJICMD_GET_WIFI_ENABLED:
    case FUJICMD_GET_WIFISTATUS:
    case FUJICMD_GENERATE_GUID:
    case FUJICMD_MOUNT_ALL:
    case FUJICMD_MOUNT_HOST:
    case FUJICMD_MOUNT_IMAGE:
    case FUJICMD_OPEN_DIRECTORY:
    case FUJICMD_READ_DEVICE_SLOTS:
    case FUJICMD_READ_DIR_ENTRY:
    case FUJICMD_READ_HOST_SLOTS:
    case FUJICMD_RESET:
    case FUJICMD_SCAN_NETWORKS:
    case FUJICMD_SET_BOOT_MODE:
    case FUJICMD_SET_DEVICE_FULLPATH:
    case FUJICMD_SET_DIRECTORY_POSITION:
    case FUJICMD_SET_SSID:
    case FUJICMD_STATUS:
    case FUJICMD_UNMOUNT_HOST:
    case FUJICMD_UNMOUNT_IMAGE:
    case FUJICMD_WRITE_DEVICE_SLOTS:
    case FUJICMD_WRITE_HOST_SLOTS:
    case FUJICMD_COPY_FILE:
    case FUJICMD_UPDATE_FIRMWARE:
    case FUJICMD_GET_HOST_PREFIX:
    case FUJICMD_SET_HOST_PREFIX:
        result = true;
        break;
    default:
        result = false;
        break;
    }

    Debug_printf("Fuji Command 0x%02x recognized: %s\n", packet.command(),
                 result ? "YES" : "NO");
    return result;
}

void iecFuji::get_status_raw()
{
    // convert iecStatus to a responseV for the host to read
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(iec_status_to_vector());
    // don't set the status!!
    // set_fuji_iec_status(0, "");
}

void iecFuji::net_scan_networks_raw()
{
    fujicmd_net_scan_networks();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(_countScannedSSIDs);
    set_fuji_iec_status(0, "");
}

void iecFuji::net_scan_result_raw(const FujiIECPacket &packet)
{
    fujicmd_net_scan_result(packet.param(0));
    set_fuji_iec_status(0, "");
}

void iecFuji::net_get_ssid_raw()
{
    SSIDConfig net_config = fujicore_net_get_ssid();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(&net_config, sizeof(net_config));
    set_fuji_iec_status(0, "");
}

void iecFuji::net_set_ssid_raw(bool store)
{
    SSIDConfig net_config;
    memset(&net_config, 0, sizeof(SSIDConfig));

    // the data is coming to us packed not in struct format, so depack it into the NetConfig
    // Had issues with it not sending password after large number of \0 padding ssid.
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(&net_config, sizeof(net_config));
    std::string password = mstr::toPETSCII2(net_config.password);
    strlcpy(net_config.password, password.c_str(), sizeof(net_config.password));
    Debug_printv("ssid[%s] pass[%s]", net_config.ssid, net_config.password);

    fujicmd_net_set_ssid_success(net_config.ssid, net_config.password, store);
}

void iecFuji::net_get_wifi_status_raw()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(fujicore_net_get_wifi_status());
    set_fuji_iec_status(0, "");
}

void iecFuji::net_get_wifi_enabled_raw()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(fujicore_net_get_wifi_enabled());
    set_fuji_iec_status(0, "");
}

void iecFuji::unmount_host_raw(const FujiIECPacket &packet)
{
    if (!fujicmd_unmount_host_success(packet.param(0))) {
        set_fuji_iec_status(DEVICE_ERROR, "error unmounting host");
        return;
    }

    set_fuji_iec_status(0, "");
}

void iecFuji::mount_host_raw(const FujiIECPacket &packet)
{
    if (!fujicmd_mount_host_success(packet.param(0)))
    {
        set_fuji_iec_status(DEVICE_ERROR, "Failed to mount host slot");
    }
}

void iecFuji::mount_disk_image_raw(const FujiIECPacket &packet)
{
    if (!fujicmd_mount_disk_image_success(packet.param(0), (disk_access_flags_t) packet.param8(1)))
    {
        set_fuji_iec_status(DEVICE_ERROR, "Failed to mount disk image");
    }
    set_fuji_iec_status(0, "");
}

void iecFuji::set_boot_config_raw(const FujiIECPacket &packet)
{
    fujicmd_set_boot_config(packet.param8(0) != 0);
    set_fuji_iec_status(0, "");
}

void iecFuji::set_boot_mode_raw(const FujiIECPacket &packet)
{
    fujicmd_set_boot_mode(packet.param(0), MEDIATYPE_UNKNOWN, &bootdisk);
    set_fuji_iec_status(0, "");
}

void iecFuji::unmount_disk_image_raw(const FujiIECPacket &packet)
{
    if (!fujicmd_unmount_disk_image_success(packet.param(0))) {
        set_fuji_iec_status(DEVICE_ERROR, "invalid device slot");
    } else {
        set_fuji_iec_status(0, "");
    }
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

void iecFuji::open_directory_raw(const FujiIECPacket &packet)
{
    if (!fujicmd_open_directory_success(packet.param(0))) {
        set_fuji_iec_status(DEVICE_ERROR, "Failed to open directory");
        return;
    }
    set_fuji_iec_status(0, "");
}


success_is_true iecFuji::validate_directory_slot() {
    RETURN_SUCCESS_IF(_current_open_directory_slot != -1);
}

void iecFuji::read_directory_entry_raw(const FujiIECPacket &packet)
{
    fujicmd_read_directory_entry(packet.param8(0), packet.param(1));
    set_fuji_iec_status(0, "");
}

void iecFuji::get_directory_position_raw()
{
    if (!validate_directory_slot())
    {
        set_fuji_iec_status(DEVICE_ERROR, "no currently open directory");
        return;
    }

    uint16_t pos = fujicore_get_directory_position();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(&pos, sizeof(pos));
    set_fuji_iec_status(0, "");
}

void iecFuji::set_directory_position_raw(const FujiIECPacket &packet)
{
    fujicmd_set_directory_position(packet.param(0));
    set_fuji_iec_status(0, "");
}

void iecFuji::close_directory_raw()
{
    fujicmd_close_directory();
    set_fuji_iec_status(0, "");
}

void iecFuji::get_adapter_config_raw()
{
    fujicmd_get_adapter_config();
    set_fuji_iec_status(0, "");
}

void iecFuji::get_adapter_config_extended_raw()
{
    AdapterConfigExtended cfg = fujicore_get_adapter_config_extended();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(&cfg, sizeof(cfg));
    set_fuji_iec_status(0, "");
}

//  Make new disk and shove into device slot
void iecFuji::new_disk()
{
    // TODO: Implement when we actually have a good idea of
    // media types.
}

// RAW version doesn't take a parameter for the host slot, BASIC does. I didn't make the rules
void iecFuji::read_host_slots_raw()
{
    Debug_println("Fuji cmd: READ HOST SLOTS");
    ByteBuffer buffer(MAX_HOSTS * MAX_HOSTNAME_LEN, 0);
    for (size_t i = 0; i < MAX_HOSTS; i++) {
        const char* hostname = _fnHosts[i].get_hostname();
        size_t offset = i * MAX_HOSTNAME_LEN;
        std::memcpy(&buffer[offset], hostname, std::min(std::strlen(hostname), static_cast<size_t>(MAX_HOSTNAME_LEN)));
    }
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(buffer);
    set_fuji_iec_status(0, "");
}

void iecFuji::write_host_slots_raw()
{
    fujicmd_write_host_slots();
    set_fuji_iec_status(0, "");
}

// RAW version doesn't take a parameter for the device slot, BASIC does. I didn't make the rules
void iecFuji::read_device_slots_raw()
{
    Debug_println("Fuji cmd: READ DEVICE SLOTS");
    fujicmd_read_device_slots();
    set_fuji_iec_status(0, "");
}

void iecFuji::write_device_slots_raw()
{
    fujicmd_write_device_slots();
    set_fuji_iec_status(0, "");
}

void iecFuji::set_device_filename_raw(const FujiIECPacket &packet)
{
    fujicmd_set_device_filename_success(packet.param(0), packet.param8(1),
                                        (disk_access_flags_t) packet.param8(2));
    set_fuji_iec_status(0, "");
}

void iecFuji::get_device_filename_raw(const FujiIECPacket &packet)
{
    fujicmd_get_device_filename(packet.param(0));
    set_fuji_iec_status(0, "");
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

void iecFuji::copy_file(std::string source, std::string destination)
{
    std::unique_ptr<MFile> in_file(MFSOwner::File(source));
    std::unique_ptr<MFile> out_file(MFSOwner::File(destination));

    // If destination is a directory then save with source filename
    if ( out_file->isDirectory() )
    {
        destination += "/" + in_file->name;
        out_file.reset(MFSOwner::File(destination));
    }

    // Create streams and copy file
    {
        auto in_stream = in_file->getSourceStream(std::ios_base::in);
        if (in_stream == nullptr)
        {
            Debug_printf("2 Error: Can't open source!\r\n");
            set_fuji_iec_status(62, "can't open source stream");
            return;
        }

        auto out_stream = out_file->getSourceStream(std::ios_base::out);
        if (out_stream == nullptr)
        {
            Debug_printf("2 Error: Can't open destination!\r\n");
            set_fuji_iec_status(62, "can't open destination stream");
            return;
        }

        Debug_printv("size[%lu] name[%s] url[%s] destination[%s]", in_file->size, in_file->name.c_str(), in_stream->url.c_str(), destination.c_str());

        // Receive File
        int count = 0;
        uint8_t bytes[255];
        while (true)
        {
            int bytes_read = in_stream->read(bytes, 255);
            if (bytes_read < 1)
            {
                if (in_stream->available())
                    Debug_printf("\nError reading '%s'\r", in_file->name.c_str());
                break;
            }

            int bytes_written = out_stream->write(bytes, bytes_read);
            if (bytes_written != bytes_read)
            {
                Debug_printf("\nError writing '%s'\r", out_file->name.c_str());
                break;
            }

            // Show percentage complete in stdout
            uint8_t percent = (in_stream->position() * 100) / in_stream->size();
#ifdef ENABLE_DISPLAY
            LEDS.progress = percent;
#endif
            Debug_printf("Downloading '%s' %d%% [%lu]\r", in_file->name.c_str(), percent, in_stream->position());
            count++;
        }
        Debug_printf("\n");
    }

    set_fuji_iec_status(0, "");
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
