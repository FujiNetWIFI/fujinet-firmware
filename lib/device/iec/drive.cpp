#ifdef BUILD_IEC

#include "drive.h"

#include <cstring>
#include <sstream>
#include <unordered_map>

#include "../../include/debug.h"
#include "../../include/cbm_defines.h"

#include "make_unique.h"

#include "fuji.h"
#include "fnFsSD.h"
#include "led.h"
#include "utils.h"

#include "meat_media.h"

// External ref to fuji object.
extern iecFuji theFuji;

iecDrive::iecDrive()
{
    // device_active = false;
    device_active = true; // temporary during bring-up

    _base.reset( MFSOwner::File("/") );
    _last_file = "";
}

// Read disk data and send to computer
void iecDrive::read()
{
    // TODO: IMPLEMENT
}

// Write disk data from computer
void iecDrive::write(bool verify)
{
    // TODO: IMPLEMENT
}

// Disk format
void iecDrive::format()
{
    // TODO IMPLEMENT
}

/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t iecDrive::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    std::string url = this->host->get_hostname();
    mstr::toLower(url);
    if ( url == "sd" )
        url = "//sd";
    url += filename;

    Debug_printv("DRIVE[#%d] URL[%s] MOUNT[%s]\n", this->_devnum, url.c_str(), filename);

    _base.reset( MFSOwner::File( url ) );

    return MediaType::discover_mediatype(filename); // MEDIATYPE_UNKNOWN
}

// Destructor
iecDrive::~iecDrive()
{
    // if (_base != nullptr)
    //     delete _base;
}

// Unmount disk file
void iecDrive::unmount()
{
    Debug_printv("DRIVE[#%d] UNMOUNT\r\n", this->_devnum);

    if (_base != nullptr)
    {
        //_base->unmount();
        device_active = false;
    }
}

// Create blank disk
bool iecDrive::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    // TODO IMPLEMENT
    return false;
}


// Process command
device_state_t iecDrive::process()
{
    virtualDevice::process();

//    Debug_printv("channel[%d]", commanddata.channel);


    switch (commanddata.channel)
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

//    Debug_printv("url[%s] file[%s] state[%d]", _base->url.c_str(), _last_file.c_str(), state);
    return state;
}

