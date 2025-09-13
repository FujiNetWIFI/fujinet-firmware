
#include "httpServiceParser.h"

#include <sstream>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "httpService.h"
#include "fuji.h"

using namespace std;

#define MAX_PRINTER_LIST_BUFFER (2048)

const string fnHttpServiceParser::substitute_tag(const string &tag)
{
    enum tagids
    {
        FN_HOSTNAME = 0,
#ifndef ESP_PLATFORM
        FN_DEVICE_NAME,
        FN_LABEL,
#endif
        FN_VERSION,
        FN_IPADDRESS,
        FN_IPMASK,
        FN_IPGATEWAY,
        FN_IPDNS,
        FN_WIFISSID,
        FN_WIFIBSSID,
        FN_WIFIMAC,
        FN_WIFIDETAIL,
#ifndef ESP_PLATFORM
        FN_UNAME,
#endif
        FN_SPIFFS_SIZE,
        FN_SPIFFS_USED,
        FN_SD_SIZE,
        FN_SD_USED,
        FN_UPTIME_STRING,
        FN_UPTIME,
        FN_CURRENTTIME,
        FN_TIMEZONE,
        FN_ROTATION_SOUNDS,
        FN_UDPSTREAM_HOST,
        FN_HEAPSIZE,
        FN_SYSSDK,
        FN_SYSCPUREV,
        FN_BUSVOLTS,
        FN_SIO_HSINDEX,
        FN_SIO_HSBAUD,
        FN_PRINTER1_MODEL,
        FN_PRINTER1_PORT,
        FN_PLAY_RECORD,
        FN_PULLDOWN,
        FN_CASSETTE_ENABLED,
        FN_CONFIG_ENABLED,
        FN_CONFIG_NG,
        FN_STATUS_WAIT_ENABLED,
        FN_BOOT_MODE,
        FN_PRINTER_ENABLED,
        FN_MODEM_ENABLED,
        FN_MODEM_SNIFFER_ENABLED,
#if !defined(ESP_PLATFORM) || defined(BUILD_RS232)
        FN_SERIAL_PORT_BAUD,
#endif
#ifndef ESP_PLATFORM
        FN_SERIAL_PORT,
        FN_SERIAL_COMMAND,
        FN_SERIAL_PROCEED,
        FN_SIO_HSTEXT,
#endif
        FN_BOIP_ENABLED,
        FN_BOIP_HOST,
        FN_DRIVE1HOST,
        FN_DRIVE2HOST,
        FN_DRIVE3HOST,
        FN_DRIVE4HOST,
        FN_DRIVE5HOST,
        FN_DRIVE6HOST,
        FN_DRIVE7HOST,
        FN_DRIVE8HOST,
#ifndef ESP_PLATFORM
        FN_DRIVE1BROWSER,
        FN_DRIVE2BROWSER,
        FN_DRIVE3BROWSER,
        FN_DRIVE4BROWSER,
        FN_DRIVE5BROWSER,
        FN_DRIVE6BROWSER,
        FN_DRIVE7BROWSER,
        FN_DRIVE8BROWSER,
#endif
        FN_DRIVE1MOUNT,
        FN_DRIVE2MOUNT,
        FN_DRIVE3MOUNT,
        FN_DRIVE4MOUNT,
        FN_DRIVE5MOUNT,
        FN_DRIVE6MOUNT,
        FN_DRIVE7MOUNT,
        FN_DRIVE8MOUNT,
        FN_HOST1,
        FN_HOST2,
        FN_HOST3,
        FN_HOST4,
        FN_HOST5,
        FN_HOST6,
        FN_HOST7,
        FN_HOST8,
        FN_DRIVE1DEVICE,
        FN_DRIVE2DEVICE,
        FN_DRIVE3DEVICE,
        FN_DRIVE4DEVICE,
        FN_DRIVE5DEVICE,
        FN_DRIVE6DEVICE,
        FN_DRIVE7DEVICE,
        FN_DRIVE8DEVICE,
        FN_HOST1PREFIX,
        FN_HOST2PREFIX,
        FN_HOST3PREFIX,
        FN_HOST4PREFIX,
        FN_HOST5PREFIX,
        FN_HOST6PREFIX,
        FN_HOST7PREFIX,
        FN_HOST8PREFIX,
        FN_ERRMSG,
        FN_HARDWARE_VER,
        FN_PRINTER_LIST,
        FN_ENCRYPT_PASSPHRASE_ENABLED,
        FN_APETIME_ENABLED,
        FN_CPM_ENABLED,
        FN_CPM_CCP,
        FN_ALT_CFG,
        FN_PCLINK_ENABLED,
        FN_LASTTAG
    };

    const char *tagids[FN_LASTTAG] =
    {
        "FN_HOSTNAME",
#ifndef ESP_PLATFORM
        "FN_DEVICE_NAME",
        "FN_LABEL",
#endif
        "FN_VERSION",
        "FN_IPADDRESS",
        "FN_IPMASK",
        "FN_IPGATEWAY",
        "FN_IPDNS",
        "FN_WIFISSID",
        "FN_WIFIBSSID",
        "FN_WIFIMAC",
        "FN_WIFIDETAIL",
#ifndef ESP_PLATFORM
        "FN_UNAME",
#endif
        "FN_SPIFFS_SIZE",
        "FN_SPIFFS_USED",
        "FN_SD_SIZE",
        "FN_SD_USED",
        "FN_UPTIME_STRING",
        "FN_UPTIME",
        "FN_CURRENTTIME",
        "FN_TIMEZONE",
        "FN_ROTATION_SOUNDS",
        "FN_UDPSTREAM_HOST",
        "FN_HEAPSIZE",
        "FN_SYSSDK",
        "FN_SYSCPUREV",
        "FN_BUSVOLTS",
        "FN_SIO_HSINDEX",
        "FN_SIO_HSBAUD",
        "FN_PRINTER1_MODEL",
        "FN_PRINTER1_PORT",
        "FN_PLAY_RECORD",
        "FN_PULLDOWN",
        "FN_CASSETTE_ENABLED",
        "FN_CONFIG_ENABLED",
        "FN_CONFIG_NG",
        "FN_STATUS_WAIT_ENABLED",
        "FN_BOOT_MODE",
        "FN_PRINTER_ENABLED",
        "FN_MODEM_ENABLED",
        "FN_MODEM_SNIFFER_ENABLED",
#if !defined(ESP_PLATFORM) || defined(BUILD_RS232)
        "FN_SERIAL_PORT_BAUD",
#endif
#ifndef ESP_PLATFORM
        "FN_SERIAL_PORT",
        "FN_SERIAL_COMMAND",
        "FN_SERIAL_PROCEED",
        "FN_SIO_HSTEXT",
#endif
        "FN_BOIP_ENABLED",
        "FN_BOIP_HOST",
        "FN_DRIVE1HOST",
        "FN_DRIVE2HOST",
        "FN_DRIVE3HOST",
        "FN_DRIVE4HOST",
        "FN_DRIVE5HOST",
        "FN_DRIVE6HOST",
        "FN_DRIVE7HOST",
        "FN_DRIVE8HOST",
#ifndef ESP_PLATFORM
        "FN_DRIVE1BROWSER",
        "FN_DRIVE2BROWSER",
        "FN_DRIVE3BROWSER",
        "FN_DRIVE4BROWSER",
        "FN_DRIVE5BROWSER",
        "FN_DRIVE6BROWSER",
        "FN_DRIVE7BROWSER",
        "FN_DRIVE8BROWSER",
#endif
        "FN_DRIVE1MOUNT",
        "FN_DRIVE2MOUNT",
        "FN_DRIVE3MOUNT",
        "FN_DRIVE4MOUNT",
        "FN_DRIVE5MOUNT",
        "FN_DRIVE6MOUNT",
        "FN_DRIVE7MOUNT",
        "FN_DRIVE8MOUNT",
        "FN_HOST1",
        "FN_HOST2",
        "FN_HOST3",
        "FN_HOST4",
        "FN_HOST5",
        "FN_HOST6",
        "FN_HOST7",
        "FN_HOST8",
        "FN_DRIVE1DEVICE",
        "FN_DRIVE2DEVICE",
        "FN_DRIVE3DEVICE",
        "FN_DRIVE4DEVICE",
        "FN_DRIVE5DEVICE",
        "FN_DRIVE6DEVICE",
        "FN_DRIVE7DEVICE",
        "FN_DRIVE8DEVICE",
        "FN_HOST1PREFIX",
        "FN_HOST2PREFIX",
        "FN_HOST3PREFIX",
        "FN_HOST4PREFIX",
        "FN_HOST5PREFIX",
        "FN_HOST6PREFIX",
        "FN_HOST7PREFIX",
        "FN_HOST8PREFIX",
        "FN_ERRMSG",
        "FN_HARDWARE_VER",
        "FN_PRINTER_LIST",
        "FN_ENCRYPT_PASSPHRASE_ENABLED",
        "FN_APETIME_ENABLED",
        "FN_CPM_ENABLED",
        "FN_CPM_CCP",
        "FN_ALT_CFG",
        "FN_PCLINK_ENABLED",
    };

    stringstream resultstream;

    // Debug_printf("Substituting tag '%s'\n", tag.c_str());

    int tagid;
    for (tagid = 0; tagid < FN_LASTTAG; tagid++)
    {
        if (0 == tag.compare(tagids[tagid]))
        {
            break;
        }
    }

    int drive_slot, host_slot;
    char disk_id;
#ifndef ESP_PLATFORM
    int hsioindex;
#endif

    // Provide a replacement value
    switch (tagid)
    {
    case FN_HOSTNAME:
        resultstream << fnSystem.Net.get_hostname();
        break;
#ifndef ESP_PLATFORM
    case FN_DEVICE_NAME:
        resultstream << Config.get_general_devicename();
        break;
    case FN_LABEL:
        // TODO html escape
        resultstream << Config.get_general_label();
        break;
#endif
    case FN_VERSION:
        resultstream << fnSystem.get_fujinet_version();
        break;
    case FN_IPADDRESS:
        resultstream << fnSystem.Net.get_ip4_address_str();
        break;
    case FN_IPMASK:
        resultstream << fnSystem.Net.get_ip4_mask_str();
        break;
    case FN_IPGATEWAY:
        resultstream << fnSystem.Net.get_ip4_gateway_str();
        break;
    case FN_IPDNS:
        resultstream << fnSystem.Net.get_ip4_dns_str();
        break;
    case FN_WIFISSID:
        resultstream << fnWiFi.get_current_ssid();
        break;
    case FN_WIFIBSSID:
        resultstream << fnWiFi.get_current_bssid_str();
        break;
    case FN_WIFIMAC:
        resultstream << fnWiFi.get_mac_str();
        break;
    case FN_WIFIDETAIL:
        resultstream << fnWiFi.get_current_detail_str();
        break;
#ifndef ESP_PLATFORM
    case FN_UNAME:
        resultstream << fnSystem.get_uname();
        break;
#endif
    case FN_SPIFFS_SIZE:
        resultstream << fsFlash.total_bytes();
        break;
    case FN_SPIFFS_USED:
        resultstream << fsFlash.used_bytes();
        break;
    case FN_SD_SIZE:
        resultstream << fnSDFAT.total_bytes();
        break;
    case FN_SD_USED:
        resultstream << fnSDFAT.used_bytes();
        break;
    case FN_UPTIME_STRING:
        resultstream << format_uptime();
        break;
    case FN_UPTIME:
        resultstream << uptime_seconds();
        break;
    case FN_CURRENTTIME:
        resultstream << fnSystem.get_current_time_str();
        break;
    case FN_TIMEZONE:
        resultstream << Config.get_general_timezone();
        break;
#ifdef BUILD_ATARI
    case FN_APETIME_ENABLED:
        resultstream << Config.get_apetime_enabled();
        break;
    case FN_PCLINK_ENABLED:
        resultstream << Config.get_pclink_enabled();
        break;
#endif /* BUILD_ATARI */

    case FN_ROTATION_SOUNDS:
        resultstream << Config.get_general_rotation_sounds();
        break;
    case FN_UDPSTREAM_HOST:
        if (Config.get_network_udpstream_port() > 0)
            resultstream << Config.get_network_udpstream_host() << ":" << Config.get_network_udpstream_port();
        else
            resultstream << Config.get_network_udpstream_host();
        break;
    case FN_HEAPSIZE:
        resultstream << fnSystem.get_free_heap_size();
        break;
    case FN_SYSSDK:
        resultstream << fnSystem.get_sdk_version();
        break;
    case FN_SYSCPUREV:
        resultstream << fnSystem.get_cpu_rev();
        break;
    case FN_BUSVOLTS:
        resultstream << ((float)fnSystem.get_sio_voltage()) / 1000.00 << "V";
        break;
#ifdef BUILD_ATARI
    case FN_SIO_HSINDEX:
        resultstream << SIO.getHighSpeedIndex();
        break;
#ifndef ESP_PLATFORM
    case FN_SIO_HSTEXT:
        hsioindex = SIO.getHighSpeedIndex();
        if (hsioindex == HSIO_DISABLED_INDEX)
            resultstream << "HSIO Disabled";
        else
            resultstream << hsioindex;
        break;
#endif
    case FN_SIO_HSBAUD:
        resultstream << SIO.getHighSpeedBaud();
        break;
#endif /* BUILD_ATARI */
#if defined(BUILD_RS232)
#warning "Why isn't this using Config.get_serial_baud() below?"
    case FN_SERIAL_PORT_BAUD:
        resultstream << Config.get_rs232_baud();
        break;
#endif
#ifndef ESP_PLATFORM
    case FN_SERIAL_PORT:
        resultstream << Config.get_serial_port();
        break;
#if 0
    case FN_SERIAL_PORT_BAUD:
        resultstream << Config.get_serial_baud();
        break;
#endif
    case FN_SERIAL_COMMAND:
        resultstream << Config.get_serial_command();
        break;
    case FN_SERIAL_PROCEED:
        resultstream << Config.get_serial_proceed();
        break;
#endif
    case FN_PRINTER1_MODEL:
        {
#ifdef BUILD_ADAM
            adamPrinter *ap = fnPrinters.get_ptr(0);
            if (ap != nullptr)
            {
                resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
            } else
                resultstream << "No Virtual Printer";
#endif /* BUILD_ADAM */
#ifdef BUILD_ATARI
            resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
#endif /* BUILD_ATARI */
#ifdef BUILD_APPLE
            resultstream << fnPrinters.get_ptr(0)->getPrinterPtr()->modelname();
#endif /* BUILD_APPLE */
        }
        break;
    case FN_PRINTER1_PORT:
        {
#ifdef BUILD_ADAM
            adamPrinter *ap = fnPrinters.get_ptr(0);
            if (ap != nullptr)
            {
                resultstream << (fnPrinters.get_port(0) + 1);
            } else
                resultstream << "";
#endif/* BUILD_ADAM */
#ifdef BUILD_ATARI
            resultstream << (fnPrinters.get_port(0) + 1);
#endif /* BUILD_ATARI */
#ifdef BUILD_APPLE
            resultstream << (fnPrinters.get_port(0) + 1);
#endif /* BUILD_APPLE */
        }
        break;
#ifdef BUILD_ATARI
    case FN_PLAY_RECORD:
        if (theFuji.cassette()->get_buttons())
            resultstream << "0 PLAY";
        else
            resultstream << "1 RECORD";
        break;
    case FN_PULLDOWN:
        if (theFuji.cassette()->has_pulldown())
            resultstream << "1 Pulldown Resistor";
        else
            resultstream << "0 B Button Press";
        break;
    case FN_CASSETTE_ENABLED:
        resultstream << Config.get_cassette_enabled();
        break;
    case FN_CONFIG_NG:
        resultstream << Config.get_general_config_ng();
        break;
#endif /* BUILD_ATARI */
    case FN_CONFIG_ENABLED:
        resultstream << Config.get_general_config_enabled();
        break;
    case FN_STATUS_WAIT_ENABLED:
        resultstream << Config.get_general_status_wait_enabled();
        break;
    case FN_BOOT_MODE:
        resultstream << Config.get_general_boot_mode();
        break;
    case FN_PRINTER_ENABLED:
        resultstream << Config.get_printer_enabled();
        break;
    case FN_MODEM_ENABLED:
        resultstream << Config.get_modem_enabled();
        break;
    case FN_MODEM_SNIFFER_ENABLED:
        resultstream << Config.get_modem_sniffer_enabled();
        break;
    case FN_BOIP_ENABLED:
        resultstream << Config.get_boip_enabled();
        break;
    case FN_BOIP_HOST:
        resultstream << Config.get_boip_host();
        if (Config.get_boip_port() != CONFIG_DEFAULT_BOIP_PORT)
            resultstream << ":" << Config.get_boip_port();
        break;
    case FN_DRIVE1HOST:
    case FN_DRIVE2HOST:
    case FN_DRIVE3HOST:
    case FN_DRIVE4HOST:
    case FN_DRIVE5HOST:
    case FN_DRIVE6HOST:
    case FN_DRIVE7HOST:
    case FN_DRIVE8HOST:
        /* From what host is each disk is mounted on each Drive Slot? */
        drive_slot = tagid - FN_DRIVE1HOST;
        host_slot = Config.get_mount_host_slot(drive_slot);
        if (host_slot != HOST_SLOT_INVALID) {
            resultstream << Config.get_host_name(host_slot);
        } else {
            resultstream << "";
        }
        break;
#ifndef ESP_PLATFORM
    case FN_DRIVE1BROWSER:
    case FN_DRIVE2BROWSER:
    case FN_DRIVE3BROWSER:
    case FN_DRIVE4BROWSER:
    case FN_DRIVE5BROWSER:
    case FN_DRIVE6BROWSER:
    case FN_DRIVE7BROWSER:
    case FN_DRIVE8BROWSER:
        /* Link to browse the files */
        drive_slot = tagid - FN_DRIVE1BROWSER;
        host_slot = Config.get_mount_host_slot(drive_slot);
        if (host_slot != HOST_SLOT_INVALID) {
            resultstream << "/browse/host/" << host_slot+1 << Config.get_mount_path(drive_slot) << "?action=slotlist";
        } else {
            resultstream << "#";
        }
        break;
#endif
    case FN_DRIVE1MOUNT:
    case FN_DRIVE2MOUNT:
    case FN_DRIVE3MOUNT:
    case FN_DRIVE4MOUNT:
    case FN_DRIVE5MOUNT:
    case FN_DRIVE6MOUNT:
    case FN_DRIVE7MOUNT:
    case FN_DRIVE8MOUNT:
        /* What disk is mounted on each Drive Slot (and is it read-only or read-write)? */
        drive_slot = tagid - FN_DRIVE1MOUNT;
        host_slot = Config.get_mount_host_slot(drive_slot);
        if (host_slot != HOST_SLOT_INVALID) {
            resultstream << Config.get_mount_path(drive_slot);
            resultstream << " (" << (Config.get_mount_mode(drive_slot) == fnConfig::mount_modes::MOUNTMODE_READ ? "R" : "W") << ")";
        } else {
            resultstream << "(Empty)";
        }
    break;
    case FN_HOST1:
    case FN_HOST2:
    case FN_HOST3:
    case FN_HOST4:
    case FN_HOST5:
    case FN_HOST6:
    case FN_HOST7:
    case FN_HOST8:
        /* What TNFS host is mounted on each Host Slot? */
        host_slot = tagid - FN_HOST1;
        if (Config.get_host_type(host_slot) != fnConfig::host_types::HOSTTYPE_INVALID) {
            resultstream << Config.get_host_name(host_slot);
        } else {
            resultstream << "(Empty)";
        }
        break;
    case FN_DRIVE1DEVICE:
    case FN_DRIVE2DEVICE:
    case FN_DRIVE3DEVICE:
    case FN_DRIVE4DEVICE:
    case FN_DRIVE5DEVICE:
    case FN_DRIVE6DEVICE:
    case FN_DRIVE7DEVICE:
    case FN_DRIVE8DEVICE:
        /* What Dx: drive (if any rotation has occurred) does each Drive Slot currently map to? */
        drive_slot = tagid - FN_DRIVE1DEVICE;
        disk_id = (char) theFuji.get_disk_id(drive_slot);
        if (disk_id > 0 && disk_id != (char) (0x31 + drive_slot)) {
            resultstream << " (D" << disk_id << ":)";
        }
        break;
    case FN_HOST1PREFIX:
    case FN_HOST2PREFIX:
    case FN_HOST3PREFIX:
    case FN_HOST4PREFIX:
    case FN_HOST5PREFIX:
    case FN_HOST6PREFIX:
    case FN_HOST7PREFIX:
    case FN_HOST8PREFIX:
        /* What directory prefix is set right now
           for the TNFS host mounted on each Host Slot? */
        host_slot = tagid - FN_HOST1PREFIX;
        if (Config.get_host_type(host_slot) != fnConfig::host_types::HOSTTYPE_INVALID) {
            resultstream << theFuji.get_host_prefix(host_slot);
        } else {
            resultstream << "";
        }
        break;
    case FN_ERRMSG:
        resultstream << fnHTTPD.getErrMsg();
        break;
    case FN_HARDWARE_VER:
        resultstream << fnSystem.get_hardware_ver_str();
        break;
    case FN_PRINTER_LIST:
        {
            char *result = (char *) malloc(MAX_PRINTER_LIST_BUFFER);
            if (result != NULL)
            {
                strcpy(result, "");

                for(int i=0; i<(int) PRINTER_CLASS::PRINTER_INVALID; i++)
                {
                    strncat(result, "<option value=\"", MAX_PRINTER_LIST_BUFFER-1);
                    strncat(result, PRINTER_CLASS::printer_model_str[i], MAX_PRINTER_LIST_BUFFER-1);
                    strncat(result, "\">", MAX_PRINTER_LIST_BUFFER);
                    strncat(result, PRINTER_CLASS::printer_model_str[i], MAX_PRINTER_LIST_BUFFER-1);
                    strncat(result, "</option>\n", MAX_PRINTER_LIST_BUFFER-1);
                }
                resultstream << result;
                free(result);
            } else
                resultstream << "Insufficent memory";
        }
        break;
    case FN_ENCRYPT_PASSPHRASE_ENABLED:
        resultstream << Config.get_general_encrypt_passphrase();
        break;
    case FN_CPM_ENABLED:
        resultstream << Config.get_cpm_enabled();
        break;
    case FN_CPM_CCP:
        resultstream << Config.get_ccp_filename();
        break;
    case FN_ALT_CFG:
        resultstream << Config.get_config_filename();
        break;
    default:
        resultstream << tag;
        break;
    }
    // Debug_printf("Substitution result: \"%s\"\n", resultstream.str().c_str());
    return resultstream.str();
}

