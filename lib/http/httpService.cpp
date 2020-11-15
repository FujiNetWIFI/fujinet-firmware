#include <vector>
#include <map>
#include <sstream>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "esp_wps.h"

#include "httpService.h"
#include "httpServiceParser.h"
#include "httpServiceConfigurator.h"
#include "printerlist.h"
#include "fnWiFi.h"
#include "keys.h"

#include "../../lib/modem-sniffer/modem-sniffer.h"
#include "../../lib/sio/modem.h"

#include "../../include/debug.h"

using namespace std;

// Global HTTPD
fnHttpService fnHTTPD;

extern sioModem *sioR;

/* Send some meaningful(?) error message to client
*/
void fnHttpService::return_http_error(httpd_req_t *req, _fnwserr errnum)
{
    const char *message;

    switch (errnum)
    {
    case fnwserr_fileopen:
        message = MSG_ERR_OPENING_FILE;
        break;
    case fnwserr_memory:
        message = MSG_ERR_OUT_OF_MEMORY;
        break;
    default:
        message = MSG_ERR_UNEXPECTED_HTTPD;
        break;
    }
    httpd_resp_send(req, message, strlen(message));
}

const char *fnHttpService::find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map{
        {"css", "text/css"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},
        {"ico", "image/x-icon"},
        {"txt", "text/plain"},
        {"bin", "application/octet-stream"},
        {"js", "text/javascript"},
        {"atascii", "application/octet-stream"}};

    if (extension != NULL)
    {
        std::map<std::string, std::string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
    }
    return NULL;
}

char *fnHttpService::get_extension(const char *filename)
{
    char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

/* Set the response content type based on the file being sent.
*  Just using the file extension
*  If nothing is set here, the default is 'text/html'
*/
void fnHttpService::set_file_content_type(httpd_req_t *req, const char *filepath)
{
    // Find the current file extension
    char *dot = get_extension(filepath);
    if (dot != NULL)
    {
        const char *mimetype = find_mimetype_str(dot);
        if (mimetype)
            httpd_resp_set_type(req, mimetype);
    }
}

/* Send file content after parsing for replaceable strings
*/
void fnHttpService::send_file_parsed(httpd_req_t *req, const char *filename)
{
    // Note that we don't add FNWS_FILE_ROOT as it should've been done in send_file()
    Debug_printf("Opening file for parsing: '%s'\n", filename);

    _fnwserr err = fnwserr_noerrr;

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *fInput = pState->_FS->file_open(filename);

    if (fInput == nullptr)
    {
        Debug_println("Failed to open file for parsing");
        err = fnwserr_fileopen;
    }
    else
    {
        // Set the response content type
        set_file_content_type(req, filename);
        // We're going to load the whole thing into memory, so watch out for big files!
        size_t sz = FileSystem::filesize(fInput) + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\n", sz);
            err = fnwserr_memory;
        }
        else
        {
            fread(buf, 1, sz, fInput);
            string contents(buf);
            free(buf);
            contents = fnHttpServiceParser::parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());
        }
    }

    if (fInput != nullptr)
        fclose(fInput);

    if (err != fnwserr_noerrr)
        return_http_error(req, err);
}

/* Send content of given file out to client
*/
void fnHttpService::send_file(httpd_req_t *req, const char *filename)
{
    // Build the full file path
    string fpath = FNWS_FILE_ROOT;
    // Trim any '/' prefix before adding it to the base directory
    while (*filename == '/')
        filename++;
    fpath += filename;

    // Handle file differently if it's one of the types we parse
    if (fnHttpServiceParser::is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);

    FILE *fInput = pState->_FS->file_open(fpath.c_str());
    if (fInput == nullptr)
    {
        Debug_printf("Failed to open file for sending: '%s'\n", fpath.c_str());
        return_http_error(req, fnwserr_fileopen);
    }
    else
    {
        // Set the response content type
        set_file_content_type(req, fpath.c_str());
        // Set the expected length of the content
        char hdrval[10];
        snprintf(hdrval, 10, "%ld", FileSystem::filesize(fInput));
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks
        char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
        size_t count = 0;
        do
        {
            count = fread(buf, 1, FNWS_SEND_BUFF_SIZE, fInput);
            httpd_resp_send_chunk(req, buf, count);
        } while (count > 0);
        fclose(fInput);
        free(buf);
    }
}

void fnHttpService::parse_query(httpd_req_t *req, queryparts *results)
{
    results->full_uri += req->uri;
    // See if we have any arguments
    int path_end = results->full_uri.find_first_of('?');
    if (path_end < 0)
    {
        results->path += results->full_uri;
        return;
    }
    results->path += results->full_uri.substr(0, path_end - 1);
    results->query += results->full_uri.substr(path_end + 1);
    // TO DO: parse arguments, but we've no need for them yet
}

