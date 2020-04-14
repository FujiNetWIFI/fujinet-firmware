#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <map>

#include <SPIFFS.h>
#include <SD.h>
#include <FS.h>

#include "httpService.h"
#include "printer.h"
#include "../../src/main.h"

#include <esp_http_server.h>

using namespace std;

httpd_handle_t webserver = NULL;
FS* _FS;

enum _fnwserr
{
    fnwserr_fileopen = 1
};

const char *  _fnws_root = FNWS_FILE_ROOT;

/* Send some meaningful(?) error message to client
*/
void return_http_error(httpd_req_t *req, _fnwserr errnum)
{
    const char * message;
    
    switch(errnum)
    {
        case fnwserr_fileopen:
            message = "Error opening file";
            break;
        default:
            message = "Unexpected web server error";
            break;
    }
    httpd_resp_send(req, message, strlen(message));
}

static std::map<string, string> mime_map {
    {"css", "text/css"},
    {"png", "image/png"},
    {"jpg", "image/jpeg"},
    {"gif", "image/gif"},
    {"svg", "image/svg+xml"},
    {"pdf", "application/pdf"},
    {"ico", "image/x-icon"},
    {"txt", "text/plain"},
    {"bin", "application/octet-stream"},
    {"atascii", "application/octet-stream"}
};

const char * find_type_str(const char *extension)
{
    if(extension != NULL) 
    {
        std::map<string, string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
    }
    return NULL;
}

/* Set the response content type based on the file being sent.
*  Just using the file extension for now
*  If nothing is set, the default is 'text/html'
*/
void set_file_content_type(httpd_req_t *req, char *filepath)
{
    // Find the current file extension
    char * dot = strrchr(filepath, '.');

    if(dot != NULL)
    {
        // Skip the period
        dot++;
        const char *mimetype = find_type_str(dot);
        if(mimetype)
            httpd_resp_set_type(req, mimetype);
    }
}

/* Send content of given file out to client
*/
void send_file(httpd_req_t *req, char *filename)
{
    // Build the full file path
    int fpsize = sizeof(FNWS_FILE_ROOT) + strlen(filename) + 1;
    char * fpath = (char *)malloc(fpsize);
    strncpy(fpath, FNWS_FILE_ROOT, fpsize);
    // Trim any '/' prefix before adding it to the base directory
    while(*filename == '/')
        filename++;
    strncat(fpath, filename, fpsize);

    File f = _FS->open(fpath, "r");
    if (!f || !f.available()) {
#ifdef DEBUG
        Debug_printf("Failed to open file for sending: '%s'\n", fpath);
#endif        
        return_http_error(req, fnwserr_fileopen);
    } else {
        // Set the response content type
        set_file_content_type(req, fpath);
        // Set the expected length of the content
        char hdrval[10];
        snprintf(hdrval, 10, "%u", f.size());
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks    
        char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
        size_t count = 0;
        do 
        {
            count = f.read((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
            httpd_resp_send_chunk(req, buf, count);
        } while(count > 0);
        f.close();
        free(buf);
    }

    free(fpath);
}

struct queryparts {
    std::string full_uri;
    std::string path;
    std::string query;
};

void parse_query(httpd_req_t *req, queryparts *results)
{
    results->full_uri += req->uri;
    // See if we have any arguments
    int path_end = results->full_uri.find_first_of('?');
    if(path_end < 0)
    {
        results->path += results->full_uri;
        return;
    }
    results->path += results->full_uri.substr(0, path_end -1);
    results->query += results->full_uri.substr(path_end +1);
    // TO DO: parse arguments, but we've no need for them yet
}

esp_err_t get_handler_index(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_println("Index request handler");
#endif

    send_file(req, const_cast<char *>("index.html"));
    return ESP_OK;
}

esp_err_t get_handler_file_in_query(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_printf("File_in_query request handler '%s'\n", req->uri);
#endif

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, const_cast<char *>(qp.query.c_str()));

    return ESP_OK;
}

esp_err_t get_handler_file_in_path(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_printf("File_in_path request handler '%s'\n", req->uri);
#endif

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, const_cast<char *>(qp.path.c_str()));

    return ESP_OK;
}

esp_err_t get_handler_print(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_println("Print request handler");
#endif

    // A bit of a kludge for now: get printer from main routine
    sioPrinter *currentPrinter = getCurrentPrinter();

    // Build a print output name
    const char *exts;
    // Choose an extension based on current printer papertype
    switch (currentPrinter->getPaperType())
    {
        case RAW:
            exts = "bin";
            break;
        case TRIM:
            exts = "atascii";
            break;
        case ASCII:
            exts = "txt";
            break;
        case PDF:
            exts = "pdf";
            break;
        case SVG:
            exts = "svg";
            break;
        default:
            exts = "bin";
    }
    String filename = "printout.";
    filename += exts;
    // Set the expected content type based on the filename/extension
    set_file_content_type(req, const_cast<char *>(filename.c_str()));

    // Flush and close the print output before continuing
    currentPrinter->pageEject(); // flushOutput(); is now inside of pageEject()
    // Add a couple of attchment-specific details
    char hdrval1[60];
    snprintf(hdrval1, 60, "attachment; filename=\"%s\"", filename.c_str());
    httpd_resp_set_hdr(req, "Content-Disposition", hdrval1);
    char hdrval2[10];
    snprintf(hdrval2, 10, "%u", currentPrinter->getOutputSize());
#ifdef DEBUG
    Debug_printf("Printer says there's %u bytes in the output file\n", currentPrinter->getOutputSize());
#endif    
    httpd_resp_set_hdr(req, "Content-Length", hdrval2);

    // Finally, write the data
    // Send the file content out in chunks    
    char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
    size_t count = 0;
    do 
    {
        count = currentPrinter->readFromOutput((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
#ifdef DEBUG
        Debug_printf("Read %u bytes from print file\n", count);
#endif    
        httpd_resp_send_chunk(req, buf, count);
    } while(count > 0);
    free(buf);

    // Tell the printer it can start writing from the beginning
    currentPrinter->resetOutput();

#ifdef DEBUG
    Debug_println("Print request completed");
#endif
    return ESP_OK;
}

/* Much of this will be simplified when we update to the current esp-idf
*  where they added support for wildcards in the URI specification
*/
vector<httpd_uri_t> uris {
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

/* Set up and start the web server
*/
void httpServiceInit()
{
    _FS = &SPIFFS;

    webserver = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
#ifdef DEBUG
    Debug_printf("Starting web server on port %d\n", config.server_port);
#endif    
    if (httpd_start(&webserver, &config) == ESP_OK) {
        // Register URI handlers
        for (const httpd_uri_t uridef : uris)
            httpd_register_uri_handler(webserver, &uridef);
        return;
    }

    webserver = NULL;
#ifdef DEBUG
    Debug_println("Error starting web server!");
#endif
}
