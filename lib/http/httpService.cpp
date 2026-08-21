
#include "fnHttpClient.h"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpService.h"

#include <sstream>
#include <vector>
#include <ctime>

#include <cJSON.h>

#include "../../include/debug.h"

// WebDAV
#include "webdav/handler.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnPassword.h"
#include "fnSession.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "../device/modem.h"
#include "printer.h"
#include "httpServiceBrowse.h"
#include "httpServiceApi.h"
#include "httpServiceConfigurator.h"
#include "httpServiceParser.h"
#include "clipboardManager.h"
#include "appKeyManager.h"
#include "fileManager.h"
#include "fnFsSD.h"
#include "fujiDevice.h"
#include "utils.h"
#ifdef BUILD_ATARI
#include "sio/sioFuji.h"
#endif /* BUILD_ATARI */

#if defined(BUILD_LYNX) || defined(BUILD_ADAM)
#define NO_MODEM_DEVICE         // doesn't have a modem device
#endif

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

using namespace std;

// Global HTTPD
fnHttpService fnHTTPD;

/**
 * URL encoding/decoding helper functions
 * These may be re-written to go into utils at some point.
 */

/* Converts a hex character to its integer value */
char from_hex(char ch)
{
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_decode(char *str)
{
    char *pstr = str, *buf = (char *)malloc(strlen(str) + 1), *pbuf = buf;
    while (*pstr)
    {
        if (*pstr == '%')
        {
            if (pstr[1] && pstr[2])
            {
                *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
                pstr += 2;
            }
        }
        else if (*pstr == '+')
        {
            *pbuf++ = ' ';
        }
        else
        {
            *pbuf++ = *pstr;
        }
        pstr++;
    }
    *pbuf = '\0';
    return buf;
}

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

/* Sends header.html or footer.html from SPIFFS. 0 for header, 1 for footer */
void fnHttpService::send_header_footer(httpd_req_t *req, int headfoot)
{
    // Build the full file path
    string fpath = FNWS_FILE_ROOT;
    switch (headfoot)
    {
    case 0:
        fpath += "header.html";
        break;
    case 1:
        fpath += "footer.html";
        break;
    default:
        Debug_println("Header / Footer choice invalid");
        return;
        break;
    }

    // Retrieve server state
    serverstate *pState = (serverstate *)httpd_get_global_user_ctx(req->handle);
    FILE *fInput = pState->_FS->file_open(fpath.c_str());

    if (fInput == nullptr)
    {
        Debug_println("Failed to open header file for parsing");
    }
    else
    {
        size_t sz = FileSystem::filesize(fInput) + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\n", sz);
        }
        else
        {
            fread(buf, 1, sz, fInput);
            string contents(buf);
            contents = fnHttpServiceParser::parse_contents(contents);

            httpd_resp_sendstr_chunk(req, contents.c_str());
        }
        free(buf);
    }

    if (fInput != nullptr)
        fclose(fInput);
}

/* Send file content after parsing for replaceable strings
 */
void fnHttpService::send_file_parsed(httpd_req_t *req, const char *filename)
{
    // Note that we don't add FNWS_FILE_ROOT as it should've been done in send_file()
#ifdef VERBOSE_HTTP
    Debug_printf("Opening file for parsing: '%s'\n", filename);
#endif

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
            contents = fnHttpServiceParser::parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());
        }
        free(buf);
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
    char *decoded_query;
    vector<string> vItems;

    results->full_uri += req->uri;
    // See if we have any arguments
    int path_end = results->full_uri.find_first_of('?');
    if (path_end < 0)
    {
        results->path += results->full_uri;
        return;
    }

    /// @todo Error if path_end == 0, the index to substr becomes -1
    results->path += results->full_uri.substr(0, path_end - 1);
    results->query += results->full_uri.substr(path_end + 1);

    // URL Decode query
    decoded_query = url_decode((char *)results->query.c_str());
    results->query = string(decoded_query);

    if (results->query.empty())
    {
        free(decoded_query);
        return;
    }

    char *token = strtok(decoded_query, "&");

    while (token != NULL)
    {
#ifdef VERBOSE_HTTP
        Debug_printf("Item: %s\n", token);
#endif
        vItems.push_back(string(token));
        token = strtok(NULL, "&");
    }

    if (vItems.empty())
    {
        free(decoded_query);
        return;
    }

    for (vector<string>::iterator it = vItems.begin(); it != vItems.end(); ++it)
    {
        string key;
        string value;

        if (it->find_first_of("=") == string::npos)
            continue;

        key = it->substr(0, it->find_first_of("="));
        value = it->substr(it->find_first_of("=") + 1);

#ifdef VERBOSE_HTTP
        Debug_printf("Key %s : Value %s\n", key.c_str(), value.c_str());
#endif

        results->query_parsed.insert(make_pair(key, value));
    }

    free(decoded_query);
}

