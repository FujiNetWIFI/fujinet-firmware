/*
 * mongoose based version (of httpService.cpp) for FujiNet-PC
 */

#ifndef ESP_PLATFORM
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "modem.h"
#include "printer.h"
#include "fujiDevice.h"

#include "mongoose.h"
#include "httpService.h"
#include "httpServiceConfigurator.h"
#include "httpServiceParser.h"
#include "httpServiceBrowser.h"

#include "../../include/debug.h"


using namespace std;

// Global HTTPD
fnHttpService fnHTTPD;

/* Send some meaningful(?) error message to client
*/
void fnHttpService::return_http_error(struct mg_connection *c, _fnwserr errnum)
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
    // httpd_resp_send(req, message, strlen(message));
    mg_http_reply(c, 400, "", "%s\n", message);
}

const char *fnHttpService::find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map{
        {"html", "text/html"},
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
        {"com", "application/octet-stream"},
        {"bin", "application/octet-stream"},
        {"exe", "application/octet-stream"},
        {"xex", "application/octet-stream"},
        {"atr", "application/octet-stream"},
        {"atx", "application/octet-stream"},
        {"cas", "application/octet-stream"},
        {"tur", "application/octet-stream"},
        {"wav", "audio/wav"},
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

const char *fnHttpService::get_extension(const char *filename)
{
    const char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

const char *fnHttpService::get_basename(const char *filepath)
{
    const char *result = strrchr(filepath, '/');
    if (result != NULL)
        return ++result;
    return filepath;
}

/* Set the response content type based on the file being sent.
*  Just using the file extension
*  If nothing is set here, the default is 'text/html'
*/
void fnHttpService::set_file_content_type(struct mg_connection *c, const char *filepath)
{
    // Find the current file extension
    const char *dot = get_extension(filepath);
    if (dot != NULL)
    {
        const char *mimetype = find_mimetype_str(dot);
        if (mimetype)
            mg_printf(c, "Content-Type: %s\r\n", mimetype);
    }
}

/* Send content of given file out to client
*/
void fnHttpService::send_file_parsed(struct mg_connection *c, const char *filename)
{
    Debug_printf("Opening file for parsing: '%s'\n", filename);

    _fnwserr err = fnwserr_noerrr;

    // Retrieve server state
    serverstate *pState = &fnHTTPD.state; // ops TODO
    FILE *fInput = pState->_FS->file_open(filename);

    if (fInput == nullptr)
    {
        Debug_println("Failed to open file for parsing");
        err = fnwserr_fileopen;
    }
    else
    {
        // We're going to load the whole thing into memory, so watch out for big files!
        size_t sz = FileSystem::filesize(fInput) + 1;
        char *buf = (char *)calloc(sz, 1);
        if (buf == NULL)
        {
            Debug_printf("Couldn't allocate %u bytes to load file contents!\n", (unsigned)sz);
            err = fnwserr_memory;
        }
        else
        {
            size_t bytes_read = fread(buf, 1, sz - 1, fInput); // sz - 1 because we added 1 for null terminator
            if (bytes_read < (sz - 1)) {
                Debug_printf("Warning: Only read %u of %u bytes from file\n", (unsigned)bytes_read, (unsigned)(sz - 1));
            }
            string contents(buf);
            free(buf);
            contents = fnHttpServiceParser::parse_contents(contents);

            mg_printf(c, "HTTP/1.1 200 OK\r\n");
            // Set the response content type
            set_file_content_type(c, filename);
            // Set the expected length of the content
            size_t len = contents.length();
            mg_printf(c, "Content-Length: %lu\r\n\r\n", (unsigned long)len);
            // Send parsed content
            mg_send(c, contents.c_str(), len);
        }
    }

    if (fInput != nullptr)
        fclose(fInput);

    if (err != fnwserr_noerrr)
        return_http_error(c, err);
}

/* Send file content after parsing for replaceable strings
*/
void fnHttpService::send_file(struct mg_connection *c, const char *filename)
{
    // Debug_printf("send_file '%s'\r\n", filename);

    // Build the full file path
    string fpath = FNWS_FILE_ROOT;
    // Trim any '/' prefix before adding it to the base directory
    while (*filename == '/')
        filename++;
    fpath += filename;

    // Handle file differently if it's one of the types we parse
    if (fnHttpServiceParser::is_parsable(get_extension(filename)))
    {
        send_file_parsed(c, fpath.c_str());
        return;
    }

    // Retrieve server state
    serverstate *pState = &fnHTTPD.state; // ops TODO

    FILE *fInput = pState->_FS->file_open(fpath.c_str());
    if (fInput == nullptr)
    {
        Debug_printf("Failed to open file for sending: '%s'\n", fpath.c_str());
        return_http_error(c, fnwserr_fileopen);
    }
    else
    {
        mg_printf(c, "HTTP/1.1 200 OK\r\n");
        // Set the response content type
        set_file_content_type(c, fpath.c_str());
        // Set the expected length of the content
        mg_printf(c, "Content-Length: %lu\r\n\r\n", (unsigned long)FileSystem::filesize(fInput));

        // Send the file content out in chunks
        char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
        size_t count = 0;
        do
        {
            count = fread((uint8_t *)buf, 1, FNWS_SEND_BUFF_SIZE, fInput);
            if (count > 0) mg_send(c, buf, count);
        } while (count > 0);
        free(buf);
        fclose(fInput);
    }
}

int fnHttpService::redirect_or_result(mg_connection *c, mg_http_message *hm, int result)
{
    // get "redirect" query variable
    char redirect[10] = "";
    mg_http_get_var(&hm->query, "redirect", redirect, sizeof(redirect));
    if (atoi(redirect))
    {
        // Redirect back to the main page
        mg_printf(c, "HTTP/1.1 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n");
    }
    else
    {
        mg_http_reply(c, 200, "", "{\"result\": %d}\n", result); // send reply
    }
    return result;
}

int fnHttpService::get_handler_print(struct mg_connection *c)
{
    Debug_println("Print request handler");

    uint64_t now = fnSystem.millis();
    // Get a pointer to the current (only) printer
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);

    if (now - printer->lastPrintTime() < PRINTER_BUSY_TIME)
    {
        _fnwserr err = fnwserr_post_fail;
        return_http_error(c, err);
        return -1; //ESP_FAIL;
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

    // Tell printer to finish its output and get a read handle to the file
    FILE *poutput = currentPrinter->closeOutputAndProvideReadHandle();
    if (poutput == nullptr)
    {
        Debug_printf("Unable to open printer output\n");
        return_http_error(c, fnwserr_fileopen);
        return -1; //ESP_FAIL;
    }

    // Set the expected content type based on the filename/extension
    mg_printf(c, "HTTP/1.1 200 OK\r\n");
    set_file_content_type(c, filename.c_str());

    // char hdrval1[60];
    if (sendAsAttachment)
    {
        // Add a couple of attchment-specific details
        mg_printf(c, "Content-Disposition: attachment; filename=\"%s\"\r\n", filename.c_str());
    }
    mg_printf(c, "Content-Length: %lu\r\n\r\n", (unsigned long)FileSystem::filesize(poutput));

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

        mg_send(c, buf, count);
    } while (count > 0);

    Debug_printf("Sent %u bytes total from print file\n", (unsigned)total);

    free(buf);
    fclose(poutput);

    // Tell the printer it can start writing from the beginning
    printer->reset_printer(); // destroy,create new printer emulator object of previous type.

    Debug_println("Print request completed");

    return 0; //ESP_OK;
}

int fnHttpService::post_handler_config(struct mg_connection *c, struct mg_http_message *hm)
{

    Debug_println("Post_config request handler");

    _fnwserr err = fnwserr_noerrr;

    if (fnHttpServiceConfigurator::process_config_post(hm->body.buf, hm->body.len) < 0)
    {
        return_http_error(c, fnwserr_post_fail);
        return -1; //ESP_FAIL;
    }

    // Redirect back to the main page
    mg_printf(c, "HTTP/1.1 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n");

    return 0; //ESP_OK;
}


int fnHttpService::get_handler_browse(mg_connection *c, mg_http_message *hm)
{
    const char prefix[] = "/browse/host/";
    int prefixlen = sizeof(prefix) - 1;
    int pathlen = hm->uri.len - prefixlen -1;

    Debug_println("Browse request handler");
    if (pathlen >= 0 && strncmp(hm->uri.buf, prefix, hm->uri.len))
    {
        const char *s = hm->uri.buf + prefixlen;
        // /browse/host/{1..8}[/path/on/host...]
        if (*s >= '1' && *s <= '8' && (pathlen == 0 || s[1] == '/'))
        {
            int host_slot = *s - '1';
            fnHttpServiceBrowser::process_browse_get(c, hm, host_slot, s+1, pathlen);
        }
        else
        {
            mg_http_reply(c, 403, "", "Bad host slot\n");
        }
    }
    else
    {
        mg_http_reply(c, 403, "", "Bad browse request\n");
    }

    return 0;
}

int fnHttpService::get_handler_swap(mg_connection *c, mg_http_message *hm)
{
    // rotate disk images
    Debug_printf("Disk swap from webui\n");
    theFuji->image_rotate();
    return redirect_or_result(c, hm, 0);
}

int fnHttpService::get_handler_mount(mg_connection *c, mg_http_message *hm)
{
    char mountall[10] = "";
    mg_http_get_var(&hm->query, "mountall", mountall, sizeof(mountall));
    if (atoi(mountall))
    {
        // Mount all the things
        Debug_printf("Mount all from webui\n");
#ifdef BUILD_ATARI
        theFuji->mount_all(false);
#else
        theFuji->mount_all();
#endif
    }
    return redirect_or_result(c, hm, 0);
}

int fnHttpService::get_handler_eject(mg_connection *c, mg_http_message *hm)
{
    // get "deviceslot" query variable
    char slot_str[3] = "", mode_str[3] = "";
    mg_http_get_var(&hm->query, "deviceslot", slot_str, sizeof(slot_str));
    unsigned char ds = atoi(slot_str);

    fnHTTPD.clearErrMsg();

    if (ds > MAX_DISK_DEVICES)
    {
        fnHTTPD.addToErrMsg("<li>deviceslot should be between 0 and 7</li>");
    }
    else
    {
#ifdef BUILD_APPLE
        if(theFuji->get_disk(ds)->disk_dev.device_active) //set disk switched only if device was previosly mounted.
            theFuji->get_disk(ds)->disk_dev.switched = true;
#endif
        theFuji->get_disk(ds)->disk_dev.unmount();
#ifdef BUILD_ATARI
        if (theFuji->get_disk(ds)->disk_type == MEDIATYPE_CAS || theFuji->get_disk(ds)->disk_type == MEDIATYPE_WAV)
        {
            theFuji->cassette()->umount_cassette_file();
            theFuji->cassette()->sio_disable_cassette();
        }
#endif
        theFuji->get_disk(ds)->reset();
        Config.clear_mount(ds);
        Config.save();
        theFuji->_populate_slots_from_config(); // otherwise they don't show up in config.
        theFuji->get_disk(ds)->disk_dev.device_active = false;

        // Finally, scan all device slots, if all empty, and config enabled, enable the config device.
        if (Config.get_general_config_enabled())
        {
            if ((theFuji->get_disk(0)->host_slot == 0xFF) &&
                (theFuji->get_disk(1)->host_slot == 0xFF) &&
                (theFuji->get_disk(2)->host_slot == 0xFF) &&
                (theFuji->get_disk(3)->host_slot == 0xFF) &&
                (theFuji->get_disk(4)->host_slot == 0xFF) &&
                (theFuji->get_disk(5)->host_slot == 0xFF) &&
                (theFuji->get_disk(6)->host_slot == 0xFF) &&
                (theFuji->get_disk(7)->host_slot == 0xFF))
            {
                theFuji->boot_config = true;
#ifdef BUILD_ATARI
                theFuji->status_wait_count = 5;
#endif
                theFuji->device_active = true;
            }
        }
    }
    if (!fnHTTPD.errMsgEmpty())
    {
        send_file(c, "error_page.html");
    }
    else
    {
        send_file(c, "redirect_to_index.html");
    }
    return 0;
}

int fnHttpService::get_handler_hosts(mg_connection *c, mg_http_message *hm)
{
    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji->get_host(hs)->get_hostname()) + "\n";
    }
    mg_http_reply(c, 200, "", "%s", response.c_str());
    return 0;
}

