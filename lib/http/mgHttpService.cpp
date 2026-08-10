/*
 * mongoose based version (of httpService.cpp) for FujiNet-PC
 */

#ifndef ESP_PLATFORM
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <ctime>
#include <cstdlib>

#include "clipboardManager.h"
#include "compat_string.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnPassword.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "modem.h"
#include "printer.h"
#include "fujiDevice.h"
#include "fnTaskManager.h"
#include "fnio.h"
#include "utils.h"
#ifdef BUILD_ATARI
#include "sio/sioFuji.h"
#endif /* BUILD_ATARI */

#include "mongoose.h"
#include <cJSON.h>
#include "mgHttpClient.h"
#include "httpService.h"
#include "httpServiceConfigurator.h"
#include "httpServiceParser.h"
#include "httpServiceBrowse.h"
#include "appKeyManager.h"
#include "privatePage.h"

#include "../../include/debug.h"


using namespace std;

// Global HTTPD
fnHttpService fnHTTPD;

// SSE connection tracking
std::vector<struct mg_connection*> fnHttpService::m_sseClients;
size_t fnHttpService::m_lastOutputSize = 0;
uint64_t fnHttpService::m_lastPrinterCheckTime = 0;
uint64_t fnHttpService::m_lastSizeChangeTime = 0;
uint64_t fnHttpService::m_lastClearTime = 0;
bool fnHttpService::m_eventEmittedForCurrentJob = false;

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

/* Sends header.html or footer.html. 0 for header, 1 for footer.
   Assumes the response line and headers have already gone out and the body is
   being sent as chunks.
*/
void fnHttpService::send_header_footer(struct mg_connection *c, int headfoot)
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
    }

    // Retrieve server state
    serverstate *pState = &fnHTTPD.state;
    FILE *fInput = pState->_FS->file_open(fpath.c_str());

    if (fInput == nullptr)
    {
        Debug_printf("Failed to open '%s' for parsing\n", fpath.c_str());
        return;
    }

    size_t sz = FileSystem::filesize(fInput) + 1;
    char *buf = (char *)calloc(sz, 1);
    if (buf == NULL)
    {
        Debug_printf("Couldn't allocate %u bytes to load file contents!\n", (unsigned)sz);
    }
    else
    {
        fread(buf, 1, sz - 1, fInput);
        string contents = fnHttpServiceParser::parse_contents(string(buf));
        free(buf);
        mg_http_write_chunk(c, contents.data(), contents.length());
    }

    fclose(fInput);
}

/* Fetch a query variable as a string. Returns false if it isn't present.
*/
static bool query_str(mg_http_message *hm, const char *key, string &out)
{
    char buf[MAX_FILENAME_LEN * 2];
    int len = mg_http_get_var(&hm->query, key, buf, sizeof(buf));
    if (len < 0)
        return false;
    out.assign(buf, len);
    return true;
}

/* Fetch a query variable as an integer, returning -1 when it is absent or empty.
*/
static int query_int(mg_http_message *hm, const char *key)
{
    char buf[12];
    int len = mg_http_get_var(&hm->query, key, buf, sizeof(buf));
    if (len <= 0)
        return -1;
    return atoi(buf);
}

/* Start a chunked text/html response.
*/
static void begin_chunked_html(mg_connection *c)
{
    mg_printf(c, "%s\r\n", "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n");
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

int fnHttpService::get_handler_printer_status(struct mg_connection *c)
{
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
    if (printer == nullptr)
    {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
            "{\"enabled\":false}\n");
        return 0;
    }

    printer_emu *emu = printer->getPrinterPtr();
    if (emu == nullptr)
    {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
            "{\"enabled\":false}\n");
        return 0;
    }

    uint64_t now = fnSystem.millis();
    bool ready = (now - printer->lastPrintTime() >= PRINTER_BUSY_TIME);
    size_t sz = emu->getOutputSize();

    const char *ct;
    switch (emu->getPaperType())
    {
    case RAW:
    case TRIM:
    case ASCII:
        ct = "text/plain";
        break;
    case PDF:
        ct = "application/pdf";
        break;
    case SVG:
        ct = "image/svg+xml";
        break;
    case PNG:
        ct = "image/png";
        break;
    case HTML:
    case HTML_ATASCII:
        ct = "text/html";
        break;
    default:
        ct = "application/octet-stream";
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"enabled\":true,\"model\":\"%s\",\"ready\":%s,\"has_output\":%s,"
        "\"output_size\":%lu,\"content_type\":\"%s\",\"last_print_time\":%llu}\n",
        emu->modelname(),
        ready ? "true" : "false",
        sz > 0 ? "true" : "false",
        (unsigned long)sz,
        ct,
        (unsigned long long)printer->lastPrintTime()
    );

    return 0;
}