esp_err_t fnHttpService::get_handler_index(httpd_req_t *req)
{
#ifdef VERBOSE_HTTP
    Debug_printf("Index request handler %p\n", xTaskGetCurrentTaskHandle());
#endif

    send_file(req, "index.html");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_test(httpd_req_t *req)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
#ifdef VERBOSE_HTTP
    Debug_printf("Test request handler %p\n", task);
#endif

    // Debug_printf("WiFI handle %p\n", handle_WiFi);
    // vTaskPrioritySet(handle_WiFi, 5);

    // Send the file content out in chunks
    char testln[100];
    for (int i = 0; i < 2000; i++)
    {
        int z = sprintf(testln, "%04d %06lu %p 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz<br/>\n",
                        i, fnSystem.millis() / 100, task);
        httpd_resp_send_chunk(req, testln, z);
    }
    httpd_resp_send_chunk(req, nullptr, 0);

    // vTaskPrioritySet(handle_WiFi, 23);

    Debug_println("Test completed");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_query(httpd_req_t *req)
{
    // Debug_printf("File_in_query request handler '%s'\n", req->uri);

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.query.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_path(httpd_req_t *req)
{
    // Debug_printf("File_in_path request handler '%s'\n", req->uri);

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.path.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_print(httpd_req_t *req)
{
    Debug_println("Print request handler");

    fnHTTPD.clearErrMsg();

    time_t now = fnSystem.millis();
    // Get a pointer to the current (only) printer
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
    if (printer == nullptr)
    {
        Debug_println("No virtual printer");
        return ESP_FAIL;
    }
    if (now - printer->lastPrintTime() < PRINTER_BUSY_TIME)
    {
        fnHTTPD.addToErrMsg("Printer is busy. Try again later.\n");
        send_file(req, "error_page.html");
        return ESP_OK;
    }
    // Get printer emulator pointer from sioP (which is now extern)
    printer_emu *currentPrinter = printer->getPrinterPtr();

    if (currentPrinter == nullptr)
    {
        Debug_println("No current virtual printer");
        _fnwserr err = fnwserr_post_fail;
        return_http_error(req, err);
        return ESP_FAIL;
    }

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

    // Tell printer to finish its output and get a read handle to the file
    FILE *poutput = currentPrinter->closeOutputAndProvideReadHandle();
    if (poutput == nullptr)
    {
        fnHTTPD.addToErrMsg("Unable to open printer output.\n");
        send_file(req, "error_page.html");
        return ESP_OK;
    }

    // Set the expected content type based on the filename/extension
    set_file_content_type(req, filename.c_str());

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
        // count = currentPrinter->readFromOutput((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
        total += count;

        // Debug_printf("Read %u bytes from print file\n", count);

        httpd_resp_send_chunk(req, buf, count);
    } while (count > 0);

#ifdef VERBOSE_HTTP
    Debug_printf("Sent %u bytes total from print file\n", total);
#endif

    free(buf);
    fclose(poutput);

    // Tell the printer it can start writing from the beginning
    printer->reset_printer(); // destroy,create new printer emulator object of previous type.

    Debug_println("Print request completed");

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_modem_sniffer(httpd_req_t *req)
{
#ifdef VERBOSE_HTTP
    Debug_printf("Modem Sniffer output request handler\n");
#endif

#ifndef NO_MODEM_DEVICE

    fnHTTPD.clearErrMsg();

    ModemSniffer *modemSniffer = sioR->get_modem_sniffer();
#ifdef VERBOSE_HTTP
    Debug_printf("Got modem Sniffer.\n");
#endif
    time_t now = fnSystem.millis();

    if (now - sioR->get_last_activity_time() < PRINTER_BUSY_TIME) // re-using printer timeout constant.
    {
        fnHTTPD.addToErrMsg("Modem is busy. Try again later.\n");
        send_file(req, "error_page.html");
        return ESP_OK;
    }

    FILE *sOutput = modemSniffer->closeOutputAndProvideReadHandle();
#ifdef VERBOSE_HTTP
    Debug_printf("Got file handle %p\n", sOutput);
#endif
    if (sOutput == nullptr)
    {
        fnHTTPD.addToErrMsg("Unable to open modem sniffer output.\n");
        send_file(req, "error_page.html");
        return ESP_OK;
    }

    set_file_content_type(req, "modem-sniffer.txt");

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

#ifdef VERBOSE_HTTP
    Debug_printf("Sent %u bytes total from sniffer file\n", total);
#endif

    free(buf);
    fclose(sOutput);

#ifdef VERBOSE_HTTP
    Debug_printf("Sniffer dump completed.\n");
#endif

#endif      // NO_MODEM_DEVICE

    return ESP_OK;

}

/* Look up an optional integer query parameter, returning -1 when it is absent
*/
static int query_int(std::map<std::string, std::string> &query, const char *key)
{
    auto it = query.find(key);
    return it == query.end() ? -1 : atoi(it->second.c_str());
}

esp_err_t fnHttpService::get_handler_mount(httpd_req_t *req)
{
    queryparts qp;

    fnHTTPD.clearErrMsg();

    parse_query(req, &qp);

    // if request contains 'mountall=1' skip to mounting all disks
    if (qp.query_parsed.find("mountall") != qp.query_parsed.end())
    {
        // Mount all the things
        Debug_printf("Mount all slots from webui\n");
        theFuji->fujicore_mount_all_success();
    }
    else
    {
        fnHttpBrowse::mount_params params;
        params.host_slot = query_int(qp.query_parsed, "hostslot");
        params.device_slot = query_int(qp.query_parsed, "deviceslot");
        params.mode = query_int(qp.query_parsed, "mode");

        auto fn = qp.query_parsed.find("filename");
        if (fn != qp.query_parsed.end())
        {
            params.filename = fn->second;
            params.filename_given = true;
        }

        fnHttpBrowse::mount_file(params);
    }

    if (!fnHTTPD.errMsgEmpty())
    {
        send_file(req, "error_page.html");
    }
    else
    {
        send_file(req, "redirect_to_index.html");
    }

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_eject(httpd_req_t *req)
{
    queryparts qp;

    fnHTTPD.clearErrMsg();

    parse_query(req, &qp);

    fnHttpBrowse::eject_slot(query_int(qp.query_parsed, "deviceslot"));

    if (!fnHTTPD.errMsgEmpty())
    {
        send_file(req, "error_page.html");
    }
    else
    {
        send_file(req, "redirect_to_index.html");
    }

    return ESP_OK;
}

#ifdef BUILD_ADAM
esp_err_t fnHttpService::get_handler_term(httpd_req_t *req)
{
    esp_err_t ret;
    uint8_t *buf = NULL;

    if (req->method == HTTP_GET)
    {
        Debug_printf("/term get DONE");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // See if we need to get any keypresses
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        Debug_printf("err = %x\n",ret);
        return ret;
    }
    Debug_printf("ws_pkt.len = %x\n",ws_pkt.len);

    if (ws_pkt.len)
    {
        buf = (uint8_t *)calloc(ws_pkt.len + 1, sizeof(uint8_t));
        if (buf == NULL)
            return ESP_ERR_NO_MEM;
        else
            ws_pkt.payload = buf;

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            Debug_printf("recv_frame data failed %d\n",ret);
            free(buf);
            return ret;
        }
    }

    free(buf);

    // Now see if we need to send anything back

    return ret;
}

esp_err_t fnHttpService::get_handler_kybd(httpd_req_t *req)
{
    esp_err_t ret;
    uint8_t *buf = NULL;

    if (req->method == HTTP_GET)
    {
        Debug_printf("/kybd get DONE");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // See if we need to get any keypresses
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        Debug_printf("err = %x\n",ret);
        return ret;
    }
    Debug_printf("ws_pkt.len = %x\n",ws_pkt.len);

    if (ws_pkt.len)
    {
        buf = (uint8_t *)calloc(ws_pkt.len + 1, sizeof(uint8_t));
        if (buf == NULL)
            return ESP_ERR_NO_MEM;
        else
            ws_pkt.payload = buf;

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            Debug_printf("recv_frame data failed %d\n",ret);
            free(buf);
            return ret;
        }
    }

    free(buf);

    return ret;
}
#endif /* BUILD_ADAM */

esp_err_t fnHttpService::get_handler_dir(httpd_req_t *req)
{
    queryparts qp;

    fnHTTPD.clearErrMsg();

    parse_query(req, &qp);

    if (qp.query_parsed.find("hostslot") == qp.query_parsed.end())
    {
        fnHTTPD.addToErrMsg("<li>hostslot is empty</li>");
    }

    auto path = qp.query_parsed.find("path");
    auto pattern = qp.query_parsed.find("pattern");

    fnHttpBrowse::render_opts opts; // no /download route on the ESP32

    // The listing is streamed, but a host that won't open has to be answered
    // with the error page instead - so hold the header back until there is
    // something to send.
    bool header_sent = false;
    auto emit = [req, &header_sent](const std::string &chunk) {
        if (!header_sent)
        {
            send_header_footer(req, 0); // header
            header_sent = true;
        }
        httpd_resp_sendstr_chunk(req, chunk.c_str());
    };

    httpd_resp_set_type(req, "text/html");

    bool ok = fnHttpBrowse::render_hostdir(
        query_int(qp.query_parsed, "hostslot"),
        path == qp.query_parsed.end() ? "" : path->second,
        pattern == qp.query_parsed.end() ? "*" : pattern->second,
        opts, emit);

    if (!ok)
    {
        fnHTTPD.addToErrMsg("<li>Could not open directory</li>");
        send_file(req, "error_page.html");
        return ESP_OK;
    }

    send_header_footer(req, 1); // footer

    httpd_resp_send_chunk(req, NULL, 0); // end of response.

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_slot(httpd_req_t *req)
{
    queryparts qp;

    fnHTTPD.clearErrMsg();

    parse_query(req, &qp);

    if ((qp.query_parsed.find("hostslot") == qp.query_parsed.end()) ||
        (qp.query_parsed.find("filename") == qp.query_parsed.end()))
    {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int hs = query_int(qp.query_parsed, "hostslot");
    const string &filename = qp.query_parsed["filename"];

    httpd_resp_set_type(req, "text/html");

    auto emit = [req](const std::string &chunk) {
        httpd_resp_sendstr_chunk(req, chunk.c_str());
    };

    // Render into a buffer first: a cassette image needs a redirect instead of
    // a picker, and by then the page header would already have gone out.
    string body;
    auto buffer = [&body](const std::string &chunk) { body += chunk; };

    fnHttpBrowse::slot_result result = fnHttpBrowse::render_slotpicker(hs, filename, buffer);

    if (result == fnHttpBrowse::slot_result::CASSETTE)
    {
        // Cassette image passed in, put it in the cassette slot and redirect
        string chunk;
        chunk += "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        chunk += "<!doctype html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"DTD/xhtml1-strict.dtd\">\r\n";
        chunk += "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\r\n";
        chunk += " <head>\r\n";
        chunk += "  <title>Redirecting to cassette mount</title>";
        chunk += "  <meta http-equiv=\"refresh\" content=\"0; url=/mount?hostslot=" + to_string(hs) +
                 "&deviceslot=" + to_string(fnHttpBrowse::CASSETTE_DEVICE_SLOT) +
                 "&mode=" + to_string(fnHttpBrowse::CASSETTE_MOUNT_MODE) +
                 "&filename=" + util_url_encode(filename) + "\" />";
        chunk += " </head>\r\n";
        chunk += " <body>\r\n";
        chunk += "  <h1>Cassette detected. Mounting in slot " +
                 to_string(fnHttpBrowse::CASSETTE_DEVICE_SLOT + 1) + ".</h1>\r\n";
        chunk += " </body>\r\n";
        chunk += "</html>\r\n";

        httpd_resp_sendstr(req, chunk.c_str());

        return ESP_OK;
    }

    if (result == fnHttpBrowse::slot_result::RENDER_ERROR)
    {
        fnHTTPD.addToErrMsg("<li>Could not list drive slots</li>");
        send_file(req, "error_page.html");
        return ESP_OK;
    }

    send_header_footer(req, 0); // header
    emit(body);
    send_header_footer(req, 1);          // footer
    httpd_resp_send_chunk(req, NULL, 0); // end response.

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_hosts(httpd_req_t *req)
{
    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji->get_host(hs)->get_hostname()) + "\n";
    }
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_hosts(httpd_req_t *req)
{
    queryparts qp;
    parse_query(req, &qp);

    int hostslot = atoi(qp.query_parsed["hostslot"].c_str());
    char *hostname = (char *)qp.query_parsed["hostname"].c_str();

    theFuji->set_slot_hostname(hostslot, hostname);

    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji->get_host(hs)->get_hostname()) + "\n";
    }
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}

std::string fnHttpService::shorten_url(std::string url)
{
    int id = shortURLs.size();
    shortURLs.push_back(url);

    // Ideally would use hostname, but it doesn't include .local needed for mDNS devices
    std::string shortened = "http://" + fnSystem.Net.get_ip4_address_str() + "/url/" + std::to_string(id);
    Debug_printf("Short URL /url/%d registered for URL: %s\n", id, url.c_str());
    return shortened;
}

esp_err_t fnHttpService::get_handler_shorturl(httpd_req_t *req)
{
    // Strip the /url/ from the path
    std::string id_str = std::string(req->uri).substr(5);
    Debug_printf("Short URL handler: %s\n", id_str.c_str());

    if (!std::all_of(id_str.begin(), id_str.end(), ::isdigit)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    int id = std::stoi(id_str);
    if (id > fnHTTPD.shortURLs.size())
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, NULL, 0);
    }
    else
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", fnHTTPD.shortURLs[id].c_str());
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_config(httpd_req_t *req)
{
#ifdef VERBOSE_HTTP
    Debug_printf("Post_config request handler '%s'\n", req->uri);
#endif

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
    }

    free(buf);

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

// ─── Device password ─────────────────────────────────────────────────────────

static void password_send_json(httpd_req_t *req, const char *status_line, cJSON *body)
{
    char *json = cJSON_PrintUnformatted(body);
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json != nullptr ? json : "{}");
    if (json != nullptr)
        cJSON_free(json);
    cJSON_Delete(body);
}

static void password_send_error(httpd_req_t *req, const char *status_line, const std::string &message)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "status", "error");
    cJSON_AddStringToObject(out, "message", message.c_str());
    password_send_json(req, status_line, out);
}

