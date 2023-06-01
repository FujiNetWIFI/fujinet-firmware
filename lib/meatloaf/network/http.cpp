#include "http.h"

/********************************************************
 * File impls
 ********************************************************/

MeatHttpClient* HttpFile::fromHeader() {
    if(client == nullptr) {
        //Debug_printv("Client was not present, creating");
        client = new MeatHttpClient();

        // let's just get the headers so we have some info
        //Debug_printv("Client requesting head");
        //Debug_printv("before head url[%s]", url.c_str());
        client->HEAD(url);
        //Debug_printv("after head url[%s]", client->url.c_str());
        parseUrl(client->url);
    }
    return client;
}

bool HttpFile::isDirectory() {
    if(fromHeader()->m_isDirectory)
        return true;

    if(fromHeader()->m_isWebDAV) {
        // try webdav PROPFIND to get a listing
        return true;
    }
    else
        // otherwise return false
        return false;
}

MStream* HttpFile::meatStream() {
    // has to return OPENED stream
    //Debug_printv("Input stream requested: [%s]", url.c_str());
    MStream* istream = new HttpIStream(url);
    istream->open();

    return istream;
}

MStream* HttpFile::createIStream(std::shared_ptr<MStream> is) {
    return is.get(); // DUMMY return value - we've overriden istreamfunction, so this one won't be used
}

