#include "fnConfig.h"
#include "compat_string.h"

fnConfig Config;

// Initialize some defaults
fnConfig::fnConfig()
{
    int i;

    strlcpy(_network.sntpserver, CONFIG_DEFAULT_SNTPSERVER, sizeof(_network.sntpserver));

    // clear stored wifi data. The default enabled flag is true which we are using to indicate an entry in stored wifi
    for (i = 0; i < MAX_WIFI_STORED; i++)
    {
        _wifi_stored[i].ssid.clear();
        _wifi_stored[i].passphrase.clear();
        _wifi_stored[i].enabled = false;
    }
}
