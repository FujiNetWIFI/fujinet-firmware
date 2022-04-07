#ifndef IECDISK_H
#define IECDISK_H

#include <esp_vfs.h>

#include <string>

#include "../../../include/cbmdefines.h"
#include "../../../include/petscii.h"

#include "bus.h"
#include "fnFsSPIFFS.h"
#include "media.h"

#define ARCHIVE_TYPES "ZIP|7Z|RAR"
#define IMAGE_TYPES "D64|D71|D80|D81|D82|D8B|G64|X64|Z64|TAP|T64|TCRT|CRT|D1M|D2M|D4M|DHD|HDD|DNP|DFI|M2I|NIB"
#define FILE_TYPES "C64|PRG|P00|SEQ|S00|USR|U00|REL|R00"

class iecDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;

    void _read();
    void _write(bool verify);
    void _format();

    void _status();
    void _process();

    void shutdown() {};

	// handler helpers.
	void _open(void);
	void _listen_data(void);
	void _talk_data(int chan);
	void _close(void);

	void sendStatus(void);
	void sendSystemInfo(void);
	void sendDeviceStatus(void);

	void sendListing(void);
	// void sendListingHTTP(void);
	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);
	uint16_t sendFooter(uint16_t &basicPtr);
	void sendFile(void);
	// void sendFileHTTP(void);

	void saveFile(void);


	std::string _command;
	int _drive;
	int _partition;
    std::string _url;
    std::string _path;
	std::string _archive; // Future streaming of images/files in archives .zip/.7z/.rar
    std::string _image;
	std::string _filename;
	std::string _extension;
	std::string _type;
	std::string _mode;

public:
    iecDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t disktype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

	~iecDisk();
};

#endif // IECDISK_H