void iecDrive::process_load()
{
//    Debug_printv("secondary[%.2X]", commanddata.secondary);
    switch (commanddata.secondary)
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

void iecDrive::process_save()
{
    Debug_printv("secondary[%.2X]", commanddata.secondary);
    switch (commanddata.secondary)
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

void iecDrive::process_command()
{
//    Debug_printv("primary[%.2X] secondary[%.2X]", commanddata.primary, commanddata.secondary);
    if (commanddata.primary == IEC_TALK) // && commanddata.secondary == IEC_REOPEN)
    {
        iec_talk_command();
    }
    else if (commanddata.primary == IEC_UNLISTEN)
    {
        if (commanddata.secondary == IEC_CLOSE)
            iec_close();
        else
            iec_command();
    }
}

void iecDrive::process_channel()
{
    //Debug_printv("secondary[%.2X]", commanddata.secondary);
    switch (commanddata.secondary)
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


void iecDrive::iec_open()
{
    if ( commanddata.primary == IEC_UNLISTEN )
        return;

    pt = util_tokenize(payload, ',');
    if ( pt.size() > 1 )
    {
        payload = pt[0];
        //Debug_printv("filename[%s] type[%s] mode[%s]", pt[0].c_str(), pt[1].c_str(), pt[2].c_str());
    }

    Debug_printv("_base[%s] payload[%s]", _base->url.c_str(), payload.c_str());

    if ( mstr::startsWith(payload, "0:") )
    {
        // Remove media ID from command string
        payload = mstr::drop(payload, 2);
    }
    if ( mstr::startsWith(payload, "CD") )
    {
        payload = mstr::drop(payload, 2);
        if ( payload[0] == ':' || payload[0] == ' ' )
            payload = mstr::drop(payload, 1);
    }

    if ( payload.length() )
    {
        if ( payload[0] == '$' ) 
            payload.clear();

        payload = mstr::toUTF8(payload);
        auto n = _base->cd( payload );
        if ( n != nullptr )
            _base.reset( n );

        Debug_printv("_base[%s]", _base->url.c_str());
        if ( !_base->isDirectory() )
        {
            if ( !registerStream(commanddata.channel) )
            {
                Debug_printv("File Doesn't Exist [%s]", _base->url.c_str());
            }
        }
    }
}

void iecDrive::iec_close()
{
    if (_base == nullptr)
    {
        IEC.senderTimeout();
        return; // Punch out.
    }
//    Debug_printv("url[%s]", _base->url.c_str());

    closeStream( commanddata.channel );
    commanddata.init();
    state = DEVICE_IDLE;
//    Debug_printv("device init");
}

void iecDrive::iec_reopen_load()
{
    Debug_printv( "_base[%s] _last_file[%s]", _base->url.c_str(), _last_file.c_str() );

    // if ( payload.length() )
    // {
    //     auto n = _base->cd( payload );
    //     if ( n != nullptr )
    //         _base.reset( n );

    //     Debug_printv("_base[%s]", _base->url.c_str());
    //     if ( !_base->isDirectory() )
    //     {
    //         if ( !registerStream(commanddata.channel) )
    //         {
    //             sendFileNotFound();
    //             return;
    //         }
    //     }
    // }

    if ( _base->isDirectory() ) 
    {
        sendListing();
    }
    else
    {
        sendFile();
    }
}

void iecDrive::iec_reopen_save()
{
    if (_base == nullptr)
    {
        IEC.senderTimeout();
        return; // Punch out.
    }
    Debug_printv("url[%s]", _base->url.c_str());

    saveFile();
}

void iecDrive::iec_reopen_channel()
{
    //Debug_printv("primary[%.2X]", commanddata.primary);
    switch (commanddata.primary)
    {
    case IEC_LISTEN:
        iec_reopen_channel_listen();
        break;
    case IEC_TALK:
        iec_reopen_channel_talk();
        break;
    }
}


void iecDrive::iec_reopen_channel_listen()
{
    std::string s = IEC.receiveBytes();
    Debug_printv("{%s}", s.c_str() );
}

void iecDrive::iec_reopen_channel_talk()
{
    //Debug_printv("here");
    sendFile();
}


void iecDrive::iec_listen_command()
{
    Debug_printv("here");
}

void iecDrive::iec_talk_command()
{
    //Debug_printv("here");
    if (response_queue.empty())
        iec_talk_command_buffer_status();
}

void iecDrive::iec_talk_command_buffer_status()
{
    // std::ostringstream ss;
    // ss << iecStatus.error << "," << "\"" << iecStatus.msg << "\"" << "," << iecStatus.connected << "," << iecStatus.channel;
    // std::string s = ss.str();
    std::string s = "00, OK,00,00\r";
    IEC.sendBytes(s, true);
}

void iecDrive::iec_command()
{
    Debug_printv("command[%s]", payload.c_str());

    // if (mstr::startsWith(payload, "cd"))
    // 	set_prefix();
    // else if (pt[0] == "pwd")
    // 	get_prefix();
    // else if (pt[0] == "id")
    // 	set_device_id();

    // Drive level commands
    // CBM DOS 2.5
    switch ( payload[0] )
    {
        case 'B':
            if (payload[1] == '-')
            {
                // B-P buffer pointer
                if (payload[2] == 'P')
                {
                    payload = mstr::drop(payload, 3);
                    mstr::trim(payload);
                    mstr::replaceAll(payload, "  ", " ");
                    pti = util_tokenize_uint8(payload);
                    Debug_printv("payload[%s] channel[%d] position[%d]", payload.c_str(), pti[0], pti[1]);

                    auto stream = retrieveStream( pti[0] );
                    if ( stream )
                    {
                        stream->position( pti[1] );
                    }
                }
                // B-A allocate bit in BAM not implemented
                // B-F free bit in BAM not implemented
                // B-E block execute impossible at this level of emulation!
            }
            //Error(ERROR_31_SYNTAX_ERROR);
            Debug_printv( "block/buffer");
        break;
        case 'C':
            if ( payload[1] != 'D' && payload[2] == ':')
            {
                //Copy(); // Copy File
                Debug_printv( "copy file");
            }
        break;
        case 'D':
            Debug_printv( "duplicate disk");
            //Error(ERROR_31_SYNTAX_ERROR);	// DI, DR, DW not implemented yet
        break;
        case 'I':
            // Initialize
            Debug_printv( "initialize");
        break;
        case 'M':
            if ( payload[1] == '-' ) // Memory
            {
                if (payload[2] == 'R') // M-R memory read
                {
                    payload = mstr::drop(payload, 3);
                    std::string code = mstr::toHex(payload);
                    uint16_t address = (payload[0] | payload[1] << 8);
                    uint8_t size = payload[2];
                    Debug_printv("Memory Read [%s]", code.c_str());
                    Debug_printv("address[%.4X] size[%d]", address, size);
                }
                else if (payload[2] == 'W') // M-W memory write
                {
                    payload = mstr::drop(payload, 3);
                    std::string code = mstr::toHex(payload);
                    uint16_t address = (payload[0] | payload[1] << 8);
                    Debug_printv("Memory Write address[%.4X][%s]", address, code.c_str());
                }
                else if (payload[2] == 'E') // M-E memory execute
                {
                    payload = mstr::drop(payload, 3);
                    std::string code = mstr::toHex(payload);
                    uint16_t address = (payload[0] | payload[1] << 8);
                    Debug_printv("Memory Execute address[%.4X][%s]", address, code.c_str());
                }
            }
        break;
        case 'N':
            //New();
            Debug_printv( "new (format)");
        break;
        case 'R':
            if ( payload[1] != 'D' && payload[2] == ':' ) // Rename
            {
                Debug_printv( "rename file");
                // Rename();
            }
        break;
        case 'S':
            if (payload[2] == ':') // Scratch
            {
                Debug_printv( "scratch");
                //Scratch();
            }
        break;
        case 'U':
            Debug_printv( "user 01a2b");
            //User();
            if (payload[1] == '1') // User 1
            {
                payload = mstr::drop(payload, 3);
                mstr::trim(payload);
                mstr::replaceAll(payload, "  ", " ");
                pti = util_tokenize_uint8(payload);
                Debug_printv("payload[%s] channel[%d] media[%d] track[%d] sector[%d]", payload.c_str(), pti[0], pti[1], pti[2], pti[3]);

                auto stream = retrieveStream( pti[0] );
                if ( stream )
                {
                    stream->seekSector( pti[2], pti[3] );
                    stream->reset();
                }
            }
        break;
        case 'V':
            Debug_printv( "validate bam");
        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }

    // SD2IEC Commands
    // http://www.n2dvm.com/UIEC.pdf
    switch ( payload[0] )
    {
        case 'C':
            if ( payload[1] == 'P') // Change Partition
            {
                Debug_printv( "change partition");
                //ChangeDevice();
            }
            else if ( payload[1] == 'D') // Change Directory
            {
                Debug_printv( "change directory");
                set_prefix();
            }
        break;
        case 'E':
            if (payload[1] == '-')
            {
                Debug_printv( "eeprom");
            }
        break;
        case 'G':
            Debug_printv( "get partition info");
            //Error(ERROR_31_SYNTAX_ERROR);	// G-P not implemented yet
        break;
        case 'M':
            if ( payload[1] == 'D') // Make Directory
            {
                Debug_printv( "make directory");
            }
        break;
        case 'P':
            Debug_printv( "position");
            //Error(ERROR_31_SYNTAX_ERROR);	// P not implemented yet
        break;
        case 'R':
            if ( payload[1] == 'D') // Remove Directory
            {
                Debug_printv( "remove directory");
            }
        break;
        case 'S':
            if (payload[1] == '-')
            {
                // Swap drive number 
                Debug_printv( "swap drive number");
                //Error(ERROR_31_SYNTAX_ERROR);
                break;
            }
        break;
        case 'T':
            if (payload[1] == '-')
            {
                Debug_printv( "time"); // RTC support
                //Error(ERROR_31_SYNTAX_ERROR);	// T-R and T-W not implemented yet
            }
        break;
        case 'W':
            // Save out current options?
            //OPEN1, 9, 15, "XW":CLOSE1
            Debug_printv( "user 1a2b");
        break;
        case 'X':
            Debug_printv( "xtended commands");
            // X{0-4}
            // XE+ / XE-
            // XB+ / XB-
            // X{0-7}={0-15}
            // XD?
            // XJ+ / XJ-
            // X
            // XS:{name} / XS
            // XW
            // X?
            //Extended();
        break;
        case '/':

        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }
}


void iecDrive::set_device_id()
{
    if (pt.size() < 2)
    {
        iecStatus.error = 0; // TODO: Add error number for this
        iecStatus.msg = "device id required";
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        return;
    }

    int new_id = atoi(pt[1].c_str());

    IEC.changeDeviceId(this, new_id);

    iecStatus.error = 0;
    iecStatus.msg = "ok";
    iecStatus.connected = 0;
    iecStatus.channel = commanddata.channel;
}

void iecDrive::get_prefix()
{
    int channel = -1;

    if (pt.size() < 2)
    {
        iecStatus.error = 255; // TODO: Add error number for this
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        iecStatus.msg = "need channel #";
        return;
    }

    channel = atoi(pt[1].c_str());
    auto stream = retrieveStream( channel );

    iecStatus.error = 0;
    iecStatus.msg = stream->url;
    iecStatus.connected = 0;
    iecStatus.channel = channel;
}

void iecDrive::set_prefix()
{
    std::string path = payload;

    // Isolate path
    path = mstr::drop(path, 2);
    if ( mstr::startsWith(path, ":") || mstr::startsWith(path, " " ) )
        path = mstr::drop(path, 1);

    Debug_printv("path[%s]", path.c_str());
    path = mstr::toUTF8( path );
    auto n = _base->cd( path );
    if ( n != nullptr )
        _base.reset( n );

    if ( !_base->isDirectory() )
    {
        if ( !registerStream(0) )
        {
            sendFileNotFound();
        }
    }
}



// used to start working with a stream, registering it as underlying stream of some
// IEC channel on some IEC device
bool iecDrive::registerStream ( uint8_t channel )
{
    // Debug_printv("dc_basepath[%s]",  device_config.basepath().c_str());
    // Debug_printv("_file[%s]", _file.c_str());

    // TODO: Determine mode and create the proper stream
    std::ios_base::openmode mode = std::ios_base::in;

    Debug_printv("_base[%s]", _base->url.c_str());
    _base.reset( MFSOwner::File( _base->url ) );

    std::shared_ptr<MStream> new_stream;

    // LOAD / GET / INPUT
    if ( channel == CHANNEL_LOAD )
    {
        if ( !_base->exists() )
            return false;

        Debug_printv("LOAD \"%s\"", _base->url.c_str());
        new_stream = std::shared_ptr<MStream>(_base->getSourceStream());
    }

    // SAVE / PUT / PRINT / WRITE
    else if ( channel == CHANNEL_SAVE )
    {
        Debug_printv("SAVE \"%s\"", _base->url.c_str());
        // CREATE STREAM HERE FOR OUTPUT
        new_stream = std::shared_ptr<MStream>(_base->getSourceStream(std::ios::out));
        new_stream->open();
    }
    else
    {
        Debug_printv("OTHER \"%s\"", _base->url.c_str());
        new_stream = std::shared_ptr<MStream>(_base->getSourceStream());
    }


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
        closeStream( channel );
    }


    //size_t key = ( IEC.data.device * 100 ) + IEC.data.channel;

    // // Check to see if a stream is open on this device/channel already
    // auto found = streams.find(key);
    // if ( found != streams.end() )
    // {
    //     Debug_printv( "Stream already registered on this device/channel!" );
    //     return false;
    // }

    // Add stream to streams 
    auto newPair = std::make_pair ( channel, new_stream );
    streams.insert ( newPair );

    Debug_printv("Stream created. key[%d] count[%d]", channel, streams.bucket_count());
    return true;
}

std::shared_ptr<MStream> iecDrive::retrieveStream ( uint8_t channel )
{
    Debug_printv("Stream key[%d]", channel);

    if ( streams.find ( channel ) != streams.end() )
    {
        Debug_printv("Stream retrieved. key[%d] count[%d]", channel, streams.bucket_count());
        return streams.at ( channel );
    }
    else
    {
        Debug_printv("Error! Trying to recall not-registered stream!");
        return nullptr;
    }
}

bool iecDrive::closeStream ( uint8_t channel, bool close_all )
{
    auto found = streams.find(channel);

    if ( found != streams.end() )
    {
        Debug_printv("Stream closed. key[%d] count[%d]", channel, streams.bucket_count());
        auto closingStream = (*found).second;
        closingStream->close();
        return streams.erase ( channel );
    }

    return false;
}

uint16_t iecDrive::retrieveLastByte ( uint8_t channel )
{
    if ( streamLastByte.find ( channel ) != streamLastByte.end() )
    {
        return streamLastByte.at ( channel );
    }
    else
    {
        return 999;
    }
}

void iecDrive::storeLastByte( uint8_t channel, char last)
{
    auto newPair = std::make_pair ( channel, (uint16_t)last );
    streamLastByte.insert ( newPair );
}

void iecDrive::flushLastByte( uint8_t channel )
{
    auto newPair = std::make_pair ( channel, (uint16_t)999 );
    streamLastByte.insert ( newPair );
}



// send single basic line, including heading basic pointer and terminating zero.
uint16_t iecDrive::sendLine(uint16_t blocks, const char *format, ...)
{
    // Debug_printv("bus[%d]", IEC.state);

    // Exit if ATN is PULLED while sending
    // Exit if there is an error while sending
    if ( IEC.state == BUS_ERROR )
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

uint16_t iecDrive::sendLine(uint16_t blocks, char *text)
{
    Serial.printf("%d %s ", blocks, text);

    // Exit if ATN is PULLED while sending
    // Exit if there is an error while sending
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};

    // Get text length
    uint8_t len = strlen(text);

    // Send that pointer
    // No basic line pointer is used in the directory listing set to 0x0101
    IEC.sendByte(0x01);		// IEC.sendByte(basicPtr bitand 0xFF);
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};
    IEC.sendByte(0x01);		// IEC.sendByte(basicPtr >> 8);
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};

    // Send blocks
    IEC.sendByte(blocks bitand 0xFF);
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};
    IEC.sendByte(blocks >> 8);
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};

    // Send line contents
    for (uint8_t i = 0; i < len; i++)
    {
        IEC.sendByte(text[i]);
        if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};
    }

    // Finish line
    IEC.sendByte(0);
    if ( IEC.flags & ERROR ) {Debug_printv("line[%s]", text); return 0;};

    Serial.println("");
    
    return len + 5;
} // sendLine

