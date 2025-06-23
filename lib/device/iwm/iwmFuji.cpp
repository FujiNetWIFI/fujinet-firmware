#ifdef BUILD_APPLE
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstring>

#include "iwmFuji.h"

#include "fujiCmd.h"
#include "httpService.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "fnFsTNFS.h"
#include "utils.h"
#include "string_utils.h"

#include "compat_string.h"

#define ADDITIONAL_DETAILS_BYTES 12
#define DIR_MAX_LEN 40

iwmFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

iwmFuji::iwmFuji()
{
	Debug_printf("Announcing the iwmFuji::iwmFuji()!!!\n");
	for (int i = 0; i < MAX_HOSTS; i++)
		_fnHosts[i].slotid = i;

    command_handlers = {
        { SP_CMD_STATUS, [this](iwm_decoded_cmd_t cmd) { iwm_status(cmd); }},                           // 0x00
        { SP_CMD_CONTROL, [this](iwm_decoded_cmd_t cmd) { iwm_ctrl(cmd); }},                            // 0x04
        { SP_CMD_OPEN, [this](iwm_decoded_cmd_t cmd) { iwm_open(cmd); }},                               // 0x06
        { SP_CMD_CLOSE, [this](iwm_decoded_cmd_t cmd) { iwm_close(cmd); }},                              // 0x07
        { SP_CMD_READ, [this](iwm_decoded_cmd_t cmd) { iwm_read(cmd); }},                               // 0x08

        { SP_CMD_READBLOCK, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                 // 0x01
        { SP_CMD_WRITEBLOCK, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                // 0x02
        { SP_CMD_FORMAT, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }},                    // 0x03
        { SP_CMD_WRITE, [this](iwm_decoded_cmd_t cmd) { iwm_return_badcmd(cmd); }}                      // 0x09
    };

    control_handlers = {
        { 0xAA, [this]()                               { this->iwm_dummy_command(); }},
        { IWM_CTRL_SET_DCB, [this]()                   { this->iwm_dummy_command(); }},                 // 0x01
        { IWM_CTRL_SET_NEWLINE, [this]()               { this->iwm_dummy_command(); }},                 // 0x02

        { FUJICMD_CLOSE_DIRECTORY, [this]()            { this->iwm_ctrl_close_directory(); }},          // 0xF5
        { FUJICMD_CONFIG_BOOT, [this]()                { this->iwm_ctrl_set_boot_config(); }},          // 0xD9
        { FUJICMD_COPY_FILE, [this]()                  { this->iwm_ctrl_copy_file(); }},                // 0xD8
        { FUJICMD_DISABLE_DEVICE, [this]()             { this->iwm_ctrl_disable_device(); }},           // 0xD4
        { FUJICMD_ENABLE_DEVICE, [this]()              { this->iwm_ctrl_enable_device(); }},            // 0xD5
        { FUJICMD_GET_SCAN_RESULT, [this]()            { this->iwm_ctrl_net_scan_result(); }},          // 0xFC

        { FUJICMD_HASH_INPUT, [this]()                 { this->iwm_ctrl_hash_input(); }},               // 0xC8
        { FUJICMD_HASH_COMPUTE, [this]()               { this->iwm_ctrl_hash_compute(true); }},         // 0xC7
        { FUJICMD_HASH_COMPUTE_NO_CLEAR, [this]()      { this->iwm_ctrl_hash_compute(false); }},        // 0xC7
        { FUJICMD_HASH_LENGTH, [this]()                { this->iwm_stat_hash_length(); }},              // 0xC6
        { FUJICMD_HASH_OUTPUT, [this]()                { this->iwm_stat_hash_output(); }},              // 0xC5
        { FUJICMD_HASH_CLEAR, [this]()                 { this->iwm_ctrl_hash_clear(); }},               // 0xC2

        { FUJICMD_QRCODE_INPUT, [this]()               { this->iwm_ctrl_qrcode_input(); }},             // 0xBC
        { FUJICMD_QRCODE_ENCODE, [this]()              { this->iwm_ctrl_qrcode_encode(); }},            // 0xBD
        { FUJICMD_QRCODE_OUTPUT, [this]()              { this->iwm_ctrl_qrcode_output(); }},            // 0xBF

        { FUJICMD_MOUNT_HOST, [this]()                 { this->iwm_ctrl_mount_host(); }},               // 0xF9
        { FUJICMD_NEW_DISK, [this]()                   { this->iwm_ctrl_new_disk(); }},                 // 0xE7
        { FUJICMD_OPEN_APPKEY, [this]()                { this->iwm_ctrl_open_app_key(); }},             // 0xDC
        { FUJICMD_READ_DIR_ENTRY, [this]()             { this->iwm_ctrl_read_directory_entry(); }},     // 0xF6
        { FUJICMD_SET_BOOT_MODE, [this]()              { this->iwm_ctrl_set_boot_mode(); }},            // 0xD6
        { FUJICMD_SET_DEVICE_FULLPATH, [this]()        { this->iwm_ctrl_set_device_filename(); }},      // 0xE2
        { FUJICMD_SET_DIRECTORY_POSITION, [this]()     { this->iwm_ctrl_set_directory_position(); }},   // 0xE4
        { FUJICMD_SET_HOST_PREFIX, [this]()            { this->iwm_ctrl_set_host_prefix(); }},          // 0xE1
        { FUJICMD_SET_SSID, [this]()                   { this->iwm_ctrl_net_set_ssid(); }},             // 0xFB
        { FUJICMD_UNMOUNT_HOST, [this]()               { this->iwm_ctrl_unmount_host(); }},             // 0xE6
        { FUJICMD_UNMOUNT_IMAGE, [this]()              { this->iwm_ctrl_disk_image_umount(); }},        // 0xE9
        { FUJICMD_WRITE_APPKEY, [this]()               { this->iwm_ctrl_write_app_key(); }},            // 0xDE
        { FUJICMD_WRITE_DEVICE_SLOTS, [this]()         { this->iwm_ctrl_write_device_slots(); }},       // 0xF1
        { FUJICMD_WRITE_HOST_SLOTS, [this]()           { this->iwm_ctrl_write_host_slots(); }},         // 0xF3

        { FUJICMD_RESET,  [this]()                     { this->send_reply_packet(err_result); this->iwm_ctrl_reset_fujinet(); }},   // 0xFF
        { IWM_CTRL_RESET, [this]()                     { this->send_reply_packet(err_result); this->iwm_ctrl_reset_fujinet(); }},   // 0x00
#ifndef DEV_RELAY_SLIP
	{ IWM_CTRL_CLEAR_ENSEEN, [this]()	       { diskii_xface.d2_enable_seen = 0; err_result = SP_ERR_NOERROR; }},
#endif

        { FUJICMD_MOUNT_ALL, [&]()                     { err_result = (mount_all() ? SP_ERR_IOERROR : SP_ERR_NOERROR); }},          // 0xD7
        { FUJICMD_MOUNT_IMAGE, [&]()                   { err_result = iwm_ctrl_disk_image_mount(); }},  // 0xF8
        { FUJICMD_OPEN_DIRECTORY, [&]()                { err_result = iwm_ctrl_open_directory(); }}     // 0xF7
    };

    status_handlers = {
        { 0xAA, [this]()                               { this->iwm_hello_world(); }},

        { IWM_STATUS_DIB, [this]()                     { this->send_status_dib_reply_packet(); status_completed = true; }},     // 0x03
        { IWM_STATUS_STATUS, [this]()                  { this->send_status_reply_packet(); status_completed = true; }},         // 0x00
#ifndef DEV_RELAY_SLIP
	{ IWM_STATUS_ENSEEN, [this]()		       { data_len = 1; data_buffer[0] = diskii_xface.d2_enable_seen; }},
#endif

        { FUJICMD_DEVICE_ENABLE_STATUS, [this]()       { this->send_stat_get_enable(); }},                      // 0xD1
        { FUJICMD_GET_ADAPTERCONFIG_EXTENDED, [this]() { this->iwm_stat_get_adapter_config_extended(); }},      // 0xC4
        { FUJICMD_GET_ADAPTERCONFIG, [this]()          { this->iwm_stat_get_adapter_config(); }},               // 0xE8
        { FUJICMD_GET_DEVICE_FULLPATH, [this]()        { this->iwm_stat_get_device_filename(status_code); }},   // 0xDA
        { FUJICMD_GET_DEVICE1_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA0
        { FUJICMD_GET_DEVICE2_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA1
        { FUJICMD_GET_DEVICE3_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA2
        { FUJICMD_GET_DEVICE4_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA3
        { FUJICMD_GET_DEVICE5_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA4
        { FUJICMD_GET_DEVICE6_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA5
        { FUJICMD_GET_DEVICE7_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA6
        { FUJICMD_GET_DEVICE8_FULLPATH, [this]()       { this->iwm_stat_get_device_filename(status_code); }},   // 0xA7
        { FUJICMD_GET_DIRECTORY_POSITION, [this]()     { this->iwm_stat_get_directory_position(); }},           // 0xE5
        { FUJICMD_GET_HOST_PREFIX, [this]()            { this->iwm_stat_get_host_prefix(); }},                  // 0xE0
        { FUJICMD_GET_SCAN_RESULT, [this]()            { this->iwm_stat_net_scan_result(); }},                  // 0xFC
        { FUJICMD_GET_SSID, [this]()                   { this->iwm_stat_net_get_ssid(); }},                     // 0xFE
        { FUJICMD_GET_WIFI_ENABLED, [this]()           { this->iwm_stat_get_wifi_enabled(); }},                 // 0xEA
        { FUJICMD_GET_WIFISTATUS, [this]()             { this->iwm_stat_net_get_wifi_status(); }},              // 0xFA
        { FUJICMD_READ_APPKEY, [this]()                { this->iwm_stat_read_app_key(); }},                     // 0xDD
        { FUJICMD_READ_DEVICE_SLOTS, [this]()          { this->iwm_stat_read_device_slots(); }},                // 0xF2
        { FUJICMD_READ_DIR_ENTRY, [this]()             { this->iwm_stat_read_directory_entry(); }},             // 0xF6
        { FUJICMD_READ_HOST_SLOTS, [this]()            { this->iwm_stat_read_host_slots(); }},                  // 0xF4
        { FUJICMD_SCAN_NETWORKS, [this]()              { this->iwm_stat_net_scan_networks(); }},                // 0xFD
        { FUJICMD_QRCODE_LENGTH, [this]()              { this->iwm_stat_qrcode_length(); }},                    // 0xBE
        { FUJICMD_QRCODE_OUTPUT, [this]()              { this->iwm_stat_qrcode_output(); }},                    // 0xBE
        { FUJICMD_STATUS, [this]()                     { this->iwm_stat_fuji_status(); }},                      // 0x53
        { FUJICMD_GET_HEAP, [this]()                   { this->iwm_stat_get_heap(); }},                         // 0xC1
    };

}

//// UNHANDLED CONTROL FUNCTIONS
// case FUJICMD_CLOSE_APPKEY:        	// 0xDB
// case FUJICMD_GET_ADAPTERCONFIG:      // 0xE8
// case FUJICMD_GET_DEVICE_FULLPATH:	// 0xDA
// case FUJICMD_GET_DIRECTORY_POSITION: // 0xE5
// case FUJICMD_GET_HOST_PREFIX:     	// 0xE0
// case FUJICMD_GET_SSID:               // 0xFE
// case FUJICMD_GET_WIFISTATUS:         // 0xFA
// case FUJICMD_READ_APPKEY:	 		// 0xDD
// case FUJICMD_READ_DEVICE_SLOTS:      // 0xF2
// case FUJICMD_READ_HOST_SLOTS:        // 0xF4
// case FUJICMD_SCAN_NETWORKS:          // 0xFD
// case FUJICMD_STATUS:              	// 0x53

//// Unhandled Status Commands
// case FUJICMD_CLOSE_APPKEY:           // 0xDB
// case FUJICMD_CLOSE_DIRECTORY:        // 0xF5
// case FUJICMD_CONFIG_BOOT:            // 0xD9
// case FUJICMD_COPY_FILE:              // 0xD8
// case FUJICMD_DISABLE_DEVICE:         // 0xD4
// case FUJICMD_ENABLE_DEVICE:          // 0xD5
// case FUJICMD_MOUNT_ALL:              // 0xD7
// case FUJICMD_MOUNT_HOST:             // 0xF9
// case FUJICMD_MOUNT_IMAGE:            // 0xF8
// case FUJICMD_NEW_DISK:               // 0xE7
// case FUJICMD_OPEN_APPKEY:            // 0xDC
// case FUJICMD_OPEN_DIRECTORY:         // 0xF7
// case FUJICMD_RESET:               	// 0xFF
// case FUJICMD_SET_BOOT_MODE:          // 0xD6
// case FUJICMD_SET_DEVICE_FULLPATH:    // 0xE2
// case FUJICMD_SET_DIRECTORY_POSITION: // 0xE4
// case FUJICMD_SET_HOST_PREFIX:        // 0xE1
// case FUJICMD_SET_SSID:            	// 0xFB
// case FUJICMD_UNMOUNT_HOST:           // 0xE6
// case FUJICMD_UNMOUNT_IMAGE:          // 0xE9
// case FUJICMD_WRITE_APPKEY:           // 0xDE
// case FUJICMD_WRITE_DEVICE_SLOTS:     // 0xF1
// case FUJICMD_WRITE_HOST_SLOTS:       // 0xF3
// case IWM_STATUS_DCB:                 // 0x01
// case IWM_STATUS_NEWLINE:             // 0x02

void iwmFuji::iwm_dummy_command() // SP CTRL command
{
	Debug_printf("\r\nData Received: ");
	for (int i = 0; i < data_len; i++)
		Debug_printf(" %02x", data_buffer[i]);
}

void iwmFuji::iwm_hello_world()
{
	Debug_printf("\r\nFuji cmd: HELLO WORLD");
	memcpy(data_buffer, "HELLO WORLD", 11);
	data_len = 11;
}

void iwmFuji::iwm_ctrl_reset_fujinet() // SP CTRL command
{
	Debug_printf("\r\nFuji cmd: REBOOT");
	send_status_reply_packet();
	// save device unit SP address somewhere and restore it after reboot?
	fnSystem.reboot();
}

void iwmFuji::iwm_stat_net_get_ssid() // SP STATUS command
{
	Debug_printf("\r\nFuji cmd: GET SSID");

	// Response to FUJICMD_GET_SSID
	struct
	{
		char ssid[MAX_SSID_LEN + 1];
		char password[MAX_WIFI_PASS_LEN];
	} cfg;

	memset(&cfg, 0, sizeof(cfg));

	/*
	 We memcpy instead of strcpy because technically the SSID and phasephras aren't strings and aren't null terminated,
	 they're arrays of bytes officially and can contain any byte value - including a zero - at any point in the array.
	 However, we're not consistent about how we treat this in the different parts of the code.
	*/
	std::string s = Config.get_wifi_ssid();
	memcpy(cfg.ssid, s.c_str(), s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());
	Debug_printf("\r\nReturning SSID: %s", cfg.ssid);

	s = Config.get_wifi_passphrase();
	memcpy(cfg.password, s.c_str(), s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

	// Move into response.
	memcpy(data_buffer, &cfg, sizeof(cfg));
	data_len = sizeof(cfg);
} // 0xFE

void iwmFuji::iwm_stat_net_scan_networks() // SP STATUS command
{
	Debug_printf("\r\nFuji cmd: SCAN NETWORKS");

	isReady = false;

	// if (scanStarted == false)
	//{
	_countScannedSSIDs = fnWiFi.scan_networks();
	//    scanStarted = true;
	setSSIDStarted = false;
	//}

	isReady = true;

	data_buffer[0] = _countScannedSSIDs;
	data_len = 1;
} // 0xFD

void iwmFuji::iwm_ctrl_net_scan_result() // SP STATUS command
{
	Debug_print("\r\nFuji cmd: GET SCAN RESULT");
	// scanStarted = false;

	uint8_t n = data_buffer[0];

	memset(&detail, 0, sizeof(detail));

	if (n < _countScannedSSIDs)
		fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);

	Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);
} // 0xFC

void iwmFuji::iwm_stat_net_scan_result() // SP STATUS command
{
	Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

	memset(data_buffer, 0, sizeof(data_buffer));
	memcpy(data_buffer, &detail, sizeof(detail));
	data_len = sizeof(detail);
} // 0xFC

void iwmFuji::iwm_ctrl_net_set_ssid() // SP CTRL command
{
	Debug_printf("\r\nFuji cmd: SET SSID");
	// if (!fnWiFi.connected() && setSSIDStarted == false)
	//   {

	// uint16_t s = data_len;
	// s--;

	// Data for FUJICMD_SET_SSID
	struct
	{
		char ssid[MAX_SSID_LEN + 1];
		char password[MAX_WIFI_PASS_LEN];
	} cfg;

	// to do - copy data over to cfg
	memcpy(cfg.ssid, &data_buffer, sizeof(cfg.ssid));
	memcpy(cfg.password, &data_buffer[sizeof(cfg.ssid)], sizeof(cfg.password));
	// adamnet_recv_buffer((uint8_t *)&cfg, s);

	bool save = false; // for now don't save - to do save if connection was succesful

	// URL Decode SSID/PASSWORD to handle special chars FIXME
    //mstr::urlDecode(cfg.ssid, sizeof(cfg.ssid));
    //mstr::urlDecode(cfg.password, sizeof(cfg.password));

	Debug_printf("\r\nConnecting to net: %s password: %s\n", cfg.ssid, cfg.password);

// TODO: better
#if ESP_OK != 0
#error "ESP_OK != 0"
#endif
	if (fnWiFi.connect(cfg.ssid, cfg.password) == 0) // ESP_OK
	{
		Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
		Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
	}
	setSSIDStarted = true; // gets reset to false by scanning networks ... hmmm
	// Only save these if we're asked to, otherwise assume it was a test for connectivity
	// should only save if connection was successful - i think
	if (save)
	{
		Config.save();
	}
	// }
} // 0xFB

// Get WiFi Status
void iwmFuji::iwm_stat_net_get_wifi_status() // SP Status command
{
	Debug_printf("\r\nFuji cmd: GET WIFI STATUS");
	// WL_CONNECTED = 3, WL_DISCONNECTED = 6
	uint8_t wifiStatus = fnWiFi.connected() ? 3 : 6;
	data_buffer[0] = wifiStatus;
	data_len = 1;
	Debug_printf("\r\nReturning Status: %d", wifiStatus);
}

// Mount Server
void iwmFuji::iwm_ctrl_mount_host() // SP CTRL command
{
	unsigned char hostSlot = data_buffer[0]; // adamnet_recv();
	Debug_printf("\r\nFuji cmd: MOUNT HOST no. %d", hostSlot);

	if ((hostSlot < 8) && (hostMounted[hostSlot] == false))
	{
		_fnHosts[hostSlot].mount();
		hostMounted[hostSlot] = true;
	}
}

// UnMount Server
void iwmFuji::iwm_ctrl_unmount_host() // SP CTRL command
{
	unsigned char hostSlot = data_buffer[0]; // adamnet_recv();
	Debug_printf("\r\nFuji cmd: UNMOUNT HOST no. %d", hostSlot);

	if ((hostSlot < 8) && (hostMounted[hostSlot] == false))
	{
		_fnHosts[hostSlot].umount();
		hostMounted[hostSlot] = true;
	}
}

// Disk Image Mount
uint8_t iwmFuji::iwm_ctrl_disk_image_mount() // SP CTRL command
{
	Debug_printf("\r\nFuji cmd: MOUNT IMAGE");

	uint8_t deviceSlot = data_buffer[0]; // adamnet_recv();
	uint8_t options = data_buffer[1];	 // adamnet_recv(); // DISK_ACCESS_MODE

	// TODO: Implement FETCH?
	char flag[4] = {'r', 'b', 0, 0};
	if (options == DISK_ACCESS_MODE_WRITE)
		flag[2] = '+';

	// A couple of reference variables to make things much easier to read...
	fujiDisk &disk = _fnDisks[deviceSlot];
	fujiHost &host = _fnHosts[disk.host_slot];
	DEVICE_TYPE *disk_dev = get_disk_dev(deviceSlot);

	Debug_printf("\r\nSelecting '%s' from host #%u as %s on D%u:\n", disk.filename, disk.host_slot, flag, deviceSlot + 1);

	disk_dev->host = &host;
	disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

	if (disk.fileh == nullptr)
	{
		Debug_printf("\r\nFailed to open %s", disk.filename);
		return SP_ERR_NODRIVE;
	}

	// We've gotten this far, so make sure our bootable CONFIG disk is disabled
	boot_config = false;

	// We need the file size for loading XEX files and for CASSETTE, so get that too
	disk.disk_size = host.file_size(disk.fileh);

	// special handling for Disk ][ .woz images
	// mediatype_t mt = MediaType::discover_mediatype(disk.filename);
	// if (mt == mediatype_t::MEDIATYPE_PO)
	// { // And now mount it

	if (options == DISK_ACCESS_MODE_WRITE)
	{
		disk_dev->readonly = false;
	}

	disk.disk_type = disk_dev->mount(disk.fileh, disk.filename, disk.disk_size);

	return SP_ERR_NOERROR;
}

// Toggle boot config on/off, aux1=0 is disabled, aux1=1 is enabled
void iwmFuji::iwm_ctrl_set_boot_config() // SP CTRL command
{
	boot_config = data_buffer[0]; // adamnet_recv();

	if (!boot_config)
	{
		fujiDisk &disk = _fnDisks[0];
		if (disk.host_slot == INVALID_HOST_SLOT)
		{
			get_disk_dev(0)->unmount();
			_fnDisks[0].reset();
		}
	}
}

// Do SIO copy
void iwmFuji::iwm_ctrl_copy_file()
{
	std::string copySpec;
	std::string sourcePath;
	std::string destPath;
	fnFile *sourceFile;
	fnFile *destFile;
	char *dataBuf;
	unsigned char sourceSlot;
	unsigned char destSlot;

	sourceSlot = data_buffer[0];
	destSlot = data_buffer[1];
	copySpec = std::string((char *)&data_buffer[2]);
	Debug_printf("copySpec: %s\n", copySpec.c_str());

	// Chop up copyspec.
	sourcePath = copySpec.substr(0, copySpec.find_first_of("|"));
	destPath = copySpec.substr(copySpec.find_first_of("|") + 1);

	// At this point, if last part of dest path is / then copy filename from source.
	if (destPath.back() == '/')
	{
		Debug_printf("append source file\n");
		std::string sourceFilename = sourcePath.substr(sourcePath.find_last_of("/") + 1);
		destPath += sourceFilename;
	}

	// Mount hosts, if needed.
	_fnHosts[sourceSlot].mount();
	_fnHosts[destSlot].mount();

	// Open files...
	sourceFile = _fnHosts[sourceSlot].fnfile_open(sourcePath.c_str(), (char *)sourcePath.c_str(), sourcePath.size() + 1, "rb");
	destFile = _fnHosts[destSlot].fnfile_open(destPath.c_str(), (char *)destPath.c_str(), destPath.size() + 1, "wb");

	dataBuf = (char *)malloc(532);
	size_t count = 0;
	do
	{
		count = fnio::fread(dataBuf, 1, 532, sourceFile);
		fnio::fwrite(dataBuf, 1, count, destFile);
	} while (count > 0);

	// copyEnd:
	fnio::fclose(sourceFile);
	fnio::fclose(destFile);
	free(dataBuf);
}

// Mount all
bool iwmFuji::mount_all()
{
	bool nodisks = true; // Check at the end if no disks are in a slot and disable config

	for (int i = 0; i < MAX_DISK_DEVICES; i++)
	{
		fujiDisk &disk = _fnDisks[i];
		fujiHost &host = _fnHosts[disk.host_slot];
		DEVICE_TYPE *disk_dev = get_disk_dev(i);
		char flag[4] = {'r', 'b', 0, 0};
		if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
			flag[2] = '+';

        if (disk.host_slot != INVALID_HOST_SLOT && strlen(disk.filename) > 0)
		{
			nodisks = false; // We have a disk in a slot

			if (host.mount() == false)
			{
				return true;
			}

			Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n", disk.filename, disk.host_slot, flag, i + 1);

			disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

			if (disk.fileh == nullptr)
			{
				return true;
			}

			// We've gotten this far, so make sure our bootable CONFIG disk is disabled
			boot_config = false;

			// We need the file size for loading XEX files and for CASSETTE, so get that too
			disk.disk_size = host.file_size(disk.fileh);

			// And now mount it
			disk.disk_type = disk_dev->mount(disk.fileh, disk.filename, disk.disk_size);
			if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
			{
				disk_dev->readonly = false;
			}
		}
	}

	if (nodisks)
	{
		// No disks in a slot, disable config
		boot_config = false;
	}

	// Go ahead and respond ok
	return false;
}

// Set boot mode
void iwmFuji::iwm_ctrl_set_boot_mode()
{
	uint8_t bm = data_buffer[0]; // adamnet_recv();

	insert_boot_device(bm);
	boot_config = true;
}

char *_generate_appkey_filename(appkey *info)
{
	static char filenamebuf[30];

	snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key", info->creator, info->app, info->key);
	return filenamebuf;
}

/*
 Opens an "app key" for reading/writing - stores appkey name for subsequent read/write calls
*/
void iwmFuji::iwm_ctrl_open_app_key()
{
	int idx = 0;
	FILE *fp;
	uint8_t creatorL = data_buffer[idx++];
	uint8_t creatorM = data_buffer[idx++];
	uint8_t app = data_buffer[idx++];
	uint8_t key = data_buffer[idx++];
	uint8_t mode = data_buffer[idx++];

	snprintf(_appkeyfilename, sizeof(_appkeyfilename), "/FujiNet/%02hhx%02hhx%02hhx%02hhx.key", creatorM, creatorL, app, key);
	Debug_printf("\r\nFuji Cmd: OPEN APPKEY %s in mode %i\n", _appkeyfilename, mode);

	// If reading, we will update the control stat length for the subsequent read_app_key status call
	if (mode == 1) return;	// write mode

	// set the appkey_size according to the mode, if mode is unknown, default to 64
	appkey_size = get_value_or_default(mode_to_keysize, mode, 64);

	fp = fnSDFAT.file_open(_appkeyfilename, "r");
	if (fp == nullptr)
	{
		Debug_printf("iwm_ctrl_open_app_key ERROR: Could not read from SD Card.\r\n");

		// Set stat buffer to 0 to signify the app key was not found
		ctrl_stat_len=0;
		return;
	}

	// don't need to do this if we're returning exact number of bytes
	// // Clear out stat buffer before reading into it
	// memset(ctrl_stat_buffer, 0, sizeof(ctrl_stat_buffer));

	// Read in the app key file data, to be sent in read_app_key call
	ctrl_stat_len = fread(ctrl_stat_buffer, sizeof(char), appkey_size, fp);
	fclose(fp);

}

/*
 Write an "app key" to SD (ONLY!) storage.
*/
void iwmFuji::iwm_ctrl_write_app_key()
{
	FILE *fp;
    std::vector<uint8_t> data(appkey_size, 0);
    std::copy(&data_buffer[0], &data_buffer[0] + data_len, data.begin());

	Debug_printf("\r\nFuji Cmd: WRITE APPKEY\n");


	// Make sure we have a "/FujiNet" directory, since that's where we're putting these files
	fnSDFAT.create_path("/FujiNet");

	fp = fnSDFAT.file_open(_appkeyfilename, "w");
	if (fp == nullptr)
	{
		Debug_printf("iwm_ctrl_write_app_key ERROR: Could not write to SD Card.\r\n");
		return;
	}

	fwrite(data.data(), sizeof(uint8_t), data_len, fp);
	fclose(fp);
}

/*
 Read an "app key" from SD (ONLY!) storage
*/
void iwmFuji::iwm_stat_read_app_key() // return the app key that was just read by the open() control command
{
	Debug_printf("\r\nFuji cmd: READ APP KEY");

	memset(data_buffer, 0, sizeof(data_buffer));
	memcpy(data_buffer, ctrl_stat_buffer, ctrl_stat_len);
	data_len = ctrl_stat_len;
}

// DEBUG TAPE
void iwmFuji::debug_tape() {}

// Disk Image Unmount
void iwmFuji::iwm_ctrl_disk_image_umount()
{
	unsigned char ds = data_buffer[0]; // adamnet_recv();
	DEVICE_TYPE *disk_dev = get_disk_dev(ds);
	if (disk_dev->device_active)
		disk_dev->switched = true;
	disk_dev->unmount();
	_fnDisks[ds].reset();
}

//==============================================================================================================================

// Disk Image Rotate
/*
  We rotate disks my changing their disk device ID's. That prevents
  us from having to unmount and re-mount devices.
*/
void iwmFuji::image_rotate()
{
	Debug_printf("\r\nFuji cmd: IMAGE ROTATE");

	int count = 0;
	// Find the first empty slot
	while (_fnDisks[count].fileh != nullptr)
		count++;

	if (count > 1)
	{
		count--;

		// Save the device ID of the disk in the last slot
		int last_id = get_disk_dev(count)->id();

		for (int n = count; n > 0; n--)
		{
			int swap = get_disk_dev(n - 1)->id();
			Debug_printf("setting slot %d to ID %hx\n", n, swap);
			_iwm_bus->changeDeviceId(get_disk_dev(n), swap); // to do!
		}

		// The first slot gets the device ID of the last slot
		_iwm_bus->changeDeviceId(get_disk_dev(0), last_id);
	}
}

// This gets called when we're about to shutdown/reboot
void iwmFuji::shutdown()
{
	for (int i = 0; i < MAX_DISK_DEVICES; i++)
		get_disk_dev(i)->unmount();
}

uint8_t iwmFuji::iwm_ctrl_open_directory()
{
	Debug_printf("\r\nFuji cmd: OPEN DIRECTORY");

	uint8_t err_result = SP_ERR_NOERROR;

	int idx = 0;
	uint8_t hostSlot = data_buffer[idx++]; // adamnet_recv();

	uint16_t s = data_len - 1; // two strings but not the slot number

	memcpy((uint8_t *)&dirpath, (uint8_t *)&data_buffer[idx], s); // adamnet_recv_buffer((uint8_t *)&dirpath, s);

	if (_current_open_directory_slot == -1)
	{
		// See if there's a search pattern after the directory path
		const char *pattern = nullptr;
		int pathlen = strnlen(dirpath, s);
		if (pathlen < s - 3) // Allow for two NULLs and a 1-char pattern
		{
			pattern = dirpath + pathlen + 1;
			int patternlen = strnlen(pattern, s - pathlen - 1);
			if (patternlen < 1)
				pattern = nullptr;
		}

		// Remove trailing slash
		if (pathlen > 1 && dirpath[pathlen - 1] == '/')
			dirpath[pathlen - 1] = '\0';

		Debug_printf("Opening directory: \"%s\", pattern: \"%s\"\n", dirpath, pattern ? pattern : "");

		if (_fnHosts[hostSlot].dir_open(dirpath, pattern, 0))
			_current_open_directory_slot = hostSlot;
		else
			err_result = SP_ERR_IOERROR;
	}
	return err_result;
}

void _set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen)
{
	// File modified date-time
	struct tm *modtime = localtime(&f->modified_time);
	modtime->tm_mon++;
	modtime->tm_year -= 100;

	dest[0] = modtime->tm_year;
	dest[1] = modtime->tm_mon;
	dest[2] = modtime->tm_mday;
	dest[3] = modtime->tm_hour;
	dest[4] = modtime->tm_min;
	dest[5] = modtime->tm_sec;

	// File size
	uint32_t fsize = f->size;
	dest[6] = fsize & 0xFF;
	dest[7] = (fsize >> 8) & 0xFF;
	dest[8] = (fsize >> 16) & 0xFF;
	dest[9] = (fsize >> 24) & 0xFF;

	// File flags
#define FF_DIR 0x01
#define FF_TRUNC 0x02

	dest[10] = f->isDir ? FF_DIR : 0;

	maxlen -= ADDITIONAL_DETAILS_BYTES; // Adjust the max return value with the number of additional bytes we're copying
	if (f->isDir)						// Also subtract a byte for a terminating slash on directories
		maxlen--;
	if (strlen(f->filename) >= maxlen)
		dest[11] |= FF_TRUNC;

	// File type
	dest[12] = MediaType::discover_mediatype(f->filename);

	Debug_printf("Addtl: ");
	for (int i = 0; i < ADDITIONAL_DETAILS_BYTES; i++)
		Debug_printf("%02x ", dest[i]);
	Debug_printf("\n");
}

void iwmFuji::iwm_ctrl_read_directory_entry()
{
	uint8_t maxlen = data_buffer[0];
	uint8_t addtl = data_buffer[1];

	// if (response[0] == 0x00) // to do - figure out the logic here?
	// {
	Debug_printf("Fuji cmd: READ DIRECTORY ENTRY (max=%hu)\n", maxlen);

	fsdir_entry_t *f = _fnHosts[_current_open_directory_slot].dir_nextfile();

	if (f != nullptr)
	{
		Debug_printf("::read_direntry \"%s\"\n", f->filename);

		int bufsize = sizeof(dirpath);
		char *filenamedest = dirpath;

		// If 0x80 is set on AUX2, send back additional information
		if (addtl & 0x80)
		{
			_set_additional_direntry_details(f, (uint8_t *)dirpath, maxlen);
			// Adjust remaining size of buffer and file path destination
			bufsize = sizeof(dirpath) - ADDITIONAL_DETAILS_BYTES;
			filenamedest = dirpath + ADDITIONAL_DETAILS_BYTES;
		}
		else
		{
			bufsize = maxlen;
		}

		int filelen;
		// int filelen = strlcpy(filenamedest, f->filename, bufsize);
		if (maxlen < 128)
		{
			filelen = util_ellipsize(f->filename, filenamedest, bufsize - 1);
		}
		else
		{
			filelen = strlcpy(filenamedest, f->filename, bufsize);
		}

		// Add a slash at the end of directory entries
		if (f->isDir && filelen < (bufsize - 2))
		{
			dirpath[filelen] = '/';
			dirpath[filelen + 1] = '\0';
			Debug_printf("::entry is dir - %s\n", dirpath);
		}
		// Hack-o-rama to add file type character to beginning of path. - this was for Adam, but must keep for CONFIG compatability
		// in Apple 2 config will somehow have to work around these extra char's
		if (maxlen == DIR_MAX_LEN)
		{
			memmove(&dirpath[2], dirpath, 254);
			// if (strstr(dirpath, ".DDP") || strstr(dirpath, ".ddp"))
			// {
			//     dirpath[0] = 0x85;
			//     dirpath[1] = 0x86;
			// }
			// else if (strstr(dirpath, ".DSK") || strstr(dirpath, ".dsk"))
			// {
			//     dirpath[0] = 0x87;
			//     dirpath[1] = 0x88;
			// }
			// else if (strstr(dirpath, ".ROM") || strstr(dirpath, ".rom"))
			// {
			//     dirpath[0] = 0x89;
			//     dirpath[1] = 0x8a;
			// }
			// else if (strstr(dirpath, "/"))
			// {
			//     dirpath[0] = 0x83;
			//     dirpath[1] = 0x84;
			// }
			// else
			dirpath[0] = dirpath[1] = 0x20;
		}
	}
	else
	{
		Debug_println("Reached end of of directory");
		dirpath[0] = 0x7F;
		dirpath[1] = 0x7F;
	}
	memset(ctrl_stat_buffer, 0, sizeof(ctrl_stat_buffer));
	memcpy(ctrl_stat_buffer, dirpath, maxlen);
	ctrl_stat_len = maxlen;
	// }
	// else
	// {
	//     AdamNet.start_time = esp_timer_get_time();
	//     adamnet_response_ack();
	// }
}

void iwmFuji::iwm_stat_read_directory_entry()
{
	Debug_printf("\r\nFuji cmd: READ DIRECTORY ENTRY");
	memcpy(data_buffer, ctrl_stat_buffer, ctrl_stat_len);
	data_len = ctrl_stat_len;
}

void iwmFuji::iwm_stat_get_directory_position()
{
	Debug_printf("\r\nFuji cmd: GET DIRECTORY POSITION");

	uint16_t pos = _fnHosts[_current_open_directory_slot].dir_tell();

	data_len = sizeof(pos);
	memcpy(data_buffer, &pos, sizeof(pos));
}

void iwmFuji::iwm_ctrl_set_directory_position()
{
	Debug_printf("\nFuji cmd: SET DIRECTORY POSITION");

	// DAUX1 and DAUX2 hold the position to seek to in low/high order
	uint16_t pos = 0;

	// adamnet_recv_buffer((uint8_t *)&pos, sizeof(uint16_t));
	memcpy((uint8_t *)&pos, (uint8_t *)&data_buffer, sizeof(uint16_t));

	Debug_printf("\npos is now %u", pos);

	_fnHosts[_current_open_directory_slot].dir_seek(pos);
}

void iwmFuji::iwm_ctrl_close_directory()
{
	Debug_printf("\nFuji cmd: CLOSE DIRECTORY");

	if (_current_open_directory_slot != -1)
		_fnHosts[_current_open_directory_slot].dir_close();

	_current_open_directory_slot = -1;
	fnSystem.delay(100); // add delay because bad traces
}

void iwmFuji::iwm_stat_get_adapter_config_extended()
{
    // also return string versions of the data to save the host some computing
	Debug_printf("Fuji cmd: GET ADAPTER CONFIG EXTENDED\r\n");
    AdapterConfigExtended cfg;
    memset(&cfg, 0, sizeof(cfg));       // ensures all strings are null terminated

    strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

    if (!fnWiFi.connected())
    {
        strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
    }
    else
    {
        strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
        strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
        fnWiFi.get_current_bssid(cfg.bssid);
        fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
        fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
    }

    fnWiFi.get_mac(cfg.macAddress);

    // convert fields to strings
    strlcpy(cfg.sLocalIP, fnSystem.Net.get_ip4_address_str().c_str(), 16);
    strlcpy(cfg.sGateway, fnSystem.Net.get_ip4_gateway_str().c_str(), 16);
    strlcpy(cfg.sDnsIP,   fnSystem.Net.get_ip4_dns_str().c_str(),     16);
    strlcpy(cfg.sNetmask, fnSystem.Net.get_ip4_mask_str().c_str(),    16);

    snprintf(cfg.sMacAddress, sizeof(cfg.sMacAddress), "%02X:%02X:%02X:%02X:%02X:%02X", cfg.macAddress[0], cfg.macAddress[1], cfg.macAddress[2], cfg.macAddress[3], cfg.macAddress[4], cfg.macAddress[5]);
    snprintf(cfg.sBssid, sizeof(cfg.sBssid), "%02X:%02X:%02X:%02X:%02X:%02X", cfg.bssid[0], cfg.bssid[1], cfg.bssid[2], cfg.bssid[3], cfg.bssid[4], cfg.bssid[5]);

	memcpy(data_buffer, &cfg, sizeof(cfg));
	data_len = sizeof(cfg);

}

void iwmFuji::iwm_stat_fuji_status()
{
	// Place holder for 4 bytes to fill the Fuji device status.
	// TODO: decide what we want to tell the host.
	// e.g.
	// - are all devices working? maybe some bitmap
	// - how many devices do we have?
	char ret[4] = {0};
	memcpy(data_buffer, &ret[0], 4);
	data_len = 4;
}

void iwmFuji::iwm_stat_get_heap()
{
#ifdef ESP_PLATFORM
	uint32_t avail = esp_get_free_internal_heap_size();
#else
	uint32_t avail = 0;
#endif

    memcpy(data_buffer, &avail, sizeof(avail));
    data_len = sizeof(avail);
    return;
}

// Get network adapter configuration
void iwmFuji::iwm_stat_get_adapter_config()
{
	Debug_printf("Fuji cmd: GET ADAPTER CONFIG\r\n");
	AdapterConfig cfg;
	memset(&cfg, 0, sizeof(cfg));

	strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

	if (!fnWiFi.connected())
	{
		strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
	}
	else
	{
		strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
		strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
		fnWiFi.get_current_bssid(cfg.bssid);
		fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
		fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
	}

	fnWiFi.get_mac(cfg.macAddress);

	memcpy(data_buffer, &cfg, sizeof(cfg));
	data_len = sizeof(cfg);
}

//  Make new disk and shove into device slot
void iwmFuji::iwm_ctrl_new_disk()
{
	int idx = 0;
	uint8_t hs = data_buffer[idx++]; // adamnet_recv();
	uint8_t ds = data_buffer[idx++]; // adamnet_recv();
	uint8_t t = data_buffer[idx++];	 // added for apple2;
	uint32_t numBlocks;
	uint8_t *c = (uint8_t *)&numBlocks;
	uint8_t p[256];

	// adamnet_recv_buffer(c, sizeof(uint32_t));
	memcpy((uint8_t *)c, (uint8_t *)&data_buffer[idx], sizeof(uint32_t));
	idx += sizeof(uint32_t);

	memcpy(p, (uint8_t *)&data_buffer[idx], sizeof(p));
	// adamnet_recv_buffer(p, 256);

	fujiDisk &disk = _fnDisks[ds];
	fujiHost &host = _fnHosts[hs];

	if (host.file_exists((const char *)p))
	{
		return;
	}

	disk.host_slot = hs;
	disk.access_mode = DISK_ACCESS_MODE_WRITE;
	strlcpy(disk.filename, (const char *)p, 256);

	disk.fileh = host.fnfile_open(disk.filename, disk.filename, sizeof(disk.filename), "wb");

	Debug_printf("Creating file %s on host slot %u mounting in disk slot %u numblocks: %lu\n", disk.filename, hs, ds, numBlocks);

	DEVICE_TYPE *disk_dev = get_disk_dev(ds);
	disk_dev->blank_header_type = t;
	disk_dev->write_blank(disk.fileh, numBlocks);

	fnio::fclose(disk.fileh);

	// Persist slots
	populate_config_from_slots();
	Config.mark_dirty();
	Config.save();
}

// Send host slot data to computer
void iwmFuji::iwm_stat_read_host_slots()
{
	Debug_printf("\nFuji cmd: READ HOST SLOTS");

	// adamnet_recv(); // ck

	char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
	memset(hostSlots, 0, sizeof(hostSlots));

	for (int i = 0; i < MAX_HOSTS; i++)
		strlcpy(hostSlots[i], _fnHosts[i].get_hostname(), MAX_HOSTNAME_LEN);

	memcpy(data_buffer, hostSlots, sizeof(hostSlots));
	data_len = sizeof(hostSlots);
}

// Read and save host slot data from computer
void iwmFuji::iwm_ctrl_write_host_slots()
{
	Debug_printf("\nFuji cmd: WRITE HOST SLOTS");

	char hostSlots[MAX_HOSTS][MAX_HOSTNAME_LEN];
	// adamnet_recv_buffer((uint8_t *)hostSlots, sizeof(hostSlots));
	memcpy((uint8_t *)hostSlots, data_buffer, sizeof(hostSlots));

	for (int i = 0; i < MAX_HOSTS; i++)
	{
		hostMounted[i] = false;
		_fnHosts[i].set_hostname(hostSlots[i]);
	}
	populate_config_from_slots();
	Config.save();
}

// Store host path prefix
void iwmFuji::iwm_ctrl_set_host_prefix() { Debug_printf("\nFuji cmd: SET HOST PREFIX - NOT IMPLEMENTED"); }

// Retrieve host path prefix
void iwmFuji::iwm_stat_get_host_prefix() { Debug_printf("\nFuji cmd: GET HOST PREFIX - NOT IMPLEMENTED"); }

// Send device slot data to computer
void iwmFuji::iwm_stat_read_device_slots()
{
	Debug_printf("\nFuji cmd: READ DEVICE SLOTS");

	struct disk_slot
	{
		uint8_t hostSlot;
		uint8_t mode;
		char filename[MAX_DISPLAY_FILENAME_LEN];
	};
	disk_slot diskSlots[MAX_DISK_DEVICES];

	memset(&diskSlots, 0, sizeof(diskSlots));

	int returnsize;

	// Load the data from our current device array
	for (int i = 0; i < MAX_DISK_DEVICES; i++)
	{
		diskSlots[i].mode = _fnDisks[i].access_mode;
		diskSlots[i].hostSlot = _fnDisks[i].host_slot;
		strlcpy(diskSlots[i].filename, _fnDisks[i].filename, MAX_DISPLAY_FILENAME_LEN);

                DEVICE_TYPE *disk_dev = get_disk_dev(i);
                if (disk_dev->device_active && !disk_dev->is_config_device)
                    diskSlots[i].mode |= DISK_ACCESS_MODE_MOUNTED;
	}

	returnsize = sizeof(disk_slot) * MAX_DISK_DEVICES;

	memcpy(data_buffer, &diskSlots, returnsize);
	data_len = returnsize;
}

// Get the wifi enabled value
void iwmFuji::iwm_stat_get_wifi_enabled()
{
	uint8_t e = Config.get_wifi_enabled() ? 1 : 0;
	Debug_printf("\nFuji cmd: GET WIFI ENABLED: %d", e);
	data_buffer[0] = e;
	data_len = 1;
}

// Read and save disk slot data from computer
void iwmFuji::iwm_ctrl_write_device_slots()
{
	Debug_printf("\nFuji cmd: WRITE DEVICE SLOTS");

	struct
	{
		uint8_t hostSlot;
		uint8_t mode;
		char filename[MAX_DISPLAY_FILENAME_LEN];
	} diskSlots[MAX_DISK_DEVICES];

	// adamnet_recv_buffer((uint8_t *)&diskSlots, sizeof(diskSlots));
	memcpy((uint8_t *)&diskSlots, data_buffer, sizeof(diskSlots));

	// Load the data into our current device array
	for (int i = 0; i < MAX_DISK_DEVICES; i++)
		_fnDisks[i].reset(diskSlots[i].filename, diskSlots[i].hostSlot, diskSlots[i].mode);

	// Save the data to disk
	populate_config_from_slots();
	Config.save();
}

// Write a 256 byte filename to the device slot
uint8_t iwmFuji::iwm_ctrl_set_device_filename()
{
	uint8_t err_return = SP_ERR_NOERROR;
	char f[MAX_FILENAME_LEN];
	int idx = 0;
	uint8_t deviceSlot = data_buffer[idx++];
	uint8_t host = data_buffer[idx++];
	uint8_t mode = data_buffer[idx++];

	uint16_t s = data_len;
	s -= 3; 	// remove 3 bytes for other args

	Debug_printf("\nSET DEVICE SLOT: %d, HOST: %d, MODE: %d", deviceSlot, host, mode);

	memcpy((uint8_t *)&f, &data_buffer[idx], s);
	Debug_printf("\nfilename: %s", f);

	if (deviceSlot < MAX_DISK_DEVICES) {
		memcpy(_fnDisks[deviceSlot].filename, f, MAX_FILENAME_LEN);

        // If the filename is empty, mark this as an invalid host, so that mounting will ignore it too
        if (strlen(_fnDisks[deviceSlot].filename) == 0) {
            _fnDisks[deviceSlot].host_slot = INVALID_HOST_SLOT;
        } else {
            _fnDisks[deviceSlot].host_slot = host;
        }

		_fnDisks[deviceSlot].access_mode = mode;
		populate_config_from_slots();
	}
	else
	{
		Debug_println("\nBAD DEVICE SLOT");
		err_return = SP_ERR_BADCTL;
	}
	return err_return;
}

// Get a 256 byte filename from device slot
void iwmFuji::iwm_stat_get_device_filename(uint8_t s)
{
	int d = s - 160;

	if (s == 0xDA)
	{
		Debug_print("Get filename generic unsupported for SmartPort\r\n");
		return;
	}

	Debug_printf("Get Filename for Drive %i: %s\r\n", d, _fnDisks[d].filename);
	data_len = MAX_FILENAME_LEN;
	memcpy(data_buffer, _fnDisks[d].filename, data_len);
}

// Mounts the desired boot disk number
void iwmFuji::insert_boot_device(uint8_t d)
{
	const char *boot_img = nullptr;
	fnFile *fBoot = nullptr;
	DEVICE_TYPE *disk_dev = get_disk_dev(0);

	switch (d)
	{
	case 0:
		boot_img = "/autorun.po";
		fBoot = fsFlash.fnfile_open(boot_img);
		break;
	case 1:
		boot_img = "/mount-and-boot.po";
		fBoot = fsFlash.fnfile_open(boot_img);
		break;
	case 2:
		Debug_printf("Mounting lobby server\n");
		if (fnTNFS.start("tnfs.fujinet.online"))
		{
			Debug_printf("opening lobby.\n");
			boot_img = "/APPLE2/_lobby.po";
			fBoot = fnTNFS.fnfile_open(boot_img);
		}
		break;
	default:
		Debug_printf("Invalid boot mode: %d\n", d);
		return;
	}

	if (fBoot == nullptr)
	{
		Debug_printf("Failed to open boot disk image: %s\n", boot_img);
		return;
	}

	disk_dev->mount(fBoot, boot_img, 143360, MEDIATYPE_PO);
	disk_dev->is_config_device = true;
}

void iwmFuji::iwm_ctrl_enable_device()
{
	unsigned char d = data_buffer[0]; // adamnet_recv();

	Debug_printf("\nFuji cmd: ENABLE DEVICE");
	IWM.enableDevice(d);
}

void iwmFuji::iwm_ctrl_disable_device()
{
	unsigned char d = data_buffer[0]; // adamnet_recv();

	Debug_printf("\nFuji cmd: DISABLE DEVICE");
	IWM.disableDevice(d);
}

iwmDisk *iwmFuji::bootdisk() { return _bootDisk; }

// Initializes base settings and adds our devices to the SIO bus
void iwmFuji::setup(systemBus *iwmbus)
{
	// set up Fuji device
	_iwm_bus = iwmbus;

	populate_slots_from_config();

	// Disable booting from CONFIG if our settings say to turn it off
	boot_config = false; // to do - understand?

	// Disable status_wait if our settings say to turn it off
	status_wait_enabled = false; // to do - understand?

	// add ourselves as a device
	_iwm_bus->addDevice(this, iwm_fujinet_type_t::FujiNet);

	theNetwork = new iwmNetwork();
	_iwm_bus->addDevice(theNetwork, iwm_fujinet_type_t::Network);

	theClock = new iwmClock();
	_iwm_bus->addDevice(theClock, iwm_fujinet_type_t::Clock);

	theCPM = new iwmCPM();
	_iwm_bus->addDevice(theCPM, iwm_fujinet_type_t::CPM);

	for (int i = MAX_SP_DEVICES - 1; i >= 0; i--)
	{
		DEVICE_TYPE *disk_dev = get_disk_dev(i);
		disk_dev->set_disk_number('0' + i);
		_iwm_bus->addDevice(disk_dev, iwm_fujinet_type_t::BlockDisk);
	}

	Debug_printf("\nConfig General Boot Mode: %u\n", Config.get_general_boot_mode());
	insert_boot_device(Config.get_general_boot_mode());

}

int iwmFuji::get_disk_id(int drive_slot) { return 0; }
std::string iwmFuji::get_host_prefix(int host_slot) { return std::string(); }

// Public method to update host in specific slot
fujiHost *iwmFuji::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    populate_config_from_slots();
    return &_fnHosts[host_slot];
}

void iwmFuji::send_status_reply_packet()
{

	uint8_t data[4];

	// Build the contents of the packet
	data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
	data[1] = 0; // block size 1
	data[2] = 0; // block size 2
	data[3] = 0; // block size 3
	IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmFuji::send_status_dib_reply_packet()
{
	Debug_printf("\r\nTHE_FUJI: Sending DIB reply\r\n");
	std::vector<uint8_t> data = create_dib_reply_packet(
		"THE_FUJI",                                             // name
		STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,         // status
		{ 0, 0, 0 },                                            // block size
		{ SP_TYPE_BYTE_FUJINET, SP_SUBTYPE_BYTE_FUJINET },      // type, subtype
		{ 0x00, 0x01 }                                          // version.
	);
	IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());
}

void iwmFuji::send_stat_get_enable()
{
	data_len = 1;
	data_buffer[0] = 1;
}

void iwmFuji::iwm_open(iwm_decoded_cmd_t cmd)
{
	send_status_reply_packet();
}

void iwmFuji::iwm_close(iwm_decoded_cmd_t cmd) {}
void iwmFuji::iwm_read(iwm_decoded_cmd_t cmd) {}

void iwmFuji::iwm_status(iwm_decoded_cmd_t cmd)
{
	status_code = get_status_code(cmd);
	status_completed = false;

	Debug_printf("\r\n[Fuji] Device %02x Status Code %02x\r\n", id(), status_code);

	auto it = status_handlers.find(status_code);
    if (it != status_handlers.end()) {
        it->second();
    } else {
		Debug_printf("ERROR: Unhandled status code: %02X\n", status_code);
    }

	if (status_completed) return;

	Debug_printf("\nStatus code complete, sending response");
	IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
}

void iwmFuji::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
	uint8_t control_code = get_status_code(cmd);
	Debug_printf("\ntheFuji Device %02x Control Code %02x", id(), control_code);

	err_result = SP_ERR_NOERROR;
	data_len = 512;

	Debug_printf("\nDecoding Control Data Packet for code: 0x%02x\r\n", control_code);
	IWM.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
	print_packet((uint8_t *)data_buffer, data_len);

	auto it = control_handlers.find(control_code);
    if (it != control_handlers.end()) {
        it->second();
    } else {
		Debug_printf("ERROR: Unhandled control code: %02X\n", control_code);
        err_result = SP_ERR_BADCTL;
    }

	send_reply_packet(err_result);
}


void iwmFuji::process(iwm_decoded_cmd_t cmd)
{
	fnLedManager.set(LED_BUS, true);

    auto it = command_handlers.find(cmd.command);
	// Debug_printf("\r\n----- iwmFuji::process handling command: %02X\r\n", cmd.command);
    if (it != command_handlers.end()) {
        it->second(cmd);
    } else {
        Debug_printv("\r\nUnknown command: %02x\r\n", cmd.command);
		iwm_return_badcmd(cmd);
    }

	fnLedManager.set(LED_BUS, false);
}

void iwmFuji::handle_ctl_eject(uint8_t spid)
{
	int ds = 255;
	for (int i = 0; i < MAX_DISK_DEVICES; i++)
	{
		if (theFuji->get_disk_dev(i)->id() == spid)
		{
			ds = i;
		}
	}
	if (ds != 255)
	{
		theFuji->get_disk(ds)->reset();
		Config.clear_mount(ds);
		Config.save();
		theFuji->populate_slots_from_config();
	}
}

void iwmFuji::iwm_ctrl_hash_input()
{
    std::vector<uint8_t> data(data_len, 0);
    std::copy(&data_buffer[0], &data_buffer[0] + data_len, data.begin());
    hasher.add_data(data);
}

void iwmFuji::iwm_ctrl_hash_compute(bool clear_data)
{
    Debug_printf("FUJI: HASH COMPUTE\n");
    algorithm = Hash::to_algorithm(data_buffer[0]);
    hasher.compute(algorithm, clear_data);
}

void iwmFuji::iwm_stat_hash_length()
{
    uint8_t is_hex = data_buffer[0] == 1;
    uint8_t r = hasher.hash_length(algorithm, is_hex);

	memset(data_buffer, 0, sizeof(data_buffer));
	data_buffer[0] = r;
	data_len = 1;
}

void iwmFuji::iwm_ctrl_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT CONTROL\n");
    hash_is_hex_output = data_buffer[0] == 1;
}

void iwmFuji::iwm_stat_hash_output()
{
    Debug_printf("FUJI: HASH OUTPUT STAT\n");
	memset(data_buffer, 0, sizeof(data_buffer));

	if (hash_is_hex_output) {
		std::string hex_output = hasher.output_hex();
		std::memcpy(data_buffer, hex_output.c_str(), hex_output.size());
		data_len = static_cast<int>(hex_output.size());
	} else {
		std::vector<uint8_t> binary_output = hasher.output_binary();
		std::memcpy(data_buffer, binary_output.data(), binary_output.size());
		data_len = static_cast<int>(binary_output.size());
	}
}

void iwmFuji::iwm_ctrl_hash_clear()
{
    hasher.clear();
}

void iwmFuji::iwm_ctrl_qrcode_input()
{
    Debug_printf("FUJI: QRCODE INPUT (len: %d)\n", data_len);
    std::vector<uint8_t> data(data_len, 0);
    std::copy(&data_buffer[0], &data_buffer[0] + data_len, data.begin());
    qrManager.in_buf += std::string((const char *)data.data(), data_len);
}

void iwmFuji::iwm_ctrl_qrcode_encode()
{
    size_t out_len = 0;

    qrManager.output_mode = 0;
    qrManager.version = data_buffer[0] & 0b01111111;
    qrManager.ecc_mode = data_buffer[1];
    bool shorten = data_buffer[2];

    Debug_printf("FUJI: QRCODE ENCODE\n");
    Debug_printf("QR Version: %d, ECC: %d, Shorten: %s\n", qrManager.version, qrManager.ecc_mode, shorten ? "Y" : "N");

    std::string url = qrManager.in_buf;

    if (shorten) {
        url = fnHTTPD.shorten_url(url);
    }

    std::vector<uint8_t> p = QRManager::encode(
        url.c_str(),
        url.size(),
        qrManager.version,
        qrManager.ecc_mode,
        &out_len
    );

    qrManager.in_buf.clear();

    if (!out_len)
    {
        Debug_printf("QR code encoding failed\n");
        return;
    }

    Debug_printf("Resulting QR code is: %u modules\n", out_len);
}

void iwmFuji::iwm_stat_qrcode_length()
{
    Debug_printf("FUJI: QRCODE LENGTH\n");
    size_t len = qrManager.out_buf.size();
	data_buffer[0] = (uint8_t)(len >> 0);
    data_buffer[1] = (uint8_t)(len >> 8);
	data_len = 2;
}

void iwmFuji::iwm_ctrl_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT CONTROL\n");

    uint8_t output_mode = data_buffer[0];
    Debug_printf("Output mode: %i\n", output_mode);

    size_t len = qrManager.out_buf.size();

    if (len && (output_mode != qrManager.output_mode)) {
        if (output_mode == QR_OUTPUT_MODE_BINARY) {
            qrManager.to_binary();
        }
        else if (output_mode == QR_OUTPUT_MODE_ATASCII) {
            qrManager.to_atascii();
        }
        else if (output_mode == QR_OUTPUT_MODE_BITMAP) {
            qrManager.to_bitmap();
        }
        qrManager.output_mode = output_mode;
    }
}

void iwmFuji::iwm_stat_qrcode_output()
{
    Debug_printf("FUJI: QRCODE OUTPUT STAT\n");
	memset(data_buffer, 0, sizeof(data_buffer));

	data_len = qrManager.out_buf.size();
	memcpy(data_buffer, &qrManager.out_buf[0], data_len);

	qrManager.out_buf.erase(qrManager.out_buf.begin(), qrManager.out_buf.begin() + data_len);
    qrManager.out_buf.shrink_to_fit();
}

#endif /* BUILD_APPLE */
