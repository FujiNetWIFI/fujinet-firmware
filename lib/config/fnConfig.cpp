#include "fnConfig.h"
#include <cstring>

fnConfig Config;

// Initialize some defaults
fnConfig::fnConfig()
{
    strlcpy(_network.sntpserver, CONFIG_DEFAULT_SNTPSERVER, sizeof(_network.sntpserver));
}