int fnHttpService::post_handler_printer_clear(struct mg_connection *c)
{
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
    printer_emu *emu = printer->getPrinterPtr();

    emu->closeOutput();
    printer->reset_printer();

    // Try to remove the file (may fail on some filesystems)
    int remove_result = fsFlash.remove("/paper");
    Debug_printf("Attempting to remove /paper, result: %d\n", remove_result);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"status\":\"success\"}\n");

    // Grace period - don't reset tracking; handles failed file deletion
    m_lastClearTime = fnSystem.millis();

    broadcast_printer_event("{\"event\":\"printer_cleared\"}");

    return 0;
}

void fnHttpService::add_sse_client(struct mg_connection* c)
{
    m_sseClients.push_back(c);
    Debug_printf("SSE client connected, total clients: %d\n", m_sseClients.size());
}

void fnHttpService::remove_sse_client(struct mg_connection* c)
{
    m_sseClients.erase(std::remove(m_sseClients.begin(), m_sseClients.end(), c), m_sseClients.end());
    Debug_printf("SSE client disconnected, remaining clients: %d\n", m_sseClients.size());
}

void fnHttpService::broadcast_printer_event(const char* data)
{
    if (m_sseClients.empty())
        return;

    Debug_printf("Broadcasting printer event to %d clients: %s\n", m_sseClients.size(), data);

    for (auto c : m_sseClients)
    {
        mg_printf(c, "data: %s\r\n\r\n", data);
    }
}

int fnHttpService::get_handler_printer_events(struct mg_connection *c)
{
    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/event-stream\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: keep-alive\r\n"
                 "Access-Control-Allow-Origin: *\r\n"
                 "\r\n");

    add_sse_client(c);

    Debug_println("SSE /printer/events connection established");

    return 0;
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

// ─── Device password ─────────────────────────────────────────────────────────

static void password_send_error(struct mg_connection *c, int code, const std::string &message)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "status", "error");
    cJSON_AddStringToObject(out, "message", message.c_str());
    char *json = cJSON_PrintUnformatted(out);
    mg_http_reply(c, code, "Content-Type: application/json\r\n", "%s", json != nullptr ? json : "{}");
    if (json != nullptr) cJSON_free(json);
    cJSON_Delete(out);
}

int fnHttpService::post_handler_password(struct mg_connection *c, struct mg_http_message *hm)
{
    Debug_println("Post_password request handler");

    std::map<std::string, std::string> postvals =
        fnHttpServiceConfigurator::parse_postdata_decoded(hm->body.buf, hm->body.len);

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
        password_send_error(c, 400, error);
        return -1;
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "status", "ok");
    cJSON_AddBoolToObject(out, "password_set", fnPassword.is_set());
    char *json = cJSON_PrintUnformatted(out);
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json != nullptr ? json : "{}");
    if (json != nullptr) cJSON_free(json);
    cJSON_Delete(out);
    return 0;
}

/* Returns true when the request may proceed. Otherwise a challenge or an
   explanatory page has already been sent and the handler should return. */
static bool require_device_password(struct mg_connection *c, struct mg_http_message *hm)
{
    if (!fnPassword.is_set())
    {
        mg_http_reply(c, 403, "Content-Type: text/html\r\n", "%s", PRIVATE_NO_PASSWORD_HTML);
        return false;
    }

    struct mg_str *auth = mg_http_get_header(hm, "Authorization");
    if (auth != nullptr && auth->len > 0 && auth->len < 1024)
    {
        std::string header(auth->buf, auth->len);
        if (fnPassword.check_basic_auth(header.c_str()))
            return true;
    }

    mg_http_reply(c, 401,
                  "WWW-Authenticate: " PRIVATE_AUTH_REALM "\r\nContent-Type: text/html\r\n",
                  "%s", PRIVATE_UNAUTHORIZED_HTML);
    return false;
}

