#ifdef BUILD_CBM

//#include "../../include/global_defines.h"
//#include "debug.h"

#include "disk.h"

#include "iec.h"
#include "device_db.h"

#include "led.h"

#include "wrappers/iec_buffer.h"

iecDisk disk;

using namespace CBM;
using namespace Protocol;


iecDisk::iecDisk()
{
	reset();
} // ctor


void iecDisk::reset(void)
{
	m_openState = O_NOTHING;
	setDeviceStatus(73);
	//device_config.reset();
} // reset


void iecDisk::sendFileNotFound(void)
{
	setDeviceStatus(62);
 	IEC.sendFNF();
}

void iecDisk::sendStatus(void)
{
	std::string status = m_device_status;
	if (status.size() == 0)
		status = "00, OK,00,00";

	Debug_printv("status: {%s}", status.c_str());
	//Debug_print("[");

 	IEC.send(status);
	IEC.sendEOI('\x0D');

	//Debug_println(BACKSPACE "]");

	// Clear the status message
	m_device_status.clear();
} // sendStatus

void iecDisk::setDeviceStatus(int number, int track, int sector)
{
	switch(number)
	{
		// 1 - FILES SCRATCHED - number scratched in track variable
		case 1:
			m_device_status = "01,FILES SCRATCHED,00,00";
			break;
		// 20 READ ERROR
		case 20:
			m_device_status = "20,FILE NOT OPEN,00,00";
			break;
		// 26 WRITE PROTECT ON
		case 26:
			m_device_status = "26,WRITE PROTECT ON,00,00";
			break;
		// 30 SYNTAX ERROR - in arguments
		case 30:
			m_device_status = "30,SYNTAX ERROR,00,00";
			break;
		// 31 SYNTAX ERROR - unknown command
		case 31:
			m_device_status = "31,SYNTAX ERROR,00,00";
			break;
		// 33 S.E. - invalid pattern
		case 33:
			m_device_status = "33,SYNTAX ERROR,00,00";
			break;
		// 34 S.E. - no file name given
		case 34:
			m_device_status = "34,SYNTAX ERROR,00,00";
			break;
		// 50 RECORD NOT PRESENT - eof
		case 50:
			m_device_status = "50,RECORD NOT PRESENT,00,00";
			break;
		// 60 WRITE FILE OPEN - trying to open for wrtiting a file that is open for writing
		case 60:
			m_device_status = "60,WRITE FILE OPEN,00,00";
			break;
		// 61 FILE NOT OPEN
		case 61:
			m_device_status = "61,FILE NOT OPEN,00,00";
			break;
		// 62 FILE NOT FOUND
		case 62:
			m_device_status = "62,FILE NOT FOUND,00,00";
			break;
		// 63 FILE EXISTS
		case 63:
			m_device_status = "63,FILE EXISTS,00,00";
			break;
		// 65 NO BLOCK - for B-A
		case 65:
			m_device_status = "65,NO BLOCK,00,00";
			break;
		// 73 boot message: device name, rom version etc.
		case 73:
			m_device_status = "73," PRODUCT_ID " [" FW_VERSION "],00,00";
			break;
		// 74 DRIVE NOT READY - also drive out of memory
		case 74:
			m_device_status = "74,DRIVE NOT READY,00,00";
			break;
		// 77 SELECTED PARTITION ILLEGAL
		case 77:
			m_device_status = "77,SELECTED PARTITION ILLEGAL,00,00";
			break;
		// 78 BUFFER TOO SMALL (sd2iec buffer ops)
		case 78:
			m_device_status = "78,BUFFER TOO SMALL,00,00";
			break;
		// 8x 64HDD errors
		// case 8:
		// 	break;
		// 89 COMMAND NOT SUPPORTED
		case 89:
			m_device_status = "89,COMMAND NOT SUPPORTED,00,00";
			break;
		// 91 64HDD device activated
		case 91:
			m_device_status = "91,DEVICE ACTIVATED,00,00";
			break;
		case 125:
			m_device_status = "125,NETWORK TIMEOUT,00,00";
			break;
		case 126:
			m_device_status = "126,NODE NOT FOUND,00,00";
			break;
	}
}



