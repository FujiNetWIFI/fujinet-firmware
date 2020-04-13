/* FujiNet web server

Server is built on the esp-idf webserer class in <esp_http_server.h>
The design of the class in this version of the esp-idf requires registering
URI handlers for every URI you expect to handle and does not support
wildcards. Because of this, we handle serving static files this way:

URI: "/" - Forces loading/parsing (see below) of /<FNWS_FILE_ROOT>/index.html
URI: "/file?<filename>" - Sends static file /<FNWS_FILE_ROOT>/<filename>
URI: "/favico.ico" - Sends /<FNWS_FILE_ROOT>/favico.ico
URI: "/print" - Sends current printer output to user

MIME types are assigned based on file extention.  See/update
    static std::map<string, string> mime_map

Unless parsable, files are sent in FNWS_SEND_BUFF_SIZE blocks.

If a file has an extention pre-determined to support parsing (see/update
    fnHttpServiceParser::is_parsable() for a the list) then the
    following happens:

    * The entire file contents are loaded into an in-memory string.
    * Anything with the pattern <%PARSE_TAG%> is replaced with an
    * appropriate value as determined by the 
    *       string substitute_tag(const string &tag)
    * function.
*/

#ifndef HTTPSERVICE_H
#define HTTPSERVICE_H

#include <vector>

#include <FS.h>
#include <esp_http_server.h>

// FNWS_FILE_ROOT should end in a slash '/'
#define FNWS_FILE_ROOT "/www/"
#define FNWS_SEND_BUFF_SIZE 512

class fnHttpService 
{
    struct serverstate {
        httpd_handle_t hServer;
        FS* pFS;
    } state;

    enum _fnwserr
    {
        fnwserr_noerrr = 0,
        fnwserr_fileopen,
        fnwserr_memory
    };

    struct queryparts {
        std::string full_uri;
        std::string path;
        std::string query;
    };

    std::vector<httpd_uri_t> uris {
        {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = get_handler_index,
            .user_ctx  = NULL
        }
        ,
        {
            .uri       = "/file",
            .method    = HTTP_GET,
            .handler   = get_handler_file_in_query,
            .user_ctx  = NULL
        },
        {
            .uri       = "/print",
            .method    = HTTP_GET,
            .handler   = get_handler_print,
            .user_ctx  = NULL
        },
        {
            .uri       = "/favicon.ico",
            .method    = HTTP_GET,
            .handler   = get_handler_file_in_path,
            .user_ctx  = NULL
        }
    };

    static void return_http_error(httpd_req_t *req, _fnwserr errnum);
    static const char * find_mimetype_str(const char *extension);
    static char * get_extension(const char *filename);
    static void set_file_content_type(httpd_req_t *req, const char *filepath);
    static void send_file_parsed(httpd_req_t *req, const char *filename);
    static void send_file(httpd_req_t *req, const char *filename);
    static void parse_query(httpd_req_t *req, queryparts *results);

public:    
    static esp_err_t get_handler_index(httpd_req_t *req);
    static esp_err_t get_handler_file_in_query(httpd_req_t *req);
    static esp_err_t get_handler_file_in_path(httpd_req_t *req);
    static esp_err_t get_handler_print(httpd_req_t *req);

    void httpServiceInit();
};

#endif // HTTPSERVICE_H
