#ifndef HTTPUSERAGENT_H
#define HTTPUSERAGENT_H

#include <string>

#include "fnSystem.h"

// Some origins (Wikipedia among them) refuse a request with no or a generic User-Agent.
inline const char *http_user_agent()
{
    static const std::string user_agent = std::string("FujiNet/")
        + fnSystem.get_fujinet_version(true) + " (" + fnSystem.get_target_platform_str()
        + "; +https://fujinet.online/)";
    return user_agent.c_str();
}

#endif /* HTTPUSERAGENT_H */
