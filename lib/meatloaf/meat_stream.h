#ifndef MEATLOAF_STREAM
#define MEATLOAF_STREAM

/********************************************************
 * Universal streams
 ********************************************************/

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#define SA0 0b0001111
#define SA1 0b00011111
#define SA2 0b00101111
#define SA3 0b00111111
#define SA4 0b01001111
#define SA5 0b01011111
#define SA6 0b01101111
#define SA7 0b01111111
#define SA8 0b10001111
#define SA9 0b10011111
#define SA10 0b10101111
#define SA11 0b10111111
#define SA12 0b11001111
#define SA13 0b11011111
#define SA14 0b11101111
#define SA15 0b11111111
// SA for TCP:
// TCP_NON_BLOCKING = clear bit 4
// TCP_BLOCKING = set bit 4
// TCP_CLENT_SOCKET = clear bit 5
// TCP_SERVER_SOCKET = set bit 5

class MStream {
public:
    virtual ~MStream() {};

    virtual uint32_t available() = 0;
    virtual uint32_t size() = 0;
    virtual uint32_t position() = 0;
    virtual size_t error() = 0;
    
    virtual bool isOpen() = 0;
    virtual bool isBrowsable() { return false; };
    virtual bool isRandomAccess() { return false; };

    virtual void close() = 0;
    virtual bool open() = 0;

    virtual uint32_t write(const uint8_t *buf, uint32_t size) = 0;
    virtual uint32_t read(uint8_t* buf, uint32_t size) = 0;

    uint8_t secondaryAddress = 0;
    std::string url = "";

    virtual bool seek(uint32_t pos, int mode) {
        if(mode == SEEK_SET) {
            return seek(pos);
        }
        else if(mode == SEEK_CUR) {
            return seek(position()+pos);
        }
        else {
            return seek(size() - pos);
        }
    }
    virtual bool seek(uint32_t pos) = 0;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    virtual bool seekPath(std::string path) {
        return false;
    };

    // For files with no directory structure
    // tap, crt, tar
    virtual std::string seekNextEntry() {
        return "";
    };
};


#endif // MEATLOAF_STREAM
