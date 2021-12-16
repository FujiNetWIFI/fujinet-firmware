#ifdef BUILD_CBM
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

#ifdef BUILD_CBM

#include "disk.h"

#include <stdarg.h>
#include <string.h>
#include <FS.h>

#include "../../include/version.h"

#include "fnSystem.h"
#include "led.h"
#include "utils.h"

using namespace CBM;

// External ref to fuji object.
//extern iecFuji theFuji;

iecDisk::iecDisk()
{
    device_active = false;
}


void iecDisk::sendStatus(void)
{
	int i, readResult;
	
	std::string status("00, OK, 00, 08");
	readResult = status.length();

	Debug_printf("\r\nsendStatus: ");
	// Length does not include the CR, write all but the last one should be with EOI.
	for (i = 0; i < readResult - 2; ++i)
		IEC.send(status[i]);

	// ...and last byte in string as with EOI marker.
	IEC.sendEOI(status[i]);
} // sendStatus


void iecDisk::sendDeviceStatus()
{
	Debug_printf("\r\nsendDeviceStatus:\r\n");

	// Reset basic memory pointer:
	uint16_t basicPtr = PET_BASIC_START;

	// Send load address
	IEC.send(PET_BASIC_START bitand 0xff);
	IEC.send((PET_BASIC_START >> 8) bitand 0xff);
	Debug_println("");

	// Send List HEADER
	// sendLine(basicPtr, 0, "\x12 %s V%s ", PRODUCT_ID, FW_VERSION);
	sendLine(basicPtr, 0, "\x12 %s V%s ", PRODUCT_ID, FN_VERSION_FULL);

	// Current Config
	sendLine(basicPtr, 0, "DEVICE    : %d", _device_id);
	sendLine(basicPtr, 0, "DRIVE     : %d", _drive);
	sendLine(basicPtr, 0, "PARTITION : %d", _partition);
	sendLine(basicPtr, 0, "URL       : %s", _url);
	sendLine(basicPtr, 0, "PATH      : %s", _path);
	sendLine(basicPtr, 0, "IMAGE     : %s", _image);
	sendLine(basicPtr, 0, "FILENAME  : %s", _filename);

	// End program with two zeros after last line. Last zero goes out as EOI.
	IEC.send(0);
	IEC.sendEOI(0);

	fnLedManager.set(LED_BUS);
} // sendDeviceStatus


