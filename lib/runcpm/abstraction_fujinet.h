/**
 * Abstraction functions for #FujiNet
 */

#ifndef ABSTRACTION_FUJINET_H
#define ABSTRACTION_FUJINET_H

#include "../hardware/fnSystem.h"
#include "../hardware/fnUART.h"
#include "globals.h"
#include <glob.h>
#include "fnFS.h"
#include "fnFsSD.h"
#include <string.h>

#define HostOS 0x07 // FUJINET

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

glob_t pglob;
int dirPos;

char full_filename[128];

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

	Debug_printf("_RamLoad(%s, 0x%04x)\n", address);

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
	return (result);
}

/* filesystem (disk) abstraction fuctions */
/*===============================================================================*/
FILE *rootdir;
FILE *userdir;

FILE *_sys_fopen_w(uint8_t *fn)
{
	Debug_printf("_sys_fopen_w(%s)\n", fn);
	return fnSDFAT.file_open(full_path((char *)fn), "w");
}

int _sys_fputc(uint8_t ch, FILE *f)
{
	Debug_printf("_sys_fopen_w(%c)\n", ch);
	return fputc(ch, f);
}

void _sys_fflush(FILE *f)
{
	Debug_printf("_sys_fflush()\n");
	fflush(f);
}

void _sys_fclose(FILE *f)
{
	Debug_printf("_sys_fclose()\n");
	fclose(f);
}

int _sys_select(uint8_t *disk)
{
	Debug_printf("_sys_select(%s)\n", full_path((char *)disk));
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
	Debug_printf("_sys_filesize(%s,%lu)\n", full_path((char *)fn), fs);
	return fs;
}

int _sys_openfile(uint8_t *fn)
{
	Debug_printf("_sys_openfile(%s,%s)\n", fn, full_path((char *)fn));
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
	Debug_printf("_sys_makefile(%s)\n", full_path((char *)fn));
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
	Debug_printf("_sys_deletefile(%s)\n", full_path((char *)fn));
	return fnSDFAT.remove(full_path((char *)fn));
}

int _sys_renamefile(uint8_t *fn, uint8_t *newname)
{
	Debug_printf("_sys_renamefile(%s)\n", full_path((char *)fn));
	return fnSDFAT.rename(full_path((char *)fn), full_path((char *)newname));
}

void _sys_logbuffer(uint8_t *buffer)
{
	// not implemented at present.
}

bool _sys_extendfile(char *fn, unsigned long fpos)
{
	Debug_printf("_sys_extendfile(%s,%lu)\n", full_path((char *)fn), fpos);
	FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "w+");

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
		fclose(fp);
	}
	return true;
}

uint8_t _sys_readseq(uint8_t *fn, long fpos)
{
	uint8_t result = 0xff;
	FILE *f;
	uint8_t bytesread;
	uint8_t dmabuf[BlkSZ];
	uint8_t i;
	int seekErr;

	Debug_printf("_sys_readseq(%s,%lu)\n", full_path((char *)fn), fpos);

	f = fnSDFAT.file_open(full_path((char *)fn), "r");
	seekErr = fseek(f,fpos,SEEK_SET);
	Debug_printf("seekErr = %d\n",seekErr);
	if (f)
	{
		if (fpos > 0 && seekErr != 0)
		{
			// EOF
			result = 0x01;
		}
		else
		{
			for (i = 0; i < BlkSZ; ++i)
				dmabuf[i] = 0x1a;
			bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
			if (bytesread)
			{
				for (i = 0; i < BlkSZ; ++i)
				{
					_RamWrite(dmaAddr + i, dmabuf[i]);
				}
				Debug_printf("\n");
			}
			result = bytesread ? 0x00 : 0x01;
		}
		fclose(f);
	}
	else
	{
		result = 0x10;
	}
	return (result);
}

uint8_t _sys_writeseq(uint8_t *fn, long fpos)
{
	uint8_t result = 0xff;
	FILE *f;

	Debug_printf("_sys_writeseq(%s,%lu)\n", full_path((char *)fn), fpos);

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
		fclose(f);
	}
	else
	{
		result = 0x10;
	}
	return (result);
}

uint8_t _sys_readrand(uint8_t *fn, long fpos)
{
	uint8 result = 0xff;
	FILE *f;
	uint8 bytesread;
	uint8 dmabuf[BlkSZ];
	uint8 i;
	long extSize;

	Debug_printf("_sys_readrand(%s,%lu)\n", full_path((char *)fn), fpos);

	f = fnSDFAT.file_open(full_path((char *)fn), "r");
	if (f)
	{
		if (fseek(f, fpos, SEEK_SET) == 0)
		{
			for (i = 0; i < BlkSZ; ++i)
				dmabuf[i] = 0x1a;
			bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
			if (bytesread)
			{
				for (i = 0; i < BlkSZ; ++i)
					_RamWrite(dmaAddr + i, dmabuf[i]);
			}
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
		fclose(f);
	}
	else
	{
		result = 0x10;
	}
	return (result);
}

uint8_t _sys_writerand(uint8_t *fn, long fpos)
{
	uint8 result = 0xff;
	FILE *f;

	Debug_printf("_sys_writerand(%s,%lu)\n", full_path((char *)fn), fpos);

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
		fclose(f);
	}
	else
	{
		result = 0x10;
	}
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
			Debug_printf("_findnext(%s)\n", findNextDirName);
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
	Debug_printf("_findfirst(%d,%s)\n", isdir, filename);
	return (_findnext(isdir));
}

uint8_t _findnextallusers(uint8_t isdir)
{
	Debug_printf("_findnextallusers(%d)\n", isdir);
	return _findnext(isdir);
}

uint8_t _findfirstallusers(uint8_t isdir)
{
	Debug_printf("_findfirstallusers(%d)\n", isdir);
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

	fnSDFAT.create_path((char *)path);
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
	return fnUartSIO.available();
}

uint8_t _getch(void)
{
	while (!fnUartSIO.available())
	{
	}
	Debug_printf("_getch(%02x)\n", fnUartSIO.peek());
	return fnUartSIO.read() & 0x7f;
}

uint8_t _getche(void)
{
	uint8_t ch = _getch() & 0x7f;
	fnUartSIO.write(ch);
	return ch;
}

void _putch(uint8_t ch)
{
	fnUartSIO.write(ch & 0x7f);
}

void _clrscr(void)
{
}

#endif /* ABSTRACTION_FUJINET_H */