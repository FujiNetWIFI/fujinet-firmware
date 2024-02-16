#include "httpServiceConfigurator.h"

#include "../../include/debug.h"

#include "printer.h"
#include "fuji.h"

#include "fnSystem.h"
#include "fnConfig.h"
#ifndef ESP_PLATFORM
#include "bus.h"
#endif

#include "utils.h"


#ifdef BUILD_APPLE
#include "iwm/printerlist.h"
#include "iwm/fuji.h"
#define PRINTER_CLASS iwmPrinter
extern iwmFuji theFuji;
#endif /* BUILD_APPLE */

// TODO: This was copied from another source and needs some bounds-checking!
char *fnHttpServiceConfigurator::url_decode(char *dst, const char *src, size_t dstsize)
{
    char a, b;
    size_t i = 0;
#ifdef ESP_PLATFORM // TODO merge

    while (*src && i++ < dstsize)
#else
    size_t j = 0;

    while (*src && i++ < dstsize && j++ < dstsize)
#endif
    {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b)))
        {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
#ifdef ESP_PLATFORM
            src += 3;
#else
            src += 3; j += 2;
#endif
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';

    return dst;
}

std::map<std::string, std::string> fnHttpServiceConfigurator::parse_postdata(const char *postdata, size_t postlen)
{
    size_t i;
    enum _state
    {
        STATE_SEARCH_ENDKEY,
        STATE_SEARCH_ENDVALUE
    };

    _state s = STATE_SEARCH_ENDKEY;

    size_t iKey = 0;
    size_t iVal = 0;

    std::string sKey;
    std::string sVal;

    std::map<std::string, std::string> results;

    for (i = 0; i < postlen; i++)
    {
        char c = postdata[i];
        switch (s)
        {
        case STATE_SEARCH_ENDKEY:
            if (c == '=')
            {
                sKey.clear();
                sKey.append(postdata + iKey, i - iKey);
                Debug_printf("key=\"%s\"\n", sKey.c_str());

                iVal = i + 1;
                s = STATE_SEARCH_ENDVALUE;
            }
            break;
        case STATE_SEARCH_ENDVALUE:
            if (c == '&' || c == '\0' || i == postlen - 1)
            {
                sVal.clear();
                sVal.append(postdata + iVal, (i == postlen - 1) ? postlen - iVal : i - iVal);
                Debug_printf("value=\"%s\"\n", sVal.c_str());

                results[sKey] = sVal;
                iKey = ++i;
                s = STATE_SEARCH_ENDKEY;
            }
            break;
        }
    }

    return results;
}


void fnHttpServiceConfigurator::config_hsio(std::string hsioindex)
{
#ifdef BUILD_ATARI
#ifdef ESP_PLATFORM
    int index = -1;
    char pc = hsioindex[0];
    if (pc >= '0' && pc <= '9')
        index = pc - '0';
    else
    {
        Debug_printf("Bad HSIO index value: %s\n", hsioindex);
        return;
    }
#else
    Debug_printf("New HSIO index value: %s\n", hsioindex.c_str());

    int index = atoi(hsioindex.c_str());

    // get HSIO index and HSIO mode
    if (index < -1 || (index > 10 && index != 16)) // accepted valued: -1 (HSIO disabled), 0 .. 10, 16
    {
        Debug_printf("Bad HSIO index value: %s\n", hsioindex.c_str());
        return;
    }
#endif

    SIO.setHighSpeedIndex(index);
    // Store our change in Config
    Config.store_general_hsioindex(index);
    Config.save();
#endif /* BUILD_ATARI */
}