void iecDisk::_open(void)
{
	uint8_t pos = 0;
	uint8_t lpos = 0;
	std::string command(IEC.ATN.data);

	// Parse Command
	lpos = command.find_first_of(":", pos);
	_command = command.substr(pos, lpos - 1);
	pos = lpos + 1;
	_drive = atoi(_command.c_str());

	// Parse Filename
	lpos = command.find_first_of(",", pos);
	_filename = command.substr(pos, lpos - 1);
	pos = lpos + 1;

	// Parse Extension
	_extension = _filename.substr(_filename.find_last_of(".") + 1);
	util_string_toupper(_extension);
	if ( _extension.length() > 4 || _extension.length() == _filename.length() )
		_extension = "";	

	// Parse File Type
	lpos = command.find_first_of(",", pos);
	_type = command.substr(pos, lpos - 1);
	pos = lpos + 1;

	// Parse File Mode
	_mode = command.substr(pos);
	


	// TODO this whole directory handling needs to be
	// rewritten using the fnFs** classes. in fact it
	// might be handled by the fuji device class
	// that is called by the sio config routines

	// FILE* local_file = m_fileSystem->file_open( std::string(m_device.path() + m_filename).c_str() );

	//Serial.printf("\r\n$IEC: DEVICE[%d] DRIVE[%d] PARTITION[%d] URL[%s] PATH[%s] IMAGE[%s] FILENAME[%s] FILETYPE[%s] COMMAND[%s]\r\n", m_device.device(), m_device.drive(), m_device.partition(), m_device.url().c_str(), m_device.path().c_str(), m_device.image().c_str(), m_filename.c_str(), m_filetype.c_str(), ATN.data);
	if (_filename[0] == '$')
	{
		_openState = O_DIR;
	}
	// else if ( local_file.isDirectory() )
	// {
	// 	// Enter directory
	// 	Debug_printf("\r\nchangeDir: [%s] >", m_filename.c_str());
	// 	m_device.path(m_device.path() + m_filename.substr(3) + "/");
	// 	_openState = O_DIR;	
	// }
	// else if (std::string( IMAGE_TYPES ).find(m_filetype) >= 0 && m_filetype.length() > 0 )
	else if (std::string(IMAGE_TYPES).find(_extension) < std::string::npos && _extension.length() > 0)
	{
		// Mount image file
		Debug_printf("\r\nmount: [%s] >", _filename.c_str());
		//m_device.image( m_filename );

		_openState = O_DIR;
	}
	else if (util_starts_with(_filename, "HTTP://") || util_starts_with(_filename, "TNFS://"))
	{
		// Mount url
		Debug_printf("\r\nnet mount: [%s] >", _filename.c_str());
		_partition = 0;
		_url = _filename.substr(7);
		_path = "/";
		_image = "";

		_openState = O_DIR;
	}
	else if (util_starts_with(_filename, "CD"))
	{
		if (_filename.back() == '_')
		{
			if (_image.length())
			{
				// Unmount image file
				//Debug_printf("\r\nunmount: [%s] <", m_device.image().c_str());
				_image ="";
			}
			else if ( _url.length() && _path == "/" )
			{
				// Unmount url
				//Debug_printf("\r\nunmount: [%s] <", m_device.url().c_str());
				_url = "";
			}
			else
			{
				// Go back a directory
				//Debug_printf("\r\nchangeDir: [%s] <", m_filename.c_str());
				_path = _path.substr(0, _path.find_last_of("/", _path.length() - 2) + 1);

				if (!_path.length())
				{
					_path = "/";
				}				
			}
		}
		else if (_filename.length() > 3)
		{
			// Switch to root
			if (util_starts_with(_filename, "CD//"))
			{
				_path = "";
				_image = "";
			}

			if (std::string(IMAGE_TYPES).find(_extension) < std::string::npos && !_extension.empty())
			{
				// Mount image file
				//Debug_printf("\r\nmount: [%s] >", m_filename.c_str());
				_image = _filename.substr(3);
			}
			else
			{
				// Enter directory
				//Debug_printf("\r\nchangeDir: [%s] >", m_filename.c_str());
				_path = _path + _filename.substr(3) + "/";
			}
		}

		if (IEC.ATN.channel == 0x00)
		{
			_openState = O_DIR;
		}
	}
	else if (util_starts_with(_filename, "@INFO"))
	{
		_filename = "";
		_openState = O_SYSTEM_INFO;
	}
	else if (util_starts_with(_filename, "@STAT"))
	{
		_filename = "";
		_openState = O_DEVICE_STATUS;
	}
	else
	{
		_openState = O_FILE;
	}

	if ( _openState == O_DIR )
	{
		_filename = "$";
		_extension = "";
		IEC.ATN.data[0] = '\0';
	}

	//Debug_printf("\r\n_open: %d (M_OPENSTATE) [%s]", _openState, m__iec_cmd.data);
	Debug_printf("\r\n$IEC: DEVICE[%d] DRIVE[%d] PARTITION[%d] URL[%s] PATH[%s] IMAGE[%s] FILENAME[%s] FILETYPE[%s] COMMAND[%s]\r\n", _device_id, _drive, _partition, _url.c_str(), _path.c_str(), _image.c_str(), _filename.c_str(), _extension.c_str(), IEC.ATN.data);

} // _open


void iecDisk::_talk_data(int chan)
{
	// process response into _queuedError.
	// Response: ><code in binary><CR>

	Debug_printf("\r\n_talk_data: %d (CHANNEL) %d (M_OPENSTATE)", chan, _openState);

	if (chan == CMD_CHANNEL)
	{
		// Send status message
		sendStatus();
		// go back to OK state, we have dispatched the error to IEC host now.
		_queuedError = ErrOK;
	}
	else
	{

		//Debug_printf("\r\n_openState: %d", _openState);

		switch (_openState)
		{
			case O_NOTHING:
				// Say file not found
				IEC.sendFNF();
				break;

			case O_INFO:
				// Reset and send SD card info
				reset();
				sendListing();
				break;

			case O_FILE:
				// Send program file
				sendFile();
				break;

			case O_DIR:
				// Send listing
				sendListing();
				break;

			case O_FILE_ERR:
				// FIXME: interface with Host for error info.
				//sendListing(/*&send_file_err*/);
				IEC.sendFNF();
				break;

			case O_SYSTEM_INFO:
				// Send device info
				sendSystemInfo();
				break;

			case O_DEVICE_STATUS:
				// Send device info
				sendDeviceStatus();
				break;
		}
	}

} // _talk_data