MFile* iecDisk::getPointed(MFile* urlFile) {
	Debug_printv("getPointed [%s]", urlFile->url.c_str());
	auto istream = Meat::ifstream(urlFile);

	istream.open();

    if(!istream.is_open()) {
        Debug_printf("couldn't open stream of urlfile");
		return nullptr;
    }
	else {
		std::string linkUrl;
		istream >> linkUrl;
		Debug_printv("path read from [%s]=%s", urlFile->url.c_str(), linkUrl.c_str());

		return MFSOwner::File(linkUrl);
	}
};

CommandPathTuple iecDisk::parseLine(std::string command, size_t channel)
{

	//Debug_printv("* PARSE INCOMING LINE *******************************");

	//Debug_printv("we are in              [%s]", m_mfile->url.c_str());
	//Debug_printv("unprocessed user input [%s]", command.c_str());

	if (mstr::startsWith(command, "*"))
	{
		// Find first program in listing
		if (device_config.path().empty())
		{
			// If in LittleFS root then set it to FB64
			// TODO: Load configured autoload program
			command = SYSTEM_DIR "fb64";
		}
		else
		{
			// Find first PRG file in current directory
			std::unique_ptr<MFile> entry(m_mfile->getNextFileInDir());
			std::string match = mstr::dropLast(command, 1);
			while ( entry != nullptr )
			{
				//Debug_printv("match[%s] extension[%s]", match.c_str(), entry->extension.c_str());
				if ( !mstr::startsWith(entry->extension, "prg") || (match.size() > 0 && !mstr::startsWith( entry->name, match.c_str() ))  )
				{
					entry.reset(m_mfile->getNextFileInDir());
				}
				else
				{
					break;
				}
			}
			command = entry->name;
		}
	}

	std::string guessedPath = command;
	CommandPathTuple tuple;

	mstr::toASCII(guessedPath);

	// check to see if it starts with a known command token
	if ( mstr::startsWith(command, "cd", false) ) // would be case sensitive, but I don't know the proper case
	{
		guessedPath = mstr::drop(guessedPath, 2);
		tuple.command = "cd";
		if ( mstr::startsWith(guessedPath, ":") || mstr::startsWith(guessedPath, " " ) ) // drop ":" if it was specified
			guessedPath = mstr::drop(guessedPath, 1);
		//else if ( channel != 15 )
		//	guessedPath = command;

		//Debug_printv("guessedPath[%s]", guessedPath.c_str());
	}
	else if(mstr::startsWith(command, "@info", false))
	{
		guessedPath = mstr::drop(guessedPath, 5);
		tuple.command = "@info";
	}
	else if(mstr::startsWith(command, "@stat", false))
	{
		guessedPath = mstr::drop(guessedPath, 5);
		tuple.command = "@stat";
	}
	else if(mstr::startsWith(command, ":")) {
		// JiffyDOS eats commands it knows, it might be T: which means ASCII dump requested
		guessedPath = mstr::drop(guessedPath, 1);
		tuple.command = "t";
	}
	else if(mstr::startsWith(command, "S:")) {
		// capital S = heart, that's a FAV!
		guessedPath = mstr::drop(guessedPath, 2);
		tuple.command = "mfav";
	}
	else if(mstr::startsWith(command, "MFAV:")) {
		// capital S = heart, that's a FAV!
		guessedPath = mstr::drop(guessedPath, 5);
		tuple.command = "mfav";
	}
	else
	{
		tuple.command = command;
	}

	// TODO more of them?

	// NOW, since user could have requested ANY kind of our supported magic paths like:
	// LOAD ~/something
	// LOAD ../something
	// LOAD //something
	// we HAVE TO PARSE IT OUR WAY!


	// and to get a REAL FULL PATH that the user wanted to refer to, we CD into it, using supplied stripped path:
	mstr::rtrim(guessedPath);
	tuple.rawPath = guessedPath;

	//Debug_printv("found command     [%s]", tuple.command.c_str());

	if(guessedPath == "$")
	{
		//Debug_printv("get directory of [%s]", m_mfile->url.c_str());
	}
	else if(!guessedPath.empty())
	{
		auto fullPath = Meat::Wrap(m_mfile->cd(guessedPath));

		tuple.fullPath = fullPath->url;

		//Debug_printv("full referenced path [%s]", tuple.fullPath.c_str());
	}

	//Debug_printv("* END OF PARSE LINE *******************************");

	return tuple;
}

