#include "t64.h"

//#include "meat_broker.h"
#include "endianness.h"

/********************************************************
 * Streams
 ********************************************************/

std::string T64MStream::decodeType(uint8_t file_type, bool show_hidden)
{
    std::string type = "PRG";

    if ( file_type == 0x00 )
    {
        if ( entry.entry_type == 1 )
            type = "TAP";
        if ( entry.entry_type > 1 )
            type = "FRZ";
    }

    return " " + type;
}

bool T64MStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if ( filename.size() )
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
        while ( seekEntry( index ) )
        {
            std::string entryFilename = entry.filename;
            uint8_t i = entryFilename.find_first_of(0x20); // (in PETASCII, padded with $20, not $A0)
            entryFilename = entryFilename.substr(0, (i > 16 ? 16 : i));
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

            if ( filename == entryFilename ) // Match exact
            {
                return true;
            }
            else if ( wildcard ) // Wildcard Match
            {
                if (filename == "*") // Match first entry
                {
                    filename = entryFilename;
                    return true;
                }
                else if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                {
                    // Set filename to this filename
                    Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
                    return true;
                }
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool T64MStream::seekEntry( uint16_t index )
{
    // Calculate Sector offset & Entry offset
    index--;
    uint16_t entryOffset = 0x40 + (index * sizeof(entry));

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    containerStream->seek(entryOffset);
    containerStream->read((uint8_t *)&entry, sizeof(entry));

    //Debug_printv("r[%d] file_type[%02X] file_name[%.16s]", r, entry.file_type, entry.filename);

    //if ( next_track == 0 && next_sector == 0xFF )
    entry_index = index + 1;    
    if ( entry.file_type == 0x00 )
        return false;
    else
        return true;
}


uint32_t T64MStream::readFile(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( _position < 2)
    {
        //Debug_printv("position[%d] load00[%d] load01[%d]", _position, _load_address[0], _load_address[1]);

        buf[0] = _load_address[_position];
        bytesRead = size;
        // if ( size > 1 )
        // {
        //     buf[0] = m_load_address[0];
        //     buf[1] = m_load_address[1];
        //     bytesRead += containerStream->read(buf, size);
        // }
    }
    else
    {
        bytesRead += containerStream->read(buf, size);
    }

    return bytesRead;
}


bool T64MStream::seekPath(std::string path) {
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if ( seekEntry(path) )
    {
        //auto entry = containerImage->entry;
        auto type = decodeType(entry.file_type).c_str();
        uint32_t start_address = UINT16_FROM_HILOBYTES(entry.start_address[1], entry.start_address[0]);
        uint32_t end_address = UINT16_FROM_HILOBYTES(entry.end_address[1], entry.end_address[0]);
        uint32_t data_offset = UINT32_FROM_LE_UINT32(entry.data_offset);
        Debug_printv("filename [%.16s] type[%s] start_address[%lu] end_address[%lu] data_offset[%lu]", entry.filename, type, start_address, end_address, data_offset);

        // Calculate file size
        _size = ( end_address - start_address ) + 2; // 2 bytes for load address

        // Load Address
        _load_address[0] = entry.start_address[0];
        _load_address[1] = entry.start_address[1];

        // Set position to beginning of file
        _position = 0;
        containerStream->seek(entry.data_offset);

        Debug_printv("File Size: size[%lu] available[%lu] position[%lu]", _size, available(), _position);

        return true;
    }
    else
    {
        Debug_printv( "Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool T64MFile::isDirectory() {
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool T64MFile::rewindDirectory() {
    dirIsOpen = true;
    Debug_printv("streamFile->url[%s]", streamFile->url.c_str());
    auto image = ImageBroker::obtain<T64MStream>(streamFile->url);
    if ( image == nullptr )
        Debug_printv("image pointer is null");

    image->resetEntryCounter();

    // Read Header
    image->readHeader();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.disk_name);
    media_id = " T64 ";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    //mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile* T64MFile::getNextFileInDir() {

    if(!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<T64MStream>(streamFile->url);
    if ( image == nullptr )
        goto exit;

    if ( image->getNextImageEntry() )
    {
        std::string filename = image->entry.filename;
        uint8_t i = filename.find_first_of(0x20); // (in PETASCII, padded with $20, not $A0)
        filename = filename.substr(0, (i > 16 ? 16 : i));
        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (streamFile->url + "/" + filename).c_str() );
        auto file = MFSOwner::File(streamFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);

        size_t end_address = UINT16_FROM_HILOBYTES(image->entry.end_address[1], image->entry.end_address[0]);
        size_t start_address = UINT16_FROM_HILOBYTES(image->entry.start_address[1], image->entry.start_address[0]);
        file->size = ( end_address - start_address ) + 2; // 2 bytes for load address

        Debug_printv( "entry[%s] ext[%s]", filename.c_str(), file->extension.c_str() );
        
        return file;
    }

exit:
    //Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

