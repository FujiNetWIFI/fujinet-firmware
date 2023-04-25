// HTTP:// - Hypertext Transfer Protocol

#ifndef MEATLOAF_SCHEME_HTTP
#define MEATLOAF_SCHEME_HTTP

#include "meat_io.h"
#include "../../include/global_defines.h"
#include <esp_http_client.h>
#include <functional>

#define HTTP_BLOCK_SIZE 256

class MeatHttpClient {
    esp_http_client_handle_t m_http = nullptr;
    static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
    int openAndFetchHeaders(esp_http_client_method_t meth, int resume = 0);
    esp_http_client_method_t lastMethod;
    std::function<int(char*, char*)> onHeader = [] (char* key, char* value){ 
        //Debug_printv("HTTP_EVENT_ON_HEADER, key=%s, value=%s", key, value);
        return 0; 
    };

public:

    MeatHttpClient() {

    }
    ~MeatHttpClient() {
        close();
    }

    bool GET(std::string url);
    bool POST(std::string url);
    bool PUT(std::string url);
    bool HEAD(std::string url);

    bool processRedirectsAndOpen(int range);
    bool open(std::string url, esp_http_client_method_t meth);
    void close();
    void setOnHeader(const std::function<int(char*, char*)> &f);
    bool seek(uint32_t pos);
    uint32_t read(uint8_t* buf, uint32_t size);
    uint32_t write(const uint8_t* buf, uint32_t size);
    bool m_isOpen = false;
    bool m_exists = false;
    uint32_t m_length = 0;
    uint32_t m_bytesAvailable = 0;
    uint32_t m_position = 0;
    size_t m_error = 0;
    bool m_isWebDAV = false;
    bool m_isDirectory = false;
    bool isText = false;
    bool isFriendlySkipper = false;
    bool wasRedirected = false;
    std::string url;
    //char response[HTTP_BLOCK_SIZE + 1] = { 0 };
    int lastRC;
};

/********************************************************
 * File implementations
 ********************************************************/


class HttpFile: public MFile {
    MeatHttpClient* fromHeader();
    MeatHttpClient* client = nullptr;

public:
    HttpFile() {
        Debug_printv("C++, if you try to call this, be damned!");
    };
    HttpFile(std::string path): MFile(path) { 
        // Debug_printv("constructing http file from url [%s]", url.c_str());
    };
    HttpFile(std::string path, std::string filename): MFile(path) {};
    ~HttpFile() override {
        if(client != nullptr)
            delete client;
    }
    bool isDirectory() override;
    MStream* meatStream() override ; // has to return OPENED streamm
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    uint32_t size() override ;
    bool remove() override ;
    bool isText() override ;
    bool rename(std::string dest) { return false; };
    MStream* createIStream(std::shared_ptr<MStream> src);
    //void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
};


/********************************************************
 * Streams
 ********************************************************/

class HttpIStream: public MStream {

public:
    HttpIStream(std::string path) {
        url = path;
    };
    ~HttpIStream() {
        close();
    };

    // MStream methods
    uint32_t size() override;
    uint32_t available() override;     
    uint32_t position() override;
    size_t error() override;

    virtual bool seek(uint32_t pos);

    void close() override;
    bool open() override;

    // MStream methods
    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    bool isOpen() override;

protected:
    MeatHttpClient m_http;

};



/********************************************************
 * FS
 ********************************************************/

class HttpFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) override {
        return new HttpFile(path);
    }

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"http:", false) )
            return true;

        if ( mstr::equals(name, (char *)"https:", false) )
            return true;
            
        return false;
    }
public:
    HttpFileSystem(): MFileSystem("http") {};
};


#endif /* MEATLOAF_SCHEME_HTTP */
