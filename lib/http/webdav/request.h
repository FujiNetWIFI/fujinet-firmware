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