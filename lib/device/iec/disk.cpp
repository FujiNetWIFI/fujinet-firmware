#ifdef BUILD_IEC

#include "disk.h"

#include <cstring>

#include <unordered_map>

#include "../../include/debug.h"
#include "../../include/cbm_defines.h"

#include "fuji.h"
#include "fnFsSD.h"
#include "utils.h"

// External ref to fuji object.
extern iecFuji theFuji;

iecDisk::iecDisk()
{
    // device_active = false;
    device_active = true; // temporary during bring-up

    _disk.reset(MFSOwner::File("/"));
    _file = "/";
}

// Read disk data and send to computer
void iecDisk::read()
{
    // TODO: IMPLEMENT
}

// Write disk data from computer
void iecDisk::write(bool verify)
{
    // TODO: IMPLEMENT
}

// Disk format
void iecDisk::format()
{
    // TODO IMPLEMENT
}

/* Mount Disk
   We determine the type of image based on the filename exteniecn.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extension or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t iecDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    // TODO IMPLEMENT
    _disk.reset( MFSOwner::File(filename) );

    return MEDIATYPE_UNKNOWN; // MEDIATYPE_UNKNOWN
}

// Destructor
iecDisk::~iecDisk()
{
    // if (_disk != nullptr)
    //     delete _disk;
}

// Unmount disk file
void iecDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        //_disk->unmount();
        device_active = false;
    }
}

// Create blank disk
bool iecDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    // TODO IMPLEMENT
    return false;
}


// Process command
device_state_t iecDisk::process(IECData *id)
{
    virtualDevice::process(id);

    Debug_printv("channel[%d]", commanddata->channel);

    switch (commanddata->channel)
    {
    case CHANNEL_LOAD: // LOAD
        process_load();
        break;
    case CHANNEL_SAVE: // SAVE
        process_save();
        break;
    case CHANNEL_COMMAND: // COMMAND
        process_command();
        break;
    default: // Open files (2-14)
        process_channel();
        break;
    }
    return device_state;
}

void iecDisk::process_load()
{
    Debug_printv("secondary[%.2X]", commanddata->secondary);
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN:
        iec_reopen_load();
        break;
    default:
        break;
    }
}

void iecDisk::process_save()
{
    Debug_printv("secondary[%.2X]", commanddata->secondary);
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN:
        iec_reopen_save();
        break;
    default:
        break;
    }
}

void iecDisk::process_command()
{
    Debug_printv("primary[%.2X] secondary[%.2X]", commanddata->primary, commanddata->secondary);
    if (commanddata->primary == IEC_TALK && commanddata->secondary == IEC_REOPEN)
    {
        iec_talk_command();
    }
    else if (commanddata->primary == IEC_UNLISTEN)
    {
        iec_command();
    }
}

void iecDisk::process_channel()
{
    Debug_printv("secondary[%.2X]", commanddata->secondary);
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN:
        iec_reopen_channel();
        break;
    default:
        break;
    }
}


void iecDisk::iec_open()
{
    std::string s = commanddata->payload;
    mstr::toPETSCII(s);

    if ( mstr::startsWith(s, "0:") )
    {
        // Remove media ID from command string
        s = mstr::drop(s, 2);
    }
    else if ( mstr::equals(s, "$") ) 
    {
        s = "/";
    }
    _disk.reset( MFSOwner::File( s ) );
    _file = _disk->name;

    Debug_printv("_file[%s]", _file.c_str());
    if ( !registerStream(std::ios_base::in) )
    {
        Debug_printv("File Doesn't Exist [%s]", s.c_str());
    }
}

void iecDisk::iec_close()
{
    if (_disk == nullptr)
    {
        IEC.senderTimeout();
        return; // Punch out.
    }
    Debug_printv("url[%s]", _disk->url.c_str());

    closeStream();
}

void iecDisk::iec_reopen_load()
{
    if ( _disk->isDirectory() ) 
    {
        sendListing();
    }
    else
    {
        sendFile();
    }
}

void iecDisk::iec_reopen_save()
{
    if (_disk == nullptr)
    {
        IEC.senderTimeout();
        return; // Punch out.
    }
    Debug_printv("url[%s]", _disk->url.c_str());

    saveFile();
}

void iecDisk::iec_reopen_channel()
{
    Debug_printv("primary[%.2X]", commanddata->primary);
    switch (commanddata->primary)
    {
    case IEC_LISTEN:
        iec_reopen_channel_listen();
        break;
    case IEC_TALK:
        iec_reopen_channel_talk();
        break;
    }
}


void iecDisk::iec_reopen_channel_listen()
{
    Debug_printv("here");
}

void iecDisk::iec_reopen_channel_talk()
{
    Debug_printv("here");
}


void iecDisk::iec_listen_command()
{
    Debug_printv("here");
}

void iecDisk::iec_talk_command()
{
    Debug_printv("here");
    if (response_queue.empty())
        iec_talk_command_buffer_status();
}

void iecDisk::iec_talk_command_buffer_status()
{
    Debug_printv("here");

    char reply[80];
    string s = "0, OK, 0, 0";

    // snprintf(reply, 80, "%u,\"%s\",%u,%u", iecStatus.error, iecStatus.msg.c_str(), iecStatus.connected, iecStatus.channel);
    // s = string(reply);
    mstr::toPETSCII(s);
    IEC.sendBytes(s);
}

void iecDisk::iec_command()
{
    Debug_printv("here");
    // if (channelMode[commanddata->channel] == PROTOCOL)
    // {
    //     if (pt[0] == "cd")
    //         set_prefix();
    //     else if (pt[0] == "id")
    //         set_device_id();
    //     else if (pt[0] == "jsonparse")
    //         parse_json();
    //     else if (pt[0] == "jq")
    //         query_json();
    //     else if (pt[0] == "settrans")
    //         set_translation_mode();
    //     else if (pt[0] == "pwd")
    //         get_prefix();
    //     else if (pt[0] == "login")
    //         set_login_password();
    //     else if (pt[0] == "rename" || pt[0] == "ren")
    //         fsop(0x20);
    //     else if (pt[0] == "delete" || pt[0] == "del" || pt[0] == "rm")
    //         fsop(0x21);
    //     else if (pt[0] == "lock")
    //         fsop(0x23);
    //     else if (pt[0] == "unlock")
    //         fsop(0x24);
    //     else if (pt[0] == "mkdir")
    //         fsop(0x2A);
    //     else if (pt[0] == "rmdir")
    //         fsop(0x2B);
    //     else if (protocol[commanddata->channel] != nullptr &&
    //              protocol[commanddata->channel]->special_inquiry(pt[0][0]) == 0x00)
    //         perform_special_00();
    //     else if (protocol[commanddata->channel] != nullptr &&
    //              protocol[commanddata->channel]->special_inquiry(pt[0][0]) == 0x40)
    //         perform_special_40();
    //     else if (protocol[commanddata->channel] != nullptr &&
    //              protocol[commanddata->channel]->special_inquiry(pt[0][0]) == 0x80)
    //         perform_special_80();
    // }
    // else if (channelMode[commanddata->channel] == JSON)
    // {
    //     Debug_printf("JSON channelmode command %s\n", pt[0].c_str());
    // }
}



std::shared_ptr<MStream> iecDisk::retrieveStream ( void )
{
    size_t key = ( IEC.data.device * 100 ) + IEC.data.channel;
    Debug_printv("Stream key[%d]", key);

    if ( streams.find ( key ) != streams.end() )
    {
        //Debug_printv("Stream retrieved. key[%d]", key);
        return streams.at ( key );
    }
    else
    {
		//Debug_printv("Error! Trying to recall not-registered stream!");
        return nullptr;
    }
}

// used to start working with a stream, registering it as underlying stream of some
// IEC channel on some IEC device
bool iecDisk::registerStream (std::ios_base::open_mode mode)
{
    // Debug_printv("dc_basepath[%s]",  device_config.basepath().c_str());
    // Debug_printv("_file[%s]", _file.c_str());
    // //auto file = Meat::New<MFile>( device_config.basepath() + "/" + _file );
    // auto file = Meat::New<MFile>( _disk->url + _file );
    // auto file = Meat::New<MFile>( _file );
    // if ( !file->exists() )
    //     return false;
    if ( !_disk->exists() )
        return false;
    
    Debug_printv("_disk[%s]", _disk->url.c_str());

    std::shared_ptr<MStream> new_stream;

    // LOAD / GET / INPUT
    if ( mode == std::ios_base::in )
    {
        Debug_printv("LOAD _disk[%s] _file[%s]", _disk->url.c_str(), _file.c_str());
        new_stream = std::shared_ptr<MStream>(_disk->meatStream());

        if ( new_stream == nullptr )
        {
            return false;
        }

        if( !new_stream->isOpen() )
        {
            Debug_printv("Error creating stream");
            return false;
        }
        else
        {
            // Close the stream if it is already open
            closeStream();
        }
    }

    // SAVE / PUT / PRINT / WRITE
    else
    {
        Debug_printv("SAVE _file[%s]", _file.c_str());
        // CREATE STREAM HERE FOR OUTPUT
        return false;
    }


    size_t key = ( IEC.data.device * 100 ) + IEC.data.channel;

    // // Check to see if a stream is open on this device/channel already
    // auto found = streams.find(key);
    // if ( found != streams.end() )
    // {
    //     Debug_printv( "Stream already registered on this device/channel!" );
    //     return false;
    // }

    // Add stream to streams 
    auto newPair = std::make_pair ( key, new_stream );
    streams.insert ( newPair );

    Debug_printv("Stream created. key[%d]", key);
    return true;
}

bool iecDisk::closeStream ( bool close_all )
{
    size_t key = ( IEC.data.device * 100 ) + IEC.data.channel;
    auto found = streams.find(key);

    if ( found != streams.end() )
    {
        //Debug_printv("Stream closed. key[%d]", key);
        auto closingStream = (*found).second;
        closingStream->close();
        return streams.erase ( key );
    }

    return false;
}



// send single basic line, including heading basic pointer and terminating zero.
uint16_t iecDisk::sendLine(uint16_t blocks, const char *format, ...)
{
	// Debug_printv("bus[%d]", IEC.bus_state);

	// Exit if ATN is PULLED while sending
	// Exit if there is an error while sending
	if ( IEC.bus_state == BUS_ERROR )
	{
		// Save file pointer position
		//streamUpdate(basicPtr);
		//setDeviceStatus(74);
		return 0;
	}

	// Format our string
	va_list args;
	va_start(args, format);
	char text[vsnprintf(NULL, 0, format, args) + 1];
	vsnprintf(text, sizeof text, format, args);
	va_end(args);

	return sendLine(blocks, text);
}

uint16_t iecDisk::sendLine(uint16_t blocks, char *text)
{
	Debug_printf("%d %s ", blocks, text);

	// Exit if ATN is PULLED while sending
	// Exit if there is an error while sending
	if ( IEC.bus_state == BUS_ERROR )
	{
		// Save file pointer position
		// streamUpdate(basicPtr);
		//setDeviceStatus(74);
		return 0;
	}

	// Get text length
	uint8_t len = strlen(text);

	// Send that pointer
	// No basic line pointer is used in the directory listing set to 0x0101
	IEC.sendByte(0x01);		// IEC.sendByte(basicPtr bitand 0xFF);
	IEC.sendByte(0x01);		// IEC.sendByte(basicPtr >> 8);

	// Send blocks
	IEC.sendByte(blocks bitand 0xFF);
	IEC.sendByte(blocks >> 8);

	// Send line contents
	for (uint8_t i = 0; i < len; i++)
	{
		if ( !IEC.sendByte(text[i]) )
		{
			IEC.bus_state = BUS_ERROR;
			return 0;
		}
	}

	// Finish line
	IEC.sendByte(0);

	Debug_println("");
    
	return len + 5;
} // sendLine

uint16_t iecDisk::sendHeader(std::string header, std::string id)
{
	uint16_t byte_count = 0;
	bool sent_info = false;

	PeoplesUrlParser p;
	std::string url = _disk->url;

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

	byte_count += sendLine(0, CBM_REVERSE_ON "\"%*s%s%*s\" %s", space_cnt, "", header.c_str(), space_cnt, "", id.c_str());
	if ( byte_count == 0 ) return 0;

	//byte_count += sendLine(basicPtr, 0, "\x12\"%*s%s%*s\" %.02d 2A", space_cnt, "", PRODUCT_ID, space_cnt, "", device_config.device());
	//byte_count += sendLine(basicPtr, 0, CBM_REVERSE_ON "%s", header.c_str());

	// Send Extra INFO
	if (url.size())
	{
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[URL]");
		if ( byte_count == 0 ) return 0;
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, url.c_str());
		if ( byte_count == 0 ) return 0;
		sent_info = true;
	}
	if (path.size() > 1)
	{
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[PATH]");
		if ( byte_count == 0 ) return 0;
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, path.c_str());
		if ( byte_count == 0 ) return 0;
		sent_info = true;
	}
	if (archive.size() > 1)
	{
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[ARCHIVE]");
		if ( byte_count == 0 ) return 0;
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, archive.c_str());
		if ( byte_count == 0 ) return 0;
	}
	if (image.size())
	{
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[IMAGE]");
		if ( byte_count == 0 ) return 0;
		byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, image.c_str());
		if ( byte_count == 0 ) return 0;
		sent_info = true;
	}
	if (sent_info)
	{
		byte_count += sendLine(0, "%*s\"-------------------\" NFO", 0, "");
		if ( byte_count == 0 ) return 0;
	}
	
//#ifdef SD_CARD
	if (fnSDFAT.running() && _disk->url.size() < 2)
	{
		byte_count += sendLine(0, "%*s\"SD\"                  DIR", 0, "");
		if ( byte_count == 0 ) return 0;
		byte_count += sendLine(0, "%*s\"-------------------\" NFO", 0, "");
		if ( byte_count == 0 ) return 0;
	}
//#endif

	return byte_count;
}

uint16_t iecDisk::sendFooter()
{
	uint16_t blocks_free;
	uint16_t byte_count = 0;
	uint64_t bytes_free = _disk->getAvailableSpace();

	if ( _disk->size() )
	{
		blocks_free = _disk->media_blocks_free;
		byte_count = sendLine(blocks_free, "BLOCKS FREE.");
	}
	else
	{
		// We are not in a media file so let's show BYTES FREE instead
		blocks_free = 0;
		byte_count = sendLine(blocks_free, CBM_DELETE CBM_DELETE "%sBYTES FREE.", mstr::formatBytes(bytes_free).c_str() );
	}

	return byte_count;
}

void iecDisk::sendListing()
{
	Debug_printf("sendListing: [%s]\r\n=================================\r\n", _disk->url.c_str());

	uint16_t byte_count = 0;
	std::string extension = "dir";

	std::unique_ptr<MFile> entry(_disk->getNextFileInDir());

	if(entry == nullptr) {
		closeStream();

		bool isOpen = registerStream(std::ios_base::in);
		if(isOpen) 
		{
            sendFile();
        }
		else
		{
            IEC.senderTimeout(); // File Not Found
		}
		
		return;
	}


	// Send load address
	IEC.sendByte(CBM_BASIC_START & 0xff);
	IEC.sendByte((CBM_BASIC_START >> 8) & 0xff);
	byte_count += 2;
	Debug_println("");

	// Send Listing Header
	if (_disk->media_header.size() == 0)
	{
		// Set device default Listing Header
		char buf[7] = { '\0' };
		sprintf(buf, "%.02d 2A", IEC.data.device);
		byte_count += sendHeader(PRODUCT_ID, buf);
		if ( byte_count == 0 ) return;
	}
	else
	{
		byte_count += sendHeader(_disk->media_header.c_str(), _disk->media_id.c_str());
	}

	// Send Directory Items
	while(entry != nullptr)
	{
		uint32_t s = entry->size();
		uint32_t block_cnt = s / _disk->media_block_size;
		// Debug_printv( "size[%d] blocks[%d] blocksz[%d]", s, block_cnt, _disk->media_block_size );
		if ( s > 0 && s < _disk->media_block_size )
			block_cnt = 1;

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

		if (entry->name[0]!='.')
		{
			// Exit if ATN is PULLED while sending
			// Exit if there is an error while sending
			if ( IEC.bus_state == BUS_ERROR )
			{
				// Save file pointer position
				// streamUpdate(byte_count);
				//setDeviceStatus(74);
				return;
			}

			byte_count += sendLine(block_cnt, "%*s\"%s\"%*s %s", block_spc, "", name.c_str(), space_cnt, "", extension.c_str());
			if ( byte_count == 0 ) return;
		}

		entry.reset(_disk->getNextFileInDir());

		//fnLedManager.toggle(eLed::LED_BUS);
	}

	// Send Listing Footer
	byte_count += sendFooter();
	if ( byte_count == 0 ) return;

	// End program with two zeros after last line. Last zero goes out as EOI.
	IEC.sendByte(0);
	IEC.sendByte(0, true);
	//closeStream();

	Debug_printf("\r\n=================================\r\n%d bytes sent\r\n", byte_count);

	//fnLedManager.set(eLed::LED_BUS, true);
} // sendListing


bool iecDisk::sendFile()
{
	size_t i = 0;
	bool success_rx = true;
	bool success_tx = true;

	uint8_t b;  // byte
	uint8_t nb; // next byte
	size_t bi = 0;
	size_t load_address = 0;
	size_t sys_address = 0;

#ifdef DATA_STREAM
	char ba[9];
	ba[8] = '\0';
#endif

	// std::shared_ptr<MStream> istream = std::static_pointer_cast<MStream>(currentStream);
	auto istream = retrieveStream();
	if ( istream == nullptr )
	{
		//Debug_printv("Stream not found!");
		IEC.senderTimeout(); // File Not Found
		closeStream();
		return false;
	}

	if ( IEC.data.channel == CHANNEL_LOAD )
	{
		PeoplesUrlParser u;
		u.parseUrl(istream->url);
		//Debug_printv( "Change Directory Here! [%s]", u.base().c_str() );
		//changeDir(u.base());
	}

	uint32_t len = istream->size();
	uint32_t avail = istream->available();

	{
		if( IEC.data.channel == CHANNEL_LOAD )
		{
			// Get/Send file load address
			i = 2;
			istream->read(&b, 1);
			success_tx = IEC.sendByte(b);
			load_address = b & 0x00FF; // low byte
			sys_address = b;
			istream->read(&b, 1);
			success_tx = IEC.sendByte(b);
			load_address = load_address | b << 8;  // high byte
			sys_address += b * 256;
			Debug_printv( "load_address[$%.4X] sys_address[%d]", load_address, sys_address );

			// Get SYSLINE
		}

		// Read byte
		success_rx = istream->read(&b, 1);
        Debug_printv("b[%02X] success[%d]", b, success_rx);

		Debug_printf("sendFile: [$%.4X]\r\n=================================\r\n", load_address);
		while( success_rx && !istream->error() )
		{
            // Read next byte
            success_rx = istream->read(&nb, 1);

			//Debug_printv("b[%02X] nb[%02X] success_rx[%d] error[%d]", b, nb, success_rx, istream->error());
#ifdef DATA_STREAM
			if (bi == 0)
			{
				Debug_printf(":%.4X ", load_address);
				load_address += 8;
			}
#endif
			// Send Byte
			avail = istream->available();
			if ( !success_rx )
			{
                //Debug_printv("b[%02X] EOI", b);
				success_tx = IEC.sendByte(b, true); // indicate end of file.
				if ( !success_tx )
					Debug_printv("tx fail");
				
				break;
			}
			else
			{
				success_tx = IEC.sendByte(b);
				if ( !success_tx )
					Debug_printv("tx fail");
			}
			b = nb; // byte = next byte

#ifdef DATA_STREAM
			// Show ASCII Data
			if (b < 32 || b >= 127)
				ba[bi++] = 46;
			else
				ba[bi++] = b;

			if(bi == 8)
			{
				size_t t = (i * 100) / len;
				Debug_printf(" %s (%d %d%%) [%d]\r\n", ba, i, t, avail);
				bi = 0;
			}
#else
			size_t t = (i * 100) / len;
			Debug_printf("\rTransferring %d%% [%d, %d]", t, i, avail);
#endif

			// Exit if ATN is PULLED while sending
			if ( IEC.flags & ATN_PULLED )
			{
				Debug_printv("ATN pulled while sending. i[%d]", i);

				// Save file pointer position
				istream->seek(istream->position() - 2);
				success_rx = true;
				break;
			}

			// Toggle LED
			if (i % 50 == 0)
			{
				//fnLedManager.toggle(eLed::LED_BUS);
			}

			i++;
		}
		Debug_printf("\r\n=================================\r\n%d bytes sent of %d [SYS%d]\r\n", i, avail, sys_address);

		//Debug_printv("len[%d] avail[%d] success_rx[%d]", len, avail, success_rx);		
	}


	//fnLedManager.set(eLed::LED_BUS, true);

	if ( istream->error() )
	{
		Debug_println("sendFile: Transfer aborted!");
		IEC.senderTimeout();
		closeStream();
	}

	return success_rx;
} // sendFile


bool iecDisk::saveFile()
{
	size_t i = 0;
	bool success = true;
	bool done = false;

	size_t bi = 0;
	size_t load_address = 0;
	size_t b_len = 1;
	uint8_t b[b_len];
	uint8_t ll[b_len];
	uint8_t lh[b_len];

#ifdef DATA_STREAM
	char ba[9];
	ba[8] = '\0';
#endif

	auto ostream = retrieveStream();

    if ( ostream == nullptr ) {
        Debug_printv("couldn't open a stream for writing");
        IEC.senderTimeout(); // File Not Found
        return false;
    }
    else
	{
	 	// Stream is open!  Let's save this!

		// wait - what??? If stream position == x you don't have to seek(x)!!!
		// if ( ostream->position() > 0 )
		// {
		// 	// // Position file pointer
		// 	// ostream->seek(currentStream.cursor);
		// }
		// else
		{
			// Get file load address
			ll[0] = IEC.receiveByte();
			load_address = *ll & 0x00FF; // low byte
			lh[0] = IEC.receiveByte();
			load_address = load_address | *lh << 8;  // high byte
		}


		Debug_printv("saveFile: [$%.4X]\r\n=================================\r\n", load_address);

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

			b[0] = IEC.receiveByte();
			// if(ostream->isText())
			// 	ostream->putPetsciiAsUtf8(b[0]);
			// else
				ostream->write(b, b_len);
			i++;

			uint16_t f = IEC.flags;
			done = (f & EOI_RECVD) or (f & ERROR);

			// Exit if ATN is PULLED while sending
			if ( f & ATN_PULLED )
			{
				// Save file pointer position
				// streamUpdate(ostream->position());
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
				//fnLedManager.toggle(eLed::LED_BUS);
			}
		} while (not done);
    }
    // ostream->close(); // nor required, closes automagically

	Debug_printf("=================================\r\n%d bytes saved\r\n", i);
	//fnLedManager.set(eLed::LED_BUS, true);

	// TODO: Handle errorFlag

	return success;
} // saveFile



#endif /* BUILD_IEC */