esp_err_t fnHttpService::post_handler_password(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > FNWS_RECV_BUFF_SIZE)
    {
        password_send_error(req, "400 Bad Request", "Bad password request");
        return ESP_OK;
    }

    std::vector<char> buf(req->content_len);
    int ret = httpd_req_recv(req, buf.data(), buf.size());
    if (ret <= 0)
    {
        Debug_printf("Error (%d) receiving posted password data\n", ret);
        password_send_error(req, "400 Bad Request", MSG_ERR_RECEIVE_FAILURE);
        return ESP_OK;
    }

    std::map<std::string, std::string> postvals =
        fnHttpServiceConfigurator::parse_postdata_decoded(buf.data(), (size_t)ret);
    // Don't leave the plaintext password sitting in the receive buffer
    memset(buf.data(), 0, buf.size());

    std::string error;
    bool ok;
    if (postvals["action"] == "clear")
    {
        ok = fnPassword.remove(postvals["current"], error);
    }
    else if (postvals["new"] != postvals["confirm"])
    {
        ok = false;
        error = "New password and confirmation do not match";
    }
    else
    {
        ok = fnPassword.change(postvals["current"], postvals["new"], error);
    }

    if (!ok)
    {
        password_send_error(req, "400 Bad Request", error);
        return ESP_OK;
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "status", "ok");
    cJSON_AddBoolToObject(out, "password_set", fnPassword.is_set());
    password_send_json(req, "200 OK", out);
    return ESP_OK;
}

// ─── Login / Logout handlers ─────────────────────────────────────────────────