void iecDisk::changeDir(std::string url)
{
	device_config.url(url);
	m_mfile.reset(MFSOwner::File(url));
	if ( this->data.channel == 0 )
	{
		m_openState = O_DIR;
		//Debug_printv("!!!! CD into [%s]", url.c_str());
		//Debug_printv("new current url: [%s]", m_mfile->url.c_str());
		//Debug_printv("LOAD $");		
	}
}

void iecDisk::prepareFileStream(std::string url)
{
	device_config.url(url);
	m_filename = url;
	m_openState = O_FILE;
	Debug_printv("LOAD [%s]", url.c_str());
}



void iecDisk::handleListenCommand( void )
{
	if (device_config.select(this->data.device))
	{
		Debug_printv("!!!! device changed: unit:%d current url: [%s]", device_config.id(), device_config.url().c_str());
		m_mfile.reset(MFSOwner::File(device_config.url()));
		Debug_printv("m_mfile[%s]", m_mfile->url.c_str());
	}

	size_t channel = this->data.channel;
	m_openState = O_NOTHING;

	if (this->data.device_command.length() == 0 )
	{
		Debug_printv("No command to process");

		if ( this->data.channel == CMD_CHANNEL )
			m_openState = O_STATUS;
		return;
	}

	// 1. obtain command and fullPath
	auto commandAndPath = parseLine(this->data.device_command, channel);
	auto referencedPath = Meat::New<MFile>(commandAndPath.fullPath);

	Debug_printv("command[%s]", commandAndPath.command.c_str());
	if (mstr::startsWith(commandAndPath.command, "$"))
	{
		m_openState = O_DIR;
		Debug_printv("LOAD $");
	}
	else if (mstr::equals(commandAndPath.command, (char*)"@info", false))
	{
		m_openState = O_ML_INFO;
	}
	else if (mstr::equals(commandAndPath.command, (char*)"@stat", false))
	{
		m_openState = O_ML_STATUS;
	}
	else if (commandAndPath.command == "mfav") {
		// create a urlfile named like the argument, containing current m_mfile path
		// here we don't want the full path provided by commandAndPath, though
		// the full syntax should be: heart:urlfilename,[filename] - optional name of the file that should be pointed to

		auto favStream = Meat::ofstream(commandAndPath.rawPath+".url"); // put the name from argument here!
		favStream.open();
		if(favStream.is_open()) {
			favStream << m_mfile->url;
		}
		favStream.close();
	}
	else if(!commandAndPath.rawPath.empty())
	{
		// 2. fullPath.extension == "URL" - change dir or load file
		if (mstr::equals(referencedPath->extension, (char*)"url", false))
		{
			// CD to the path inside the .url file
			referencedPath.reset(getPointed(referencedPath.get()));

			if ( referencedPath->isDirectory() )
			{
				//Debug_printv("change dir called for urlfile");
				changeDir(referencedPath->url);
			}
			else if ( referencedPath->exists() )
			{
				prepareFileStream(referencedPath->url);
			}
		}
		// 2. OR if command == "CD" OR fullPath.isDirectory - change directory
		if (mstr::equals(commandAndPath.command, (char*)"cd", false) || referencedPath->isDirectory())
		{
			//Debug_printv("change dir called by CD command or because of isDirectory");
			changeDir(referencedPath->url);
		}
		// 3. else - stream file
		else if ( referencedPath->exists() )
		{
			// Set File
			prepareFileStream(referencedPath->url);
		}
		else
		{
			Debug_printv("file doesn't exist [%s]", referencedPath->url.c_str());
			sendFileNotFound();
		}
	}

	dumpState();
	this->data.init();

} // handleListenCommand


void iecDisk::handleListenData()
{
	Debug_printv("[%s]", device_config.url().c_str());

	saveFile();
} // handleListenData


void iecDisk::handleTalk(uint8_t chan)
{
	//Debug_printv("channel[%d] openState[%d]", chan, m_openState);

	switch (m_openState)
	{
		case O_NOTHING:
			break;

		case O_STATUS:
			// Send status
			sendStatus();
			break;

		case O_FILE:
			// Send file
			sendFile();
			break;

		case O_DIR:
			// Send listing
			sendListing();
			break;

		case O_ML_INFO:
			// Send system information
			// sendMeatloafSystemInformation();
			break;

		case O_ML_STATUS:
			// Send virtual device status
			// sendMeatloafVirtualDeviceStatus();
			break;
	}

	m_openState = O_NOTHING;
} // handleTalk

