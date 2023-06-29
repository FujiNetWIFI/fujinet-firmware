#ifndef DISK_H
#define DISK_H

#include "../fuji/fujiHost.h"

#include <string>
#include <unordered_map>

#include "bus.h"
#include "../media/media.h"
#include "../meatloaf/meat_io.h"
#include "../meatloaf/meat_buffer.h"

#define PRODUCT_ID "MEATLOAF CBM"

class iecDisk : public virtualDevice
{
private:
    //MediaType *_disk = nullptr;

    std::unique_ptr<MFile> _base;   // Always points to current directory/image
    std::string _last_file;         // Always points to last loaded file

    // Named Channel functions
    //std::shared_ptr<MStream> currentStream;
    bool registerStream (uint8_t channel);
    std::shared_ptr<MStream> retrieveStream ( uint8_t channel );
    bool closeStream ( uint8_t channel, bool close_all = false );

    // Directory
	uint16_t sendHeader(std::string header, std::string id);
	uint16_t sendLine(uint16_t blocks, char *text);
	uint16_t sendLine(uint16_t blocks, const char *format, ...);
	uint16_t sendFooter();
	void sendListing();

    // File
	bool sendFile();
	bool saveFile();

    struct _error_response
    {
        unsigned char errnum = 73;
        std::string msg = "CBM DOS V2.6 1541";
        unsigned char track = 0;
        unsigned char sector = 0;
    } error_response;

    void read();
    void write(bool verify);
    void format();

protected:
    /**
     * @brief Process command fanned out from bus
     * @return new device state
     */
    device_state_t process() override;

    /**
     * @brief process command for channel 0 (load)
     */
    void process_load();

    /**
     * @brief process command for channel 1 (save)
     */
    void process_save();

    /**
     * @brief process command channel
     */
    void process_command();

    /**
     * @brief process every other channel (2-14)
     */
    void process_channel();

    /**
     * @brief called to open a connection to a protocol
     */
    void iec_open();

    /**
     * @brief called to close a connection.
     */
    void iec_close();
    
    /**
     * @brief called when a TALK, then REOPEN happens on channel 0
     */
    void iec_reopen_load();

    /**
     * @brief called when TALK, then REOPEN happens on channel 1
     */
    void iec_reopen_save();

    /**
     * @brief called when REOPEN (to send/receive data)
     */
    void iec_reopen_channel();

    /**
     * @brief called when channel needs to listen for data from c=
     */
    void iec_reopen_channel_listen();

    /**
     * @brief called when channel needs to talk data to c=
     */
    void iec_reopen_channel_talk();

    /**
     * @brief called when LISTEN happens on command channel (15).
     */
    void iec_listen_command();

    /**
     * @brief called when TALK happens on command channel (15).
     */
    void iec_talk_command();

    /**
     * @brief called to process command either at open or listen
     */
    void iec_command();

    /**
     * @brief Set device ID from dos command
     */
    void set_device_id();

    /**
     * @brief Set desired prefix for channel
     */
    void set_prefix();

    /**
     * @brief Get prefix for channel
     */
    void get_prefix();

    /**
     * @brief If response queue is empty, Return 1 if ANY receive buffer has data in it, else 0
     */
    void iec_talk_command_buffer_status() override;

public:
    iecDisk();
    fujiHost *host;
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    //mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    std::unordered_map<uint16_t, std::shared_ptr<MStream>> streams;

    ~iecDisk();
};

#endif
