
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

#include <map>
#include <string>
#include <vector>

#include "response.h"

using namespace WebDav;

void Response::setDavHeaders() {
    setHeader("DAV", "1, 2");
    setHeader("MS-Author-Via", "DAV");
    setHeader("Allow", "COPY,DELETE,GET,HEAD,LOCK,MKCOL,MOVE,OPTIONS,PROPFIND,PROPPATCH,PUT,UNLOCK");
    setHeader("Public", "COPY,DELETE,GET,HEAD,LOCK,MKCOL,MOVE,OPTIONS,PROPFIND,PROPPATCH,PUT,UNLOCK");
    setHeader("Access-Control-Allow-Origin", "*");
    setHeader("Access-Control-Allow-Headers", "*");
    setHeader("Access-Control-Allow-Methods", "*");
    // No "Connection: close" — persistent connections let bulk WebDAV
    // transfers reuse one TCP connection for thousands of requests instead
    // of paying a handshake + slow-start per PROPFIND/PUT/PROPPATCH. Idle
    // sessions are reclaimed by lru_purge_enable in the httpd config.
}

void Response::setHeader(std::string header, std::string value) {
    headers[header] = value;
}

void Response::setHeader(std::string header, size_t value) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%zu", value);
    headers[header] = tmp;
}

void Response::flushHeaders() {
    for (const auto &h: headers)
        writeHeader(h.first.c_str(), h.second.c_str());
    // Do NOT clear headers here. httpd_resp_set_hdr() stores raw pointers without
    // copying — the strings must remain alive until the response is fully transmitted
    // (i.e. until the first sendChunk/closeBody call). The map is cleaned up naturally
    // when the Response object goes out of scope at the end of the handler.
}
