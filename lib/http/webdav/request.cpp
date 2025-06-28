
#include "request.h"

#include <esp_http_server.h>
#include <string>

using namespace WebDav;

bool Request::parseRequest() {
    std::string s;

    s = getHeader("Overwrite");
    if (!s.empty()) {
        if (s == "F")
            overwrite = false;
        else if (s != "T")
            return false;
    }

    s = getHeader("Depth");
    if (!s.empty()) {
        if (s == "0")
            depth = DEPTH_0;
        else if (s == "1")
            depth = DEPTH_1;
        else if (s != "infinity")
            return false;
    }

    return true;
}

std::string Request::getDestination() {
    std::string destination = getHeader("Destination");
    std::string host = getHeader("Host");

    if (destination.empty() || host.empty())
        return "";

    size_t pos = destination.find(host);
    if (pos == std::string::npos)
        return "";

    return destination.substr(pos + host.length());
}
