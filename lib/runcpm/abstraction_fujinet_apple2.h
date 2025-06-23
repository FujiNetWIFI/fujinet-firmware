/**
 * Abstraction functions for #FujiNet
 */

#ifndef ABSTRACTION_FUJINET_APPLE2_H
#define ABSTRACTION_FUJINET_APPLE2_H

#include <queue>
#include <string.h>
#include <errno.h>
#include "compat_string.h"

#include "globals.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#include "fnUART.h"
#include "fnTcpServer.h"
#include "fnTcpClient.h"

#include "iwm/iwmFuji.h"

#define HostOS 0x07 // FUJINET

// using namespace std;

#ifdef ESP_PLATFORM // OS
QueueHandle_t rxq;
QueueHandle_t txq;
#endif

typedef struct
{
	uint8_t dr;
	uint8_t fn[8];
	uint8_t tp[3];
	uint8_t ex, s1, s2, rc;
	uint8_t al[16];
	uint8_t cr, r0, r1, r2;
} CPM_FCB;

typedef struct
{
	uint8_t dr;
	uint8_t fn[8];
	uint8_t tp[3];
	uint8_t ex, s1, s2, rc;
	uint8_t al[16];
} CPM_DIRENTRY;

int dirPos;

char full_filename[128];

fnTcpClient client;
fnTcpServer *server;
bool teeMode = false;
unsigned short portActive = 0;

char *full_path(char *fn)
{
	memset(full_filename, 0, sizeof(full_filename));
	strcpy(full_filename, "/CPM/");
	strcat(full_filename, fn);
	return full_filename;
}

/* Memory abstraction functions */
/*===============================================================================*/
bool _RamLoad(char *fn, uint16_t address)
{
	FILE *f = fnSDFAT.file_open(full_path(fn), "r");
	bool result = false;
	uint8_t b;

	if (f)
	{
		while (!feof(f))
		{
			if (fread(&b, sizeof(uint8_t), 1, f) == 1)
			{
				_RamWrite(address++, b);
				result = true;
			}
			else
				result = false;
		}
		fclose(f);
	}
	Debug_printf("CCP last address: %04x\r\n",address);
	return (result);
}

//
// Hardware functions, new in 5.x
//
void _HardwareOut(const uint32 Port, const uint32 Value) {

}

uint32 _HardwareIn(const uint32 Port) {
	return 0;
}

/* filesystem (disk) abstraction fuctions */
/*===============================================================================*/
FILE *rootdir;
FILE *userdir;

bool _sys_exists(uint8* filename)
{
	return fnSDFAT.exists(full_path((char *)filename));
}

int _sys_fputc(uint8_t ch, FILE *f)
{
	return fputc(ch, f);
}

void _sys_fflush(FILE *f)
{
	fflush(f);
}

void _sys_fclose(FILE *f)
{
	fclose(f);
}

int _sys_select(uint8_t *disk)
{
	return fnSDFAT.exists(full_path((char *)disk));
}

long _sys_filesize(uint8_t *fn)
{
	unsigned long fs = -1;
	FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");

	if (fp)
	{
		fseek(fp, 0L, SEEK_END);
		fs = ftell(fp);
	}

	fclose(fp);
	return fs;
}

int _sys_openfile(uint8_t *fn)
{
	FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");
	if (fp)
	{
		fclose(fp);
		return 1;
	}
	else
		return 0;
}

int _sys_makefile(uint8_t *fn)
{
	FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "w");
	if (fp)
	{
		fclose(fp);
		return true;
	}
	else
		return false;
}

int _sys_deletefile(uint8_t *fn)
{
	return fnSDFAT.remove(full_path((char *)fn));
}

int _sys_renamefile(uint8_t *fn, uint8_t *newname)
{
	std::string from, to;

	from = std::string(full_path((char *)fn));
	to = std::string(full_path((char *)newname));

	return fnSDFAT.rename(from.c_str(), to.c_str());
}

void _sys_logbuffer(uint8_t *buffer)
{
	// not implemented at present.
}