int fnHttpService::get_handler_private(struct mg_connection *c, struct mg_http_message *hm)
{
    if (!require_device_password(c, hm))
        return -1;

    mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", PRIVATE_PAGE_HTML);
    return 0;
}

int fnHttpService::handler_appkeys(struct mg_connection *c, struct mg_http_message *hm)
{
    if (!require_device_password(c, hm))
        return -1;

    std::string message;
    if (hm->method.len == 4 && strncasecmp(hm->method.buf, "POST", 4) == 0)
    {
        if (hm->body.len > APPKEYS_MAX_POST_SIZE)
        {
            mg_http_reply(c, 413, "", "Posted app key data too large\n");
            return -1;
        }
        message = AppKeyManager::handle_post(
            fnHttpServiceConfigurator::parse_postdata_decoded(hm->body.buf, hm->body.len));
    }

    std::string page = AppKeyManager::render_page(message);
    mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", page.c_str());
    return 0;
}


/* Streams a host file to the client without blocking the mongoose event loop.
   The FileSystem belongs to the fujiHost and outlives the task, so only the
   file handle is closed here.
*/
class fnHttpSendFileTask : public fnTask
{
public:
    fnHttpSendFileTask(fnFile *fh, mg_connection *c);
protected:
    virtual int start() override;
    virtual int abort() override;
    virtual int step() override;
private:
    char buf[FNWS_SEND_BUFF_SIZE];
    fnFile * _fh;
    mg_connection * _c;
    size_t _filesize;
    size_t _total;
};

fnHttpSendFileTask::fnHttpSendFileTask(fnFile *fh, mg_connection *c)
{
    _fh = fh;
    _c = c;
    _filesize = 0;
    _total = 0;
}

int fnHttpSendFileTask::start()
{
    _filesize = FileSystem::filesize(_fh);
    Debug_printf("fnHttpSendFileTask started #%d\n", _id);
    return 0;
}

int fnHttpSendFileTask::abort()
{
    _c->is_draining = 1;
    fnio::fclose(_fh); // close (and delete _fh)
    Debug_printf("fnHttpSendFileTask aborted #%d\n", _id);
    return 0;
}

int fnHttpSendFileTask::step()
{
    // Send the file content out in chunks
    size_t count = fnio::fread((uint8_t *)buf, 1, FNWS_SEND_BUFF_SIZE, _fh);
    _total += count;
    mg_send(_c, buf, count);

    if (count)
        return 0; // continue

    // done
    _c->is_resp = 0;
    fnio::fclose(_fh); // close (and delete _fh)
    Debug_printf("Sent %lu of %lu bytes\n", (unsigned long)_total, (unsigned long)_filesize);

    return 1; // task has completed
}

int fnHttpService::get_handler_dir(mg_connection *c, mg_http_message *hm)
{
    Debug_println("Host directory request handler");

    fnHTTPD.clearErrMsg();

    int hs = query_int(hm, "hostslot");
    if (hs < 0)
        fnHTTPD.addToErrMsg("<li>hostslot is empty</li>");

    string path, pattern;
    if (!query_str(hm, "path", path))
        path.clear();
    if (!query_str(hm, "pattern", pattern) || pattern.empty())
        pattern = "*";

    fnHttpBrowse::render_opts opts;
    opts.download_links = true; // FujiNet-PC serves /download

    // The listing is streamed, but a host that won't open has to be answered
    // with the error page instead - so hold the response back until there is
    // something to send.
    bool header_sent = false;
    auto emit = [c, &header_sent](const string &chunk) {
        if (!header_sent)
        {
            begin_chunked_html(c);
            send_header_footer(c, 0); // header
            header_sent = true;
        }
        mg_http_write_chunk(c, chunk.data(), chunk.length());
    };

    if (!fnHttpBrowse::render_hostdir(hs, path, pattern, opts, emit))
    {
        fnHTTPD.addToErrMsg("<li>Could not open directory</li>");
        send_file(c, "error_page.html");
        return -1;
    }

    send_header_footer(c, 1);     // footer
    mg_http_write_chunk(c, "", 0); // end of response

    return 0;
}