// Defined further down with the file manager handlers; reads and url-decodes a
// single query-string value.
static std::string files_query_value(httpd_req_t *req, const char *key);

esp_err_t fnHttpService::get_handler_login(httpd_req_t *req)
{
    std::string next = files_query_value(req, "next");
    next = fnSession::sanitize_next(next);

    if (!fnPassword.is_set())
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", next.c_str());
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Check if already logged in
    size_t hdrlen = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdrlen > 0 && hdrlen < 2048)
    {
        std::vector<char> buf(hdrlen + 1);
        if (httpd_req_get_hdr_value_str(req, "Cookie", buf.data(), buf.size()) == ESP_OK)
        {
            std::string token = fnSession::cookie_value(buf.data(), FN_SESSION_COOKIE);
            if (fnPassword.check_session(token))
            {
                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", next.c_str());
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
    }

    // Render login page
    std::string page = fnSession::render_login_page(next, "");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.size());
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_login(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > FNWS_RECV_BUFF_SIZE)
    {
        std::string page = fnSession::render_login_page("/", "Bad login request");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page.c_str(), page.size());
        return ESP_OK;
    }

    std::vector<char> buf(req->content_len);
    int ret = httpd_req_recv(req, buf.data(), buf.size());
    if (ret <= 0)
    {
        Debug_printf("Error (%d) receiving posted login data\n", ret);
        std::string page = fnSession::render_login_page("/", "Bad login request");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page.c_str(), page.size());
        return ESP_OK;
    }

    std::map<std::string, std::string> postvals =
        fnHttpServiceConfigurator::parse_postdata_decoded(buf.data(), (size_t)ret);
    memset(buf.data(), 0, buf.size());

    std::string next = fnSession::sanitize_next(postvals["next"]);
    std::string token = fnPassword.login(postvals["password"]);

    if (token.empty())
    {
        httpd_resp_set_status(req, "401 Unauthorized");
        std::string page = fnSession::render_login_page(next, "Incorrect password");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page.c_str(), page.size());
        return ESP_OK;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", next.c_str());
    std::string cookie = fnSession::set_cookie(token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_logout(httpd_req_t *req)
{
    fnPassword.logout();

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    std::string cookie = fnSession::clear_cookie();
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Returns true when the request may proceed. Otherwise a redirect or a 401 has
   already been sent and the handler should return ESP_OK.

   With no device password set the whole web UI is open; once one is set every
   route needs a session cookie except the login page and its static assets. */
static bool require_session(httpd_req_t *req)
{
    if (!fnPassword.is_set())
        return true;

    // Split req->uri into path and query
    const char *query_start = strchr(req->uri, '?');
    std::string path = query_start ? std::string(req->uri, query_start - req->uri) : std::string(req->uri);
    std::string query = query_start ? std::string(query_start + 1) : std::string();

    if (fnSession::is_public(path.c_str(), query.c_str()))
        return true;

    // Read the Cookie header
    size_t hdrlen = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdrlen > 0 && hdrlen < 2048)
    {
        std::vector<char> buf(hdrlen + 1);
        if (httpd_req_get_hdr_value_str(req, "Cookie", buf.data(), buf.size()) == ESP_OK)
        {
            std::string token = fnSession::cookie_value(buf.data(), FN_SESSION_COOKIE);
            if (fnPassword.check_session(token))
                return true;
        }
    }

    // Deny the request
    size_t accept_len = httpd_req_get_hdr_value_len(req, "Accept");
    std::string accept_hdr;
    if (accept_len > 0 && accept_len < 1024)
    {
        std::vector<char> buf(accept_len + 1);
        if (httpd_req_get_hdr_value_str(req, "Accept", buf.data(), buf.size()) == ESP_OK)
            accept_hdr = std::string(buf.data());
    }

    size_t xrw_len = httpd_req_get_hdr_value_len(req, "X-Requested-With");
    std::string xrw_hdr;
    if (xrw_len > 0 && xrw_len < 1024)
    {
        std::vector<char> buf(xrw_len + 1);
        if (httpd_req_get_hdr_value_str(req, "X-Requested-With", buf.data(), buf.size()) == ESP_OK)
            xrw_hdr = std::string(buf.data());
    }

    size_t ct_len = httpd_req_get_hdr_value_len(req, "Content-Type");
    std::string ct_hdr;
    if (ct_len > 0 && ct_len < 1024)
    {
        std::vector<char> buf(ct_len + 1);
        if (httpd_req_get_hdr_value_str(req, "Content-Type", buf.data(), buf.size()) == ESP_OK)
            ct_hdr = std::string(buf.data());
    }

    if (fnSession::wants_json(accept_hdr.empty() ? nullptr : accept_hdr.c_str(),
                             xrw_hdr.empty() ? nullptr : xrw_hdr.c_str(),
                             ct_hdr.empty() ? nullptr : ct_hdr.c_str()))
    {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, fnSession::json_unauthorized());
    }
    else
    {
        std::string login_url = fnSession::login_url(path, query);
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", login_url.c_str());
        httpd_resp_send(req, NULL, 0);
    }
    return false;
}

// ─── Authentication dispatch trampoline ──────────────────────────────────────

typedef esp_err_t (*fn_uri_handler)(httpd_req_t *);

// esp_http_server has no pre-dispatch hook, so every route is registered
// against this trampoline with its real handler carried in user_ctx.
static fn_uri_handler s_real_handlers[64];

static esp_err_t auth_dispatch(httpd_req_t *req)
{
    if (!require_session(req))
        return ESP_OK;
    return (*(fn_uri_handler *)req->user_ctx)(req);
}

esp_err_t fnHttpService::get_handler_appkeys(httpd_req_t *req)
{
    std::string page = AppKeyManager::render_page("");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.size());
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_appkeys(httpd_req_t *req)
{
    // A full table of MAX_APPKEY_LEN values can be considerably larger than the
    // shared receive buffer, so size the read to the posted content.
    if (req->content_len == 0 || req->content_len > APPKEYS_MAX_POST_SIZE)
    {
        return_http_error(req, fnwserr_post_fail);
        return ESP_FAIL;
    }

    std::vector<char> buf(req->content_len);
    size_t received = 0;
    while (received < buf.size())
    {
        int ret = httpd_req_recv(req, buf.data() + received, buf.size() - received);
        if (ret <= 0)
        {
            Debug_printf("Error (%d) receiving posted appkey data\n", ret);
            return_http_error(req, fnwserr_post_fail);
            return ESP_FAIL;
        }
        received += ret;
    }

    std::string message = AppKeyManager::handle_post(
        fnHttpServiceConfigurator::parse_postdata_decoded(buf.data(), received));

    std::string page = AppKeyManager::render_page(message);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.size());
    return ESP_OK;
}

// ─── SD card file manager ────────────────────────────────────────────────────

