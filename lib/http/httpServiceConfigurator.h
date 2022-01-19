/* FujiNet web server helper class

Broke out system configuration functions to make things easier to read.
*/
#ifndef HTTPSERVICECONFIGURATOR_H
#define HTTPSERVICECONFIGURATOR_H

#include <stddef.h>
#include <map>

class fnHttpServiceConfigurator
{
    static void config_printer(std::string printernumber, std::string printermodel, std::string printerport);
    static void config_hsio(std::string hsio_index);
    static void config_timezone(std::string timezone);
    static void config_hostname(std::string hostname);
    static void config_midimaze(std::string host_ip);
    static void config_cassette(std::string play_record, std::string resistor, bool rew);
    static void config_rotation_sounds(std::string rotation_sounds);
    static void config_enable_config(std::string enable_config);
    static void config_boot_mode(std::string boot_mode);
    static void config_status_wait_enable(std::string status_wait_enable);
    static void config_printer_enabled(std::string printer_enabled);
    static void config_modem_enabled(std::string modem_enabled);
    static void config_modem_sniffer_enabled(std::string modem_sniffer_enabled);

public:
    static char * url_decode(char * dst, const char * src, size_t dstsize);
    static std::map<std::string, std::string> parse_postdata(const char * postdata, size_t postlen);
    static int process_config_post(const char * postdata, size_t postlen);
};

#endif // HTTPSERVICECONFIGURATOR_H