bool _sys_extendfile(char *fn, unsigned long fpos)
{
	FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "a");

	if (!fp)
		return false;

	long origSize = fnSDFAT.filesize(full_path(fn));

	// This was patterned after the arduino abstraction, and I do not like how this works.

	if (fpos > origSize)
	{
		for (long i = 0; i < (origSize - fpos); ++i)
		{
			if (fwrite("\0", sizeof(uint8_t), 1, fp) != 1)
			{
				fclose(fp);
				return false;
			}
		}
	}
	fclose(fp);
	return true;
}

uint8_t _sys_readseq(uint8_t *fn, long fpos)
{
	uint8_t result = 0xff;
	FILE *f;
	uint8_t bytesread;
	uint8_t dmabuf[BlkSZ];
	int seekErr;

	f = fnSDFAT.file_open(full_path((char *)fn), "r");
	if (!f)
	{
		result = 0x10;
		return result;
	}

	seekErr = fseek(f, fpos, SEEK_SET);
	if (f)
	{
		if (fpos > 0 && seekErr != 0)
		{
			// EOF
			result = 0x01;
		}
		else
		{
			// set DMA buffer to EOF
			memset(dmabuf, 0x1a, BlkSZ);
			bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
			if (bytesread)
				memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
			result = bytesread ? 0x00 : 0x01;
		}
	}
	else
	{
		result = 0x10;
	}
	fclose(f);
	return (result);
}

uint8_t _sys_writeseq(uint8_t *fn, long fpos)
{
	uint8_t result = 0xff;
	FILE *f;

	if (_sys_extendfile((char *)fn, fpos))
		f = fnSDFAT.file_open(full_path((char *)fn), "r+");
	else
		return result;

	if (f)
	{
		if (fseek(f, fpos, SEEK_SET) == 0)
		{
			if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), f))
				result = 0x00;
		}
		else
		{
			result = 0x01;
		}
	}
	else
	{
		result = 0x10;
	}
	fclose(f);
	return (result);
}

uint8_t _sys_readrand(uint8_t *fn, long fpos)
{
	uint8 result = 0xff;
	FILE *f;
	uint8 bytesread;
	uint8 dmabuf[BlkSZ];
	long extSize;

	f = fnSDFAT.file_open(full_path((char *)fn), "r+");
	if (f)
	{
		if (fseek(f, fpos, SEEK_SET) == 0)
		{
			memset(dmabuf, 0x1A, BlkSZ);
			bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
			if (bytesread)
				memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
			result = bytesread ? 0x00 : 0x01;
		}
		else
		{
			if (fpos >= 65536L * BlkSZ)
			{
				result = 0x06; // seek past 8MB (largest file size in CP/M)
			}
			else
			{
				extSize = _sys_filesize((uint8_t *)full_path((char *)fn));

				// round file size up to next full logical extent
				extSize = ExtSZ * ((extSize / ExtSZ) + ((extSize % ExtSZ) ? 1 : 0));
				if (fpos < extSize)
					result = 0x01; // reading unwritten data
				else
					result = 0x04; // seek to unwritten extent
			}
		}
	}
	else
	{
		result = 0x10;
	}
	fclose(f);
	return (result);
}

uint8_t _sys_writerand(uint8_t *fn, long fpos)
{
	uint8 result = 0xff;
	FILE *f;

	if (_sys_extendfile((char *)fn, fpos))
	{
		f = fnSDFAT.file_open(full_path((char *)fn), "r+");
	}
	else
		return result;

	if (f)
	{
		if (fseek(f, fpos, SEEK_SET) == 0)
		{
			if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), f))
				result = 0x00;
		}
		else
		{
			result = 0x06;
		}
	}
	else
	{
		result = 0x10;
	}
	fclose(f);
	return (result);
}

uint8_t findNextDirName[17];
uint16_t fileRecords = 0;
uint16_t fileExtents = 0;
uint16_t fileExtentsUsed = 0;
uint16_t firstFreeAllocBlock;