/* Value of one query string variable, url-decoded. Empty when not present. */
static std::string files_query_value(httpd_req_t *req, const char *key)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 1024)
        return std::string();

    std::vector<char> query(qlen + 1);
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK)
        return std::string();

    std::vector<char> value(qlen + 1);
    if (httpd_query_key_value(query.data(), key, value.data(), value.size()) != ESP_OK)
        return std::string();

    // httpd_query_key_value does not url-decode
    std::vector<char> decoded(strlen(value.data()) + 1);
    fnHttpServiceConfigurator::url_decode(decoded.data(), value.data(), strlen(value.data()));
    return std::string(decoded.data());
}

static void files_send_page(httpd_req_t *req, const std::string &dir, const std::string &message)
{
    std::string page = FileManager::render_page(dir, message);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t fnHttpService::get_handler_files(httpd_req_t *req)
{
    std::string dir;
    if (!FileManager::normalize_path(files_query_value(req, "path"), dir))
    {
        files_send_page(req, "/", "Invalid path.");
        return ESP_OK;
    }

    files_send_page(req, dir, "");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_files_download(httpd_req_t *req)
{
    std::string path;
    if (!FileManager::normalize_path(files_query_value(req, "path"), path) || path == "/")
    {
        return_http_error(req, fnwserr_fileopen);
        return ESP_FAIL;
    }

    FILE *fin = fnSDFAT.file_open(path.c_str(), FILE_READ);
    if (fin == nullptr)
    {
        return_http_error(req, fnwserr_fileopen);
        return ESP_FAIL;
    }

    set_file_content_type(req, path.c_str());

    char hdrval[16];
    snprintf(hdrval, sizeof(hdrval), "%ld", FileSystem::filesize(fin));
    httpd_resp_set_hdr(req, "Content-Length", hdrval);

    std::string disposition =
        "attachment; filename=\"" + path.substr(path.find_last_of('/') + 1) + "\"";
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());

    char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
    if (buf == nullptr)
    {
        fclose(fin);
        return_http_error(req, fnwserr_memory);
        return ESP_FAIL;
    }
    size_t count;
    do
    {
        count = fread(buf, 1, FNWS_SEND_BUFF_SIZE, fin);
        httpd_resp_send_chunk(req, buf, count);
    } while (count > 0);

    free(buf);
    fclose(fin);
    return ESP_OK;
}

esp_err_t fnHttpService::post_handler_files_action(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > FILEMANAGER_MAX_POST_SIZE)
    {
        return_http_error(req, fnwserr_post_fail);
        return ESP_FAIL;
    }

    std::vector<char> buf(req->content_len);
    size_t received = 0;
    while (received < buf.size())
    {
        int ret = httpd_req_recv(req, buf.data() + received, buf.size() - received);
        if (ret <= 0)
        {
            Debug_printf("Error (%d) receiving posted file action\n", ret);
            return_http_error(req, fnwserr_post_fail);
            return ESP_FAIL;
        }
        received += ret;
    }

    std::string dir = "/";
    std::string message = FileManager::handle_action(
        fnHttpServiceConfigurator::parse_postdata_decoded(buf.data(), received), dir);

    files_send_page(req, dir, message);
    return ESP_OK;
}

// ─── Clipboard handlers ──────────────────────────────────────────────────────

// Read the whole posted body, up to the clipboard size limit.
static bool clipboard_recv_body(httpd_req_t *req, std::string &body)
{
    if (req->content_len > CLIPBOARD_MAX_SIZE)
    {
        Debug_printf("Clipboard: rejecting %u byte upload\n", (unsigned)req->content_len);
        return false;
    }

    body.resize(req->content_len);

    size_t received = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, &body[received], req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (ret <= 0)
        {
            Debug_printf("Clipboard: error (%d) receiving posted data\n", ret);
            return false;
        }
        received += ret;
    }

    return true;
}

esp_err_t fnHttpService::post_handler_files_upload(httpd_req_t *req)
{
    std::string dir;
    if (!FileManager::normalize_path(files_query_value(req, "path"), dir))
    {
        files_send_page(req, "/", "Invalid path.");
        return ESP_OK;
    }

    if (!fnSDFAT.running())
    {
        files_send_page(req, dir, "No SD card is mounted.");
        return ESP_OK;
    }

    char content_type[128] = {0};
    httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));

    MultipartFileWriter writer;
    std::string error;
    if (!writer.begin(content_type, dir, error))
    {
        files_send_page(req, dir, FileManager::html_escape(error) + ".");
        return ESP_OK;
    }

    // Stream the body to the SD card so an upload never has to fit in RAM
    std::vector<char> buf(FNWS_RECV_BUFF_SIZE);
    size_t remaining = req->content_len;
    bool ok = true;
    while (remaining > 0 && ok)
    {
        size_t want = remaining < buf.size() ? remaining : buf.size();
        int ret = httpd_req_recv(req, buf.data(), want);
        if (ret <= 0)
        {
            Debug_printf("Error (%d) receiving uploaded file\n", ret);
            error = "Upload was interrupted";
            ok = false;
            break;
        }
        remaining -= ret;
        ok = writer.feed(buf.data(), ret, error);
    }

    if (ok)
        ok = writer.finish(error);

    if (!ok)
    {
        files_send_page(req, dir, FileManager::html_escape(error) + ".");
        return ESP_OK;
    }

    files_send_page(req, dir,
                    "Uploaded " + writer.summary() + ".");
    return ESP_OK;
}

// GET /clipboard - clipboard state and the remembered snippets
esp_err_t fnHttpService::get_handler_clipboard(httpd_req_t *req)
{
    std::string json = fnClipboard.to_json();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());

    return ESP_OK;
}

// GET /clipboard/data?index=N - the contents of a snippet, as-is
esp_err_t fnHttpService::get_handler_clipboard_data(httpd_req_t *req)
{
    queryparts qp;
    parse_query(req, &qp);

    size_t index = atoi(qp.query_parsed["index"].c_str());

    const ClipboardSnippet *snippet = fnClipboard.snippet(index);
    if (snippet == nullptr)
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    if (snippet->binary)
    {
        std::string disposition = "attachment; filename=\"" +
            (snippet->name.empty() ? std::string("clipboard.bin") : snippet->name) + "\"";

        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    }
    else
    {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
    }

    httpd_resp_send(req, snippet->data.data(), snippet->data.size());

    return ESP_OK;
}

// POST /clipboard?binary=1&name=... - replace the clipboard with the posted body
esp_err_t fnHttpService::post_handler_clipboard(httpd_req_t *req)
{
    queryparts qp;
    parse_query(req, &qp);

    bool binary = qp.query_parsed["binary"] == "1";
    std::string name = qp.query_parsed["name"];

    std::string body;
    if (!clipboard_recv_body(req, body))
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    if (!binary)
        body = fnClipboard.normalize_host_text(body);

    fnClipboard.set(std::move(body), CLIPBOARD_SOURCE_WEBUI, binary, name);

    std::string json = fnClipboard.to_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());

    return ESP_OK;
}

// POST /clipboard/clear?all=1 - empty the clipboard, optionally the history too
esp_err_t fnHttpService::post_handler_clipboard_clear(httpd_req_t *req)
{
    queryparts qp;
    parse_query(req, &qp);

    if (qp.query_parsed["all"] == "1")
        fnClipboard.clear_all();
    else
        fnClipboard.clear();

    std::string json = fnClipboard.to_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());

    return ESP_OK;
}