int fnHttpService::post_handler_hosts(mg_connection *c, mg_http_message *hm)
{
    char hostslot[2] = "";
    mg_http_get_var(&hm->query, "hostslot", hostslot, sizeof(hostslot));
    char hostname[256] = "";
    mg_http_get_var(&hm->query, "hostname", hostname, sizeof(hostname));

    theFuji->set_slot_hostname(atoi(hostslot), hostname);

    std::string response = "";
    for (int hs = 0; hs < 8; hs++) {
        response += std::string(theFuji->get_host(hs)->get_hostname()) + "\n";
    }
    mg_http_reply(c, 200, "", "%s", response.c_str());
    return 0;
}

std::string fnHttpService::shorten_url(std::string url)
{
    int id = shortURLs.size();
    shortURLs.push_back(url);

    std::string shortened = "http://" + fnSystem.Net.get_hostname() + ":8000/url/" + std::to_string(id);
    Debug_printf("Short URL /url/%d registered for URL: %s\n", id, url.c_str());
    return shortened;
}

int fnHttpService::get_handler_shorturl(mg_connection *c, mg_http_message *hm)
{
    // Strip the /url/ from the path
    std::string id_str = std::string(hm->uri.buf).substr(5, hm->uri.len-5);
    Debug_printf("Short URL handler: %s\n", id_str.c_str());

    if (!std::all_of(id_str.begin(), id_str.end(), ::isdigit)) {
        mg_http_reply(c, 400, "", "Bad Request");
        return 0;
    }

    int id = std::stoi(id_str);
    if (id > fnHTTPD.shortURLs.size())
    {
        mg_http_reply(c, 404, "", "Not Found");
    }
    else
    {
        mg_printf(c, "HTTP/1.1 303 See Other\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n", fnHTTPD.shortURLs[id].c_str());
    }
    return 0;
}