esp_err_t fnHttpService::get_handler_index(httpd_req_t *req)
{
    Debug_printf("Index request handler %p\n", xTaskGetCurrentTaskHandle());

    send_file(req, "index.html");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_test(httpd_req_t *req)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    Debug_printf("Test request handler %p\n", task);

    //Debug_printf("WiFI handle %p\n", handle_WiFi);
    //vTaskPrioritySet(handle_WiFi, 5);

    // Send the file content out in chunks
    char testln[100];
    for (int i = 0; i < 2000; i++)
    {
        int z = sprintf(testln, "%04d %06lu %p 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz<br/>\n",
                        i, fnSystem.millis() / 100, task);
        httpd_resp_send_chunk(req, testln, z);
    }
    httpd_resp_send_chunk(req, nullptr, 0);

    //vTaskPrioritySet(handle_WiFi, 23);

    Debug_println("Test completed");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_query(httpd_req_t *req)
{
    //Debug_printf("File_in_query request handler '%s'\n", req->uri);

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.query.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_path(httpd_req_t *req)
{
    //Debug_printf("File_in_path request handler '%s'\n", req->uri);

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.path.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_print(httpd_req_t *req)
{
    Debug_println("Print request handler");

    time_t now = fnSystem.millis();
    // Get a pointer to the current (only) printer
    sioPrinter *printer = (sioPrinter *)fnPrinters.get_ptr(0);

    if (now - printer->lastPrintTime() < PRINTER_BUSY_TIME)
    {
        _fnwserr err = fnwserr_post_fail;
        return_http_error(req, err);
        return ESP_FAIL;
    }
    // Get printer emulator pointer from sioP (which is now extern)
    printer_emu *currentPrinter = printer->getPrinterPtr();

    // Build a print output name
    const char *exts;

    bool sendAsAttachment = true;

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
        sendAsAttachment = false;
        break;
    case PDF:
        exts = "pdf";
        break;
    case SVG:
        exts = "svg";
        sendAsAttachment = false;
        break;
    case PNG:
        exts = "png";
        sendAsAttachment = false;
        break;
    case HTML:
    case HTML_ATASCII:
        exts = "html";
        sendAsAttachment = false;
        break;
    default:
        exts = "bin";
    }

    string filename = "printout.";
    filename += exts;

    // Set the expected content type based on the filename/extension
    set_file_content_type(req, filename.c_str());

    // Tell printer to finish its output and get a read handle to the file
    FILE *poutput = currentPrinter->closeOutputAndProvideReadHandle();

    char hdrval1[60];
    if (sendAsAttachment)
    {
        // Add a couple of attchment-specific details
        snprintf(hdrval1, sizeof(hdrval1), "attachment; filename=\"%s\"", filename.c_str());
        httpd_resp_set_hdr(req, "Content-Disposition", hdrval1);
    }
    // NOTE: Don't set the Content-Length, as it's invalid when using CHUNKED

    // Finally, write the data
    // Send the file content out in chunks
    char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
    size_t count = 0, total = 0;
    do
    {
        count = fread((uint8_t *)buf, 1, FNWS_SEND_BUFF_SIZE, poutput);
        //count = currentPrinter->readFromOutput((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
        total += count;

        // Debug_printf("Read %u bytes from print file\n", count);

        httpd_resp_send_chunk(req, buf, count);
    } while (count > 0);

    Debug_printf("Sent %u bytes total from print file\n", total);

    free(buf);
    fclose(poutput);

    // Tell the printer it can start writing from the beginning
    printer->reset_printer(); // destroy,create new printer emulator object of previous type.

    Debug_println("Print request completed");

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_modem_sniffer(httpd_req_t *req)
{
    Debug_printf("Modem Sniffer output request handler\n");
    ModemSniffer *modemSniffer = sioR->get_modem_sniffer();
    Debug_printf("Got modem Sniffer.\n");
    time_t now = fnSystem.millis();

    if (now - sioR->get_last_activity_time() < PRINTER_BUSY_TIME) // re-using printer timeout constant.
    {
        return_http_error(req, fnwserr_post_fail);
        return ESP_FAIL;
    }

    set_file_content_type(req,"modem-sniffer.txt");

    FILE *sOutput = modemSniffer->closeOutputAndProvideReadHandle();
    Debug_printf("Got file handle %p\n",sOutput);
    if(sOutput == nullptr)
    {
        return_http_error(req, fnwserr_post_fail);
        return ESP_FAIL;
    }
    
    // Finally, write the data
    // Send the file content out in chunks
    char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
    size_t count = 0, total = 0;
    do
    {
        count = fread((uint8_t *)buf, 1, FNWS_SEND_BUFF_SIZE, sOutput);
        // Debug_printf("fread %d, %d\n", count, errno);
        total += count;

        httpd_resp_send_chunk(req, buf, count);
    } while (count > 0);

    Debug_printf("Sent %u bytes total from sniffer file\n", total);

    free(buf);
    fclose(sOutput);

    Debug_printf("Sniffer dump completed.\n");

    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_config(httpd_req_t *req)
{

    Debug_printf("Post_config request handler '%s'\n", req->uri);

    _fnwserr err = fnwserr_noerrr;

    // Load the posted data
    char *buf = (char *)calloc(FNWS_RECV_BUFF_SIZE, 1);
    if (buf == NULL)
    {

        Debug_printf("Couldn't allocate %u bytes to store POST contents\n", FNWS_RECV_BUFF_SIZE);

        err = fnwserr_memory;
    }
    else
    {
        // Ask for the smaller of either the posted content or our buffer size
        size_t recv_size = req->content_len > FNWS_RECV_BUFF_SIZE ? FNWS_RECV_BUFF_SIZE : req->content_len;

        int ret = httpd_req_recv(req, buf, recv_size);
        if (ret <= 0)
        { // 0 return value indicates connection closed
            Debug_printf("Error (%d) returned trying to retrieve %u bytes posted data\n", ret, recv_size);
            err = fnwserr_post_fail;
        }
        else
        {
            // Go handle what we just read...
            ret = fnHttpServiceConfigurator::process_config_post(buf, recv_size);
        }
        free(buf);
    }

    if (err != fnwserr_noerrr)
    {
        return_http_error(req, err);
        return ESP_FAIL;
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

/* We're pointing global_ctx to a member of our fnHttpService object,
*  so we don't want the libarary freeing it for us. It'll be freed when
*  our fnHttpService object is freed.
*/
void fnHttpService::custom_global_ctx_free(void *ctx)
{
    // keep this commented for the moment to avoid warning.
    // serverstate * ctx_state = (serverstate *)ctx;
    // We could do something fancy here, but we don't need to do anything
}

httpd_handle_t fnHttpService::start_server(serverstate &state)
{
    std::vector<httpd_uri_t> uris{
        {.uri = "/test",
         .method = HTTP_GET,
         .handler = get_handler_test,
         .user_ctx = NULL},
        {.uri = "/",
         .method = HTTP_GET,
         .handler = get_handler_index,
         .user_ctx = NULL},
        {.uri = "/file",
         .method = HTTP_GET,
         .handler = get_handler_file_in_query,
         .user_ctx = NULL},
        {.uri = "/print",
         .method = HTTP_GET,
         .handler = get_handler_print,
         .user_ctx = NULL},
        {.uri = "/modem-sniffer.txt",
         .method = HTTP_GET,
         .handler = get_handler_modem_sniffer,
         .user_ctx = NULL},
        {.uri = "/favicon.ico",
         .method = HTTP_GET,
         .handler = get_handler_file_in_path,
         .user_ctx = NULL},
        {.uri = "/config",
         .method = HTTP_POST,
         .handler = post_handler_config,
         .user_ctx = NULL}};

    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    // Set filesystem where we expect to find our static files
    state._FS = &fnSPIFFS;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_resp_headers = 12;
    // Keep a reference to our object
    config.global_user_ctx = (void *)&state;
    // Set our own global_user_ctx free function, otherwise the library will free an object we don't want freed
    config.global_user_ctx_free_fn = (httpd_free_ctx_fn_t)custom_global_ctx_free;

    Debug_printf("Starting web server on port %d\n", config.server_port);

    if (httpd_start(&(state.hServer), &config) == ESP_OK)
    {
        // Register URI handlers
        for (const httpd_uri_t uridef : uris)
            httpd_register_uri_handler(state.hServer, &uridef);
    }
    else
    {
        state.hServer = NULL;
        Debug_println("Error starting web server!");
    }

    return state.hServer;
}

/* Set up and start the web server
*/
void fnHttpService::start()
{
    if (state.hServer != NULL)
    {
        Debug_println("httpServiceInit: We already have a web server handle - aborting");
        return;
    }

    // Register event notifications to let us know when WiFi is up/down
    // Missing the constants used here.  Need to find that...
    //esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &(state.hServer));
    //esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &(state.hServer));

    // Go ahead and attempt starting the server for the first time
    start_server(state);
}

void fnHttpService::stop()
{
    if (state.hServer != nullptr)
    {
        Debug_println("Stopping web service");
        httpd_stop(state.hServer);
        state._FS = nullptr;
        state.hServer = nullptr;
    }
}
