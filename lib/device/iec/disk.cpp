#ifdef BUILD_IEC

#include "disk.h"

#include <cstring>

#include <unordered_map>

#include "../../include/debug.h"

#include "fuji.h"
#include "utils.h"

// External ref to fuji object.
extern iecFuji theFuji;

iecDisk::iecDisk()
{
    // device_active = false;
    device_active = true; // temporary during bring-up
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
    return MEDIATYPE_UNKNOWN; // MEDIATYPE_UNKNOWN
}

// Destructor
iecDisk::~iecDisk()
{
    if (_disk != nullptr)
        delete _disk;
}

// Unmount disk file
void iecDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        _disk->unmount();
        device_active = false;
    }
}

// Create blank disk
bool iecDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    // TODO IMPLEMENT
    return false;
}

void iecDisk::process_load()
{
}

void iecDisk::process_save()
{
}

void iecDisk::process_command()
{
}

void iecDisk::process_file()
{
}

// Process command
device_state_t iecDisk::process(IECData *id)
{
    Debug_printf("iecDisk::process()\n");
    virtualDevice::process(id);

    switch (commanddata->channel)
    {
    case 0: // LOAD
        process_load();
        break;
    case 1: // SAVE
        process_save();
        break;
    case 15: // COMMAND
        process_command();
        break;
    default: // Open files (2-14)
        process_file();
        break;
    }
    return device_state;
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
    Debug_printv("m_filename[%s]", m_filename.c_str());
    // //auto file = Meat::New<MFile>( device_config.basepath() + "/" + m_filename );
    // auto file = Meat::New<MFile>( m_mfile->url + m_filename );
    auto file = Meat::New<MFile>( m_filename );
    if ( !file->exists() )
        return false;
    
    Debug_printv("file[%s]", file->url.c_str());

    std::shared_ptr<MStream> new_stream;

    // LOAD / GET / INPUT
    if ( mode == std::ios_base::in )
    {
        Debug_printv("LOAD m_mfile[%s] m_filename[%s]", m_mfile->url.c_str(), m_filename.c_str());
        new_stream = std::shared_ptr<MStream>(file->meatStream());

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
        Debug_printv("SAVE m_filename[%s]", m_filename.c_str());
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

    //Debug_printv("Stream created. key[%d]", key);
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

#endif /* BUILD_IEC */