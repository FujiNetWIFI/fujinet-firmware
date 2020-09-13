#include <sstream>
#include <string>
#include <cstdio>

#include <string>
#include <map>

#include "esp_task.h"
#include "esp_heap_task_info.h"

#include "httpServiceConfigurator.h"
#include "fnConfig.h"
#include "printerlist.h"

// TODO: This was copied from another source and needs some bounds-checking!
char* fnHttpServiceConfigurator::url_decode(char *dst, const char *src, size_t dstsize)
{
    char a, b;
    size_t i = 0;

    while (*src && i++ < dstsize)
    {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
                if (a >= 'a')
                        a -= 'a'-'A';
                if (a >= 'A')
                        a -= ('A' - 10);
                else
                        a -= '0';
                if (b >= 'a')
                        b -= 'a'-'A';
                if (b >= 'A')
                        b -= ('A' - 10);
                else
                        b -= '0';
                *dst++ = 16*a+b;
                src+=3;
        } else if (*src == '+') {
                *dst++ = ' ';
                src++;
        } else {
                *dst++ = *src++;
        }
    }
    *dst++ = '\0';

    return dst;
}

std::map<std::string, std::string> fnHttpServiceConfigurator::parse_postdata(const char * postdata, size_t postlen)
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

    std::map<std::string, std::string>results;

    for(i = 0; i < postlen; i++)
    {
        char c = postdata[i];
        switch (s)
        {
        case STATE_SEARCH_ENDKEY:
            if(c == '=')
            {
                sKey.clear();
                sKey.append(postdata + iKey, i - iKey);
                #ifdef DEBUG
                Debug_printf("key=\"%s\"\n", sKey.c_str());
                #endif
            
                iVal = i+1;
                s = STATE_SEARCH_ENDVALUE;
            }
            break;
        case STATE_SEARCH_ENDVALUE:
            if(c == '&' || c == '\0' || i == postlen-1)
            {
                sVal.clear();
                sVal.append(postdata + iVal, (i == postlen-1) ? postlen - iVal : i - iVal);
                #ifdef DEBUG
                Debug_printf("value=\"%s\"\n", sVal.c_str());
                #endif
            
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
    int index = -1;
    char pc = hsioindex[0];
    if(pc >= '0' && pc <= '9')
        index = pc - '0';
    else
    {
        Debug_printf("Bad HSIO index value: %s\n", hsioindex);
        return;
    }

    SIO.setHighSpeedIndex(index);
    // Store our change in Config        
    Config.store_general_hsioindex(index);
    Config.save();
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

void fnHttpServiceConfigurator::config_midimaze(std::string hostname)
{
    Debug_printf("Set MIDIMaze host: %s\n", hostname.c_str());

    // Update the host ip variable
    SIO.setMIDIHost(hostname.c_str());
    // Save change
    Config.store_midimaze_host(hostname.c_str());
    Config.save();
}

void fnHttpServiceConfigurator::config_printer(std::string printernumber, std::string printermodel, std::string printerport)
{

    // Take the last char in the 'printernumber' string and turn it into a digit
    int pn = 1;
    char pc = printernumber[printernumber.length()-1];

    if(pc >= '0' && pc <= '9')
        pn = pc - '0';

    // Only handle 1 printer for now
    if(pn != 1)
    {
        Debug_printf("config_printer invalid printer %d\n", pn);
        return;
    }

    if(printerport.empty())
    {
        sioPrinter::printer_type t = sioPrinter::match_modelname(printermodel);
        if(t == sioPrinter::printer_type::PRINTER_INVALID)
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
    }
    else
    {
        int port = -1;
        pc = printerport[0];
        if(pc >= '0' && pc <= '9')
            port = pc - '1';

        if(port < 0 || port > 3) 
        {
            #ifdef DEBUG
            Debug_printf("Bad printer port number: %d\n", port);
            #endif
            return;
        }
        #ifdef DEBUG
        Debug_printf("config_printer changing printer %d port to %d\n", pn, port);
        #endif
        // Store our change in Config        
        Config.store_printer_port(pn - 1 , port);
        // Store our change in the printer list
        fnPrinters.set_port(0, port);
        // Tell the SIO daisy chain to change the device ID for this printer
        SIO.changeDeviceId(fnPrinters.get_ptr(0), SIO_DEVICEID_PRINTER + port);
    }
    Config.save();
}

int fnHttpServiceConfigurator::process_config_post(const char * postdata, size_t postlen)
{
#ifdef DEBUG
    Debug_printf("process_config_post: %s\n", postdata);
#endif
    // Create a new buffer for the url-decoded version of the data
    char * decoded_buf = (char *)malloc(postlen+1);
    url_decode(decoded_buf, postdata, postlen);

    std::map<std::string, std::string> postvals = parse_postdata(decoded_buf, postlen);

    free(decoded_buf);

    for(std::map<std::string, std::string>::iterator i = postvals.begin(); i != postvals.end(); i++)
    {
        if(i->first.compare("printermodel1") == 0)
        {
            config_printer(i->first, i->second, std::string());
        } else if(i->first.compare("printerport1") == 0)
        {
            config_printer(i->first, std::string(), i->second);
        } else if(i->first.compare("hsioindex") == 0)
        {
            config_hsio(i->second);
        } else if(i->first.compare("timezone") == 0)
        {
            config_timezone(i->second);
        } else if(i->first.compare("midimaze_host") == 0)
        {
            config_midimaze(i->second);
        }
    }

    return 0;
}