uint8_t _findnext(uint8_t isdir)
{
	uint8 result = 0xff;
	bool isfile;
	uint32 bytes;
	fsdir_entry *entry;

	if (allExtents && fileRecords)
	{
		_mockupDirEntry();
		result = 0;
	}
	else
	{
		while ((entry = fnSDFAT.dir_read()))
		{
			strcpy((char *)findNextDirName, entry->filename); // careful watch for string overflow!
			isfile = !entry->isDir;
			bytes = entry->size;
			if (!isfile)
				continue;
			_HostnameToFCBname(findNextDirName, fcbname);
			if (match(fcbname, pattern))
			{
				if (isdir)
				{
					// account for host files that aren't multiples of the block size
					// by rounding their bytes up to the next multiple of blocks
					if (bytes & (BlkSZ - 1))
					{
						bytes = (bytes & ~(BlkSZ - 1)) + BlkSZ;
					}
					fileRecords = bytes / BlkSZ;
					fileExtents = fileRecords / BlkEX + ((fileRecords & (BlkEX - 1)) ? 1 : 0);
					fileExtentsUsed = 0;
					firstFreeAllocBlock = firstBlockAfterDir;
					_mockupDirEntry();
				}
				else
				{
					fileRecords = 0;
					fileExtents = 0;
					fileExtentsUsed = 0;
					firstFreeAllocBlock = firstBlockAfterDir;
				}
				_RamWrite(tmpFCB, filename[0] - '@');
				_HostnameToFCB(tmpFCB, findNextDirName);
				result = 0x00;
				break;
			}
		}
	}
	return (result);
}

uint8_t _findfirst(uint8_t isdir)
{
	uint8 path[4] = {'?', FOLDERCHAR, '?', 0};
	path[0] = filename[0];
	path[2] = filename[2];
	fnSDFAT.dir_close();
	fnSDFAT.dir_open(full_path((char *)path), "*", 0);
	_HostnameToFCBname(filename, pattern);
	fileRecords = 0;
	fileExtents = 0;
	fileExtentsUsed = 0;
	return (_findnext(isdir));
}

uint8_t _findnextallusers(uint8_t isdir)
{
	return _findnext(isdir);
}

uint8_t _findfirstallusers(uint8_t isdir)
{
	dirPos = 0;
	strcpy((char *)pattern, "???????????");
	fileRecords = 0;
	fileExtents = 0;
	fileExtentsUsed = 0;
	return (_findnextallusers(isdir));
}

uint8_t _Truncate(char *fn, uint8_t rc)
{
	// Implement some other way.
	return 0;
}

void _MakeUserDir()
{
	uint8 dFolder = cDrive + 'A';
	uint8 uFolder = toupper(tohex(userCode));

	uint8 path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

	if (fnSDFAT.exists(full_path((char *)path)))
	{
		return;
	}

	fnSDFAT.create_path(full_path((char *)path));
}

uint8_t _sys_makedisk(uint8_t drive)
{
	uint8 result = 0;
	if (drive < 1 || drive > 16)
	{
		result = 0xff;
	}
	else
	{
		uint8 dFolder = drive + '@';
		uint8 disk[2] = {dFolder, 0};

		if (fnSDFAT.exists(full_path((char *)disk)))
			return 0;

		if (!fnSDFAT.create_path(full_path((char *)disk)))
		{
			result = 0xfe;
		}
		else
		{
			uint8 path[4] = {dFolder, FOLDERCHAR, '0', 0};
			fnSDFAT.create_path(full_path((char *)path));
		}
	}
	return (result);
}

/* Console abstraction functions */
/*===============================================================================*/

int _kbhit(void)
{
#ifdef ESP_PLATFORM // OS
	return uxQueueMessagesWaiting(txq);
#else
	return 0;
#endif
}

uint8_t _getch(void)
{
	uint8_t c;
#ifdef ESP_PLATFORM // OS
	xQueueReceive(txq,&c,portMAX_DELAY);
#endif
	return c;
}

uint8_t _getche(void)
{
	uint8_t c = _getch();
#ifdef ESP_PLATFORM // OS
	xQueueSend(rxq,&c,portMAX_DELAY);
#endif
	return c;
}