uint16_t iecDrive::sendHeader(std::string header, std::string id)
{
    uint16_t byte_count = 0;
    bool sent_info = false;

    std::string url = _base->host;
    url = mstr::toPETSCII2(url);
    std::string path = _base->path;
    path = mstr::toPETSCII2(path);
    std::string archive = _base->media_archive;
    archive = mstr::toPETSCII2(archive);
    std::string image = _base->media_image;
    image = mstr::toPETSCII2(image);
    //Debug_printv("path[%s] size[%d]", path.c_str(), path.size());

    // Send List HEADER
    uint8_t space_cnt = 0;
    space_cnt = (16 - header.size()) / 2;
    space_cnt = (space_cnt > 8 ) ? 0 : space_cnt;

    //Debug_printv("header[%s] id[%s] space_cnt[%d]", header.c_str(), id.c_str(), space_cnt);

    byte_count += sendLine(0, CBM_REVERSE_ON "\"%*s%s%*s\" %s", space_cnt, "", header.c_str(), space_cnt, "", id.c_str());
    if ( IEC.flags & ERROR ) return 0;

    //byte_count += sendLine(basicPtr, 0, "\x12\"%*s%s%*s\" %.02d 2A", space_cnt, "", PRODUCT_ID, space_cnt, "", device_config.device());
    //byte_count += sendLine(basicPtr, 0, CBM_REVERSE_ON "%s", header.c_str());

    // Send Extra INFO
    if (url.size())
    {
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[URL]");
        if ( IEC.flags & ERROR ) return 0;
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, url.c_str());
        if ( IEC.flags & ERROR ) return 0;
        sent_info = true;
    }
    if (path.size() > 1)
    {
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[PATH]");
        if ( IEC.flags & ERROR ) return 0;
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, path.c_str());
        if ( IEC.flags & ERROR ) return 0;
        sent_info = true;
    }
    if (archive.size() > 1)
    {
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[ARCHIVE]");
        if ( IEC.flags & ERROR ) return 0;
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, archive.c_str());
        if ( IEC.flags & ERROR ) return 0;
        sent_info = true;
    }
    if (image.size())
    {
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, "[IMAGE]");
        if ( IEC.flags & ERROR ) return 0;
        byte_count += sendLine(0, "%*s\"%-*s\" NFO", 0, "", 19, image.c_str());
        if ( IEC.flags & ERROR ) return 0;
        sent_info = true;
    }
    if (sent_info)
    {
        byte_count += sendLine(0, "%*s\"-------------------\" NFO", 0, "");
        if ( IEC.flags & ERROR ) return 0;
    }

    // If SD Card is available ad we are at the root path show it as a directory at the top
    if (fnSDFAT.running() && _base->url.size() < 2)
    {
        byte_count += sendLine(0, "%*s\"SD\"               DIR", 3, "");
        if ( IEC.flags & ERROR ) return 0;
    }

    return byte_count;
}