void fnHttpService::cb(struct mg_connection *c, int ev, void *ev_data)
{
    static const char *s_root_dir = "data/www";

    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/test"), NULL))
        {
            // test handler
            mg_http_reply(c, 200, "", "{\"result\": %d}\n", 1);  // Serve REST
        }
        else if (mg_match(hm->uri, mg_str("/"), NULL))
        {
            // index handler
            send_file(c, "index.html");
        }
        else if (mg_match(hm->uri, mg_str("/file"), NULL))
        {
            // file handler
            char fname[60];
            if (hm->query.buf != NULL && hm->query.len > 0 && hm->query.len < sizeof(fname))
            {
                strncpy(fname, hm->query.buf, hm->query.len);
                fname[hm->query.len] = '\0';
                send_file(c, fname);
            }
            else
            {
                mg_http_reply(c, 400, "", "Bad file request\n");
            }
        }
        else if (mg_match(hm->uri, mg_str("/config"), NULL))
        {
            // config POST handler
            if (hm->method.len == 4 && strncasecmp(hm->method.buf, "POST", 4) == 0)
            {
                post_handler_config(c, hm);
            }
            else
            {
                mg_http_reply(c, 400, "", "Bad config request\n");
            }
        }
        else if (mg_match(hm->uri, mg_str("/print"), NULL))
        {
            // print handler
            get_handler_print(c);
        }
        else if (mg_match(hm->uri, mg_str("/browse/#"), NULL))
        {
            // browse handler
            get_handler_browse(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/swap"), NULL))
        {
            // browse handler
            get_handler_swap(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/mount"), NULL))
        {
            // browse handler
            get_handler_mount(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/unmount"), NULL))
        {
            // eject handler
            get_handler_eject(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/restart"), NULL))
        {
            // get "exit" query variable
            char exit[10] = "";
            mg_http_get_var(&hm->query, "exit", exit, sizeof(exit));
            if (atoi(exit))
            {
                mg_http_reply(c, 200, "", "{\"result\": %d}\n", 1); // send reply
                fnSystem.reboot(500, false); // deferred exit with code 0
            }
            else
            {
                // load restart page into browser
                send_file(c, "restart.html");
                // keep running for a while to transfer restart.html page
                fnSystem.reboot(500, true); // deferred exit with code 75 -> should be started again
            }
        }
        else if (mg_match(hm->uri, mg_str("/hosts"), NULL)) {
            if (mg_casecmp(hm->method.buf, "POST") == 0)
                post_handler_hosts(c, hm);
            else
                get_handler_hosts(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/url/*"), NULL))
        {
            get_handler_shorturl(c, hm);
        }
        else
        // default handler, serve static content of www firectory
        {
            struct mg_http_serve_opts opts = {s_root_dir, NULL};
            mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
        }
        c->is_resp = 0;
    }
}

struct mg_mgr * fnHttpService::start_server(serverstate &srvstate)
{
    std::string s_listening_address = Config.get_general_interface_url();

    static struct mg_mgr s_mgr;

    struct mg_connection *c;

    if (!fnWiFi.connected())
    {
        Debug_println("WiFi not connected - aborting web server startup");
        return nullptr;
    }

    // Set filesystem where we expect to find our static files
    srvstate._FS = &fsFlash;

    Debug_printf("Starting web server %s\n", s_listening_address.c_str());

    // mg_log_set(MG_LL_DEBUG);
    mg_mgr_init(&s_mgr);

    if ((c = mg_http_listen(&s_mgr, s_listening_address.c_str(), cb, &s_mgr)) != nullptr)
    {
        srvstate.hServer = &s_mgr;
    }
    else
    {
        srvstate.hServer = nullptr;
        Debug_println("Error starting web server!");
    }
    return srvstate.hServer;
}


/* Set up and start the web server
 */
void fnHttpService::start()
{
    if (state.hServer != nullptr)
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
        // httpd_stop(state.hServer);
        mg_mgr_free(state.hServer);
        state._FS = nullptr;
        state.hServer = nullptr;
    }
}

void fnHttpService::service()
{
    if (state.hServer != nullptr)
        mg_mgr_poll(state.hServer, 0);
}

#endif // !ESP_PLATFORM