time_t HttpFile::getLastWrite() {
    if(fromHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

time_t HttpFile::getCreationTime() {
    if(fromHeader()->m_isWebDAV) {
        return 0;
    }
    else
    // take from webdav PROPFIND or fallback to Last-Modified
        return 0; 
}

bool HttpFile::exists() {
    return fromHeader()->m_exists;
}

uint32_t HttpFile::size() {
    if(fromHeader()->m_isWebDAV) {
        // take from webdav PROPFIND
        return 0;
    }
    else
        // fallback to what we had from the header
        return fromHeader()->m_length;
}

bool HttpFile::remove() {
    if(fromHeader()->m_isWebDAV) {
        // PROPPATCH allows deletion
        return false;
    }
    return false;
}

bool HttpFile::mkDir() {
    if(fromHeader()->m_isWebDAV) {
        // MKCOL creates dir
        return false;
    }
    return false;
}

bool HttpFile::rewindDirectory() {
    if(fromHeader()->m_isWebDAV) { 
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return false;
    }
    return false; 
};

MFile* HttpFile::getNextFileInDir() { 
    Debug_printv("");
    if(fromHeader()->m_isWebDAV) {
        // we can try if this is webdav, then
        // PROPFIND allows listing dir
        return nullptr;
    }
    return nullptr; 
};


bool HttpFile::isText() {
    return fromHeader()->isText;
}

/********************************************************
 * Istream impls
 ********************************************************/
bool HttpIStream::open() {
    bool r = false;
    if(secondaryAddress == 0)
        r = m_http.GET(url);
    else if(secondaryAddress == 1)
        r = m_http.PUT(url);
    else if(secondaryAddress == 2)
        r = m_http.POST(url);

    return r;
}

void HttpIStream::close() {
    //Debug_printv("CLOSE called explicitly on this HTTP stream!");    
    m_http.close();
}

bool HttpIStream::seek(uint32_t pos) {
    if ( !m_http.m_isOpen )
    {
        Debug_printv("error");
        return false;
    }

    return m_http.seek(pos);
}

uint32_t HttpIStream::read(uint8_t* buf, uint32_t size) {
    return m_http.read(buf, size);
};

uint32_t HttpIStream::write(const uint8_t *buf, uint32_t size) {
    return -1;
}



bool HttpIStream::isOpen() {
    return m_http.m_isOpen;
};

uint32_t HttpIStream::size() {
    return m_http.m_length;
};

uint32_t HttpIStream::available() {
    return m_http.m_bytesAvailable;
};

uint32_t HttpIStream::position() {
    return m_http.m_position;
}

size_t HttpIStream::error() {
    return m_http.m_error;
}

/********************************************************
 * Meat HTTP client impls
 ********************************************************/
bool MeatHttpClient::GET(std::string dstUrl) {
    Debug_printv("GET");
    return open(dstUrl, HTTP_METHOD_GET);
}

bool MeatHttpClient::POST(std::string dstUrl) {
    Debug_printv("POST");
    return open(dstUrl, HTTP_METHOD_POST);
}

bool MeatHttpClient::PUT(std::string dstUrl) {
    Debug_printv("PUT");
    return open(dstUrl, HTTP_METHOD_PUT);
}

bool MeatHttpClient::HEAD(std::string dstUrl) {
    Debug_printv("HEAD");
    bool rc = open(dstUrl, HTTP_METHOD_HEAD);
    close();
    return rc;
}

bool MeatHttpClient::processRedirectsAndOpen(int range) {
    wasRedirected = false;
    m_length = -1;
    m_bytesAvailable = 0;

    Debug_printv("reopening url[%s] from position:%d", url.c_str(), range);
    lastRC = openAndFetchHeaders(lastMethod, range);

    while(lastRC == HttpStatus_MovedPermanently || lastRC == HttpStatus_Found || lastRC == 303)
    {
        Debug_printv("--- Page moved, doing redirect to [%s]", url.c_str());
        close();
        lastRC = openAndFetchHeaders(lastMethod, range);
        wasRedirected = true;
    }
    
    if(lastRC != HttpStatus_Ok && lastRC != 301 && lastRC != 206) {
        Debug_printv("opening stream failed, httpCode=%d", lastRC);
        close();
        return false;
    }

    // TODO - set m_isWebDAV somehow
    m_isOpen = true;
    m_exists = true;
    m_position = 0;

    Debug_printv("length[%d] avail[%d] isFriendlySkipper[%d] isText[%d] httpCode[%d]", m_length, m_bytesAvailable, isFriendlySkipper, isText, lastRC);

    return true;
}

bool MeatHttpClient::open(std::string dstUrl, esp_http_client_method_t meth) {
    url = dstUrl;
    lastMethod = meth;
    m_error = 0;

    return processRedirectsAndOpen(0);
};

void MeatHttpClient::close() {
    if(m_http != nullptr) {
        if ( m_isOpen ) {
            esp_http_client_close(m_http);
        }
        esp_http_client_cleanup(m_http);
        //Debug_printv("HTTP Close and Cleanup");
        m_http = nullptr;
    }
    m_isOpen = false;
}

void MeatHttpClient::setOnHeader(const std::function<int(char*, char*)> &lambda) {
    onHeader = lambda;
}

bool MeatHttpClient::seek(uint32_t pos) {
    if(pos==m_position)
        return true;

    if(isFriendlySkipper) {
        esp_http_client_close(m_http);

        bool op = processRedirectsAndOpen(pos);

        Debug_printv("SEEK in HttpIStream %s: range request RC=%d", url.c_str(), lastRC);
        
        if(!op)
            return false;

         // 200 = range not supported! according to https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
        if(lastRC == 206){
            Debug_printv("Seek successful");

            m_position = pos;
            m_bytesAvailable = m_length-pos;
            return true;
        }
    }

    if(lastMethod == HTTP_METHOD_GET) {
        Debug_printv("Server doesn't support resume, reading from start and discarding");
        // server doesn't support resume, so...
        if(pos<m_position || pos == 0) {
            // skipping backward let's simply reopen the stream...
            esp_http_client_close(m_http);
            bool op = open(url, lastMethod);
            if(!op)
                return false;

            // and read pos bytes - requires some buffer
            for(int i = 0; i<pos; i++) {
                char c;
                int rc = esp_http_client_read(m_http, &c, 1);
                if(rc == -1)
                    return false;
            }
        }
        else {
            auto delta = pos-m_position;
            // skipping forward let's skip a proper amount of bytes - requires some buffer
            for(int i = 0; i<delta; i++) {
                char c;
                int rc = esp_http_client_read(m_http, &c, 1);
                if(rc == -1)
                    return false;
            }
        }

        m_bytesAvailable = m_length-pos;
        m_position = pos;
        Debug_printv("stream opened[%s]", url.c_str());

        return true;
    }
    else
        return false;
}

uint32_t MeatHttpClient::read(uint8_t* buf, uint32_t size) {
    if (m_isOpen) {
        auto bytesRead= esp_http_client_read(m_http, (char *)buf, size );
        
        if(bytesRead>0) {
            m_bytesAvailable -= bytesRead;
            m_position+=bytesRead;
        }
        return bytesRead;        
    }
    return 0;
};

uint32_t MeatHttpClient::write(const uint8_t* buf, uint32_t size) {
    if (m_isOpen) {
        auto bytesWritten= esp_http_client_write(m_http, (char *)buf, size );
        m_bytesAvailable -= bytesWritten;
        m_position+=bytesWritten;
        return bytesWritten;        
    }
    return 0;
};

int MeatHttpClient::openAndFetchHeaders(esp_http_client_method_t meth, int resume) {

    if ( url.size() < 5)
        return 0;

    mstr::replaceAll(url, " ", "%20");
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .user_agent = USER_AGENT,
        .method = meth,
        .timeout_ms = 10000,
        .max_redirection_count = 10,
        .event_handler = _http_event_handler,
        .user_data = this,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 1
    };

    //Debug_printv("HTTP Init url[%s]", url.c_str());
    m_http = esp_http_client_init(&config);

    if(resume > 0) {
        char str[40];
        snprintf(str, sizeof str, "bytes=%lu-", (unsigned long)resume);
        esp_http_client_set_header(m_http, "range", str);
    }

    //Debug_printv("--- PRE OPEN");

    esp_err_t initOk = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...

    if(initOk == ESP_FAIL)
        return 0;

    //Debug_printv("--- PRE FETCH HEADERS");

    int lengthResp = esp_http_client_fetch_headers(m_http);
    if(m_length == -1 && lengthResp > 0) {
        // only if we aren't chunked!
        m_length = lengthResp;
        m_bytesAvailable = m_length;
    }

    //Debug_printv("--- PRE GET STATUS CODE");

    return esp_http_client_get_status_code(m_http);
}

esp_err_t MeatHttpClient::_http_event_handler(esp_http_client_event_t *evt)
{
    MeatHttpClient* meatClient = (MeatHttpClient*)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR: // This event occurs when there are any errors during execution
            Debug_printv("HTTP_EVENT_ERROR");
            meatClient->m_error = 1;
            break;

        case HTTP_EVENT_ON_CONNECTED: // Once the HTTP has been connected to the server, no data exchange has been performed
            // Debug_printv("HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT: // After sending all the headers to the server
            // Debug_printv("HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER: // Occurs when receiving each header sent from the server
            // Does this server support resume?
            // Accept-Ranges: bytes

            if (mstr::equals("Accept-Ranges", evt->header_key, false))
            {
                if(meatClient != nullptr) {
                    meatClient->isFriendlySkipper = mstr::equals("bytes", evt->header_value,false);
                    //Debug_printv("* Ranges info present '%s', comparison=%d!",evt->header_value, strcmp("bytes", evt->header_value)==0);
                }
            }
            // what can we do UTF8<->PETSCII on this stream?
            else if (mstr::equals("Content-Type", evt->header_key, false))
            {
                std::string asString = evt->header_value;
                bool isText = mstr::isText(asString);

                if(meatClient != nullptr) {
                    meatClient->isText = isText;
                    //Debug_printv("* Content info present '%s', isText=%d!, type=%s", evt->header_value, isText, asString.c_str());
                }        
            }
            else if(mstr::equals("Last-Modified", evt->header_key, false))
            {
                // Last-Modified, value=Thu, 03 Dec 1992 08:37:20 - may be used to get file date
            }
            else if(mstr::equals("Content-Disposition", evt->header_key, false))
            {
                // Content-Disposition, value=attachment; filename*=UTF-8''GeckOS-c64.d64
                // we can set isText from real file extension, too!
                std::string value = evt->header_value;
                if ( mstr::contains( value, (char *)"index.prg" ) )
                {
                    Debug_printv("HTTP Directory Listing [%s]", meatClient->url.c_str());
                    meatClient->m_isDirectory = true;
                }
            }
            else if(mstr::equals("Content-Length", evt->header_key, false))
            {
                //Debug_printv("* Content len present '%s'", evt->header_value);
                meatClient->m_length = std::stoi(evt->header_value);
                meatClient->m_bytesAvailable = meatClient->m_length;
            }
            else if(mstr::equals("Location", evt->header_key, false))
            {
                Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
                if ( mstr::startsWith(evt->header_value, (char *)"http") )
                {
                    //Debug_printv("match");
                    meatClient->url = evt->header_value;
                }
                else
                {
                    //Debug_printv("no match");
                    meatClient->url += evt->header_value;                    
                }

                //Debug_printv("new url '%s'", meatClient->url.c_str());
            }

            // Allow override in lambda
            meatClient->onHeader(evt->header_key, evt->header_value);

            break;

        case HTTP_EVENT_REDIRECT:

            Debug_printv("* This page redirects from '%s' to '%s'", meatClient->url.c_str(), evt->header_value);
            if ( mstr::startsWith(evt->header_value, (char *)"http") )
            {
                //Debug_printv("match");
                meatClient->url = evt->header_value;
            }
            else
            {
                //Debug_printv("no match");
                meatClient->url += evt->header_value;                    
            }
            break;

        case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
            //Debug_printv("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            {
                // int status = esp_http_client_get_status_code(meatClient->m_http);

                // if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently || status == 303) /*&& client->_redirect_count < (client->_max_redirects - 1)*/)
                // {
                //     //Debug_printv("HTTP_EVENT_ON_DATA: Redirect response body, ignoring");
                // }
                // else {
                //     //Debug_printv("HTTP_EVENT_ON_DATA: Got response body");
                // }


                if (esp_http_client_is_chunked_response(evt->client)) {
                    int len;
                    esp_http_client_get_chunk_length(evt->client, &len);
                    meatClient->m_length = len;
                    meatClient->m_bytesAvailable = len;
                    //Debug_printv("HTTP_EVENT_ON_DATA: Got chunked response, chunklen=%d, contentlen[%d]", len, meatClient->m_length);
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH: 
            // Occurs when finish a HTTP session
            // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
            //Debug_printv("HTTP_EVENT_ON_FINISH %u\n", uxTaskGetStackHighWaterMark(nullptr));
            // Keep track of how many times we "finish" reading a response from the server
            //Debug_printv("HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
            //Debug_printv("HTTP_EVENT_DISCONNECTED");
            //meatClient->m_bytesAvailable = 0;
            break;
    }
    return ESP_OK;
}