// POST /clipboard/restore?index=N - make a remembered snippet current again
esp_err_t fnHttpService::post_handler_clipboard_restore(httpd_req_t *req)
{
    queryparts qp;
    parse_query(req, &qp);

    size_t index = atoi(qp.query_parsed["index"].c_str());

    if (!fnClipboard.restore(index))
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    std::string json = fnClipboard.to_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());

    return ESP_OK;
}

// ─── end clipboard handlers ──────────────────────────────────────────────────

// ─── Google Drive OAuth2 relay-based authorization-code-flow handlers ────────
//
// The FujiNet project registers ONE Google OAuth2 "Desktop application" client.
// Its client_id is public and baked in here.  PKCE (RFC 7636) is used so no
// client_secret is ever needed on the device.
//
// Every FujiNet user registers the same relay redirect URI in their copy of
// the shared OAuth client:
//   https://auth.fujinet.online/gdrive-callback
//
// FujiNet project's Web application OAuth2 client ID.
// The client_secret lives on the relay server — never in firmware.
#define GDRIVE_CLIENT_ID          "197927610161-me037pnh65lh9g8cad6fg62ifni9fik0.apps.googleusercontent.com"
#define GDRIVE_RELAY_REDIRECT_URI "https://auth.fujinet.online/gdrive-callback"
#define GDRIVE_RELAY_CODE_URL     "https://auth.fujinet.online/gdrive-code?state="
#define GDRIVE_RELAY_REFRESH_URL  "https://auth.fujinet.online/gdrive-refresh"

static std::string gdrive_auth_state;

#ifdef ESP_PLATFORM
// Synchronous HTTPS POST: open → write → fetch_headers → read.
static int gdrive_do_post(const char *url, const char *post_body, std::string &out_body)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;

    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_header(h, "Content-Type", "application/x-www-form-urlencoded");

    int wlen = (int)strlen(post_body);
    if (esp_http_client_open(h, wlen) != ESP_OK) {
        esp_http_client_cleanup(h);
        return -1;
    }
    esp_http_client_write(h, post_body, wlen);
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    char buf[256];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        out_body.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return status;
}

// Synchronous HTTPS GET: open (no body) → fetch_headers → read.
static int gdrive_do_get(const char *url, std::string &out_body)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 10000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;

    if (esp_http_client_open(h, 0) != ESP_OK) {
        esp_http_client_cleanup(h);
        return -1;
    }
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    char buf[256];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        out_body.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return status;
}

// Percent-encode a string for use as a URL query value or form field value.
static std::string gdrive_pct_encode(const std::string &s)
{
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

#else
static int gdrive_do_post(const char *, const char *, std::string &) { return -1; }
static int gdrive_do_get(const char *, std::string &) { return -1; }
static std::string gdrive_pct_encode(const std::string &s) { return s; }
#endif

/**
 * GET /gdrive-auth
 *
 * Generates a CSRF state token, stores it, and returns JSON with the full
 * Google authorization URL pointing to the shared FujiNet relay redirect URI.
 * The browser opens that URL in a new tab; FujiNet polls /gdrive-poll until
 * the relay delivers the authorization code.
 *
 *   { "auth_url": "https://accounts.google.com/o/oauth2/auth?...", "state": "XXXXXXXX" }
 */
esp_err_t fnHttpService::get_handler_gdrive_auth(httpd_req_t *req)
{
    uint32_t r = esp_random();
    char state[16];
    snprintf(state, sizeof(state), "%08lx", (unsigned long)r);
    gdrive_auth_state = state;

    std::string auth_url =
        "https://accounts.google.com/o/oauth2/auth"
        "?response_type=code"
        "&access_type=offline"
        "&prompt=consent"
        "&client_id="    + gdrive_pct_encode(GDRIVE_CLIENT_ID) +
        "&redirect_uri=" + gdrive_pct_encode(GDRIVE_RELAY_REDIRECT_URI) +
        "&scope="        + gdrive_pct_encode("https://www.googleapis.com/auth/drive"
                                            " https://www.googleapis.com/auth/gmail.readonly") +
        "&state="        + std::string(state);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "auth_url", auth_url.c_str());
    cJSON_AddStringToObject(out, "state",    state);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, s, strlen(s));
    free(s);
    return ESP_OK;
}

/**
 * GET /gdrive-poll?state=STATE
 *
 * Called by the web UI JS every few seconds while the user is approving
 * access on Google's consent page.  Queries the FujiNet relay service for
 * the authorization code keyed by STATE.  If found, exchanges it for tokens
 * directly with Google and stores them.
 *
 * Response JSON:
 *   { "status": "pending"    }  — relay hasn't received the code yet
 *   { "status": "authorized" }  — tokens obtained and stored
 *   { "status": "expired"    }  — relay TTL exceeded; user must restart
 *   { "status": "error", "message": "..." }  — unexpected failure
 */
esp_err_t fnHttpService::get_handler_gdrive_poll(httpd_req_t *req)
{
    auto send_json = [&](const char *status_val, const char *msg = nullptr) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "status", status_val);
        if (msg) cJSON_AddStringToObject(j, "message", msg);
        char *s = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, s, strlen(s));
        free(s);
    };

    // Extract ?state= from query string.
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    std::string qbuf(qlen, '\0');
    httpd_req_get_url_query_str(req, &qbuf[0], qlen);

    char state[32] = {};
    httpd_query_key_value(qbuf.c_str(), "state", state, sizeof(state));

    if (!state[0] || gdrive_auth_state.empty() || std::string(state) != gdrive_auth_state) {
        send_json("error", "state mismatch");
        return ESP_OK;
    }

    // Poll the relay for the finished tokens (relay does the exchange).
    std::string relay_url = std::string(GDRIVE_RELAY_CODE_URL) + state;
    std::string relay_body;
    int relay_status = gdrive_do_get(relay_url.c_str(), relay_body);

    if (relay_status < 0) {
        send_json("error", "relay unreachable");
        return ESP_OK;
    }

    cJSON *rj = cJSON_Parse(relay_body.c_str());
    if (!rj) { send_json("error", "bad relay response"); return ESP_OK; }

    cJSON *pending_node = cJSON_GetObjectItemCaseSensitive(rj, "pending");
    cJSON *expired_node = cJSON_GetObjectItemCaseSensitive(rj, "expired");
    cJSON *error_node   = cJSON_GetObjectItemCaseSensitive(rj, "error");
    cJSON *at_node      = cJSON_GetObjectItemCaseSensitive(rj, "access_token");
    cJSON *rt_node      = cJSON_GetObjectItemCaseSensitive(rj, "refresh_token");
    cJSON *ei_node      = cJSON_GetObjectItemCaseSensitive(rj, "expires_in");

    if (pending_node && cJSON_IsTrue(pending_node)) {
        cJSON_Delete(rj);
        send_json("pending");
        return ESP_OK;
    }
    if (expired_node && cJSON_IsTrue(expired_node)) {
        cJSON_Delete(rj);
        gdrive_auth_state.clear();
        send_json("expired");
        return ESP_OK;
    }
    if (error_node && cJSON_IsString(error_node)) {
        const char *msg = error_node->valuestring;
        Debug_printf("gdrive-poll: relay returned error: %s\n", msg);
        cJSON_Delete(rj);
        gdrive_auth_state.clear();
        send_json("error", msg);
        return ESP_OK;
    }
    if (!at_node || !cJSON_IsString(at_node)) {
        cJSON_Delete(rj);
        send_json("pending");
        return ESP_OK;
    }

    // Relay has exchanged the code — store the tokens.
    gdrive_auth_state.clear();
    if (cJSON_IsString(at_node)) Config.store_gdrive_access_token(at_node->valuestring);
    if (rt_node && cJSON_IsString(rt_node)) Config.store_gdrive_refresh_token(rt_node->valuestring);
    if (ei_node && cJSON_IsNumber(ei_node)) {
        long expiry = (long)time(nullptr) + (long)ei_node->valuedouble;
        Config.store_gdrive_token_expiry(expiry);
    }
    Config.save();
    cJSON_Delete(rj);

    send_json("authorized");
    return ESP_OK;
}

