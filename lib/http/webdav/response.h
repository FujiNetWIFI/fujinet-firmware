#pragma once

#include <string>
#include <vector>
#include <map>

#include <esp_http_server.h>

#include "../../include/debug.h"

/* Some commonly used status codes */
#define HTTPD_200      "200 OK"                     /*!< HTTP Response 200 */
#define HTTPD_201      "201 Created"
#define HTTPD_204      "204 No Content"             /*!< HTTP Response 204 */
#define HTTPD_207      "207 Multi-Status"           /*!< HTTP Response 207 */
#define HTTPD_400      "400 Bad Request"            /*!< HTTP Response 400 */
#define HTTPD_403      "403 Forbidden"
#define HTTPD_404      "404 Not Found"              /*!< HTTP Response 404 */
#define HTTPD_405      "405 Method Not Allowed"
#define HTTPD_408      "408 Request Timeout"        /*!< HTTP Response 408 */
#define HTTPD_409      "409 Conflict"
#define HTTPD_412      "412 Precondition Failed"
#define HTTPD_415      "415 Unspported Media Type"
#define HTTPD_500      "500 Internal Server Error"  /*!< HTTP Response 500 */
#define HTTPD_501      "501 Not Implemented"
#define HTTPD_507      "507 Insufficient Storage"

namespace WebDav
{

    class MultiStatusResponse
    {
    public:
        std::string href;
        std::string status;
        std::map<std::string, std::string> props;
        bool isCollection;
    };

    class Response
    {
    public:
        Response(httpd_req_t *httpd_req) {
            req = httpd_req;
        }
        ~Response() {}

        void setDavHeaders();
        void setHeader(std::string header, std::string value);
        void setHeader(std::string header, size_t value);
        void flushHeaders();

        // Functions that depend on the underlying web server implementation
        void setStatus(int ret)
        {
            const char *status = NULL;
            switch (ret)
            {
                case 200:
                    status = HTTPD_200;
                    break;
                case 201:
                    status = HTTPD_201;
                    break;
                case 204:
                    status = HTTPD_204;
                    break;
                case 207:
                    status = HTTPD_207;
                    break;
                case 400:
                    status = HTTPD_400;
                    break;
                case 403:
                    status = HTTPD_403;
                    break;
                case 404:
                    status = HTTPD_404;
                    break;
                case 405:
                    status = HTTPD_405;
                    break;
                case 408:
                    status = HTTPD_408;
                    break;
                case 409:
                    status = HTTPD_409;
                    break;
                case 412:
                    status = HTTPD_412;
                    break;
                case 415:
                    status = HTTPD_415;
                    break;
                case 500:
                    status = HTTPD_500;
                    break;
                case 501:
                    status = HTTPD_501;
                    break;
                case 507:
                    status = HTTPD_507;
                    break;
                default:
                    Debug_printv("unhandled[%d]", ret);
            }

            //Debug_printv("status[%s]", status);
            httpd_resp_set_status(req, status);
            setDavHeaders();
        }

        void setContentType(const char *ct)
        {
            //Debug_printv("%s", ct);
            httpd_resp_set_type(req, ct);
        }

        bool sendChunk(const char *buf, ssize_t len = -1)
        {
            chunked = true;

            if (len == -1)
                len = strlen(buf);

            //Debug_printv("\r\n%x\r\n%s\r\n", len, buf);

            return httpd_resp_send_chunk(req, buf, len) == ESP_OK;
        }

        void closeChunk()
        {
            httpd_resp_send_chunk(req, NULL, 0);
            chunked = false;
        }

        void sendBody(const char *buf, ssize_t len = -1)
        {
            httpd_resp_send(req, buf, len);
        }

        void closeBody()
        {
            httpd_resp_send(req, "", 0);
        }

private:
        void writeHeader(const char *header, const char *value)
        {
            httpd_resp_set_hdr(req, header, value);
            //Debug_printv("%s: %s", header, value);
        }

        httpd_req_t *req;
        bool chunked = false;

        std::map<std::string, std::string> headers;
    };

} // namespace