uint16_t iecDrive::sendFooter()
{
    uint16_t blocks_free;
    uint16_t byte_count = 0;
    uint64_t bytes_free = _base->getAvailableSpace();

    //Debug_printv("image[%s] size[%d] blocks[%d]", _base->media_image.c_str(), _base->size(), _base->media_blocks_free);
    if ( _base->media_image.size() )
    {
        blocks_free = _base->media_blocks_free;
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

void iecDrive::sendListing()
{
    Serial.printf("sendListing: [%s]\r\n=================================\r\n", _base->url.c_str());

    uint16_t byte_count = 0;
    std::string extension = "dir";

    std::unique_ptr<MFile> entry = std::unique_ptr<MFile>( _base->getNextFileInDir() );

    if(entry == nullptr) {
        closeStream( commanddata.channel );

        bool isOpen = registerStream(commanddata.channel);
        if(isOpen) 
        {
            sendFile();
        }
        else
        {
            sendFileNotFound();
        }
        
        return;
    }

    //fnLedStrip.startRainbow(300);

    // Send load address
    IEC.sendByte(CBM_BASIC_START & 0xff);
    IEC.sendByte((CBM_BASIC_START >> 8) & 0xff);
    byte_count += 2;

    // If there has been a error don't try to send any more bytes
    if ( IEC.flags & ERROR )
    {
        Debug_printv(":(");
        return;
    }

    Serial.println("");

    // Send Listing Header
    if (_base->media_header.size() == 0)
    {
        // Send device default listing header
        char buf[7] = { '\0' };
        sprintf(buf, "%.02d 2A", IEC.data.device);
        byte_count += sendHeader(PRODUCT_ID, buf);
        if ( IEC.flags & ERROR ) return;
    }
    else
    {
        // Send listing header from media file
        if ( !entry->isPETSCII )
            _base->media_header = mstr::toPETSCII2( _base->media_header );

        byte_count += sendHeader(_base->media_header.c_str(), _base->media_id.c_str());
        if ( IEC.flags & ERROR ) return;
    }

    // Send Directory Items
    while(entry != nullptr)
    {
        if (!entry->isDirectory())
        {
            // Get extension
            if (entry->extension.length())
            {
                extension = entry->extension;

                // TODO: Compatibility mode file extension
                // // Change extension to PRG if it is non-standard
                // std::string valid_extensions = "delseqprgusrrelcbm";
                // if ( !mstr::contains(valid_extensions, extension.c_str()) )
                //     extension = "prg";
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
        std::string name = entry->name;
        if ( !entry->isPETSCII )
        {
            name = mstr::toPETSCII2( entry->name );
            extension = mstr::toPETSCII2(extension);
        }
        //mstr::rtrimA0(name);
        mstr::replaceAll(name, "\\", "/");

        //uint32_t s = entry->size();
        //uint32_t block_cnt = s / _base->media_block_size;
        uint32_t block_cnt = entry->blocks();
        // Debug_printv( "size[%d] blocks[%d] blocksz[%d]", s, block_cnt, _base->media_block_size );
        //if ( s > 0 && s < _base->media_block_size )
        //    block_cnt = 1;

        uint8_t block_spc = 3;
        if (block_cnt > 9)
            block_spc--;
        if (block_cnt > 99)
            block_spc--;
        if (block_cnt > 999)
            block_spc--;

        uint8_t space_cnt = 21 - (name.size() + 5);
        if (space_cnt > 21)
            space_cnt = 0;

        if (name[0]!='.')
        {
            // Exit if ATN is PULLED while sending
            // Exit if there is an error while sending
            if ( IEC.state == BUS_ERROR )
            {
                // Save file pointer position
                // streamUpdate(byte_count);
                //setDeviceStatus(74);
                return;
            }

            byte_count += sendLine(block_cnt, "%*s\"%s\"%*s %s", block_spc, "", name.c_str(), space_cnt, "", extension.c_str());
            if ( IEC.flags & ERROR ) return;
        }

        entry.reset(_base->getNextFileInDir());

        //fnLedManager.toggle(eLed::LED_BUS);
    }

    // Send Listing Footer
    byte_count += sendFooter();
    if ( IEC.flags & ERROR ) return;

    // End program with two zeros after last line. Last zero goes out as EOI.
    IEC.sendByte(0);
    IEC.sendByte(0, true);
    //closeStream();

    Serial.printf("\r\n=================================\r\n%d bytes sent\r\n\r\n", byte_count);

    //fnLedManager.set(eLed::LED_BUS, false);
    //fnLedStrip.stopRainbow();
} // sendListing


bool iecDrive::sendFile()
{
    uint32_t count = 0;
    bool success_rx = true;
    bool success_tx = true;

    uint8_t b;  // byte
//    uint8_t nb; // next byte
    uint8_t bi = 0;
    uint16_t load_address = 0;
    uint16_t sys_address = 0;

#ifdef DATA_STREAM
    char ba[9];
    ba[8] = '\0';
#endif

    // std::shared_ptr<MStream> istream = std::static_pointer_cast<MStream>(currentStream);
    auto istream = retrieveStream(commanddata.channel);
    if ( istream == nullptr )
    {
        sendFileNotFound();
        return false;
    }

    Debug_printv("size[%d] avail[%d] pos[%d]", istream->size(), istream->available(), istream->position());

    if ( !_base->isDirectory() )
    {
        if ( istream->has_subdirs )
        {
            // Filesystem supports sub directories
            Debug_printv( "Subdir Change Directory Here! istream[%s] > base[%s]", _base->url.c_str(), _base->base().c_str() );
            _last_file = _base->name;
            _base.reset( MFSOwner::File( _base->base() ) );
        }
        else
        {
            // Handles media files that may have '/' as part of the filename
            auto f = MFSOwner::File( istream->url );
            Debug_printv( "Change Directory Here! istream[%s] > base[%s]", istream->url.c_str(), f->streamFile->url.c_str() );
            _base.reset( f->streamFile );
        }
    }
    //_base->dump();

    bool eoi = false;
    uint32_t size = istream->size();
    uint32_t avail = istream->available();

    //fnLedStrip.startRainbow(300);

    if( commanddata.channel == CHANNEL_LOAD && istream->position() == 0 )
    {
        // Get/Send file load address
        istream->read(&b, 1);
        success_tx = IEC.sendByte(b);
        load_address = b & 0x00FF; // low byte
        istream->read(&b, 1);
        success_tx = IEC.sendByte(b);
        load_address = load_address | b << 8;  // high byte
        sys_address = load_address;
        Serial.printf( "load_address[$%.4X] sys_address[%d]", load_address, sys_address );

        // Get SYSLINE
    }


    Serial.printf("\r\nsendFile: [$%.4X] pos[%d]\r\n=================================\r\n", load_address, istream->position());
    while( success_rx && !istream->error() )
    {
        //Debug_printv("b[%02X] nb[%02X] success_rx[%d] error[%d] count[%d] avail[%d]", b, nb, success_rx, istream->error(), count, avail);
#ifdef DATA_STREAM
        if (bi == 0)
        {
            Serial.printf(":%.4X ", load_address);
            load_address += 8;
        }
#endif

        // Read byte
        success_rx = istream->read(&b, 1);
        //Debug_printv("b[%02X] success[%d]", b, success_rx);

        count = istream->position();
        avail = istream->available();

        if ( istream->error() )
        {
            Debug_printv("Error reading stream.");
            break;
        }
        else if ( istream->eos() )
        {
            eoi = true;
        }

        // Send Byte
        success_tx = IEC.sendByte(b, eoi);
        if ( !success_tx )
        {
            Debug_printv("Error sending byte.")
            break;
        }

        // Exit if ATN is PULLED while sending
        if ( !eoi && IEC.flags & ATN_PULLED )
        {
            //IEC.pull ( PIN_IEC_SRQ );
            //Serial.println();
            //Debug_printv("ATN pulled while sending. b[%.2X]", b);
#ifdef DATA_STREAM
            Serial.printf("[atn]\r\n");
#endif

            // Save file pointer position
            istream->seek( -1, SEEK_CUR);
            //IEC.release ( PIN_IEC_SRQ );
            break;
        }

        uint32_t t = 0;
        if ( size )
            t = (count * 100) / size;

#ifdef DATA_STREAM
        // Show ASCII Data
        if (b < 32 || b >= 127)
            ba[bi++] = 46;
        else
            ba[bi++] = b;

        if(bi == 8)
        {
            Serial.printf(" %s (%d %d%%) [%d]\r\n", ba, count, t, avail);
            bi = 0;
        }
#else
        Serial.printf("\rTransferring %d%% [%d, %d]      ", t, count, avail);
#endif

        // // Toggle LED
        // if (i % 50 == 0)
        // {
        // 	fnLedManager.toggle(eLed::LED_BUS);
        // }

        if ( eoi )
        {
            break;
        }
    }


    Serial.printf("=================================\r\n%d bytes sent of %d [SYS%d]\r\n\r\n", count, size, sys_address);

    //Debug_printv("len[%d] avail[%d] success_rx[%d]", len, avail, success_rx);

    //fnLedManager.set(eLed::LED_BUS, false);
    //fnLedStrip.stopRainbow();

    if ( istream->error() || !success_tx )
    {
        Serial.println("sendFile: Transfer aborted!");
        IEC.senderTimeout();
        closeStream(commanddata.channel);
    }

    return success_rx;
} // sendFile


bool iecDrive::saveFile()
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

    auto ostream = retrieveStream(commanddata.channel);

    if ( ostream == nullptr ) {
        Serial.println("couldn't open a stream for writing");
        IEC.senderTimeout(); // Error
        return false;
    }
    else
    {
         // Stream is open!  Let's save this!

        // wait - what??? If stream position == x you don't have to seek(x)!!!
        i = ostream->position();
        if ( i == 0 )
        {
            // Get file load address
            ll[0] = IEC.receiveByte();
            load_address = *ll & 0x00FF; // low byte
            lh[0] = IEC.receiveByte();
            load_address = load_address | *lh << 8;  // high byte
        }


        Serial.printf("saveFile: [$%.4X]\r\n=================================\r\n", load_address);

        // Recieve bytes until a EOI is detected
        do
        {
            // Save Load Address
            if (i == 0)
            {
                Serial.print("[");
                ostream->write(ll, b_len);
                ostream->write(lh, b_len);
                i += 2;
                Serial.print("]");
            }

#ifdef DATA_STREAM
            if (bi == 0)
            {
                Serial.printf(":%.4X ", load_address);
                load_address += 8;
            }
#endif

            b[0] = IEC.receiveByte();
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
                ba[bi++] = 46;
            else
                ba[bi++] = b[0];

            if(bi == 8)
            {
                Serial.printf(" %s (%d)\r\n", ba, i);
                bi = 0;
            }
#endif

        } while (not done);
    }

    Serial.printf("=================================\r\n%d bytes saved\r\n", i);

    // TODO: Handle errorFlag

    return success;
} // saveFile

void iecDrive::sendFileNotFound()
{
    Debug_printv("File Doesn't Exist [%s]", _base->url.c_str());
    _last_file = "";
    _base.reset( MFSOwner::File( _base->base() ) );
    Debug_printv("_base[%s] reset", _base->url.c_str());
    IEC.senderTimeout(); // File Not Found
    state = DEVICE_ERROR;
} // sendFileNotFound

#endif /* BUILD_IEC */