int fnHttpService::get_handler_slot(mg_connection *c, mg_http_message *hm)
{
    Debug_println("Drive slot request handler");

    fnHTTPD.clearErrMsg();

    int hs = query_int(hm, "hostslot");
    string filename;

    if (hs < 0 || !query_str(hm, "filename", filename))
    {
        mg_http_reply(c, 400, "", "Bad drive slot request\n");
        return -1;
    }

    // Render into a buffer first: a cassette image needs a redirect instead of
    // a picker, and by then the page header would already have gone out.
    string body;
    auto buffer = [&body](const string &chunk) { body += chunk; };

    fnHttpBrowse::slot_result result = fnHttpBrowse::render_slotpicker(hs, filename, buffer);

    if (result == fnHttpBrowse::slot_result::CASSETTE)
    {
        // Cassette image passed in, put it in the cassette slot and redirect
        string url = "/mount?hostslot=" + to_string(hs) +
                     "&deviceslot=" + to_string(fnHttpBrowse::CASSETTE_DEVICE_SLOT) +
                     "&mode=" + to_string(fnHttpBrowse::CASSETTE_MOUNT_MODE) +
                     "&filename=" + util_url_encode(filename);
        mg_printf(c, "HTTP/1.1 303 See Other\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n", url.c_str());
        return 0;
    }

    if (result == fnHttpBrowse::slot_result::ERROR)
    {
        fnHTTPD.addToErrMsg("<li>Could not list drive slots</li>");
        send_file(c, "error_page.html");
        return -1;
    }

    begin_chunked_html(c);
    send_header_footer(c, 0); // header
    mg_http_write_chunk(c, body.data(), body.length());
    send_header_footer(c, 1);      // footer
    mg_http_write_chunk(c, "", 0); // end of response

    return 0;
}

int fnHttpService::get_handler_download(mg_connection *c, mg_http_message *hm)
{
    Debug_println("Download request handler");

    int hs = query_int(hm, "hostslot");
    string filename;

    if (hs < 0 || hs >= MAX_HOSTS || !query_str(hm, "filename", filename))
    {
        mg_http_reply(c, 400, "", "Bad download request\n");
        return -1;
    }

    fujiHost *host = theFuji->get_host(hs);

    char fullpath[MAX_FILENAME_LEN];
    fnFile *fh = host->mount()
        ? host->fnfile_open(filename.c_str(), fullpath, sizeof(fullpath), FILE_READ)
        : nullptr;

    if (fh == nullptr)
    {
        Debug_printf("Couldn't open host file: %s\n", filename.c_str());
        mg_http_reply(c, 400, "", "Failed to open file.\n");
        return -1;
    }

    mg_printf(c, "HTTP/1.1 200 OK\r\n");
    set_file_content_type(c, filename.c_str());
    mg_printf(c, "Content-Length: %lu\r\n\r\n", (unsigned long)FileSystem::filesize(fh));

    // Hand the transfer off so mongoose isn't blocked while it runs
    fnTask *task = new fnHttpSendFileTask(fh, c);
    if (task == nullptr)
    {
        Debug_println("Failed to create fnHttpSendFileTask");
        mg_http_reply(c, 400, "", "Failed to create a task\n");
        fnio::fclose(fh);
        return -1;
    }

    return taskMgr.submit_task(task) > 0 ? 0 : -1;
}

int fnHttpService::get_handler_swap(mg_connection *c, mg_http_message *hm)
{
    // rotate disk images
    Debug_printf("Disk swap from webui\n");
    theFuji->fujicmd_image_rotate();
    return redirect_or_result(c, hm, 0);
}