// send single basic line, including heading basic pointer and terminating zero.
uint16_t iecDisk::sendLine(uint16_t &basicPtr, uint16_t blocks, const char *format, ...)
{
	// Format our string
	va_list args;
	va_start(args, format);
	char text[vsnprintf(NULL, 0, format, args) + 1];
	vsnprintf(text, sizeof text, format, args);
	va_end(args);

	return sendLine(basicPtr, blocks, text);
}

uint16_t iecDisk::sendLine(uint16_t &basicPtr, uint16_t blocks, char *text)
{
	Debug_printf("%d %s ", blocks, text);

	// Exit if ATN is PULLED while sending
	if ( IEC.protocol.flags bitand ATN_PULLED )
	{
		// Save file pointer position
		channelUpdate(basicPtr);
		setDeviceStatus(74);
		return 0;
	}

	// Get text length
	uint8_t len = strlen(text);

	// Increment next line pointer
	basicPtr += len + 5;

	// Send that pointer
	IEC.send(basicPtr bitand 0xFF);
	IEC.send(basicPtr >> 8);

	// Send blocks
	IEC.send(blocks bitand 0xFF);
	IEC.send(blocks >> 8);

	// Send line contents
	for (uint8_t i = 0; i < len; i++)
	{
		IEC.send(text[i]);		
	}

	// Finish line
	IEC.send(0);

	Debug_println("");

	return len + 5;
} // sendLine

// uint16_t iecDisk::sendHeader(uint16_t &basicPtr, const char *format, ...)
// {
// 	Debug_printv("formatting header");

// 	// Format our string
// 	va_list args;
// 	va_start(args, format);
// 	char text[vsnprintf(NULL, 0, format, args) + 1];
// 	vsnprintf(text, sizeof text, format, args);
// 	va_end(args);

// 	Debug_printv("header[%s]", text);

// 	return sendHeader(basicPtr, std::string(text));
// }

uint16_t iecDisk::sendHeader(uint16_t &basicPtr, std::string header, std::string id)
{
	uint16_t byte_count = 0;
	bool sent_info = false;

	PeoplesUrlParser p;
	std::string url = device_config.url();

	mstr::toPETSCII(url);
	p.parseUrl(url);

	url = p.root();
	std::string path = p.pathToFile();
	std::string archive = "";
	std::string image = p.name;



	// Send List HEADER
	uint8_t space_cnt = 0;
	space_cnt = (16 - header.size()) / 2;
	space_cnt = (space_cnt > 8 ) ? 0 : space_cnt;

	//Debug_printv("header[%s] id[%s] space_cnt[%d]", header.c_str(), id.c_str(), space_cnt);

	byte_count += sendLine(basicPtr, 0, CBM_REVERSE_ON "\"%*s%s%*s\" %s", space_cnt, "", header.c_str(), space_cnt, "", id.c_str());

	//byte_count += sendLine(basicPtr, 0, "\x12\"%*s%s%*s\" %.02d 2A", space_cnt, "", PRODUCT_ID, space_cnt, "", device_config.device());
	//byte_count += sendLine(basicPtr, 0, CBM_REVERSE_ON "%s", header.c_str());

	// Send Extra INFO
	if (url.size())
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, "[URL]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, url.c_str());
		sent_info = true;
	}
	if (path.size() > 1)
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, "[PATH]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, path.c_str());
		sent_info = true;
	}
	if (archive.size() > 1)
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, "[ARCHIVE]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, device_config.archive().c_str());
	}
	if (image.size())
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, "[IMAGE]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 0, "", 19, image.c_str());
		sent_info = true;
	}
	if (sent_info)
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"-------------------\" NFO", 0, "");
	}

	return byte_count;
}