bool fnHttpServiceParser::is_parsable(const char *extension)
{
    if (extension != NULL)
    {
        if (strncmp(extension, "html", 4) == 0)
            return true;
    }
    return false;
}

/* Look for anything between <% and %> tags
 And send that to a routine that looks for suitable substitutions
 Returns string with subtitutions in place
*/
string fnHttpServiceParser::parse_contents(const string &contents)
{
    std::stringstream ss;
    size_t pos = 0, x, y;
    do
    {
        x = contents.find("<%", pos);
        if (x == string::npos)
        {
            ss << contents.substr(pos);
            break;
        }
        // Found opening tag, now find ending
        y = contents.find("%>", x + 2);
        if (y == string::npos)
        {
            ss << contents.substr(pos);
            break;
        }
        // Now we have starting and ending tags
        if (x > 0)
            ss << contents.substr(pos, x - pos);
        ss << substitute_tag(contents.substr(x + 2, y - x - 2));
        pos = y + 2;
    } while (true);

    return ss.str();
}

long fnHttpServiceParser::uptime_seconds()
{
    return fnSystem.get_uptime() / 1000000;
}

string fnHttpServiceParser::format_uptime()
{
    int64_t ms = fnSystem.get_uptime();
    long s = ms / 1000000;

    int m = s / 60;
    int h = m / 60;
    int d = h / 24;

    std::stringstream resultstream;
    if (d)
        resultstream << d << " days, ";
    if (h % 24)
        resultstream << (h % 24) << " hours, ";
    if (m % 60)
        resultstream << (m % 60) << " minutes, ";
    if (s % 60)
        resultstream << (s % 60) << " seconds";

    return resultstream.str();
}
