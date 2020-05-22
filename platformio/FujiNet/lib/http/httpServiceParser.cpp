#include <sstream>
#include <string>
#include <cstdio>

#include "../../include/debug.h"

#include "httpServiceParser.h"

#include "printer.h"

#include "../hardware/fnSystem.h"
#include "../hardware/fnWiFi.h"
#include "fnFsSPIF.h"
#include "fnFsSD.h"

using namespace std;

const string fnHttpServiceParser::substitute_tag(const string &tag)
{
    enum tagids {
        FN_HOSTNAME = 0,
        FN_VERSION,
        FN_IPADDRESS,
        FN_IPMASK,
        FN_IPGATEWAY,
        FN_IPDNS,
        FN_WIFISSID,
        FN_WIFIBSSID,
        FN_WIFIMAC,
        FN_SPIFFS_SIZE,
        FN_SPIFFS_USED,
        FN_SD_SIZE,
        FN_SD_USED,
        FN_UPTIME,
        FN_HEAPSIZE,
        FN_SYSSDK,
        FN_SYSCPUREV,
        FN_SIOVOLTS,
        FN_PRINTER1_MODEL,
        FN_LASTTAG
    };

    const char *tagids[FN_LASTTAG] =
    {
        "FN_HOSTNAME",
        "FN_VERSION",
        "FN_IPADDRESS",
        "FN_IPMASK",
        "FN_IPGATEWAY",
        "FN_IPDNS",
        "FN_WIFISSID",
        "FN_WIFIBSSID",
        "FN_WIFIMAC",
        "FN_SPIFFS_SIZE",
        "FN_SPIFFS_USED",
        "FN_SD_SIZE",
        "FN_SD_USED",
        "FN_UPTIME",
        "FN_HEAPSIZE",
        "FN_SYSSDK",
        "FN_SYSCPUREV",
        "FN_SIOVOLTS",
        "FN_PRINTER1_MODEL"
    };

    stringstream resultstream;
    #ifdef DEBUG
        //Debug_printf("Substituting tag '%s'\n", tag.c_str());
    #endif

    int tagid;
    for(tagid = 0; tagid < FN_LASTTAG; tagid++)
    {
        if(0 == tag.compare(tagids[tagid]))
        {
            break;
        }
    }

    // Provide a replacement value
    switch(tagid)
    {
    case FN_HOSTNAME:
        resultstream << fnSystem.Net.get_hostname();
        break;
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
    case FN_SPIFFS_SIZE:
        resultstream << fnSPIFFS.total_bytes();
        break;
    case FN_SPIFFS_USED:
        resultstream << fnSPIFFS.used_bytes();
        break;
    case FN_SD_SIZE:
        resultstream << fnSDFAT.total_bytes();
        break;
    case FN_SD_USED:
        resultstream << fnSDFAT.used_bytes();
        break;
    case FN_UPTIME:
        resultstream << format_uptime();
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
    case FN_SIOVOLTS:
        resultstream << (float)fnSystem.get_sio_voltage()/1000.00 << "V";
        break;
    case FN_PRINTER1_MODEL:
        resultstream << sioP.getPrinterPtr()->modelname();
        break;
    default:
        resultstream << tag;
        break;
    }
    #ifdef DEBUG
        // Debug_printf("Substitution result: \"%s\"\n", resultstream.str().c_str());
    #endif
    return resultstream.str();
}

bool fnHttpServiceParser::is_parsable(const char *extension)
{
    if(extension != NULL)
    {
        if(strncmp(extension, "html", 4) == 0)
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
    uint pos = 0, x, y;
    do {
        x = contents.find("<%", pos);
        if( x == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Found opening tag, now find ending
        y = contents.find("%>", x+2);
        if( y == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Now we have starting and ending tags
        if( x > 0)
            ss << contents.substr(pos, x-pos);
        ss << substitute_tag(contents.substr(x+2, y-x-2));
        pos = y+2;
    } while(true);

    return ss.str();
}

#include "fnFsTNFS.h"

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

/*
    TnfsFileSystem tnfs;
    tnfs.start("eris.just.lan");

    tnfs.dir_open("/");
    struct fsdir_entry *de;
    while((de = tnfs.dir_read()) != nullptr)
    {
        Debug_printf("DE: \"%s\", D=%d, S=%u\n", de->filename, de->isDir ? 1: 0, de->size);
    }
    tnfs.dir_close();

    char t[40];
    sprintf(t, "%s/test123.txt", tnfs.basepath());
    FILE * f = fopen(t, "r");
    Debug_printf("fopen = %p\n", f);

    int r = fread(t, 1, 10, f);
    if(r > 0)
        t[r] = '\0';
    Debug_printf("fread = %d \"%s\"\n", r, t);

    r = fseek(f, 2, SEEK_CUR);
    Debug_printf("lseek = %d\n", r);

    r = fread(t, 1, 10, f);
    if(r > 0)
        t[r] = '\0';
    Debug_printf("fread = %d \"%s\"\n", r, t);

    r = fseek(f, 50, SEEK_CUR);
    Debug_printf("lseek = %d\n", r);

    r = fread(t, 1, 10, f);
    if(r > 0)
        t[r] = '\0';
    Debug_printf("fread = %d \"%s\"\n", r, t);

    r = fseek(f, 0, SEEK_SET);
    Debug_printf("lseek = %d\n", r);

    r = fread(t, 1, 10, f);
    if(r > 0)
        t[r] = '\0';
    Debug_printf("fread = %d \"%s\"\n", r, t);

    r = fseek(f, -12, SEEK_END);
    Debug_printf("lseek = %d\n", r);

    r = fread(t, 1, 10, f);
    if(r > 0)
        t[r] = '\0';
    Debug_printf("fread = %d \"%s\"\n", r, t);

    r = fclose(f);
    Debug_printf("fclose = %d [%d]\n", r, errno);
    */

    return resultstream.str();
}
