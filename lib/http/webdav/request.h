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

#pragma once

#include <string>
#include <esp_http_server.h>

#include "string_utils.h"
#include "../../include/debug.h"

#define HTTPD_100      "100 Continue"

namespace WebDav
{

    class Request
    {
    public:
        enum Depth
        {
            DEPTH_0 = 0,
            DEPTH_1 = 1,
            DEPTH_INFINITY = 2,
        };

        Request(httpd_req_t *httpd_req)
        {
            req = httpd_req;
            path = httpd_req->uri;
            depth = DEPTH_INFINITY;
            overwrite = true;

            // Drop trailing slash from path
            if (mstr::endsWith(path, "/"))
                mstr::drop(path, 1);
        }

        bool parseRequest();
        std::string getDestination();

        std::string getPath() { return path; }
        enum Depth getDepth() { return depth; }
        bool getOverwrite() { return overwrite; }

        // Functions that depend on the underlying web server implementation
        std::string getHeader(std::string name)
        {
            size_t len = httpd_req_get_hdr_value_len(req, name.c_str());
            if (len <= 0)
                return "";

            std::string s;
            s.resize(len);
            httpd_req_get_hdr_value_str(req, name.c_str(), &s[0], len + 1);

            return s;
        }

        void sendContinue()
        {
            std::string c = "HTTP/1.1 " HTTPD_100 "\r\n\r\n";
            httpd_send(req, c.c_str(), c.length());
        }

        size_t getContentLength()
        {
            if (!req)
                return 0;

            return req->content_len;
        }

        int readBody(char *buf, int len)
        {
            int ret = httpd_req_recv(req, buf, len);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                /* Retry receiving if timeout occurred */
                return 0;

            return ret;
        }

    private:
        httpd_req_t *req;

    protected:
        std::string path;
        enum Depth depth;
        bool overwrite;
    };

} // namespace