int fnHttpService::get_handler_mount(mg_connection *c, mg_http_message *hm)
{
    fnHTTPD.clearErrMsg();

    char mountall[10] = "";
    mg_http_get_var(&hm->query, "mountall", mountall, sizeof(mountall));

    if (atoi(mountall))
    {
        // Mount all the things
        Debug_printf("Mount all from webui\n");
        theFuji->fujicore_mount_all_success();
        return redirect_or_result(c, hm, 0);
    }

    fnHttpBrowse::mount_params params;
    params.host_slot = query_int(hm, "hostslot");
    params.device_slot = query_int(hm, "deviceslot");
    params.mode = query_int(hm, "mode");
    params.filename_given = query_str(hm, "filename", params.filename);

    fnHttpBrowse::mount_file(params);

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

int fnHttpService::get_handler_eject(mg_connection *c, mg_http_message *hm)
{
    fnHTTPD.clearErrMsg();

    fnHttpBrowse::eject_slot(query_int(hm, "deviceslot"));

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

// ─── Google Drive OAuth2 relay handlers ──────────────────────────────────────

#define GDRIVE_CLIENT_ID          "197927610161-me037pnh65lh9g8cad6fg62ifni9fik0.apps.googleusercontent.com"
#define GDRIVE_RELAY_REDIRECT_URI "https://auth.fujinet.online/gdrive-callback"
#define GDRIVE_RELAY_CODE_URL     "https://auth.fujinet.online/gdrive-code?state="

static std::string gdrive_auth_state;

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

static std::string gdrive_do_get(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) return "";
    int status = http.GET();
    if (status < 200 || status >= 300) { Debug_printf("gdrive_do_get: HTTP %d\n", status); return ""; }
    std::string body;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) body.append((char *)buf, n);
    return body;
}

int fnHttpService::get_handler_gdrive_auth(mg_connection *c, mg_http_message *)
{
    char state[16];
    snprintf(state, sizeof(state), "%08lx",
             (unsigned long)((uint32_t)rand() ^ (uint32_t)time(nullptr)));
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
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", s);
    free(s);
    return 0;
}

int fnHttpService::get_handler_gdrive_poll(mg_connection *c, mg_http_message *hm)
{
    auto send_json = [&](const char *status_val, const char *msg = nullptr) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "status", status_val);
        if (msg) cJSON_AddStringToObject(j, "message", msg);
        char *s = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", s);
        free(s);
    };

    char state[32] = {};
    mg_http_get_var(&hm->query, "state", state, sizeof(state));

    if (!state[0] || gdrive_auth_state.empty() || std::string(state) != gdrive_auth_state) {
        send_json("error", "state mismatch");
        return 0;
    }

    std::string relay_url = std::string(GDRIVE_RELAY_CODE_URL) + state;
    std::string relay_body = gdrive_do_get(relay_url);

    if (relay_body.empty()) {
        send_json("error", "relay unreachable");
        return 0;
    }

    cJSON *rj = cJSON_Parse(relay_body.c_str());
    if (!rj) { send_json("error", "bad relay response"); return 0; }

    cJSON *pending_node = cJSON_GetObjectItemCaseSensitive(rj, "pending");
    cJSON *expired_node = cJSON_GetObjectItemCaseSensitive(rj, "expired");
    cJSON *error_node   = cJSON_GetObjectItemCaseSensitive(rj, "error");
    cJSON *at_node      = cJSON_GetObjectItemCaseSensitive(rj, "access_token");
    cJSON *rt_node      = cJSON_GetObjectItemCaseSensitive(rj, "refresh_token");
    cJSON *ei_node      = cJSON_GetObjectItemCaseSensitive(rj, "expires_in");

    if (pending_node && cJSON_IsTrue(pending_node)) {
        cJSON_Delete(rj); send_json("pending"); return 0;
    }
    if (expired_node && cJSON_IsTrue(expired_node)) {
        cJSON_Delete(rj); gdrive_auth_state.clear(); send_json("expired"); return 0;
    }
    if (error_node && cJSON_IsString(error_node)) {
        const char *msg = error_node->valuestring;
        Debug_printf("gdrive-poll: relay returned error: %s\n", msg);
        cJSON_Delete(rj); gdrive_auth_state.clear(); send_json("error", msg); return 0;
    }
    if (!at_node || !cJSON_IsString(at_node)) {
        cJSON_Delete(rj); send_json("pending"); return 0;
    }

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
    return 0;
}

// ─── end Google Drive handlers ────────────────────────────────────────────────

// ─── OneDrive OAuth2 relay handlers ──────────────────────────────────────────

