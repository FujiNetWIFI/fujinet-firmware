/* FujiNet web server helper class

Broke out system configuration functions to make things easier to read.
*/
#ifndef HTTPSERVICECONFIGURATOR_H
#define HTTPSERVICECONFIGURATOR_H

#include <map>
#include <string>

class fnHttpServiceConfigurator
{
    static void config_printer_model(std::string printernumber, std::string printerport);
    static void config_printer_port(std::string printernumber, std::string printerport);
    static void config_hsio(std::string hsio_index);
    static void config_timezone(std::string timezone);
    static void config_hostname(std::string hostname);
    static void config_udpstream(std::string host_ip);
    static void config_udpstream_servermode(std::string mode);
    static void config_cassette_play(std::string play_record);
    static void config_cassette_resistor(std::string resistor);
    static void config_cassette_rewind();
    static void config_cassette_enabled(std::string cassette_enabled);
    static void config_rotation_sounds(std::string rotation_sounds);
    static void config_enable_config(std::string enable_config);
    static void config_boot_mode(std::string boot_mode);
    static void config_status_wait_enable(std::string status_wait_enable);
    static void config_printer_enabled(std::string printer_enabled);
    static void config_modem_enabled(std::string modem_enabled);
    static void config_modem_sniffer_enabled(std::string modem_sniffer_enabled);
    static void config_encrypt_passphrase_enabled(std::string encrypt_passphrase_enabled);
    static void config_apetime_enabled(std::string apetime_enabled);
    static void config_cpm_enabled(std::string cpm_enabled);
    static void config_cpm_ccp(std::string cpm_ccp);
    static void config_ng(std::string config_ng);
    static void config_alt_filename(std::string alt_cfg);
    static void config_pclink_enabled(std::string pclink_enabled);

#ifndef ESP_PLATFORM
    static void config_serial(std::string port, std::string baud, std::string command, std::string proceed);
#endif
    static void config_boip(std::string enable_boip, std::string boip_host_port);

public:
    static char * url_decode(char * dst, const char * src, size_t dstsize);
    static std::map<std::string, std::string> parse_postdata(const char * postdata, size_t postlen);
    static int process_config_post(const char * postdata, size_t postlen);
};

#endif // HTTPSERVICECONFIGURATOR_H