void iecDisk::sendListing()
{
	Debug_printf("sendListing: [%s]\r\n=================================\r\n", m_mfile->url.c_str());

	uint16_t byte_count = 0;
	std::string extension = "dir";

	std::unique_ptr<MFile> entry(m_mfile->getNextFileInDir());

	if(entry == nullptr) {
		sendFileNotFound();
		return;
	}

	// Reset basic memory pointer:
	uint16_t basicPtr = C64_BASIC_START;

	// Send load address
	IEC.send(C64_BASIC_START bitand 0xff);
	IEC.send((C64_BASIC_START >> 8) bitand 0xff);
	byte_count += 2;
	Debug_println("");

	// Send Listing Header
	if (m_mfile->media_header.size() == 0)
	{
		// Set device default Listing Header
		char buf[7] = { '\0' };
		sprintf(buf, "%.02d 2A", device_config.id());
		byte_count += sendHeader(basicPtr, PRODUCT_ID, buf);
	}
	else
	{
		byte_count += sendHeader(basicPtr, m_mfile->media_header.c_str(), m_mfile->media_id.c_str());
	}

	// Send Directory Items
	while(entry != nullptr)
	{
		uint16_t block_cnt = entry->size() / m_mfile->media_block_size;
		uint8_t block_spc = 3;
		if (block_cnt > 9)
			block_spc--;
		if (block_cnt > 99)
			block_spc--;
		if (block_cnt > 999)
			block_spc--;

		uint8_t space_cnt = 21 - (entry->name.length() + 5);
		if (space_cnt > 21)
			space_cnt = 0;

		if (!entry->isDirectory())
		{

			// Get extension
			if (entry->extension.length())
			{
				extension = entry->extension;
			}
			else
			{
				extension = "prg";
			}
		}
		else
		{
			extension = "dir";
		}

		// Don't show hidden folders or files
		//Debug_printv("size[%d] name[%s]", entry->size(), entry->name.c_str());

		std::string name = entry->petsciiName();
		mstr::toPETSCII(extension);

		if (entry->name[0]!='.' || m_show_hidden)
		{
			// Exit if ATN is PULLED while sending
			if ( IEC.protocol.flags bitand ATN_PULLED )
			{
				// Save file pointer position
				channelUpdate(byte_count);
				setDeviceStatus(74);
				return;
			}

			byte_count += sendLine(basicPtr, block_cnt, "%*s\"%s\"%*s %s", block_spc, "", name.c_str(), space_cnt, "", extension.c_str());
		}

		entry.reset(m_mfile->getNextFileInDir());

		fnLedManager.toggle(eLed::LED_BUS);
	}

	// Send Listing Footer
	byte_count += sendFooter(basicPtr, m_mfile->media_blocks_free, m_mfile->media_block_size);

	// End program with two zeros after last line. Last zero goes out as EOI.
	IEC.send(0);
	IEC.sendEOI(0);

	Debug_printf("=================================\r\n%d bytes sent\r\n", byte_count);

	fnLedManager.set(eLed::LED_BUS, true);
} // sendListing


uint16_t iecDisk::sendFooter(uint16_t &basicPtr, uint16_t blocks_free, uint16_t block_size)
{
	// Send List FOOTER
	// #if defined(USE_LITTLEFS)
	uint64_t byte_count = 0;
	if (block_size > 256)
	{
		byte_count = sendLine(basicPtr, blocks_free, "BLOCKS FREE. (*%d bytes)", block_size);
	}
	else
	{
		byte_count = sendLine(basicPtr, blocks_free, "BLOCKS FREE.");
	}

	// if (device_config.url().length() == 0)
	// {
	// 	FSInfo64 fs_info;
	// 	m_fileSystem->info64(fs_info);
	// 	blocks_free = fs_info.totalBytes - fs_info.usedBytes;
	// }

	return byte_count;
	// #elif defined(USE_SPIFFS)
	// 	return sendLine(basicPtr, 00, "UNKNOWN BLOCKS FREE.");
	// #endif
	//Debug_println("");
}


