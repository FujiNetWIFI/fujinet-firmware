// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

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