// TODO: replace with the FujiNet project's Microsoft Entra (Azure AD) app client_id.
// The client_secret lives on the relay server — never in firmware.
#define ONEDRIVE_CLIENT_ID          "c2835b53-9604-4277-8986-91520e8401be"
#define ONEDRIVE_RELAY_REDIRECT_URI "https://auth.fujinet.online/onedrive-callback"
#define ONEDRIVE_RELAY_CODE_URL     "https://auth.fujinet.online/onedrive-code?state="
#define ONEDRIVE_AUTH_ENDPOINT      "https://login.microsoftonline.com/common/oauth2/v2.0/authorize"
#define ONEDRIVE_SCOPE              "offline_access Files.ReadWrite"

static std::string onedrive_auth_state;

int fnHttpService::get_handler_onedrive_auth(mg_connection *c, mg_http_message *)
{
    char state[16];
    snprintf(state, sizeof(state), "%08lx",
             (unsigned long)((uint32_t)rand() ^ (uint32_t)time(nullptr)));
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
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", s);
    free(s);
    return 0;
}

int fnHttpService::get_handler_onedrive_poll(mg_connection *c, mg_http_message *hm)
{
    auto send_json = [&](const char *status_val, const char *msg = nullptr) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "status", status_val);
        if (msg) cJSON_AddStringToObject(j, "message", msg);
        char *s = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", s);
        free(s);
    };

    char state[32] = {};
    mg_http_get_var(&hm->query, "state", state, sizeof(state));

    if (!state[0] || onedrive_auth_state.empty() || std::string(state) != onedrive_auth_state) {
        send_json("error", "state mismatch");
        return 0;
    }

    std::string relay_url = std::string(ONEDRIVE_RELAY_CODE_URL) + state;
    std::string relay_body = gdrive_do_get(relay_url);

    if (relay_body.empty()) {
        send_json("error", "relay unreachable");
        return 0;
    }

    cJSON *rj = cJSON_Parse(relay_body.c_str());
    if (!rj) { send_json("error", "bad relay response"); return 0; }

    cJSON *pending_node = cJSON_GetObjectItemCaseSensitive(rj, "pending");
    cJSON *expired_node = cJSON_GetObjectItemCaseSensitive(rj, "expired");
    cJSON *error_node   = cJSON_GetObjectItemCaseSensitive(rj, "error");
    cJSON *at_node      = cJSON_GetObjectItemCaseSensitive(rj, "access_token");
    cJSON *rt_node      = cJSON_GetObjectItemCaseSensitive(rj, "refresh_token");
    cJSON *ei_node      = cJSON_GetObjectItemCaseSensitive(rj, "expires_in");

    if (pending_node && cJSON_IsTrue(pending_node)) {
        cJSON_Delete(rj); send_json("pending"); return 0;
    }
    if (expired_node && cJSON_IsTrue(expired_node)) {
        cJSON_Delete(rj); onedrive_auth_state.clear(); send_json("expired"); return 0;
    }
    if (error_node && cJSON_IsString(error_node)) {
        const char *msg = error_node->valuestring;
        Debug_printf("onedrive-poll: relay returned error: %s\n", msg);
        cJSON_Delete(rj); onedrive_auth_state.clear(); send_json("error", msg); return 0;
    }
    if (!at_node || !cJSON_IsString(at_node)) {
        cJSON_Delete(rj); send_json("pending"); return 0;
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
    return 0;
}

// ─── end OneDrive handlers ────────────────────────────────────────────────────

// ─── Clipboard handlers ──────────────────────────────────────────────────────

// hm->method points into the request line and is not NUL terminated, so it has
// to be compared by length rather than as a C string.
static bool http_method_is(struct mg_http_message *hm, const char *method)
{
    size_t len = strlen(method);
    return hm->method.len == len && strncasecmp(hm->method.buf, method, len) == 0;
}

static void clipboard_send_json(struct mg_connection *c)
{
    std::string json = fnClipboard.to_json();
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s\n", json.c_str());
}

// GET /clipboard - clipboard state and the remembered snippets
int fnHttpService::get_handler_clipboard(struct mg_connection *c)
{
    clipboard_send_json(c);
    return 0;
}

