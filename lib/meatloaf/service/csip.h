// CSIP:/ - a scheme for handling CommodoreServer Internet Protocol
// see: https://www.commodoreserver.com/BlogEntryView.asp?EID=9D133160E7C344A398EC1F45AEF4BF32
//

#ifndef MEATLOAF_SCHEME_CSIP
#define MEATLOAF_SCHEME_CSIP

#include "meatloaf.h"
#include "network/tcp.h"

#include "fnSystem.h"
#include "fnTcpClient.h"

#include "utils.h"
#include "string_utils.h"

#include <streambuf>
#include <istream>

/********************************************************
 * Telnet buffer
 ********************************************************/

class csstreambuf : public std::streambuf {
    char* gbuf;
    char* pbuf;

protected:
    MeatSocket m_wifi;

public:
    csstreambuf() {}

    ~csstreambuf() {
        close();
    }      

    bool is_open() {
        return (m_wifi.isOpen());
    }

    bool open() {
        if(m_wifi.isOpen())
            return true;

        int rc = m_wifi.open("commodoreserver.com", 1541);
        printf("csstreambuf: connect to cserver returned: %d\r\n", rc);

        if(rc == 1) {
            if(gbuf == nullptr)
                gbuf = new char[512];
            if(pbuf == nullptr)
                pbuf = new char[512];
        }

        setp(pbuf, pbuf+512);

        return rc == 1;
    }

    void close() {
        printf("csstreambuf: closing\r\n");
        if(m_wifi.isOpen()) {
            m_wifi.close();
        }
        if(gbuf != nullptr)
            delete[] gbuf;
        if(pbuf != nullptr)
            delete[] pbuf;
    }

    int underflow() override {
        Debug_printv("In underflow");
        if (!m_wifi.isOpen()) {
            Debug_printv("In connection closed");
            close();
            return std::char_traits<char>::eof();
        }
        else if (this->gptr() == this->egptr()) {
            int readCount = 0;
            int attempts = 5;
            int wait = 500;
            
            readCount = m_wifi.read((uint8_t*)gbuf, 512);

            while( readCount <= 0 && (attempts--)>0 && m_wifi.isOpen()) {
                Debug_printv("got rc: %d, retrying", readCount);
                fnSystem.delay(wait);
                wait+=100;
                readCount = m_wifi.read((uint8_t*)gbuf, 512);
            } 
            Debug_printv("read success: %d", readCount);
            this->setg(gbuf, gbuf, gbuf + readCount);
        }
        else {
            //Debug_printv("else: %d - %d, (%d)", this->gptr(), this->egptr(), this->gbuf);
        }

        return this->gptr() == this->egptr()
            ? std::char_traits<char>::eof()
            : std::char_traits<char>::to_int_type(*this->gptr());
    };


    int overflow(int ch  = traits_type::eof()) override
    {
        //Debug_printv("in overflow");

        if (!m_wifi.isOpen()) {
            close();
            return EOF;
        }

        char* end = pptr();
        if ( ch != EOF ) {
            *end ++ = ch;
        }

        uint8_t* pBase = (uint8_t*)pbase();

        if ( m_wifi.write( pBase, end - pbase() ) == 0 ) {
            ch = EOF;
        } else if ( ch == EOF ) {
            ch = 0;
        }
        setp(pbuf, pbuf+512);
        
        return ch;
    };

    int sync() { 

        if (!m_wifi.isOpen()) {
            close();
            return 0;
        }
        if(pptr() == pbase()) {
            return 0;
        }
        else {
            //Debug_printv("in sync, written %d", pptr()-pbase());
            uint8_t* buffer = (uint8_t*)pbase();
            auto result = m_wifi.write(buffer, pptr()-pbase()); 
            setp(pbuf, pbuf+512);
            return (result != 0) ? 0 : -1;  
        }  
    };

    friend class CSIPMSessionMgr;
};

/********************************************************
 * Session manager
 ********************************************************/

class CSIPMSessionMgr : public std::iostream {
    std::string m_user;
    std::string m_pass;
    csstreambuf buf;

protected:
    std::string currentDir;

    bool establishSession();

    bool sendCommand(std::string);
    
    bool traversePath(MFile* path);

    bool isOK();

    std::string readLn();

public:
    CSIPMSessionMgr(std::string user = "", std::string pass = "") : std::iostream(&buf), m_user(user), m_pass(pass)
    {};

    ~CSIPMSessionMgr() {
        sendCommand("quit");
    };

    // read/write are used only by MStream
    size_t receive(uint8_t* buffer, size_t size) {
        if(buf.is_open())
            return buf.m_wifi.read(buffer, size);
        else
            return 0;
    }

    // read/write are used only by MStream
    size_t send(const uint8_t* buffer, size_t size) {
        if(buf.is_open())
            return buf.m_wifi.write(buffer, size);
        else
            return 0;
    }

    bool is_open() {
        return buf.is_open();
    }

    friend class CSIPMFile;
    friend class CSIPMStream;
};

/********************************************************
 * File implementations
 ********************************************************/
class CSIPMFile: public MFile {

public:
    CSIPMFile(std::string path, size_t size = 0): MFile(path), m_size(size) 
    {
        media_blocks_free = 65535;
        //media_block_size = 1; // blocks are already calculated
        //parseUrl(path);
        // Debug_printv("path[%s] size[%d]", path.c_str(), size);
        isPETSCII = true;
    };

    MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    MStream* getDecodedStream(std::shared_ptr<MStream> src) { return src.get(); };
    MStream* createStream(std::ios_base::openmode mode) override;

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override ;

    bool exists() override;
    bool remove() override;
    bool rename(std::string dest) { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = true;
    bool dirIsOpen = false;

private:
    bool dirIsImage = false;
    size_t m_size;
};

/********************************************************
 * Streams
 ********************************************************/

//
class CSIPMStream: public MStream {

public:
    CSIPMStream(std::string path) {
        url = path;
    }
    ~CSIPMStream() {
        close();
    }
    // MStream methods
    bool isOpen() override;
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) {
        return false;
    };


protected:
    std::string url;
    bool _is_open;
};



/********************************************************
 * FS
 ********************************************************/

class CSIPMFileSystem: public MFileSystem 
{
    bool handles(std::string name) {
        return name == "csip:";
    }
    
public:
    CSIPMFileSystem(): MFileSystem("csip") {};
    static CSIPMSessionMgr session;
    MFile* getFile(std::string path) override {
        return new CSIPMFile(path);
    }

};




#endif /* MEATLOAF_SCHEME_CSIP */