// ─── end Google Drive handlers ────────────────────────────────────────────────

// ─── OneDrive OAuth2 relay-based authorization-code-flow handlers ────────────
//
// Mirrors the Google Drive relay flow, targeting the Microsoft identity
// platform and Microsoft Graph.  The client_secret lives on the relay server
// (auth.fujinet.online) — never in firmware.  The relay redirect URI
// (registered in the FujiNet project's Entra app) is:
//   https://auth.fujinet.online/onedrive-callback
//
// TODO: replace ONEDRIVE_CLIENT_ID with the FujiNet project's Entra app client_id.
#define ONEDRIVE_CLIENT_ID          "c2835b53-9604-4277-8986-91520e8401be"
#define ONEDRIVE_RELAY_REDIRECT_URI "https://auth.fujinet.online/onedrive-callback"
#define ONEDRIVE_RELAY_CODE_URL     "https://auth.fujinet.online/onedrive-code?state="
#define ONEDRIVE_AUTH_ENDPOINT      "https://login.microsoftonline.com/common/oauth2/v2.0/authorize"
#define ONEDRIVE_SCOPE              "offline_access Files.ReadWrite"

static std::string onedrive_auth_state;

/**
 * GET /onedrive-auth
 *
 * Generates a CSRF state token and returns JSON with the full Microsoft
 * authorization URL pointing to the shared FujiNet relay redirect URI.
 *
 *   { "auth_url": "https://login.microsoftonline.com/...", "state": "XXXXXXXX" }
 */
esp_err_t fnHttpService::get_handler_onedrive_auth(httpd_req_t *req)
{
    uint32_t r = esp_random();
    char state[16];
    snprintf(state, sizeof(state), "%08lx", (unsigned long)r);
    onedrive_auth_state = state;

    std::string auth_url =
        ONEDRIVE_AUTH_ENDPOINT
        "?response_type=code"
        "&response_mode=query"
        "&client_id="    + gdrive_pct_encode(ONEDRIVE_CLIENT_ID) +
        "&redirect_uri=" + gdrive_pct_encode(ONEDRIVE_RELAY_REDIRECT_URI) +
        "&scope="        + gdrive_pct_encode(ONEDRIVE_SCOPE) +
        "&state="        + std::string(state);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "auth_url", auth_url.c_str());
    cJSON_AddStringToObject(out, "state",    state);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, s, strlen(s));
    free(s);
    return ESP_OK;
}

/**
 * GET /onedrive-poll?state=STATE
 *
 * Polls the FujiNet relay for the authorization code keyed by STATE and, once
 * the relay has exchanged it, stores the resulting tokens.  Response mirrors
 * /gdrive-poll: {status:"pending"|"authorized"|"expired"|"error"}.
 */
esp_err_t fnHttpService::get_handler_onedrive_poll(httpd_req_t *req)
{
    auto send_json = [&](const char *status_val, const char *msg = nullptr) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "status", status_val);
        if (msg) cJSON_AddStringToObject(j, "message", msg);
        char *s = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, s, strlen(s));
        free(s);
    };

    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    std::string qbuf(qlen, '\0');
    httpd_req_get_url_query_str(req, &qbuf[0], qlen);

    char state[32] = {};
    httpd_query_key_value(qbuf.c_str(), "state", state, sizeof(state));

    if (!state[0] || onedrive_auth_state.empty() || std::string(state) != onedrive_auth_state) {
        send_json("error", "state mismatch");
        return ESP_OK;
    }

    std::string relay_url = std::string(ONEDRIVE_RELAY_CODE_URL) + state;
    std::string relay_body;
    int relay_status = gdrive_do_get(relay_url.c_str(), relay_body);

    if (relay_status < 0) {
        send_json("error", "relay unreachable");
        return ESP_OK;
    }

    cJSON *rj = cJSON_Parse(relay_body.c_str());
    if (!rj) { send_json("error", "bad relay response"); return ESP_OK; }

    cJSON *pending_node = cJSON_GetObjectItemCaseSensitive(rj, "pending");
    cJSON *expired_node = cJSON_GetObjectItemCaseSensitive(rj, "expired");
    cJSON *error_node   = cJSON_GetObjectItemCaseSensitive(rj, "error");
    cJSON *at_node      = cJSON_GetObjectItemCaseSensitive(rj, "access_token");
    cJSON *rt_node      = cJSON_GetObjectItemCaseSensitive(rj, "refresh_token");
    cJSON *ei_node      = cJSON_GetObjectItemCaseSensitive(rj, "expires_in");

    if (pending_node && cJSON_IsTrue(pending_node)) {
        cJSON_Delete(rj);
        send_json("pending");
        return ESP_OK;
    }
    if (expired_node && cJSON_IsTrue(expired_node)) {
        cJSON_Delete(rj);
        onedrive_auth_state.clear();
        send_json("expired");
        return ESP_OK;
    }
    if (error_node && cJSON_IsString(error_node)) {
        const char *msg = error_node->valuestring;
        Debug_printf("onedrive-poll: relay returned error: %s\n", msg);
        cJSON_Delete(rj);
        onedrive_auth_state.clear();
        send_json("error", msg);
        return ESP_OK;
    }
    if (!at_node || !cJSON_IsString(at_node)) {
        cJSON_Delete(rj);
        send_json("pending");
        return ESP_OK;
    }

    onedrive_auth_state.clear();
    if (cJSON_IsString(at_node)) Config.store_onedrive_access_token(at_node->valuestring);
    if (rt_node && cJSON_IsString(rt_node)) Config.store_onedrive_refresh_token(rt_node->valuestring);
    if (ei_node && cJSON_IsNumber(ei_node)) {
        long expiry = (long)time(nullptr) + (long)ei_node->valuedouble;
        Config.store_onedrive_token_expiry(expiry);
    }
    Config.save();
    cJSON_Delete(rj);

    send_json("authorized");
    return ESP_OK;
}