void iecDisk::sendFile()
{
	size_t i = 0;
	bool success = true;

	uint8_t b;
	uint16_t bi = 0;
	uint16_t load_address = 0;
	uint16_t sys_address = 0;

#ifdef DATA_STREAM
	char ba[9];
	ba[8] = '\0';
#endif

	// Update device database
	device_config.save();

	std::unique_ptr<MFile> file(MFSOwner::File(m_filename));

	if(!file->exists())
	{
		Debug_printv("File Not Found!");
		sendFileNotFound();
		return;
	}
	Debug_printv("Sending File [%s]", file->name.c_str());


	// TODO!!!! you should check istream for nullptr here and return error immediately if null


	if
	(
		mstr::equals(file->extension, (char*)"txt", false) ||
		mstr::equals(file->extension, (char*)"htm", false) ||
		mstr::equals(file->extension, (char*)"html", false)
	)
	{
		// convert UTF8 files on the fly

		Debug_printv("Sending a text file to C64 [%s]", file->url.c_str());
        //std::unique_ptr<LinedReader> reader(new LinedReader(istream.get()));
		auto istream = Meat::ifstream(file.get());
		auto ostream = oiecstream();


		istream.open();
		ostream.open(&IEC);


		if(!istream.is_open()) {
			sendFileNotFound();
			return;
		}


		//we can skip the BOM here, EF BB BF for UTF8
		auto b = (char)istream.get();
		if(b != 0xef)
			ostream.put(b);
		else {
			b = istream.get();
			if(b != 0xbb)
				ostream.put(b);
			else {
				b = istream.get();
				if(b != 0xbf)
					ostream.put(b); // not BOM
			}
		}

		while(!istream.eof()) {
			auto cp = istream.getUtf8();

			ostream.putUtf8(&cp);

			if(ostream.bad() || istream.bad()) {
				Debug_printv("Error sending");
                setDeviceStatus(60); // write error
				break;
            }
		}
		ostream.close();
		istream.close();
	}
	else
	{
		std::unique_ptr<MIStream> istream(file->inputStream());

		if( istream == nullptr )
		{
			sendFileNotFound();
			return;
		}

		// Position file pointer
		// istream->seek(currentChannel.cursor);

		// Get/Send file load address
		i = 2;
		istream->read(&b, 1);
		success = IEC.send(b);
		load_address = b & 0x00FF; // low byte
		sys_address = b;
		istream->read(&b, 1);
		success = IEC.send(b);
		load_address = load_address | b << 8;  // high byte
		sys_address += b * 256;

		size_t len = istream->size();
		size_t avail = istream->available();

		Debug_printv("len[%d] avail[%d] cursor[%d] success[%d]", len, avail, currentChannel.cursor, success);

		Debug_printf("sendFile: [%s] [$%.4X] (%d bytes)\r\n=================================\r\n", file->url.c_str(), load_address, len);
		while( i < len && success )
		{
			success = istream->read(&b, 1);
			// Debug_printv("b[%02X] success[%d]", b, success);
			if (success)
			{
#ifdef DATA_STREAM
				if (bi == 0)
				{
					Debug_printf(":%.4X ", load_address);
					load_address += 8;
				}
#endif
				if ( ++i == len )
				{
					success = IEC.sendEOI(b); // indicate end of file.
				}
				else
				{
					success = IEC.send(b);
				}

#ifdef DATA_STREAM
				// Show ASCII Data
				if (b < 32 || b >= 127)
				b = 46;

				ba[bi++] = b;

				if(bi == 8)
				{
					size_t t = (i * 100) / len;
					Debug_printf(" %s (%d %d%%) [%d]\r\n", ba, i, t, avail - 1);
					bi = 0;
				}
#else
				size_t t = (i * 100) / len;
				Debug_printf("Transferring %d%% [%d, %d]      \r", t, i, avail -1);
#endif
			}

			// Exit if ATN is PULLED while sending
			if ( IEC.protocol.flags bitand ATN_PULLED )
			{
				// Save file pointer position
				channelUpdate(istream->position());
				setDeviceStatus(74);
				success = true;
				break;
			}

			// Toggle LED
			if (i % 50 == 0)
			{
				fnLedManager.toggle(eLed::LED_BUS);
			}

			avail = istream->available();
			//i++;
		}
		istream->close();
		Debug_printf("=================================\r\n%d of %d bytes sent [SYS%d]\r\n", i, len, sys_address);

		//Debug_printv("len[%d] avail[%d] success[%d]", len, avail, success);		
	}


	fnLedManager.set(eLed::LED_BUS, true);

	if (!success)
	{
		Debug_println("sendFile: Transfer aborted!");
		// TODO: Send something to signal that there was an error to the C64
	 	IEC.sendEOI(0);
	}
} // sendFile


