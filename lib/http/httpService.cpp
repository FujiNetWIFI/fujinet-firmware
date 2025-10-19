
#include "fnHttpClient.h"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpService.h"

#include <sstream>
#include <vector>

#include "../../include/debug.h"

// WebDAV
#include "webdav/webdav_server.h"
#include "webdav/request.h"
#include "webdav/response.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "../device/modem.h"
#include "printer.h"
#include "httpServiceConfigurator.h"
#include "httpServiceParser.h"
#include "fuji.h"

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

/* Converts an integer value to its hex character*/
char to_hex(char code)
{
    static char hex[] = "0123456789abcdef";
    return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(char *str)
{
    char *pstr = str, *buf = (char *)malloc(strlen(str) * 3 + 1), *pbuf = buf;
    while (*pstr)
    {
        if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
            *pbuf++ = *pstr;
        else if (*pstr == ' ')
            *pbuf++ = '+';
        else
            *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
        pstr++;
    }
    *pbuf = '\0';
    return buf;
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

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_mount(httpd_req_t *req)
{
    queryparts qp;
    unsigned char hs, ds;
    char flag[3] = {'r', 0, 0};
    fnConfig::mount_mode_t mode = fnConfig::mount_modes::MOUNTMODE_READ;

    fnHTTPD.clearErrMsg();

    parse_query(req, &qp);

    // if request contains 'mountall=1' skip to mounting all disks
    if ((qp.query_parsed.find("mountall") == qp.query_parsed.end()) && (qp.query_parsed["mountall"] != "1"))
    {
        if (qp.query_parsed.find("hostslot") == qp.query_parsed.end())
        {
            fnHTTPD.addToErrMsg("<li>hostslot is empty</li>");
        }

        if (qp.query_parsed.find("deviceslot") == qp.query_parsed.end())
        {
            fnHTTPD.addToErrMsg("<li>deviceslot is empty</li>");
        }

        if (qp.query_parsed.find("mode") == qp.query_parsed.end())
        {
            fnHTTPD.addToErrMsg("<li>mode is empty</li>");
        }

        if (qp.query_parsed.find("filename") == qp.query_parsed.end())
        {
            fnHTTPD.addToErrMsg("<li>filename is empty</li>");
        }

        hs = atoi(qp.query_parsed["hostslot"].c_str());
        ds = atoi(qp.query_parsed["deviceslot"].c_str());

        if (hs > MAX_HOSTS)
        {
            fnHTTPD.addToErrMsg("<li>hostslot must be between 0 and 8</li>");
        }

        if (ds > MAX_DISK_DEVICES)
        {
            fnHTTPD.addToErrMsg("<li>deviceslot must be between 0 and 8</li>");
        }

        if ((qp.query_parsed["mode"] != "1") && (qp.query_parsed["mode"] != "2"))
        {
            fnHTTPD.addToErrMsg("<li>mode should be either 1 for read, or 2 for write.</li>");
        }

        if (qp.query_parsed["mode"] == "2")
        {
            flag[1] = '+';
            mode = fnConfig::mount_modes::MOUNTMODE_WRITE;
        }

        if (theFuji.get_hosts(hs)->mount() == true)
        {
            fujiDisk *disk = theFuji.get_disks(ds);
            fujiHost *host = theFuji.get_hosts(hs);
#ifdef BUILD_APPLE
            DEVICE_TYPE *disk_dev = theFuji.get_disk_dev(ds);
#else
            DEVICE_TYPE *disk_dev = &disk->disk_dev;
#endif

            disk->fileh = host->fnfile_open(qp.query_parsed["filename"].c_str(), (char *)qp.query_parsed["filename"].c_str(), qp.query_parsed["filename"].length() + 1, flag);

            if (disk->fileh == nullptr)
            {
                fnHTTPD.addToErrMsg("<li>Could not open file: " + qp.query_parsed["filename"] + "</li>");
            }
            else
            {
                // Make sure CONFIG boot is disabled.
                theFuji.boot_config = false;
#ifdef BUILD_ATARI
                theFuji.status_wait_count = 0;
#endif
                strcpy(disk->filename,qp.query_parsed["filename"].c_str());
                disk->disk_size = host->file_size(disk->fileh);
                disk->disk_type = disk_dev->mount(disk->fileh, disk->filename, disk->disk_size);
                #ifdef BUILD_APPLE
                if(mode == fnConfig::mount_modes::MOUNTMODE_WRITE) {disk_dev->readonly = false;}
                #endif
                Config.store_mount(ds, hs, qp.query_parsed["filename"].c_str(), mode);
                Config.save();
                theFuji._populate_slots_from_config(); // otherwise they don't show up in config.
                disk_dev->device_active = true;
            }
        }
        else
        {
            fnHTTPD.addToErrMsg("<li>Could not mount host slot " + qp.query_parsed["hostslot"] + "</li>");
        }
    }
    else
    {
        // Mount all the things
        Debug_printf("Mount all slots from webui\n");
        theFuji.mount_all();
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
    parse_query(req, &qp);
    unsigned char ds;

    if (qp.query_parsed.find("deviceslot") == qp.query_parsed.end())
    {
        fnHTTPD.addToErrMsg("<li>deviceslot is empty</li>");
    }

    ds = atoi(qp.query_parsed["deviceslot"].c_str());

    if (ds > MAX_DISK_DEVICES)
    {
        fnHTTPD.addToErrMsg("<li>deviceslot should be between 0 and 7</li>");
    }
#ifdef BUILD_APPLE
    DEVICE_TYPE *disk_dev = theFuji.get_disk_dev(ds);
    if(disk_dev->device_active) //set disk switched only if device was previosly mounted.
        disk_dev->switched = true;
#else
    DEVICE_TYPE *disk_dev = &theFuji.get_disks(ds)->disk_dev;
#endif
    disk_dev->unmount();
#ifdef BUILD_ATARI
    if (theFuji.get_disks(ds)->disk_type == MEDIATYPE_CAS || theFuji.get_disks(ds)->disk_type == MEDIATYPE_WAV)
    {
        theFuji.cassette()->umount_cassette_file();
        theFuji.cassette()->sio_disable_cassette();
    }
#endif
    theFuji.get_disks(ds)->reset();
    Config.clear_mount(ds);
    Config.save();
    theFuji._populate_slots_from_config(); // otherwise they don't show up in config.
    disk_dev->device_active = false;

    // Finally, scan all device slots, if all empty, and config enabled, enable the config device.
    if (Config.get_general_config_enabled())
    {
        if ((theFuji.get_disks(0)->host_slot == 0xFF) &&
            (theFuji.get_disks(1)->host_slot == 0xFF) &&
            (theFuji.get_disks(2)->host_slot == 0xFF) &&
            (theFuji.get_disks(3)->host_slot == 0xFF) &&
            (theFuji.get_disks(4)->host_slot == 0xFF) &&
            (theFuji.get_disks(5)->host_slot == 0xFF) &&
            (theFuji.get_disks(6)->host_slot == 0xFF) &&
            (theFuji.get_disks(7)->host_slot == 0xFF))
        {
            theFuji.boot_config = true;
#ifdef BUILD_ATARI
            theFuji.status_wait_count = 5;
#endif
            theFuji.device_active = true;
        }
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
    unsigned char hs;
    string pattern;
    string chunk;
    char *free_me; //return string from url_encode which must be freed.
    parse_query(req, &qp);

    if (qp.query_parsed.find("hostslot") == qp.query_parsed.end())
    {
        fnHTTPD.addToErrMsg("<li>hostslot is empty</li>");
    }

    if (qp.query_parsed.find("path") == qp.query_parsed.end())
    {
        qp.query_parsed["path"] = "";
    }

    hs = atoi(qp.query_parsed["hostslot"].c_str());

    if (qp.query_parsed.find("pattern") == qp.query_parsed.end())
    {
        pattern = "*";
    }
    else
    {
        pattern = qp.query_parsed["pattern"];
    }

    chunk.clear();

    httpd_resp_set_type(req, "text/html");

    send_header_footer(req, 0); // header

    chunk +=
        "        <div class=\"fileflex\">\n"
        "            <div class=\"filechild\">\n"
        "               <header>SELECT DISK TO MOUNT<span class=\"logowob\"></span>" +
        string(theFuji.get_hosts(hs)->get_hostname()) +
        qp.query_parsed["path"] +
        "</header>\n"
        "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
        "               <div class=\"fileline\">\n"
        "                   <ul>\n";

    httpd_resp_sendstr_chunk(req, chunk.c_str());
    chunk.clear();

    theFuji._populate_slots_from_config();

    if ((theFuji.get_hosts(hs)->mount() == true) && (theFuji.get_hosts(hs)->dir_open(qp.query_parsed["path"].c_str(), pattern.c_str())))
    {
        fsdir_entry_t *f;
        string parent;

        // Create link to parent
        if (!qp.query_parsed["path"].empty())
        {
            parent = qp.query_parsed["path"].substr(0, qp.query_parsed["path"].find_last_of("/"));
            free_me = url_encode((char *)parent.c_str());
            chunk += "<a href=\"/hsdir?hostslot=" + qp.query_parsed["hostslot"] + "&path=" + string(free_me) + "\"><li>&#8617; Parent</li></a>";
            free(free_me);
        }

        while ((f = theFuji.get_hosts(hs)->dir_nextfile()) != nullptr)
        {
            chunk += "                          <li>";

            if (f->isDir == true)
            {
                free_me = url_encode((char *)qp.query_parsed["path"].c_str());
                chunk += "<a href=\"/hsdir?hostslot=" + qp.query_parsed["hostslot"] + "&path=" + string(free_me);
                free(free_me);
                free_me = url_encode(f->filename);
                chunk += "%2F" + string(free_me) + "&parent_path=";
                free(free_me);
                free_me = url_encode((char *)qp.query_parsed["path"].c_str());
                chunk += string(free_me) + "\">";
                chunk += "&#128193; "; // file folder
                free(free_me);
            }
            else
            {
                free_me = url_encode((char *)qp.query_parsed["path"].c_str());
                chunk += "<a href=\"/dslot?hostslot=" + qp.query_parsed["hostslot"] + "&filename=" + string(free_me);
                free(free_me);
                free_me = url_encode(f->filename);
                chunk += "%2F" + string(free_me) + "\">";
                free(free_me);

                if ( // Atari
                    (string(f->filename).find(".atr") != string::npos) ||
                    (string(f->filename).find(".ATR") != string::npos) ||
                    (string(f->filename).find(".atx") != string::npos) ||
                    (string(f->filename).find(".ATX") != string::npos) ||
                    // Apple II
                    (string(f->filename).find(".po") != string::npos) ||
                    (string(f->filename).find(".PO") != string::npos) ||
                    (string(f->filename).find(".woz") != string::npos) ||
                    (string(f->filename).find(".WOZ") != string::npos) ||
                    (string(f->filename).find(".hdv") != string::npos) || // Hard Disk emoji not implemented
                    (string(f->filename).find(".HDV") != string::npos) || // Hard Disk emoji not implemented
                    // ADAM
                    (string(f->filename).find(".dsk") != string::npos) ||
                    (string(f->filename).find(".DSK") != string::npos) ||
                    // Commodore
                    (string(f->filename).find(".prg") != string::npos) ||
                    (string(f->filename).find(".PRG") != string::npos) ||
                    (string(f->filename).find(".d64") != string::npos) ||
                    (string(f->filename).find(".D64") != string::npos)
                    )
                {
                    chunk += "&#128190; "; // floppy disk
                }
                else if ( // ATARI
                    (string(f->filename).find(".cas") != string::npos) ||
                    (string(f->filename).find(".CAS") != string::npos) ||
                    // ADAM
                    (string(f->filename).find(".ddp") != string::npos) ||
                    (string(f->filename).find(".DDP") != string::npos)
                    )
                {
                    chunk += "&#10175; "; // cassette tape (double curly loop)
                }
                else
                {
                    chunk += "&#128196; "; // std document (page facing up)
                }
            }

            chunk += string(f->filename);

            chunk += "</a>";

            chunk += "                          </li>\r\n";

            httpd_resp_sendstr_chunk(req, chunk.c_str());
            chunk.clear();
        }

        theFuji.get_hosts(hs)->dir_close();

        chunk +=
            "                      </ul>\r\n"
            "               </div>\n"
            "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
            "           </div>\n"
            "        </div>\n";
        httpd_resp_sendstr_chunk(req, chunk.c_str());
        chunk.clear();

        // Send HTML footer
        send_header_footer(req, 1);

        httpd_resp_send_chunk(req, NULL, 0); // end of response.
    }
    else
    {
        fnHTTPD.addToErrMsg("<li>Could not open directory</li>");
        send_file(req, "error_page.html");
    }

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_slot(httpd_req_t *req)
{
    queryparts qp;
    string chunk;
    unsigned char hs;
    char *free_me; // return string from url_encode that must be freed.
    chunk.clear();
    parse_query(req, &qp);

    if ((qp.query_parsed.find("hostslot") == qp.query_parsed.end()) ||
        (qp.query_parsed.find("filename") == qp.query_parsed.end()))
    {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    hs = atoi(qp.query_parsed["hostslot"].c_str());

    httpd_resp_set_type(req, "text/html");

    if ((qp.query_parsed["filename"].find(".cas") != string::npos) ||
        qp.query_parsed["filename"].find(".CAS") != string::npos)
    {
        // .CAS file passed in, put in slot 8, and redirect
        chunk += "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
        chunk += "<!doctype html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"DTD/xhtml1-strict.dtd\">\r\n";
        chunk += "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\r\n";
        chunk += " <head>\r\n";
        chunk += "  <title>Redirecting to cassette mount</title>";
        free_me = url_encode((char *)qp.query_parsed["filename"].c_str());
        chunk += "  <meta http-equiv=\"refresh\" content=\"0; url=/mount?hostslot=" + qp.query_parsed["hostslot"] + "&deviceslot=7&mode=1&filename=" + string(free_me) + "\" />";
        free(free_me);
        chunk += " </head>\r\n";
        chunk += " <body>\r\n";
        chunk += "  <h1>Cassette detected. Mounting in slot 8.</h1>\r\n";
        chunk += " </body>\r\n";
        chunk += "</html>\r\n";

        httpd_resp_sendstr(req, chunk.c_str());

        return ESP_OK;
    }

    send_header_footer(req, 0); // header

    // chunk += "  <h1></h1>\r\n";
    chunk +=
        "        <div class=\"fileflex\">\n"
        "            <div class=\"filechild\">\n"
        "               <header>SELECT DRIVE SLOT<span class=\"logowob\"></span>" +
        string(theFuji.get_hosts(hs)->get_hostname()) + " :: " + qp.query_parsed["filename"] +
        "</header>\n"
        "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
        "               <div class=\"fileline\">\n"
        "                      <ul>\n";

    httpd_resp_sendstr_chunk(req, chunk.c_str());
    chunk.clear();

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        stringstream ss;
        stringstream ss2;
        ss << i;
        ss2 << i + 1;

        chunk += "<li>&#128190; <a href=\"/mount?hostslot=" + qp.query_parsed["hostslot"] + "&deviceslot=" + ss.str() + "&mode=1&filename=" + qp.query_parsed["filename"] + "\">READ</a> or ";
        chunk += "<a href=\"/mount?hostslot=" + qp.query_parsed["hostslot"] + "&deviceslot=" + ss.str() + "&mode=2&filename=" + qp.query_parsed["filename"] + "\">R/W</a> ";

        chunk += "<strong>" + ss2.str() + "</strong>: ";

        if (theFuji.get_disks(i)->host_slot == 0xFF)
        {
            chunk += " :: (Empty)";
        }
        else
        {
            chunk += string(theFuji.get_hosts(theFuji.get_disks(i)->host_slot)->get_hostname());
            chunk += " :: ";
            chunk += string(theFuji.get_disks(i)->filename);
            chunk += " (";
            if (theFuji.get_disks(i)->access_mode == 2)
            {
                chunk += "W";
            }
            else
            {
                chunk += "R";
            }
            chunk += ") ";
        }

        chunk += "</li>";
        httpd_resp_sendstr_chunk(req, chunk.c_str());
        chunk.clear();
    }

    chunk +=
        "                      </ul>\r\n"
        "               </div>\n"
        "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
        "           </div>\n"
        "        </div>\n";

    send_header_footer(req, 1);          // footer
    httpd_resp_send_chunk(req, NULL, 0); // end response.

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_hosts(httpd_req_t *req)
{
    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji.get_hosts(hs)->get_hostname()) + "\n";
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

    theFuji.set_slot_hostname(hostslot, hostname);

    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji.get_hosts(hs)->get_hostname()) + "\n";
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



esp_err_t fnHttpService::webdav_handler(httpd_req_t *httpd_req)
{
    WebDav::Server *server = (WebDav::Server *)httpd_req->user_ctx;
    WebDav::Request req(httpd_req);
    WebDav::Response resp(httpd_req);
    int ret;

    //Debug_printv("url[%s]", httpd_req->uri);

    if (!req.parseRequest())
    {
        resp.setStatus(400); // Bad Request
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

    Debug_printv("%d %s[%s]", httpd_req->method, http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

    switch (httpd_req->method)
    {
    case HTTP_COPY:
        ret = server->doCopy(req, resp);
        break;
    case HTTP_DELETE:
        ret = server->doDelete(req, resp);
        break;
    case HTTP_GET:
        ret = server->doGet(req, resp);
        if ( ret == 200 )
            return ESP_OK;
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
        break;
    case HTTP_MKCOL:
        ret = server->doMkcol(req, resp);
        break;
    case HTTP_MOVE:
        ret = server->doMove(req, resp);
        break;
    case HTTP_OPTIONS:
        ret = server->doOptions(req, resp);
        break;
    case HTTP_PROPFIND:
        ret = server->doPropfind(req, resp);
        if (ret == 207)
            return ESP_OK;
        break;
    case HTTP_PROPPATCH:
        ret = server->doProppatch(req, resp);
        break;
    case HTTP_PUT:
        ret = server->doPut(req, resp);
        break;
    case HTTP_UNLOCK:
        ret = server->doUnlock(req, resp);
        break;
    default:
        return ESP_ERR_HTTPD_INVALID_REQ;
        break;
    }

    resp.setStatus(ret);

    if ( (ret > 399) & (httpd_req->method != HTTP_HEAD) )
    {
        // Send error
        httpd_resp_send(httpd_req, NULL, 0);
    }
    else
    {
        // Send empty response
        resp.setHeader("Connection","close");
        resp.flushHeaders();
        resp.closeBody();
    }

    Debug_printv("ret[%d]", ret);

    return ESP_OK;
}


void fnHttpService::webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path)
{
    WebDav::Server *webDavServer = new WebDav::Server(root_uri, root_path);

    char *uri;
    asprintf(&uri, "%s/?*", root_uri);

    httpd_uri_t uri_dav = {
        .uri = uri,
        .method = http_method(0),
        .handler = webdav_handler,
        .user_ctx = webDavServer,
        .is_websocket = false
    };

    http_method methods[] = {
        HTTP_COPY,
        HTTP_DELETE,
        HTTP_GET,
        HTTP_HEAD,
        HTTP_LOCK,
        HTTP_MKCOL,
        HTTP_MOVE,
        HTTP_OPTIONS,
        HTTP_PROPFIND,
        HTTP_PROPPATCH,
        HTTP_PUT,
        HTTP_UNLOCK,
    };

    for (int i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    {
        uri_dav.method = methods[i];
        httpd_register_uri_handler(server, &uri_dav);
    }
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
    config.stack_size = 8192;
    config.max_uri_handlers = 32;
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
        // Register URI handlers
        for (const httpd_uri_t uridef : uris)
            httpd_register_uri_handler(state.hServer, &uridef);

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