// ─── end OneDrive handlers ────────────────────────────────────────────────────

/*
 * REST API - single catch-all handler
 * Routing and logic live in httpServiceApi.cpp, shared with the mongoose
 * server; this is only the esp_http_server plumbing.
 */

// Map a status code to the string esp_http_server wants.
static const char *api_status_str(int status_code)
{
    switch (status_code)
    {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 500: return "500 Internal Server Error";
    default:  return "200 OK";
    }
}

esp_err_t fnHttpService::api_handler(httpd_req_t *req)
{
    fnHttpApi::method m = fnHttpApi::method::OTHER;
    if (req->method == HTTP_GET)
        m = fnHttpApi::method::GET;
    else if (req->method == HTTP_POST)
        m = fnHttpApi::method::POST;

    char buf[FNWS_RECV_BUFF_SIZE];
    int received = 0;
    if (m == fnHttpApi::method::POST && req->content_len > 0)
    {
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret > 0)
        {
            buf[ret] = '\0';
            received = ret;
        }
    }

    fnHttpApi::response r = fnHttpApi::handle(req->uri, m,
                                              received > 0 ? buf : nullptr,
                                              (size_t)received);

    httpd_resp_set_status(req, api_status_str(r.status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, r.body.c_str(), r.body.size());
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
        {.uri = "/hsdir",
         .method = HTTP_GET,
         .handler = get_handler_dir,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/dslot",
         .method = HTTP_GET,
         .handler = get_handler_slot,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/",
         .method = HTTP_GET,
         .handler = get_handler_index,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/file",
         .method = HTTP_GET,
         .handler = get_handler_file_in_query,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/print",
         .method = HTTP_GET,
         .handler = get_handler_print,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/modem-sniffer.txt",
         .method = HTTP_GET,
         .handler = get_handler_modem_sniffer,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/favicon.ico",
         .method = HTTP_GET,
         .handler = get_handler_file_in_path,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/mount",
         .method = HTTP_GET,
         .handler = get_handler_mount,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/unmount",
         .method = HTTP_GET,
         .handler = get_handler_eject,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/hosts",
         .method = HTTP_GET,
         .handler = get_handler_hosts,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/hosts",
         .method = HTTP_POST,
         .handler = post_handler_hosts,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/url/*",
         .method = HTTP_GET,
         .handler = get_handler_shorturl,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
#ifdef BUILD_ADAM
        {.uri = "/term",
         .method = HTTP_GET,
         .handler = get_handler_term,
         .user_ctx = NULL,
         .is_websocket = true,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
#endif
        {.uri = "/config",
         .method = HTTP_POST,
         .handler = post_handler_config,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/clipboard",
         .method = HTTP_GET,
         .handler = get_handler_clipboard,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/clipboard",
         .method = HTTP_POST,
         .handler = post_handler_clipboard,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/password",
         .method = HTTP_POST,
         .handler = post_handler_password,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/clipboard/data",
         .method = HTTP_GET,
         .handler = get_handler_clipboard_data,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/clipboard/clear",
         .method = HTTP_POST,
         .handler = post_handler_clipboard_clear,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/appkeys",
         .method = HTTP_GET,
         .handler = get_handler_appkeys,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/clipboard/restore",
         .method = HTTP_POST,
         .handler = post_handler_clipboard_restore,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/appkeys",
         .method = HTTP_POST,
         .handler = post_handler_appkeys,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/files",
         .method = HTTP_GET,
         .handler = get_handler_files,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/files/download",
         .method = HTTP_GET,
         .handler = get_handler_files_download,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/files/action",
         .method = HTTP_POST,
         .handler = post_handler_files_action,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/files/upload",
         .method = HTTP_POST,
         .handler = post_handler_files_upload,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/gdrive-auth",
         .method = HTTP_GET,
         .handler = get_handler_gdrive_auth,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/gdrive-poll",
         .method = HTTP_GET,
         .handler = get_handler_gdrive_poll,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/onedrive-auth",
         .method = HTTP_GET,
         .handler = get_handler_onedrive_auth,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/onedrive-poll",
         .method = HTTP_GET,
         .handler = get_handler_onedrive_poll,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        // REST API - one catch-all per method; routing lives in httpServiceApi.cpp.
        // It has to be a catch-all: httpd_uri_match_wildcard only treats a
        // trailing '*' as a wildcard, so a template with a wildcard mid-path is
        // compared literally and never matches a real request.
        {.uri = FN_API_ROOT "/*",
         .method = HTTP_GET,
         .handler = api_handler,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = FN_API_ROOT "/*",
         .method = HTTP_POST,
         .handler = api_handler,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/login",
         .method = HTTP_GET,
         .handler = get_handler_login,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/login",
         .method = HTTP_POST,
         .handler = post_handler_login,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr},
        {.uri = "/logout",
         .method = HTTP_POST,
         .handler = post_handler_logout,
         .user_ctx = NULL,
         .is_websocket = false,
         .handle_ws_control_frames = false,
         .supported_subprotocol = nullptr}};

    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    // Set filesystem where we expect to find our static files
    state._FS = &fsFlash;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 12; // Bump this higher than fnService loop
    config.core_id = 0; // Pin to CPU core 0
    config.stack_size = 12288;
    // Budget: 35 routes registered here + 11 WebDAV = 46 handlers
    config.max_uri_handlers = 64;
    config.max_resp_headers = 16;
    config.keep_alive_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Keep a reference to our object
    config.global_user_ctx = (void *)&state;
    // Set our own global_user_ctx free function, otherwise the library will free an object we don't want freed
    config.global_user_ctx_free_fn = (httpd_free_ctx_fn_t)custom_global_ctx_free;

    Debug_printf("Starting web server on port %d\n", config.server_port);
    //Debug_printf("Starting web server on port %d, CPU Core %d\n", config.server_port, config.core_id);

    if (httpd_start(&(state.hServer), &config) == ESP_OK)
    {
        // Register URI handlers with authentication dispatch trampoline
        size_t i = 0;
        for (httpd_uri_t uridef : uris)   // note: a copy, so it can be modified
        {
            if (i >= sizeof(s_real_handlers) / sizeof(s_real_handlers[0]))
            {
                Debug_println("httpServiceInit: too many URI handlers - route dropped");
                break;
            }
            s_real_handlers[i] = uridef.handler;
            uridef.handler = auth_dispatch;
            uridef.user_ctx = &s_real_handlers[i];
            esp_err_t e = httpd_register_uri_handler(state.hServer, &uridef);
            if (e != ESP_OK)
                Debug_printf("httpServiceInit: failed to register %s (%d)\r\n", uridef.uri, (int)e);
            i++;
        }

        // Register WebDAV handlers
        webdav_register(state.hServer, "/dav", "/sd");
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
    // esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &(state.hServer));
    // esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &(state.hServer));

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