void iecDisk::saveFile()
{
	uint16_t i = 0;
	bool done = false;

	uint16_t bi = 0;
	uint16_t load_address = 0;
	size_t b_len = 1;
	uint8_t b[b_len];
	uint8_t ll[b_len];
	uint8_t lh[b_len];

#ifdef DATA_STREAM
	char ba[9];
	ba[8] = '\0';
#endif

	mstr::toASCII(m_filename);
	std::unique_ptr<MFile> file(MFSOwner::File(m_filename));
	Debug_printv("[%s]", file->url.c_str());

	std::unique_ptr<MOStream> ostream(file->outputStream());


    if(!ostream->isOpen()) {
        Debug_printv("couldn't open a stream for writing");
		// TODO: Set status and sendFNF
		sendFileNotFound();
        return;
    }
    else
	{
	 	// Stream is open!  Let's save this!

		if ( currentChannel.cursor > 0 )
		{
			// Position file pointer
			ostream->seek(currentChannel.cursor);
		}
		else
		{
			// Get file load address
			ll[0] = IEC.receive();
			load_address = *ll & 0x00FF; // low byte
			lh[0] = IEC.receive();
			load_address = load_address | *lh << 8;  // high byte
		}


		Debug_printf("saveFile: [%s] [$%.4X]\r\n=================================\r\n", file->url.c_str(), load_address);

		// Recieve bytes until a EOI is detected
		do
		{
			// Save Load Address
			if (i == 0)
			{
				Debug_print("[");
				ostream->write(ll, b_len);
				ostream->write(lh, b_len);
				i += 2;
				Debug_println("]");
			}

#ifdef DATA_STREAM
			if (bi == 0)
			{
				Debug_printf(":%.4X ", load_address);
				load_address += 8;
			}
#endif

			b[0] = IEC.receive();
			ostream->write(b, b_len);
			i++;

			uint8_t f = IEC.protocol.flags;
			done = (f bitand EOI_RECVD) or (f bitand ERROR);

			// Exit if ATN is PULLED while sending
			if ( f bitand ATN_PULLED )
			{
				// Save file pointer position
				channelUpdate(ostream->position());
				//setDeviceStatus(74);
				break;
			}

#ifdef DATA_STREAM
			// Show ASCII Data
			if (b[0] < 32 || b[0] >= 127)
			b[0] = 46;

			ba[bi++] = b[0];

			if(bi == 8)
			{
				Debug_printf(" %s (%d)\r\n", ba, i);
				bi = 0;
			}
#endif
			// Toggle LED
			if (0 == i % 50)
			{
				fnLedManager.toggle(eLed::LED_BUS);
			}
		} while (not done);
    }
    ostream->close(); // nor required, closes automagically

	Debug_printf("=================================\r\n%d bytes saved\r\n", i);
	fnLedManager.set(eLed::LED_BUS, true);

	// TODO: Handle errorFlag
} // saveFile


void iecDisk::dumpState()
{
	Debug_println("");
	Debug_printv("device_config -----------------");
	Debug_printv("Device ID: [%d]", device_config.id());
	Debug_printv("Media ID: [%d]", device_config.media());
	Debug_printv("Partition ID: [%d]", device_config.partition());
	Debug_printv("URL: [%s]", device_config.url().c_str());
	Debug_printv("Base Path: [%s]", device_config.basepath().c_str());
	Debug_printv("Path: [%s]", device_config.path().c_str());
	Debug_printv("Archive: [%s]", device_config.archive().c_str());
	Debug_printv("Image: [%s]", device_config.image().c_str());
    Debug_printv("-------------------------------");
	Debug_println("");
	Debug_printv("m_mfile ------------------------");
	Debug_printv("URL: [%s]", m_mfile->url.c_str());
    Debug_printv("streamPath: [%s]", m_mfile->streamFile->url.c_str());
    Debug_printv("pathInStream: [%s]", m_mfile->pathInStream.c_str());
	Debug_printv("Scheme: [%s]", m_mfile->scheme.c_str());
	Debug_printv("Username: [%s]", m_mfile->user.c_str());
	Debug_printv("Password: [%s]", m_mfile->pass.c_str());
	Debug_printv("Host: [%s]", m_mfile->host.c_str());
	Debug_printv("Port: [%s]", m_mfile->port.c_str());
	Debug_printv("Base: [%s]", m_mfile->base().c_str());
	Debug_printv("Path: [%s]", m_mfile->path.c_str());
	Debug_printv("File: [%s]", m_mfile->name.c_str());
	Debug_printv("Extension: [%s]", m_mfile->extension.c_str());
	Debug_printv("");
	Debug_printv("m_filename: [%s]", m_filename.c_str());
    Debug_printv("-------------------------------");
} // dumpState

#endif // BUILD_CBM