// GET /clipboard/data?index=N - the contents of a snippet, as-is
int fnHttpService::get_handler_clipboard_data(struct mg_connection *c, struct mg_http_message *hm)
{
    char index_str[8] = "";
    mg_http_get_var(&hm->query, "index", index_str, sizeof(index_str));

    const ClipboardSnippet *snippet = fnClipboard.snippet(atoi(index_str));
    if (snippet == nullptr)
    {
        mg_http_reply(c, 404, "", "Not Found\n");
        return -1;
    }

    mg_printf(c, "HTTP/1.1 200 OK\r\n");
    if (snippet->binary)
    {
        mg_printf(c, "Content-Type: application/octet-stream\r\n");
        mg_printf(c, "Content-Disposition: attachment; filename=\"%s\"\r\n",
                  snippet->name.empty() ? "clipboard.bin" : snippet->name.c_str());
    }
    else
    {
        mg_printf(c, "Content-Type: text/plain; charset=utf-8\r\n");
    }
    mg_printf(c, "Content-Length: %lu\r\n\r\n", (unsigned long)snippet->data.size());
    mg_send(c, snippet->data.data(), snippet->data.size());

    return 0;
}

// POST /clipboard?binary=1&name=... - replace the clipboard with the posted body
int fnHttpService::post_handler_clipboard(struct mg_connection *c, struct mg_http_message *hm)
{
    char binary_str[4] = "";
    char name[64] = "";
    mg_http_get_var(&hm->query, "binary", binary_str, sizeof(binary_str));
    mg_http_get_var(&hm->query, "name", name, sizeof(name));

    if (hm->body.len > CLIPBOARD_MAX_SIZE)
    {
        Debug_printf("Clipboard: rejecting %u byte upload\n", (unsigned)hm->body.len);
        mg_http_reply(c, 400, "", "Content too large\n");
        return -1;
    }

    bool binary = atoi(binary_str) != 0;
    std::string body(hm->body.buf, hm->body.len);

    if (!binary)
        body = fnClipboard.normalize_host_text(body);

    fnClipboard.set(std::move(body), CLIPBOARD_SOURCE_WEBUI, binary, name);

    clipboard_send_json(c);
    return 0;
}

// POST /clipboard/clear?all=1 - empty the clipboard, optionally the history too
int fnHttpService::post_handler_clipboard_clear(struct mg_connection *c, struct mg_http_message *hm)
{
    char all[4] = "";
    mg_http_get_var(&hm->query, "all", all, sizeof(all));

    if (atoi(all))
        fnClipboard.clear_all();
    else
        fnClipboard.clear();

    clipboard_send_json(c);
    return 0;
}

// POST /clipboard/restore?index=N - make a remembered snippet current again
int fnHttpService::post_handler_clipboard_restore(struct mg_connection *c, struct mg_http_message *hm)
{
    char index_str[8] = "";
    mg_http_get_var(&hm->query, "index", index_str, sizeof(index_str));

    if (!fnClipboard.restore(atoi(index_str)))
    {
        mg_http_reply(c, 404, "", "Not Found\n");
        return -1;
    }

    clipboard_send_json(c);
    return 0;
}