void iecDisk::_listen_data()
{
	// int lengthOrResult = 0;
	// bool wasSuccess = false;


	// Debug_printf("\r\n_listen_data: ");

	// if (ErrOK == _queuedError)
	// 	saveFile();
	// else // FIXME: Check what the drive does here when saving goes wrong. FNF is probably not right. Dummyread entire buffer from CBM?
	// 	IEC.sendFNF();
	
} // _listen_data


void iecDisk::_close()
{
	Debug_printf("\r\n_close: Success!");

	//Serial.printf("\r\nIEC: DEVICE[%d] DRIVE[%d] PARTITION[%d] URL[%s] PATH[%s] IMAGE[%s] FILENAME[%s] FILETYPE[%s]\r\n", m_device.device(), m_device.drive(), m_device.partition(), m_device.url().c_str(), m_device.path().c_str(), m_device.image().c_str(), m_filename.c_str(), m_filetype.c_str());
	Debug_printf("\r\n=================================\r\n\r\n");

	_filename = "";
} // _close



void iecDisk::sendListing()
{
	Debug_printf("\r\nsendListing:\r\n");

	uint16_t byte_count = 0;
	std::string extension = "DIR";

	// Reset basic memory pointer:
	uint16_t basicPtr = PET_BASIC_START;

	// Send load address
	IEC.send(PET_BASIC_START bitand 0xff);
	IEC.send((PET_BASIC_START >> 8) bitand 0xff);
	byte_count += 2;
	Debug_println("");

    byte_count += sendHeader( basicPtr );

	// TODO directory handling!!!!!

	// Send List ITEMS
	// byte_count += sendLine(basicPtr, 200, "\"THIS IS A FILE\"     PRG");
	// byte_count += sendLine(basicPtr, 57, " \"THIS IS A FILE 2\"   PRG");
	DIR *dir = NULL;
    struct dirent *ent;
    char tpath[255];
    struct stat sb;

	_path = "/spiffs/";
 	dir = opendir(_path.c_str());
	if (!dir) 
	{
        Debug_printf("sendListing: Error opening directory\n");
        return;
    }

	while ((ent = readdir(dir)) != NULL) 
	{
		std::string file(ent->d_name);
		// Get file stat
		sprintf(tpath, _path.c_str());
		strcat(tpath,ent->d_name);
        stat(tpath, &sb);


		uint16_t block_cnt = sb.st_size / 256;
		int block_spc = 3;
		if (block_cnt > 9) block_spc--;
		if (block_cnt > 99) block_spc--;
		if (block_cnt > 999) block_spc--;

		int space_cnt = 21 - (file.length() + 5);
		if (space_cnt > 21)
			space_cnt = 0;
		
		if(sb.st_size) 
		{
			block_cnt = sb.st_size/256;

			uint8_t ext_pos = file.find_last_of(".") + 1;
			if (ext_pos && ext_pos != strlen(ent->d_name))
			{
				extension = file.substr(ext_pos);
				util_string_toupper(extension);
				//extension.toUpperCase();
			}
			else
			{
				extension = "PRG";
			}
		}
		else
		{
			extension = "DIR";
		}

		// Don't show hidden folders or files
		//if(!file.rfind(".", 0))
		//{
			byte_count += sendLine(basicPtr, block_cnt, "%*s\"%s\"%*s %3s", block_spc, "", file.c_str(), space_cnt, "", extension.c_str());
		//}
		
		//Debug_printf(" (%d, %d)\r\n", space_cnt, byte_count);
		fnLedManager.toggle(LED_BUS);
		//toggleLED(true);
	}	
    byte_count += sendFooter( basicPtr );

	// End program with two zeros after last line. Last zero goes out as EOI.
	IEC.send(0);
	IEC.sendEOI(0);

	Debug_printf("\r\nsendListing: %d Bytes Sent\r\n", byte_count);

	//ledON();
	fnLedManager.set(LED_BUS);
} // sendListing


uint16_t iecDisk::sendFooter(uint16_t &basicPtr)
{
	// Send List FOOTER
	// todo TODO figure out fnFS equivalents
	// vfs stat?
	//FSInfo64 fs_info;
	//m_fileSystem->info64(fs_info);
	// return sendLine(basicPtr, (fs_info.totalBytes-fs_info.usedBytes)/256, "BLOCKS FREE.");
	return sendLine(basicPtr, 65535, "BLOCKS FREE.");
	//Debug_println("");
}


