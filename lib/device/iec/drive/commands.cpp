// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
//
// This file is part of Pi1541.
// 
// Pi1541 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Pi1541 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Pi1541. If not, see <http://www.gnu.org/licenses/>.


//TODO when we have different file types we then need to detect these type of wildcards;-
//*=S selects only sequential files
//*=P	selects program files
//*=R	selects relative files
//*=U	selects user-files

/*
#include "commands.h"


static char ErrorMessage[64];

#define ERROR_00_OK 0
//01,FILES SCRATCHED,XX,00
//20,READ ERROR,TT,SS		header not found
//21,READ ERROR,TT,SS		sync not found
//22,READ ERROR,TT,SS		header checksum fail
//23,READ ERROR,TT,SS		data block checksum fail
//24,READ ERROR,TT,SS
#define ERROR_25_WRITE_ERROR 25	//25,WRITE ERROR,TT,SS		verify error
//26,WRITE PROTECT ON,TT,SS
//27,READ ERROR,TT,SS		header checksum fail
//28,WRITE ERROR,TT,SS	sync not found after write
//29,DISK ID MISMATCH,TT,SS
#define ERROR_30_SYNTAX_ERROR 30	//30,SYNTAX ERROR,00,00		could not parse the command
#define ERROR_31_SYNTAX_ERROR 31	//31,SYNTAX ERROR,00,00		unknown command
#define ERROR_32_SYNTAX_ERROR 32	//32,SYNTAX ERROR,00,00		command too long (ie > 40 characters)
#define ERROR_33_SYNTAX_ERROR 33	//33,SYNTAX ERROR,00,00		Wildcard * and ? was used in an open or save command
#define ERROR_34_SYNTAX_ERROR 34	//34,SYNTAX ERROR,00,00		File name could not be found in the command
#define ERROR_39_FILE_NOT_FOUND 39	//39,FILE NOT FOUND,00,00	User program of type 'USR' was not found
//50,RECORD NOT PRESENT,00,00
//51,OVERFLOW IN RECORD,00,00
//52,FILE TOO LARGE,00,00
//60,WRITE FILE OPEN,00,00	An at tempt was made to OPEN a file that had not previously been CLOSEd after writing.
//61,FILE NOT OPEN,00,00	A file was accessed that had not been OPENed. 
#define ERROR_62_FILE_NOT_FOUND 62		//62,FILE NOT FOUND,00,00	An attempt was made to load a program or open does not exist
#define ERROR_63_FILE_EXISTS 63		//63,FILE EXISTS,00,00		Tried to rename a file to the same name of an existing file
//65,NO BLOCK,TT,SS
//66,ILLEGAL TRACK OR SECTOR,TT,SS	
//67,ILLEGAL TRACK OR SECTOR,TT,SS
//70,NO CHANNEL,00,00		An attempt was made to open more files than channels available
//71,DIR ERROR,TT,SS
//72,DISK FULL,00,00
#define ERROR_73_DOSVERSION 73		// 73,VERSION,00,00
#define ERROR_74_DRlVE_NOT_READY 74		//74,DRlVE NOT READY,00,00
//75,FORMAT SPEED ERROR,00,00

void Error(u8 errorCode, u8 track = 0, u8 sector = 0)
{
	char* msg = "UNKNOWN";
	switch (errorCode)
	{
		case ERROR_00_OK:
			msg = " OK";
		break;
		case ERROR_25_WRITE_ERROR:
			msg = "WRITE ERROR";
		break;
		case ERROR_73_DOSVERSION:
			sprintf(ErrorMessage, "%02d,PI1541 V%02d.%02d,%02d,%02d", errorCode,
						versionMajor, versionMinor, track, sector);
			return;
		break;
		case ERROR_30_SYNTAX_ERROR:
		case ERROR_31_SYNTAX_ERROR:
		case ERROR_32_SYNTAX_ERROR:
		case ERROR_33_SYNTAX_ERROR:
		case ERROR_34_SYNTAX_ERROR:
			msg = "SYNTAX ERROR";
		break;
		case ERROR_39_FILE_NOT_FOUND:
			msg = "FILE NOT FOUND";
		break;
		case ERROR_62_FILE_NOT_FOUND:
			msg = "FILE NOT FOUND";
		break;
		case ERROR_63_FILE_EXISTS:
			msg = "FILE EXISTS";
		break;
		default:
			Debug_printv("EC=%d?\r\n", errorCode);
		break;
	}
	sprintf(ErrorMessage, "%02d,%s,%02d,%02d", errorCode, msg, track, sector);
}


static const char* ParseName(const char* text, char* name, bool convert, bool includeSpace = false)
{
	char* ptrOut = name;
	const char* ptr = text;
	*name = 0;

	if (isspace(*ptr & 0x7f) || *ptr == ',' || *ptr == '=' || *ptr == ':')
	{
		ptr++;
	}

	// TODO: Should not do this - should use command length to test for the end of a command (use indicies instead of pointers?)
	while (*ptr != '\0')
	{
		if (!isspace(*ptr & 0x7f))
			break;
		ptr++;
	}
	if (*ptr != 0)
	{
		while (*ptr != '\0')
		{
			if ((!includeSpace && isspace(*ptr & 0x7f)) || *ptr == ',' || *ptr == '=' || *ptr == ':')
				break;
			if (convert) *ptrOut++ = petscii2ascii(*ptr++);
			else *ptrOut++ = *ptr++;
		}
	}
	*ptrOut = 0;
	return ptr;
}

static const char* ParseNextName(const char* text, char* name, bool convert)
{
	char* ptrOut = name;
	const char* ptr;
	*name = 0;

	// TODO: looking for these is bad for binary parameters (binary parameter commands should not come through here)
	ptr = strchr(text, ':');
	if (ptr == 0) ptr = strchr(text, '=');
	if (ptr == 0) ptr = strchr(text, ',');

	if (ptr)
		return ParseName(ptr, name, convert);
	*ptrOut = 0;
	return ptr;
}

static bool ParseFilenames(const char* text, char* filenameA, char* filenameB)
{
	bool success = false;
	text = ParseNextName(text, filenameA, true);
	if (text)
	{
		ParseNextName(text, filenameB, true);
		if (filenameB[0] != 0) success = true;
		else Error(ERROR_34_SYNTAX_ERROR);	// File name could not be found in the command
	}
	else
	{
		Error(ERROR_31_SYNTAX_ERROR);	// could not parse the command
	}
	return success;
}

static int ParsePartition(char** buf)
{
	int part = 0;

	while ((isdigit(**buf & 0x7f)) || **buf == ' ' || **buf == '@')
	{
		if (isdigit(**buf & 0x7f))	part = part * 10 + (**buf - '0');
		(*buf)++;
	}
	return 0;
}

void IEC_Commands::CD(int partition, char* filename)
{
	char filenameEdited[256];

	if (filename[0] == '/' && filename[1] == '/')
		sprintf(filenameEdited, "\\\\1541\\%s", filename + 2);
	else
		strcpy(filenameEdited, filename);

	int len = strlen(filenameEdited);

	for (int i = 0; i < len; i++)
	{
		if (filenameEdited[i] == '/')
			filenameEdited[i] = '\\';
		filenameEdited[i] = petscii2ascii(filenameEdited[i]);
	}

	Debug_printv("CD %s\r\n", filenameEdited);
	if (filenameEdited[0] == '_' && len == 1)
	{
		updateAction = POP_DIR;
	}
	else
	{
		if (displayingDevices)
		{
			if (strncmp(filename, "SD", 2) == 0)
			{
				SwitchDrive("SD:");
				displayingDevices = false;
				updateAction = DEVICE_SWITCHED;
			}
			else
			{
				for (int USBDriveIndex = 0; USBDriveIndex < numberOfUSBMassStorageDevices; ++USBDriveIndex)
				{
					char USBDriveId[16];
					sprintf(USBDriveId, "USB%02d:", USBDriveIndex + 1);

					if (strncmp(filename, USBDriveId, 5) == 0)
					{
						SwitchDrive(USBDriveId);
						displayingDevices = false;
						updateAction = DEVICE_SWITCHED;
					}
				}
			}
		}
		else
		{
			DIR dir;
			FILINFO filInfo;

			char path[256] = { 0 };
			char* pattern = strrchr(filenameEdited, '\\');

			if (pattern)
			{
				// Now we look for a folder
				int len = pattern - filenameEdited;
				strncpy(path, filenameEdited, len);

				pattern++;

				if ((f_stat(path, &filInfo) != FR_OK) || !IsDirectory(filInfo))
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}
				else
				{
					char cwd[1024];
					if (f_getcwd(cwd, 1024) == FR_OK)
					{
						f_chdir(path);

						char cwd2[1024];
						f_getcwd(cwd2, 1024);

						bool found = f_findfirst(&dir, &filInfo, ".", pattern) == FR_OK && filInfo.fname[0] != 0;

						//Debug_printv("%s pattern = %s\r\n", filInfo.fname, pattern);

						if (found)
						{
							if (DiskImage::IsDiskImageExtention(filInfo.fname))
							{
								if (f_stat(filInfo.fname, &filInfoSelectedImage) == FR_OK)
								{
									strcpy((char*)selectedImageName, filInfo.fname);
								}
								else
								{
									f_chdir(cwd);
									Error(ERROR_62_FILE_NOT_FOUND);
								}
							}
							else
							{
								//Debug_printv("attemting changing dir %s\r\n", filInfo.fname);
								if (f_chdir(filInfo.fname) != FR_OK)
								{
									Error(ERROR_62_FILE_NOT_FOUND);
									f_chdir(cwd);
								}
								else
								{
									updateAction = DIR_PUSHED;
								}
							}
						}
						else
						{
							Error(ERROR_62_FILE_NOT_FOUND);
							f_chdir(cwd);
						}

					}
					//if (f_getcwd(cwd, 1024) == FR_OK)
					//	Debug_printv("CWD on exit = %s\r\n", cwd);
				}
			}
			else
			{
				bool found = FindFirst(dir, filenameEdited, filInfo);

				if (found)
				{
					if (DiskImage::IsDiskImageExtention(filInfo.fname))
					{
						if (f_stat(filInfo.fname, &filInfoSelectedImage) == FR_OK)
							strcpy((char*)selectedImageName, filInfo.fname);
						else
							Error(ERROR_62_FILE_NOT_FOUND);
					}
					else
					{
						//Debug_printv("attemting changing dir %s\r\n", filInfo.fname);
						if (f_chdir(filInfo.fname) != FR_OK)
							Error(ERROR_62_FILE_NOT_FOUND);
						else
							updateAction = DIR_PUSHED;
					}
				}
				else
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}
			}
		}
	}
}

void IEC_Commands::MKDir(int partition, char* filename)
{
	char filenameEdited[256];

	if (filename[0] == '/' && filename[1] == '/')
		sprintf(filenameEdited, "\\\\1541\\%s", filename + 2);
	else
		strcpy(filenameEdited, filename);
	int len = strlen(filenameEdited);

	for (int i = 0; i < len; i++)
	{
		if (filenameEdited[i] == '/')
			filenameEdited[i] = '\\';

		filenameEdited[i] = petscii2ascii(filenameEdited[i]);
	}

	f_mkdir(filenameEdited);

	// Force the FileBrowser to refresh incase it just heppeded to be in the folder that they are looking at
	updateAction = REFRESH;
}

void IEC_Commands::RMDir(void)
{
	DIR dir;
	FILINFO filInfo;
	FRESULT res;
	char filename[256];
	Channel& channel = channels[15];

	const char* text = (char*)channel.buffer;

	text = ParseNextName(text, filename, true);
	if (filename[0])
	{
		res = f_findfirst(&dir, &filInfo, ".", (const TCHAR*)filename);
		if (res == FR_OK)
		{
			if (filInfo.fname[0] != 0 && IsDirectory(filInfo))
			{
				Debug_printv("rmdir %s\r\n", filInfo.fname);
				f_unlink(filInfo.fname);
				updateAction = REFRESH;
			}
		}
		else
		{
			Error(ERROR_62_FILE_NOT_FOUND);
		}
	}
	else
	{
		Error(ERROR_34_SYNTAX_ERROR);
	}
}

void IEC_Commands::FolderCommand(void)
{
	Channel& channel = channels[15];

	switch (toupper(channel.buffer[0]))
	{
		case 'M':
		{
			char* in = (char*)channel.buffer;
			int part;

			part = ParsePartition(&in);
			if (part > 0)
			{
				// Only have one drive partition
				//Error(ERROR_74_DRlVE_NOT_READY);
				return;
			}
			in += 2;	// Skip command
			if (*in == ':')
				in++;
			MKDir(part, in);
		}
		break;
		case 'C':
		{
			char* in = (char*)channel.buffer;
			int part;

			part = ParsePartition(&in);
			if (part > 0)
			{
				// Only have one drive partition
				//Error(ERROR_74_DRlVE_NOT_READY);
				return;
			}
			in += 2;	// Skip command
			if (*in == ':')
				in++;
			CD(part, in);
		}
		break;
		case 'R':
			RMDir();
		break;
		default:
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

void IEC_Commands::Copy(void)
{
	//COPY:newfile = oldfile1, oldfile2,...

	// Only named data records can be combined.

	// TODO: checkfor wildcards and set the error if found.
	char filenameNew[256];
	char filenameToCopy[256];
	Channel& channel = channels[15];

	FILINFO filInfo;
	FRESULT res;
	const char* text = (char*)channel.buffer;

	text = ParseNextName(text, filenameNew, true);

	//Debug_printv("Copy %s\r\n", filenameNew);
	if (filenameNew[0] != 0)
	{
		res = f_stat(filenameNew, &filInfo);
		if (res == FR_NO_FILE)
		{
			int fileCount = 0;
			do
			{
				text = ParseNextName(text, filenameToCopy, true);
				if (filenameToCopy[0] != 0)
				{
					//Debug_printv("Copy source %s\r\n", filenameToCopy);
					res = f_stat(filenameToCopy, &filInfo);
					if (res == FR_OK)
					{
						if (!IsDirectory(filInfo))
						{
							//Debug_printv("copying %s to %s\r\n", filenameToCopy, filenameNew);
							if (CopyFile(filenameNew, filenameToCopy, fileCount != 0)) updateAction = REFRESH;
							else Error(ERROR_25_WRITE_ERROR);
						}
					}
					else
					{
						// If you want to copy the entire folder then implement that here.
						Error(ERROR_62_FILE_NOT_FOUND);
					}
				}
				fileCount++;
			} while (filenameToCopy[0] != 0);
		}
		else
		{
			Debug_printv("Copy file exists\r\n");
			Error(ERROR_63_FILE_EXISTS);
		}
	}
	else
	{
		Error(ERROR_34_SYNTAX_ERROR);
	}
}

void IEC_Commands::ChangeDevice(void)
{
	Channel& channel = channels[15];
	const char* text = (char*)channel.buffer;

	if (strlen(text) > 2)
	{
		int deviceIndex = atoi(text + 2);

		if (deviceIndex == 0)
		{
			SwitchDrive("SD:");
			displayingDevices = false;
			updateAction = DEVICE_SWITCHED;
		}
		else if ((deviceIndex - 1) < numberOfUSBMassStorageDevices)
		{
			char USBDriveId[16];
			sprintf(USBDriveId, "USB%02d:", deviceIndex);
			SwitchDrive(USBDriveId);
			displayingDevices = false;
			updateAction = DEVICE_SWITCHED;
		}
		else
		{
			Error(ERROR_74_DRlVE_NOT_READY);
		}
	}
	else
	{
		Error(ERROR_31_SYNTAX_ERROR);
	}
}

void IEC_Commands::Memory(void)
{
	Channel& channel = channels[15];
	char* text = (char*)channel.buffer;
	u16 address;
	int length;
	u8 bytes = 1;
	u8* ptr;

	if (channel.cursor > 2)
	{
		char code = toupper(channel.buffer[2]);
		if (code == 'R' || code == 'W' || code == 'E')
		{
			ptr = (u8*)strchr(text, ':');
			if (ptr == 0) ptr = (u8*)&channel.buffer[3];
			else ptr++;

			length = channel.cursor - 3;

			address = (u16)((u8)(ptr[1]) << 8) | (u16)ptr[0];
			if (length > 2)
			{
				bytes = ptr[2];
				if (bytes == 0)
					bytes = 1;
			}

			switch (code)
			{
				case 'R':
					Debug_printv("M-R %04x %d\r\n", address, bytes);
				break;
				case 'W':
					Debug_printv("M-W %04x %d\r\n", address, bytes);
				break;
				case 'E':
					// Memory execute impossible at this level of emulation!
					Debug_printv("M-E %04x\r\n", address);
				break;
			}
		}
		else
		{
			Error(ERROR_31_SYNTAX_ERROR);
		}
	}
}

void IEC_Commands::New(void)
{
	Channel& channel = channels[15];
	char filenameNew[256];
	char ID[256];

	if (ParseFilenames((char*)channel.buffer, filenameNew, ID))
	{
		FILINFO filInfo;

		int ret = CreateNewDisk(filenameNew, ID, true);

		if (ret==0)
			updateAction = REFRESH;
		else
			Error(ret);
	}
}

void IEC_Commands::Rename(void)
{
	// RENAME:newname=oldname

	// Note: 1541 ROM will not allow you to rename a file until it is closed.

	Channel& channel = channels[15];
	char filenameNew[256];
	char filenameOld[256];

	if (ParseFilenames((char*)channel.buffer, filenameNew, filenameOld))
	{
		FRESULT res;
		FILINFO filInfo;

		res = f_stat(filenameNew, &filInfo);
		if (res == FR_NO_FILE)
		{
			res = f_stat(filenameOld, &filInfo);
			if (res == FR_OK)
			{
				// Rename folders too.
				//Debug_printv("Renaming %s to %s\r\n", filenameOld, filenameNew);
				f_rename(filenameOld, filenameNew);
			}
			else
			{
				Error(ERROR_62_FILE_NOT_FOUND);
			}
		}
		else
		{
			Error(ERROR_63_FILE_EXISTS);
		}
	}
}

void IEC_Commands::Scratch(void)
{
	// SCRATCH: filename1, filename2...

	// More than one file can be deleted by using a single command.
	// But remember that only 40 characters at a time can be sent
	// over the transmission channel to the disk drive.

	// wildcard characters can be used

	Channel& channel = channels[15];
	DIR dir;
	FILINFO filInfo;
	FRESULT res;
	char filename[256];
	const char* text = (const char*)channel.buffer;

	text = ParseNextName(text, filename, true);
	while (filename[0])
	{
		res = f_findfirst(&dir, &filInfo, ".", (const TCHAR*)filename);
		while (res == FR_OK && filInfo.fname[0])
		{
			if (filInfo.fname[0] != 0 && !IsDirectory(filInfo))
			{
				//Debug_printv("Scratching %s\r\n", filInfo.fname);
				f_unlink(filInfo.fname);
			}
			res = f_findnext(&dir, &filInfo);
			updateAction = REFRESH;
		}
		text = ParseNextName(text, filename, true);
	}
}

void IEC_Commands::User(void)
{
	Channel& channel = channels[15];

	//Debug_printv("User channel.buffer[1] = %c\r\n", channel.buffer[1]);

	switch (toupper(channel.buffer[1]))
	{
		case 'A':
		case 'B':
		case '1':	// Direct access block read (Jumps via FFEA to B-R function)
		case '2':	// Direct access block write (Jumps via FFEC to B-W function)
			// Direct acces is unsupported. Without a mounted disk image tracks and sectors have no meaning.
			Error(ERROR_31_SYNTAX_ERROR);
		break;

		// U3/C - U8/H meaningless at this level of emulation!

		// U9 (UI)
		case 'I':
		case '9':
			//Debug_printv("ui c=%d\r\n", channel.cursor);
			if (channel.cursor == 2)
			{
				// Soft reset
				Error(ERROR_73_DOSVERSION);
				return;
			}
			switch (channel.buffer[2])
			{
				case '+':
					usingVIC20 = true;
				break;
				case '-':
					usingVIC20 = false;
				break;
				default:
					Error(ERROR_73_DOSVERSION);
				break;
			}
		break;

		case 'J':
		case ':':
			// Hard reset
			Error(ERROR_73_DOSVERSION);
		break;
		case 202:
			// Really hard reset - reboot Pi
			//Reboot_Pi();
		break;
		case '0':
			//OPEN1,8,15,"U0>"+CHR$(9):CLOSE1
			if ((channel.buffer[2] & 0x1f) == 0x1e && channel.buffer[3] >= 4 && channel.buffer[3] <= 30)
			{
				SetDeviceId(channel.buffer[3]);
				updateAction = DEVICEID_CHANGED;
				Debug_printv("Changed deviceID to %d\r\n", channel.buffer[3]);
			}
			else
			{
				Error(ERROR_31_SYNTAX_ERROR);
			}
		break;
		default:
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

void IEC_Commands::Extended(void)
{
	Channel& channel = channels[15];

	//Debug_printv("User channel.buffer[1] = %c\r\n", channel.buffer[1]);

	switch (toupper(channel.buffer[1]))
	{
		case '?':
			Error(ERROR_73_DOSVERSION);
		break;
		default:
			// Extended commands not implemented yet
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

// http://www.n2dvm.com/UIEC.pdf
void IEC_Commands::ProcessCommand(std::string command)
{
	Error(ERROR_00_OK);

	Channel& channel = channels[15];

	//Debug_printv("CMD %s %d\r\n", channel.buffer, channel.cursor);

	if (channel.cursor > 0 && channel.buffer[channel.cursor - 1] == 0x0d)
		channel.cursor--;

	if (channel.cursor == 0)
	{
		Error(ERROR_30_SYNTAX_ERROR);
	}
	else
	{
		//Debug_printv("ProcessCommand %s", channel.buffer);

		if (toupper(channel.buffer[0]) != 'X' && toupper(channel.buffer[1]) == 'D')
		{
			FolderCommand();
			return;
		}

		switch (toupper(channel.buffer[0]))
		{
			case 'B':
				// B-P not implemented
				// B-A allocate bit in BAM not implemented
				// B-F free bit in BAM not implemented
				// B-E block execute impossible at this level of emulation!
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'C':
				if (channel.buffer[1] == 'P')
					ChangeDevice();
				else
					Copy();
			break;
			case 'D':
				Error(ERROR_31_SYNTAX_ERROR);	// DI, DR, DW not implemented yet
			break;
			case 'G':
				Error(ERROR_31_SYNTAX_ERROR);	// G-P not implemented yet
			break;
			case 'I':
				// Initialise
			break;
			case 'M':
				Memory();
			break;
			case 'N':
				New();
			break;
			case 'P':
				Error(ERROR_31_SYNTAX_ERROR);	// P not implemented yet
			break;
			case 'R':
				Rename();
			break;
			case 'S':
				if (channel.buffer[1] == '-')
				{
					// Swap drive number 
					Error(ERROR_31_SYNTAX_ERROR);
					break;
				}
				Scratch();
			break;
			case 'T':
				// RTC support
				Error(ERROR_31_SYNTAX_ERROR);	// T-R and T-W not implemented yet
			break;
			case 'U':
				User();
			break;
			case 'V':
			break;
			case 'W':
				// Save out current options?
				//OPEN1, 9, 15, "XW":CLOSE1
			break;
			case 'X':
				Extended();
			break;
			case '/':
				
			break;
			default:
				Error(ERROR_31_SYNTAX_ERROR);
			break;
		}
	}
}


void IEC_Commands::OpenFile()
{
	// OPEN lfn,id,sa,"filename,filetype,mode"
	// lfn
	//		When the logical file number(lfn) is between 1 and 127, a PRINT# statement sends a RETURN character to the file after each variable.
	//		If the logical file number is greater than 127 (128 - 255), the PRINT# statement sends an additional linefeed after each RETURN.
	// sa
	//		Should several files be open at once, they must all use different secondary addresses, as only one file can use a channel.
	// mode
	// The last parameter(mode) establishes how the channel will used. There are four possibilities:
	//	W  Write a file
	//	R  Read a file
	//	A  Add to a sequential file
	//	M  read a file that has not been closed	

	// If a file that already exists is to be to opened for writing, the file 	must first be deleted.

	// The file type must be given when the file is opened. The file type may be shortened to one of following:
	//	S - sequential file
	//	U - user file
	//	P - program
	//	R - relative file
	u8 secondary = secondaryAddress;
	Channel& channel = channels[secondary];
	if (channel.command[0] == '#')
	{
		Channel& channelCommand = channels[15];

		// Direct acces is unsupported. Without a mounted disk image tracks and sectors have no meaning.
		//Debug_printv("Driect access\r\n");
		if (strcmp((char*)channelCommand.buffer, "U1:13 0 01 00") == 0)
		{
			// This is a 128 trying to auto boot
			memset(channel.buffer, 0, 256);
			channel.cursor = 256;

			if (autoBootFB128)
			{
				int index = 0;
				channel.buffer[0] = 'C';
				channel.buffer[1] = 'B';
				channel.buffer[2] = 'M';
				index += 3;
				index += 4;
				channel.buffer[index++] = 'P';
				channel.buffer[index++] = 'I';
				channel.buffer[index++] = '1';
				channel.buffer[index++] = '5';
				channel.buffer[index++] = '4';
				channel.buffer[index++] = '1';
				channel.buffer[index++] = ' ';
				channel.buffer[index++] = 'F';
				channel.buffer[index++] = 'B';
				channel.buffer[index++] = '1';
				channel.buffer[index++] = '2';
				channel.buffer[index++] = '8';
				index++;
				channel.buffer[index++] = 'F';
				channel.buffer[index++] = 'B';
				channel.buffer[index++] = '1';
				channel.buffer[index++] = '2';
				channel.buffer[index++] = '8';
				index++;
				channel.buffer[index++] = 0xa2;
				channel.buffer[index] = (index + 5);
				index++;
				channel.buffer[index++] = 0xa0;
				channel.buffer[index++] = 0xb;
				channel.buffer[index++] = 0x4c;
				channel.buffer[index++] = 0xa5;
				channel.buffer[index++] = 0xaf;
				channel.buffer[index++] = 'R';
				channel.buffer[index++] = 'U';
				channel.buffer[index++] = 'N';
				channel.buffer[index++] = '\"';
				channel.buffer[index++] = 'F';
				channel.buffer[index++] = 'B';
				channel.buffer[index++] = '1';
				channel.buffer[index++] = '2';
				channel.buffer[index++] = '8';
				channel.buffer[index++] = '\"';
				channel.fileSize = 256;
			}
			if (C128BootSectorName)
			{
				FIL fpBS;
				u32 bytes;
				if (FR_OK == f_open(&fpBS, C128BootSectorName, FA_READ))
					f_read(&fpBS, channel.buffer, 256, &bytes);
				else
					memset(channel.buffer, 0, 256);
				channel.fileSize = 256;
			}

			if (SendBuffer(channel, true))
				return;
		}
	}
	else if (channel.command[0] == '$')
	{
	}
	else
	{
		if (!channel.open)
		{
			bool found = false;
			DIR dir;
			FRESULT res;
			const char* text;
			char filename[256];
			char filetype[8];
			char filemode[8];
			bool needFileToExist = true;
			bool writing = false;
			u8 mode = FA_READ;

			filetype[0] = 0;
			filemode[0] = 0;

			if (secondary == 1)
				strcat(filemode, "W");

			char* in = (char*)channel.command;
			int part = ParsePartition(&in);
			if (part > 0)
			{
				// Only have one drive partition
				//Error(ERROR_74_DRlVE_NOT_READY);
				return;
			}
			if (*in == ':')
				in++;
			else
				in = (char*)channel.command;

			text = ParseName((char*)in, filename, true, true);
			if (text)
			{
				text = ParseNextName(text, filetype, true);
				if (text)
					text = ParseNextName(text, filemode, true);
			}

			if (starFileName && starFileName[0] != 0 && filename[0] == '*')
			{
				char cwd[1024];
				if (f_getcwd(cwd, 1024) == FR_OK)
				{
					const char* folder = strstr(cwd, "/");
					if (folder)
					{
						if (strcasecmp(folder, "/1541") == 0)
						{
							strncpy(filename, starFileName, sizeof(filename) - 1);
						}
					}
				}
			}
			

			if (toupper(filetype[0]) == 'L')
			{
				//Debug_printv("Rel file\r\n");
				return;
			}
			else
			{
				switch (toupper(filemode[0]))
				{
					case 'W':
						needFileToExist = false;
						writing = true;
						mode = FA_CREATE_ALWAYS | FA_WRITE;
					break;
					case 'A':
						needFileToExist = true;
						writing = true;
						mode = FA_OPEN_APPEND | FA_WRITE;
					break;
					case 'R':
						needFileToExist = true;
						writing = false;
						mode = FA_READ;
					break;
				}
			}

			channel.writing = writing;

			//Debug_printv("OpenFile %s %d NE=%d T=%c M=%c W=%d %0x\r\n", filename, secondary, needFileToExist, filetype[0], filemode[0], writing, mode);

			if (needFileToExist)
			{
				if (FindFirst(dir, filename, channel.filInfo))
				{
					//Debug_printv("found\r\n");
					res = FR_OK;
					while ((channel.filInfo.fattrib & AM_DIR) == AM_DIR)
					{
						res = f_findnext(&dir, &channel.filInfo);
					}

					if (res == FR_OK && channel.filInfo.fname[0] != 0)
					{
						found = true;
						res = f_open(&channel.file, channel.filInfo.fname, mode);
						if (res == FR_OK)
							channel.open = true;
						//Debug_printv("Opened existing size = %d\r\n", (int)channel.filInfo.fsize);
					}
				}
				else
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}

				if (!found)
				{
					Debug_printv("Can't find %s", filename);
					Error(ERROR_62_FILE_NOT_FOUND);
				}
			}
			else
			{
				res = f_open(&channel.file, filename, mode);
				if (res == FR_OK)
				{
					channel.open = true;
					channel.cursor = 0;
					//Debug_printv("Opened new sa=%d m=%0x\r\n", secondary, mode);
					res = f_stat(filename, &channel.filInfo);
				}
				else
				{
					//Debug_printv("Open failed %d\r\n", res);
				}
			}
		}
		else
		{
			//Debug_printv("Channel aready opened %d\r\n", channel.cursor);
		}
	}
}
*/