// ─── end clipboard handlers ──────────────────────────────────────────────────

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
        else if (mg_match(hm->uri, mg_str("/password"), NULL))
        {
            // device password change/clear handler
            if (hm->method.len == 4 && strncasecmp(hm->method.buf, "POST", 4) == 0)
                post_handler_password(c, hm);
            else
                mg_http_reply(c, 405, "", "Method Not Allowed\n");
        }
        else if (mg_match(hm->uri, mg_str("/private"), NULL))
        {
            // password-protected page
            get_handler_private(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/appkeys"), NULL))
        {
            // password-protected app key manager
            handler_appkeys(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/print"), NULL))
        {
            // print handler
            get_handler_print(c);
        }
        else if (mg_match(hm->uri, mg_str("/printer/status"), NULL))
        {
            get_handler_printer_status(c);
        }
        else if (mg_match(hm->uri, mg_str("/printer/clear"), NULL))
        {
            if (mg_casecmp(hm->method.buf, "POST") == 0)
                post_handler_printer_clear(c);
            else
                mg_http_reply(c, 405, "", "Method Not Allowed\n");
        }
        else if (mg_match(hm->uri, mg_str("/printer/events"), NULL))
        {
            get_handler_printer_events(c);
        }
        else if (mg_match(hm->uri, mg_str("/hsdir"), NULL))
        {
            // host directory listing handler
            get_handler_dir(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/dslot"), NULL))
        {
            // drive slot picker handler
            get_handler_slot(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/download"), NULL))
        {
            // host file download handler
            get_handler_download(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/browse/#"), NULL))
        {
            // the host browser moved to /hsdir - keep old links working
            const char prefix[] = "/browse/host/";
            const size_t prefixlen = sizeof(prefix) - 1;
            const char *s = hm->uri.buf + prefixlen;

            if (hm->uri.len > prefixlen && *s >= '1' && *s <= '8')
                mg_printf(c, "HTTP/1.1 303 See Other\r\nLocation: /hsdir?hostslot=%d\r\n"
                             "Content-Length: 0\r\n\r\n", *s - '1');
            else
                mg_http_reply(c, 403, "", "Bad browse request\n");
        }
        else if (mg_match(hm->uri, mg_str("/swap"), NULL))
        {
            // disk rotation handler
            get_handler_swap(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/mount"), NULL))
        {
            // mount handler
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
        else if (mg_match(hm->uri, mg_str("/clipboard"), NULL))
        {
            if (http_method_is(hm, "POST"))
                post_handler_clipboard(c, hm);
            else
                get_handler_clipboard(c);
        }
        else if (mg_match(hm->uri, mg_str("/clipboard/data"), NULL))
        {
            get_handler_clipboard_data(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/clipboard/clear"), NULL))
        {
            if (http_method_is(hm, "POST"))
                post_handler_clipboard_clear(c, hm);
            else
                mg_http_reply(c, 405, "", "Method Not Allowed\n");
        }
        else if (mg_match(hm->uri, mg_str("/clipboard/restore"), NULL))
        {
            if (http_method_is(hm, "POST"))
                post_handler_clipboard_restore(c, hm);
            else
                mg_http_reply(c, 405, "", "Method Not Allowed\n");
        }
        else if (mg_match(hm->uri, mg_str("/gdrive-auth"), NULL))
        {
            get_handler_gdrive_auth(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/gdrive-poll"), NULL))
        {
            get_handler_gdrive_poll(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/onedrive-auth"), NULL))
        {
            get_handler_onedrive_auth(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/onedrive-poll"), NULL))
        {
            get_handler_onedrive_poll(c, hm);
        }
        else
        // default handler, serve static content of www firectory
        {
            struct mg_http_serve_opts opts = {s_root_dir, NULL};
            mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
        }
        c->is_resp = 0;
    }
    else if (ev == MG_EV_CLOSE)
    {
        remove_sse_client(c);
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
    {
        mg_mgr_poll(state.hServer, 0);

        if (!m_sseClients.empty())
        {
            uint64_t now = fnSystem.millis();

            // Only check printer status every 100ms to avoid hammering filesystem
            if (now - m_lastPrinterCheckTime >= 100)
            {
                m_lastPrinterCheckTime = now;

                PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
                if (printer)
                {
                    printer_emu *emu = printer->getPrinterPtr();
                    if (!emu) {
                        return;  // Printer emulator not initialized
                    }

                    bool ready = (now - printer->lastPrintTime() >= PRINTER_BUSY_TIME);
                    size_t sz = emu->getOutputSize();

                    // Post-clear grace period
                    if (m_lastClearTime > 0 && (now - m_lastClearTime) < 1000)
                        return;
                    else if (m_lastClearTime > 0)
                        m_lastClearTime = 0;

                    if (sz != m_lastOutputSize)
                    {
                        if (sz < m_lastOutputSize)
                            m_eventEmittedForCurrentJob = false;
                        m_lastOutputSize = sz;
                        m_lastSizeChangeTime = now;
                    }
                    else if (m_lastSizeChangeTime == 0 && sz > 0)
                    {
                        m_lastSizeChangeTime = now;
                    }

                    // Emit when ready, has output, stable for 300ms, not yet emitted
                    if (ready && sz > 0 &&
                        !m_eventEmittedForCurrentJob &&
                        (now - m_lastSizeChangeTime >= 300))
                    {
                        m_eventEmittedForCurrentJob = true;
                        char event[128];
                        snprintf(event, sizeof(event),
                            "{\"event\":\"printer_ready\",\"size\":%lu}",
                            (unsigned long)sz);
                        broadcast_printer_event(event);
                    }
                }
            }
        }
    }
}

#endif // !ESP_PLATFORM