void fnHttpServiceConfigurator::config_timezone(std::string timezone)
{
    Debug_printf("New timezone value: %s\n", timezone.c_str());

    // Store our change in Config
    Config.store_general_timezone(timezone.c_str());
    // Update the system timezone variable
    fnSystem.update_timezone(timezone.c_str());
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_hostname(std::string hostname)
{
#ifndef ESP_PLATFORM
    util_string_trim(hostname); // TODO trim earlier
#endif
    Debug_printf("New hostname value: %s\n", hostname.c_str());

    // Store our change in Config
    Config.store_general_devicename(hostname.c_str());
    // Update the hostname variable
    fnSystem.update_hostname(hostname.c_str());
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_rotation_sounds(std::string rotation_sounds)
{
    Debug_printf("New rotation sounds value: %s\n", rotation_sounds.c_str());

    // Store our change in Config
    Config.store_general_rotation_sounds(util_string_value_is_true(rotation_sounds));
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_enable_config(std::string enable_config)
{
    Debug_printf("New CONFIG enable value: %s\n", enable_config.c_str());

    // Store our change in Config
    Config.store_general_config_enabled(util_string_value_is_true(enable_config));
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_status_wait_enable(std::string status_wait_enable)
{
    Debug_printf("New status_wait_enable value: %s\n", status_wait_enable.c_str());
    // Store our change in Config
    Config.store_general_status_wait_enabled(util_string_value_is_true(status_wait_enable));
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_printer_enabled(std::string printer_enabled)
{
    Debug_printf("New Printer Enable Value: %s\n",printer_enabled.c_str());

    // Store
    Config.store_printer_enabled(atoi(printer_enabled.c_str()));
    // Save
    Config.save();
}

void fnHttpServiceConfigurator::config_modem_enabled(std::string modem_enabled)
{
    Debug_printf("New Modem Enable Value: %s\n",modem_enabled.c_str());

    // Store
    Config.store_modem_enabled(atoi(modem_enabled.c_str()));
    // Save*
    Config.save();
}

void fnHttpServiceConfigurator::config_modem_sniffer_enabled(std::string modem_sniffer_enabled)
{
    Debug_printf("New Modem Sniffer Enable Value: %s\n",modem_sniffer_enabled.c_str());

    // Store
    Config.store_modem_sniffer_enabled(atoi(modem_sniffer_enabled.c_str()));
    // Save*
    Config.save();
}

void fnHttpServiceConfigurator::config_boot_mode(std::string boot_mode)
{
    Debug_printf("New CONFIG Boot Mode value: %s\n", boot_mode.c_str());

    // Store our change in Config
    Config.store_general_boot_mode(atoi(boot_mode.c_str()));
    // Save change
    Config.save();
}

void fnHttpServiceConfigurator::config_cassette_enabled(std::string cassette_enabled)
{
    Debug_printf("New Cassette Enable Value: %s\n",cassette_enabled.c_str());

    // Store
    Config.store_cassette_enabled(atoi(cassette_enabled.c_str()));
    // Save*
    Config.save();
}

void fnHttpServiceConfigurator::config_cassette_play(std::string play_record)
{
#ifdef BUILD_ATARI
    // call the cassette buttons function passing play_record.c_str()
    // find cassette via thefuji object?
    Debug_printf("New play/record button value: %s\n", play_record.c_str());
    bool isRecord = util_string_value_is_true(play_record);
    theFuji.cassette()->set_buttons(isRecord);
    Config.store_cassette_buttons(isRecord);

    Config.save();
#endif /* ATARI */
}

void fnHttpServiceConfigurator::config_cassette_resistor(std::string resistor)
{
#ifdef BUILD_ATARI
    bool isPullDown = util_string_value_is_true(resistor);
    theFuji.cassette()->set_pulldown(isPullDown);
    Config.store_cassette_pulldown(isPullDown);

    Config.save();
#endif /* ATARI */
}

void fnHttpServiceConfigurator::config_cassette_rewind()
{
#ifdef BUILD_ATARI
    Debug_printf("Rewinding cassette.\n");
    SIO.getCassette()->rewind();

    Config.save();
#endif /* ATARI */
}

void fnHttpServiceConfigurator::config_udpstream(std::string hostname)
{
    int port = 0;
    std::string delim = ":";

    // Turn off if hostname is STOP
    if (hostname.compare("STOP") == 0)
    {
        Debug_println("UDPStream Stop Request");
#ifdef BUILD_ATARI
        SIO.setUDPHost("STOP", port);
#endif /* ATARI */
#ifdef BUILD_LYNX
        ComLynx.setUDPHost("STOP", port);
#endif /* LYNX */
        Config.store_udpstream_host("");
        Config.store_udpstream_port(0);
        Config.save();

        return;
    }
    // Get the port from the hostname
    if (hostname.find(delim) != std::string::npos)
        port = stoi(hostname.substr(hostname.find(delim)+1));
    else
        port = 5004; // Default to MIDI port of 5004

    // Get the hostname
    std::string newhostname = hostname.substr(0, hostname.find(delim));

    Debug_printf("Set UDPStream host: %s\n", newhostname.c_str());
    Debug_printf("Set UDPStream port: %d\n", port);

    // Update the host ip variable
#ifdef BUILD_ATARI
    SIO.setUDPHost(newhostname.c_str(), port);
#endif /* ATARI */
#ifdef BUILD_LYNX
    ComLynx.setUDPHost(newhostname.c_str(), port);
#endif /* LYNX */
    // Save change
    Config.store_udpstream_host(newhostname.c_str());
    Config.store_udpstream_port(port);
    Config.save();
}

int printer_number_from_string(std::string printernumber)
{
    // Take the last char in the 'printernumber' string and turn it into a digit
    int pn = 1;
    char pc = printernumber[printernumber.length() - 1];

    if (pc >= '0' && pc <= '9')
        pn = pc - '0';

    return pn;
}

void fnHttpServiceConfigurator::config_printer_model(std::string printernumber, std::string printermodel)
{
    int pn = printer_number_from_string(printernumber);

#ifndef ESP_PLATFORM
    Debug_printf("config_printer_model(%s,%s)\r\n", printernumber.c_str(), printermodel.c_str());
#endif

    // Only handle 1 printer for now
    if (pn != 1)
    {
        Debug_printf("config_printer invalid printer %d\n", pn);
        return;
    }

    PRINTER_CLASS::printer_type t = PRINTER_CLASS::match_modelname(printermodel);
    if (t == PRINTER_CLASS::printer_type::PRINTER_INVALID)
    {
        Debug_printf("Unknown printer type: \"%s\"\n", printermodel.c_str());
        return;
    }
    Debug_printf("config_printer changing printer %d type to %d\n", pn, t);
    // Store our change in Config
    Config.store_printer_type(pn - 1, t);
    // Store our change in the printer list
    fnPrinters.set_type(0, t);
    // Tell the printer to change its type
    fnPrinters.get_ptr(0)->set_printer_type(t);

    Config.save();
}

void fnHttpServiceConfigurator::config_printer_port(std::string printernumber, std::string printerport)
{
    int pn = printer_number_from_string(printernumber);

#ifndef ESP_PLATFORM
    Debug_printf("config_printer_port(%s,%s)\r\n", printernumber.c_str(), printerport.c_str());
#endif

    // Only handle 1 printer for now
    if (pn != 1)
    {
        Debug_printf("config_printer invalid printer %d\n", pn);
        return;
    }

    int port = -1;
    char pc = printerport[0];
    if (pc >= '0' && pc <= '9')
        port = pc - '1'; // change to 0 based index

    if (port < 0 || port > 3)
    {
        Debug_printf("Bad printer port number: %d\n", port);
        return;
    }
    Debug_printf("config_printer changing printer %d port to %d\n", pn, port);
    // Store our change in Config
    Config.store_printer_port(pn - 1, port);
    // Store our change in the printer list
    fnPrinters.set_port(0, port);
#ifdef BUILD_ATARI
    // Tell the SIO daisy chain to change the device ID for this printer
    SIO.changeDeviceId(fnPrinters.get_ptr(0), SIO_DEVICEID_PRINTER + port);
#endif

    Config.save();
}

void fnHttpServiceConfigurator::config_encrypt_passphrase_enabled(std::string encrypt_passphrase_enabled)
{
    Debug_printf("encrypt_passphrase_enabled Enable Value: %s\n", encrypt_passphrase_enabled.c_str());
    Config.store_general_encrypt_passphrase(atoi(encrypt_passphrase_enabled.c_str()));
    Config.save();
}

void fnHttpServiceConfigurator::config_apetime_enabled(std::string enabled)
{
    Debug_printf("New APETIME Enable Value: %s\n", enabled.c_str());
    Config.store_apetime_enabled(atoi(enabled.c_str()));
    Config.save();
}

void fnHttpServiceConfigurator::config_cpm_enabled(std::string cpm_enabled)
{
    Debug_printf("New CP/M Enable Value: %s\n", cpm_enabled.c_str());
    Config.store_cpm_enabled(atoi(cpm_enabled.c_str()));
    Config.save();
}

void fnHttpServiceConfigurator::config_cpm_ccp(std::string cpm_ccp)
{
    // Use $ as a flag to reset to default CCP since empty field never gets to here
    if ( !strcmp(cpm_ccp.c_str(), "$") )
    {
        Debug_printf("Set CP/M CCP File to DEFAULT\n");
        Config.store_ccp_filename("");
    }
    else
    {
        Debug_printf("Set CP/M CCP File: %s\n", cpm_ccp.c_str());
        Config.store_ccp_filename(cpm_ccp.c_str());
    }
    Config.save();
}

void fnHttpServiceConfigurator::config_alt_filename(std::string alt_cfg)
{
    // Use $ as a flag to reset to default since empty field never gets to here
    if ( !strcmp(alt_cfg.c_str(), "$") )
    {
        Debug_printf("Set CONFIG boot disk to DEFAULT\n");
        Config.store_config_filename("");
    }
    else
    {
        Debug_printf("Set Alternate CONFIG boot disk: %s\n", alt_cfg.c_str());
        Config.store_config_filename(alt_cfg.c_str());
    }
    Config.save();
}

#ifndef ESP_PLATFORM
void fnHttpServiceConfigurator::config_serial(std::string port, std::string command, std::string proceed)
{
    Debug_printf("Set Serial: %s,%s,%s\n", port.c_str(), command.c_str(), proceed.c_str());

    // current settings
    const char *devname;
    int command_pin;
    int proceed_pin;
#ifdef BUILD_ATARI // OS
    devname = fnSioCom.get_serial_port(command_pin, proceed_pin);
#endif

    // update settings
    if (!port.empty())
    {
        Config.store_serial_port(port.c_str());

#ifdef BUILD_ATARI // OS
        if (fnSioCom.get_sio_mode() == SioCom::sio_mode::SERIAL)
        {
            fnSioCom.end();
        }
#endif

        // re-set serial port
#ifdef BUILD_ATARI // OS
        fnSioCom.set_serial_port(Config.get_serial_port().c_str(), command_pin, proceed_pin);
    
        if (fnSioCom.get_sio_mode() == SioCom::sio_mode::SERIAL)
        {
            fnSioCom.begin();
        }
#endif
    }
    if (!command.empty())
    {
        Config.store_serial_command((fnConfig::serial_command_pin)atoi(command.c_str()));
#ifdef BUILD_ATARI // OS
        fnSioCom.set_serial_port(nullptr, Config.get_serial_command(), proceed_pin);
#endif
    }
    if (!proceed.empty())
    {
        Config.store_serial_proceed((fnConfig::serial_proceed_pin)atoi(proceed.c_str()));
#ifdef BUILD_ATARI // OS
        fnSioCom.set_serial_port(nullptr, command_pin, Config.get_serial_proceed());
#endif
    }
    Config.save();
}

void fnHttpServiceConfigurator::config_netsio(std::string enable_netsio, std::string netsio_host_port)
{
#ifdef BUILD_ATARI // OS
    Debug_printf("Set NetSIO: %s, %s\n", enable_netsio.c_str(), netsio_host_port.c_str());

    // Store our change in Config
    if (!netsio_host_port.empty())
    {
        // TODO parse netsio_host_port, detect if host part is followed by :port
        std::size_t found = netsio_host_port.find(':');
        std::string host = netsio_host_port;
        int port = CONFIG_DEFAULT_NETSIO_PORT;
        if (found != std::string::npos)
        {
            host = netsio_host_port.substr(0, found);
            if (host.empty())
                host = "localhost";
            port = std::atoi(netsio_host_port.substr(found+1).c_str());
            if (port < 1 || port > 65535)
                port = CONFIG_DEFAULT_NETSIO_PORT;
        }
        Config.store_netsio_port(port);
        Config.store_netsio_host(host.c_str());
        fnSioCom.set_netsio_host(Config.get_netsio_host().c_str(), Config.get_netsio_port());
    }
    if (!enable_netsio.empty())
    {
        Config.store_netsio_enabled(util_string_value_is_true(enable_netsio));
    }
    fnSioCom.reset_sio_port(Config.get_netsio_enabled() ? SioCom::sio_mode::NETSIO : SioCom::sio_mode::SERIAL);

    // Save change
    Config.save();
#endif /* ATARI */
}
#endif // !ESP_PLATFORM

void fnHttpServiceConfigurator::config_pclink_enabled(std::string enabled)
{
    Debug_printf("New PCLink Enable Value: %s\n", enabled.c_str());
    Config.store_pclink_enabled(atoi(enabled.c_str()));
    Config.save();
}

int fnHttpServiceConfigurator::process_config_post(const char *postdata, size_t postlen)
{
    Debug_printf("process_config_post: %s\n", postdata);
    // Create a new buffer for the url-decoded version of the data
    char *decoded_buf = (char *)malloc(postlen + 1);
    url_decode(decoded_buf, postdata, postlen);

    std::map<std::string, std::string> postvals = parse_postdata(decoded_buf, postlen);

    free(decoded_buf);

#ifndef ESP_PLATFORM
    bool update_netsio = false;
    std::string str_netsio_enable;
    std::string str_netsio_host;
#endif

    for (std::map<std::string, std::string>::iterator i = postvals.begin(); i != postvals.end(); ++i)
    {
        if (i->first.compare("printermodel1") == 0)
        {
            config_printer_model(i->first, i->second);
        }
        else if (i->first.compare("printerport1") == 0)
        {
            config_printer_port(i->first, i->second);
        }
        else if (i->first.compare("hsioindex") == 0)
        {
            config_hsio(i->second);
        }
        else if (i->first.compare("timezone") == 0)
        {
            config_timezone(i->second);
        }
        else if (i->first.compare("hostname") == 0)
        {
            config_hostname(i->second);
        }
        else if (i->first.compare("udpstream_host") == 0)
        {
            config_udpstream(i->second);
        }
        else if (i->first.compare("play_record") == 0)
        {
            config_cassette_play(i->second);
        }
        else if (i->first.compare("pulldown") == 0)
        {
            config_cassette_resistor(i->second);
        }
        else if (i->first.compare("rew") == 0)
        {
            config_cassette_rewind();
        }
        else if (i->first.compare("cassette_enabled") == 0)
        {
            config_cassette_enabled(i->second);
        }
        else if (i->first.compare("rotation_sounds") == 0)
        {
            config_rotation_sounds(i->second);
        }
        else if (i->first.compare("config_enable") == 0)
        {
            config_enable_config(i->second);
        }
        else if (i->first.compare("status_wait_enable") == 0)
        {
            config_status_wait_enable(i->second);
        }
        else if (i->first.compare("boot_mode") == 0)
        {
            config_boot_mode(i->second);
        }
        else if (i->first.compare("printer_enabled") == 0)
        {
            config_printer_enabled(i->second);
        }
        else if (i->first.compare("modem_enabled") == 0)
        {
            config_modem_enabled(i->second);
        }
        else if (i->first.compare("modem_sniffer_enabled") == 0)
        {
            config_modem_sniffer_enabled(i->second);
        }
        else if (i->first.compare("passphrase_encrypt") == 0)
        {
            config_encrypt_passphrase_enabled(i->second);
        }
        else if (i->first.compare("apetime_enabled") == 0)
        {
            config_apetime_enabled(i->second);
        }
        else if (i->first.compare("cpm_enabled") == 0)
        {
            config_cpm_enabled(i->second);
        }
        else if (i->first.compare("cpm_ccp") == 0)
        {
            config_cpm_ccp(i->second);
        }
        else if (i->first.compare("alt_cfg") == 0)
        {
            config_alt_filename(i->second);
        }
#ifndef ESP_PLATFORM
        else if (i->first.compare("serial_port") == 0)
        {
            config_serial(i->second, std::string(), std::string());
        }
        else if (i->first.compare("serial_command") == 0)
        {
            config_serial(std::string(), i->second, std::string());
        }
        else if (i->first.compare("serial_proceed") == 0)
        {
            config_serial(std::string(), std::string(), i->second);
        }
        else if (i->first.compare("netsio_enable") == 0)
        {
            str_netsio_enable = i->second;
            update_netsio = true;
        }
        else if (i->first.compare("netsio_host") == 0)
        {
            str_netsio_host = i->second;
            update_netsio = true;
        }
#endif
        else if (i->first.compare("pclink_enabled") == 0)
        {
            config_pclink_enabled(i->second);
        }
    } // end for loop

#ifndef ESP_PLATFORM
    if (update_netsio)
    {
        config_netsio(str_netsio_enable, str_netsio_host);
    }
#endif

    return 0;
}