void iecDisk::sendFile()
{
	uint16_t i = 0;
	bool success = true;

	uint16_t bi = 0;
	uint16_t load_address = 0;
	char b[1];
	char ba[9];

	ba[8] = '\0';

	// Find first program
	//if(m_filename.endsWith("*"))
	if (_filename.back() == '*')
	{
		_filename = "";

		if (_path == "/" && _image.length() == 0)
		{
			_filename = "FB64";
		}
		// TODO directory handling
		// else
		// {
		// 	Dir dir = m_fileSystem->openDir(m_device.path());
		// 	while (dir.next() && dir.isDirectory())
		// 	{
		// 		Debug_printf("\r\nsendFile: %s", dir.fileName().c_str());
		// 	}
		// 	if(dir.isFile())
		// 		m_filename = dir.fileName();			
		// }
	}
	_path = "/spiffs/";
	std::string inFile = std::string(_path+_filename);
	
	Debug_printf("\r\nsendFile: %s\r\n", inFile.c_str());

	FILE* file = fnSPIFFS.file_open(inFile.c_str(), "r");
	
	if (!file)
	{
		Debug_printf("\r\nsendFile: %s (File Not Found)\r\n", inFile.c_str());
		IEC.sendFNF();
	}
	else
	{
		size_t len = fnSPIFFS.filesize(file);

		// Get file load address
		fread(&b, 1, 1, file);
		success = IEC.send(b[0]);
		load_address = *b & 0x00FF; // low byte
		fread(&b, 1, 1, file);
		success = IEC.send(b[0]);
		load_address = load_address | *b << 8;  // high byte
		// fseek(file, 0, SEEK_SET);

		Debug_printf("\r\nsendFile: [%s] [$%.4X] (%d bytes)\r\n=================================\r\n", inFile.c_str(), load_address, len);
		for (i = 2; success and i < len; ++i) 
		{
			success = fread(&b, 1, 1, file);
			if (success)
			{
#ifdef DATA_STREAM
				if (bi == 0)
				{
					Debug_printf(":%.4X ", load_address);
					load_address += 8;
				}
#endif
				if (i == len - 1)
				{
					success = IEC.sendEOI(b[0]); // indicate end of file.
				}
				else
				{
					success = IEC.send(b[0]);
				}

#ifdef DATA_STREAM
				// Show ASCII Data
				if (b[0] < 32 || b[0] == 127) 
					b[0] = 46;

				ba[bi++] = b[0];

				if(bi == 8)
				{
					Debug_printf(" %s\r\n", ba);
					bi = 0;
				}
#endif

				// Toggle LED
				if (i % 50 == 0)
				{
					fnLedManager.toggle(LED_BUS);
					//Debug_printf("progress: %d %d\r", len, i);
				}
			}
		}
		fclose(file);
		Debug_println("");
		Debug_printf("%d bytes sent\r\n", i);
		fnLedManager.set(LED_BUS);

		if (!success || i != len)
		{
			Debug_println("sendFile: Transfer failed!");
		}
	}
} // sendFile

void iecDisk::saveFile()
{
	std::string outFile = std::string(_path+_filename);
	int b;
	
	Debug_printf("\r\nsaveFile: %s", outFile.c_str());

	FILE* file = fnSPIFFS.file_open(outFile.c_str(), "w");
//	noInterrupts();
	if (!file)
	{
		Debug_printf("\r\nsaveFile: %s (Error)\r\n", outFile.c_str());
	}
	else
	{	
		bool done = false;
		// Recieve bytes until a EOI is detected
		do {
			b = IEC.receive();
			done = (IEC.state() bitand eoiFlag) or (IEC.state() bitand errorFlag);

			//file.write(b);
			fwrite(&b, 2, 1, file);
		} while(not done);
		fclose(file);
	}
//	interrupts();
} // saveFile


