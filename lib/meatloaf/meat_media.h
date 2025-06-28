
#ifndef MEATLOAF_MEDIA
#define MEATLOAF_MEDIA

#include "meatloaf.h"

#include <map>
#include <bitset>
#include <unordered_map>
#include <sstream>
#include <algorithm>

#include "../../include/debug.h"

#include "string_utils.h"


/********************************************************
 * Streams
 ********************************************************/

class MMediaStream: public MStream {

public:
    MMediaStream(std::shared_ptr<MStream> is) {
        containerStream = is;
        _is_open = true;
        has_subdirs = false;
    }

    ~MMediaStream() {
        //Debug_printv("close");
        close();
    }

    std::string url;

    void reset() override {
        seekCalled = false;
        _position = 0;
        _size = block_size;
        //m_load_address = {0, 0};
    }

    // MStream methods
    bool isOpen() override;

    // Browsable streams might call seekNextEntry to skip current bytes
    bool isBrowsable() override { return false; };
    // Random access streams might call seekPath to jump to a specific file
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    // read = (size) => this.containerStream.read(size);
    virtual uint8_t read();
    // readUntil = (delimiter = 0x00) => this.containerStream.readUntil(delimiter);
    virtual std::string readUntil( uint8_t delimiter = 0x00 );
    // readString = (size) => this.containerStream.readString(size);
    virtual std::string readString( uint8_t size );
    // readStringUntil = (delimiter = 0x00) => this.containerStream.readStringUntil(delimiter);
    virtual std::string readStringUntil( uint8_t delimiter = '\0' );

    virtual uint32_t write(const uint8_t *buf, uint32_t size);

    // seek = (offset) => this.containerStream.seek(offset + this.media_header_size);
    bool seek(uint32_t offset) override;
    // seekCurrent = (offset) => this.containerStream.seekCurrent(offset);
    bool seekCurrent(uint32_t offset);

    bool seekPath(std::string path) override { return false; };
    std::string seekNextEntry() override { return ""; };

    virtual uint32_t seekFileSize( uint8_t start_track, uint8_t start_sector );


protected:

    bool seekCalled = false;
    std::shared_ptr<MStream> containerStream;

    bool _is_open = false;

    MMediaStream* decodedStream;

    bool show_hidden = false;

    size_t media_header_size = 0x00;
    size_t media_data_offset = 0x00;
    size_t entry_index = 0;  // Currently selected directory entry (0 no selection)
    size_t entry_count = -1; // Directory list entry count (-1 unknown)

    enum open_modes { OPEN_READ, OPEN_WRITE, OPEN_APPEND, OPEN_MODIFY };
    std::string file_type_label[12] = { "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "SUS", "NAT", "CMD", "CFS", "???" };

    virtual bool readHeader() = 0;
    virtual bool writeHeader() { return false; };

    virtual bool seekEntry( std::string filename ) { return false; };
    virtual bool seekEntry( uint16_t index ) { return false; };
    virtual bool readEntry( uint16_t index ) { return false; };
    virtual bool writeEntry( uint16_t index ) { return false; };

    void resetEntryCounter() {
        entry_index = 0;
    }
    virtual bool getNextImageEntry() {
        return seekEntry(entry_index + 1);
    }

    // Disks
    virtual uint16_t blocksFree() { return 0; };
	virtual uint8_t speedZone(uint8_t track) { return 0; };

    virtual uint32_t blocks() {
        if ( _size > 0 && _size < block_size )
            return 1;
        else
            return ( _size / block_size );
    }

    virtual uint32_t readContainer(uint8_t *buf, uint32_t size);
    virtual uint32_t writeContainer(uint8_t *buf, uint32_t size);
    virtual uint32_t readFile(uint8_t* buf, uint32_t size) = 0;
    virtual uint32_t writeFile(uint8_t* buf, uint32_t size) = 0;
    virtual std::string decodeType(uint8_t file_type, bool show_hidden = false);
    virtual std::string decodeType(std::string file_type);
    virtual std::string decodeGEOSType(uint8_t geos_file_structure, uint8_t geos_file_type);

private:

    // Commodore Media
    // CARTRIDGE
    friend class CRTFile;

    // CONTAINER
    friend class D8BMFile;
    friend class DFIMFile;

    // FLOPPY DISK
    friend class D64MFile;
    friend class D71MFile;
    friend class D80MFile;
    friend class D81MFile;
    friend class D82MFile;

    // HARD DRIVE
    friend class DNPMFile;
    friend class D90MFile;

    // FILE
    friend class P00MFile;

    // CASSETTE TAPE
    friend class T64MFile;
    friend class TCRTMFile;
};



/********************************************************
 * Utility implementations
 ********************************************************/
class ImageBroker {
    static std::unordered_map<std::string, MMediaStream*> image_repo;
public:
    template<class T> static T* obtain(std::string url) 
    {
        //Debug_printv("streams[%d] url[%s]", image_repo.size(), url.c_str());

        // obviously you have to supply STREAMFILE.url to this function!
        if(image_repo.find(url)!=image_repo.end()) {
            return (T*)image_repo.at(url);
        }

        // create and add stream to broker if not found
        auto newFile = MFSOwner::File(url);

        T* newStream = (T*)newFile->getSourceStream();

        if ( newStream != nullptr )
        {
            // Are we at the root of the pathInStream?
            if ( newFile->pathInStream == "")
            {
                Debug_printv("DIRECTORY [%s]", url.c_str());
            }
            else
            {
                Debug_printv("SINGLE FILE [%s]", url.c_str());
            }

            image_repo.insert(std::make_pair(url, newStream));
            return newStream;
        }

        delete newFile;
        return nullptr;
    }

    static MMediaStream* obtain(std::string url) {
        return obtain<MMediaStream>(url);
    }

    static void dispose(std::string url) {
        if(image_repo.find(url)!=image_repo.end()) {
            auto toDelete = image_repo.at(url);
            image_repo.erase(url);
            delete toDelete;
        }
        Debug_printv("streams[%d]", image_repo.size());
    }

    static void validate() {
        
    }

    static void clear() {
        std::for_each(image_repo.begin(), image_repo.end(), [](auto& pair) {
            delete pair.second;
        });
        image_repo.clear();
    }
};

#endif // MEATLOAF_MEDIA
