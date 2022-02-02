// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// This file is part of Meatloaf but adapted for use in the FujiNet project
// https://github.com/FujiNetWIFI/fujinet-platformio
// 
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef IECDISK_H
#define IECDISK_H

#include <esp_vfs.h>

#include <string>

#include "../../../include/cbmdefines.h"
#include "../../../include/petscii.h"

#include "bus.h"
#include "fnFsSPIFFS.h"
#include "media.h"



class iecDisk : public iecDevice
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
	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);
	uint16_t sendFooter(uint16_t &basicPtr);
	void sendFile(void);

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
    bool write_blank(FILE *f, uint32_t numBlocks);
	virtual void reset();

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

	~iecDisk();
};

#endif // IECDISK_H