/* 
void iecDevice::sendListingHTTP()
{
	Debug_printf("\r\nsendListingHTTP: ");

	uint16_t byte_count = 0;

	std::string user_agent( std::string(PRODUCT_ID) + " [" + std::string(FW_VERSION) + "]" );
	std::string url("http://"+m_device.url()+"/api/");
	std::string post_data("p="+urlencode(m_device.path())+"&i="+urlencode(m_device.image())+"&f="+urlencode(m_filename));

	// Connect to HTTP server
	HTTPClient client;
	client.setUserAgent( user_agent );
	client.setFollowRedirects(true);
	client.setTimeout(10000);
	if (!client.begin(url)) {
		Debug_println("\r\nConnection failed");
		IEC.sendFNF();
		return;
	}
	client.addHeader("Content-Type", "application/x-www-form-urlencoded");	

	Debug_printf("\r\nConnected!\r\n--------------------\r\n%s\r\n%s\r\n%s\r\n", user_agent.c_str(), url.c_str(), post_data.c_str());

	int httpCode = client.POST(post_data);            //Send the request
	WiFiClient payload = client.getStream();    //Get the response payload as Stream
	//std::string payload = client.getString();    //Get the response payload as std::string

	Debug_printf("HTTP Status: %d\r\n", httpCode);   //Print HTTP return code
	if(httpCode != 200)
	{
		Debug_println("Error");
		IEC.sendFNF();
		return;
	}

	//Serial.println(payload);    //Print request response payload
	m_lineBuffer = payload.readStringUntil('\n');

	// Reset basic memory pointer:
	uint16_t basicPtr = PET_BASIC_START;

	// Send load address
	IEC.send(PET_BASIC_START bitand 0xff);
	IEC.send((PET_BASIC_START >> 8) bitand 0xff);
	byte_count += 2;
	Debug_println("");

	do
	{
		// Parse JSON object
		DeserializationError error = deserializeJson(m_jsonHTTP, m_lineBuffer);
		if (error)
		{
			Serial.print(F("\r\ndeserializeJson() failed: "));
			Serial.println(error.c_str());
			break;
		}

		byte_count += sendLine(basicPtr, m_jsonHTTP["blocks"], "%s", urldecode(m_jsonHTTP["line"].as<std::string>()).c_str());
		toggleLED(true);
		m_lineBuffer = payload.readStringUntil('\n');
		//Serial.printf("\r\nlinebuffer: %d %s", m_lineBuffer.length(), m_lineBuffer.c_str());
	} while (m_lineBuffer.length() > 1);

	//End program with two zeros after last line. Last zero goes out as EOI.
	IEC.send(0);
	IEC.sendEOI(0);

	Debug_printf("\r\nsendListingHTTP: %d Bytes Sent\r\n", byte_count);

	client.end(); //Close connection

	ledON();
} // sendListingHTTP
 */
/* 
void iecDevice::sendFileHTTP()
{
	uint16_t i = 0;
	bool success = true;

	uint16_t bi = 0;
	char b[1];
	int ba[9];

	ba[8] = '\0';

	Debug_printf("\r\nsendFileHTTP: ");

	std::string user_agent( std::string(PRODUCT_ID) + " [" + std::string(FW_VERSION) + "]" );
	std::string url("http://"+m_device.url()+"/api/");
	std::string post_data("p="+urlencode(m_device.path())+"&i="+urlencode(m_device.image())+"&f="+urlencode(m_filename));

	// Connect to HTTP server
	HTTPClient client;
	client.setUserAgent( user_agent );
	client.setFollowRedirects(true);
	client.setTimeout(10000);
	if (!client.begin(url)) {
		Debug_println("\r\nConnection failed");
		IEC.sendFNF();
		return;
	}
	client.addHeader("Content-Type", "application/x-www-form-urlencoded");	

	Debug_printf("\r\nConnected!\r\n--------------------\r\n%s\r\n%s\r\n%s\r\n", user_agent.c_str(), url.c_str(), post_data.c_str());

	int httpCode = client.POST(post_data); //Send the request
	WiFiClient file = client.getStream();  //Get the response payload as Stream

	if (!file.available())
	{
		Debug_printf("\r\nsendFileHTTP: %s (File Not Found)\r\n", url.c_str());
		IEC.sendFNF();
	}
	else
	{
		size_t len = client.getSize();

		Debug_printf("\r\nsendFileHTTP: %d bytes\r\n=================================\r\n", len);
		for(i = 0; success and i < len; ++i) { // End if sending to CBM fails.
			success = file.readBytes(b, 1);
			if(i == len - 1)
			{
				success = IEC.sendEOI(b[0]); // indicate end of file.				
			}
			else
			{
				success = IEC.send(b[0]);				
			}

#ifdef DATA_STREAM
			// Show ASCII Data
			if (b[0] < 32 || b[0] == 127) 
				b[0] = 46;

			ba[bi] = b[0];
			bi++;
			if(bi == 8)
			{
				Debug_printf(" %s\r\n", ba);
				bi = 0;
			}
#endif

			// Toggle LED
			if(i % 50 == 0)
				toggleLED(true);

			printProgress(len, i);
		}
		client.end();
		Debug_println("");
		Debug_printf("%d bytes sent\r\n", i);
		ledON();

		if( !success )
		{
			bool s1 = IEC.status(IEC_PIN_ATN);
			bool s2 = IEC.status(IEC_PIN_CLK);
			bool s3 = IEC.status(IEC_PIN_DATA);

			Debug_printf("Transfer failed! %d, %d, %d\r\n", s1, s2, s3);
		}
	}
}
 */

#endif  // BUILD_CBM
