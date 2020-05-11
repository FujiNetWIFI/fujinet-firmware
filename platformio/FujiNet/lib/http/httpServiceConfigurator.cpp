#include <sstream>
#include <string>
#include <cstdio>

#include <string>
#include <map>

#include "httpServiceConfigurator.h"
#include "config.h"
#include "printer.h"

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
            
                iVal = ++i;
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

void fnHttpServiceConfigurator::config_printer(std::string printernumber, std::string printermodel)
{

    // Take the last char in the 'printernumber' string and turn it into a digit
    int pn = 1;
    char pc = printernumber[printernumber.length()-1];

    if(pc >= '1' && pc <= '9')
        pn = pc - '0';

    // Only handle 1 printer for now
    if(pn != 1)
        return;
    
    sioPrinter::printer_type t = sioPrinter::match_modelname(printermodel);
    if(t == sioPrinter::printer_type::PRINTER_INVALID)
    {
#ifdef DEBUG
        Debug_printf("Unknown printer type: \"%s\"\n", printermodel.c_str());
#endif    
        return;
    }

    // Finally, change the printer type!
    Config.printer_slots[0].type = t;
    Config.save();
    sioP.set_printer_type(t);
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
            config_printer(i->first, i->second);
        }

    }

    return 0;
}