void _putch(uint8_t ch)
{
#ifdef ESP_PLATFORM // OS
	xQueueSend(rxq,&ch,portMAX_DELAY);
#endif
}

void _clrscr(void)
{
	_putch(0x1B);
	_putch('[');
	_putch('1');
	_putch(';');
	_putch('1');
	_putch('H');
	_putch(0x1B);
	_putch('[');
	_putch('2');
	_putch('J');
}

uint8_t bdos_networkConfig(uint16_t addr)
{
	// Response to SIO_FUJICMD_GET_ADAPTERCONFIG
	struct
	{
		char ssid[32];
		char hostname[64];
		unsigned char localIP[4];
		unsigned char gateway[4];
		unsigned char netmask[4];
		unsigned char dnsIP[4];
		unsigned char macAddress[6];
		unsigned char bssid[6];
		char fn_version[15];
	} cfg;

	memset(&cfg, 0, sizeof(cfg));

	strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

	if (!fnWiFi.connected())
	{
		strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
	}
	else
	{
		strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
		strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
		fnWiFi.get_current_bssid(cfg.bssid);
		fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
		fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
	}

	fnWiFi.get_mac(cfg.macAddress);

	// Transfer to Z80 RAM.
	memset(&RAM[addr], 0, sizeof(cfg));
	memcpy(&RAM[addr], &cfg, sizeof(cfg));

	return 0;
}

uint8_t bdos_readHostSlots(uint16_t addr)
{
	char hostSlots[8][32];
	memset(hostSlots, 0, sizeof(hostSlots));

	for (int i = 0; i < 8; i++)
		strlcpy(hostSlots[i], theFuji.get_hosts(i)->get_hostname(), 32);

	memset(&RAM[addr], 0, sizeof(hostSlots));
	memcpy(&RAM[addr], &hostSlots, sizeof(hostSlots));
	return 0;
}

uint8_t bdos_readDeviceSlots(uint16_t addr)
{
	struct disk_slot
	{
		uint8_t hostSlot;
		uint8_t mode;
		char filename[MAX_DISPLAY_FILENAME_LEN];
	};
	disk_slot diskSlots[MAX_DISK_DEVICES];

	// Load the data from our current device array
	for (int i = 0; i < MAX_DISK_DEVICES; i++)
	{
		diskSlots[i].mode = theFuji.get_disks(i)->access_mode;
		diskSlots[i].hostSlot = theFuji.get_disks(i)->host_slot;
		strlcpy(diskSlots[i].filename, theFuji.get_disks(i)->filename, MAX_DISPLAY_FILENAME_LEN);
	}

	// Transfer to Z80 RAM.
	memset(&RAM[addr], 0, sizeof(diskSlots));
	memcpy(&RAM[addr], &diskSlots, sizeof(diskSlots));

	return 0;
}

uint8_t bios_tcpListen(uint16_t port)
{
	Debug_printf("Do we get here?\r\n");

	if (client.connected())
		client.stop();

	if (server != nullptr && port != portActive)
	{
		server->stop();
		delete server;
	}

	server = new fnTcpServer(port,1);
	int res = server->begin(port);
	if (res == 0)
	{
		Debug_printf("bios_tcpListen - failed to open port %u\nError (%d): %s\r\n", port, errno, strerror(errno));
		return true;
	}
	else
	{
		Debug_printf("bios_tcpListen - Now listening on port %u\r\n", port);
		return false;
	}
}

uint8_t bios_tcpAvailable(void)
{
	if (server == nullptr)
		return 0;

	return server->hasClient();
}

uint8_t bios_tcpTeeAccept(void)
{
	if (server == nullptr)
		return false;

	if (server->hasClient())
		client = server->accept();

	teeMode = true;

	return client.connected();
}

uint8_t bios_tcpDrop(void)
{
	if (server == nullptr)
		return false;

	client.stop();

	return true;
}

#endif /* ABSTRACTION_FUJINET_H */
