#include "tcrt.h"


/********************************************************
 * Streams
 ********************************************************/

bool TCRTIStream::seekEntry( std::string filename )
{
    size_t index = 1;
    mstr::replaceAll(filename, "\\", "/");
    bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );

    // Read Directory Entries
    if ( filename.size() )
    {
        while ( seekEntry( index ) )
        {
            std::string entryFilename = entry.filename;
            mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            Debug_printv("filename[%s] entry.filename[%.16s]", filename.c_str(), entryFilename.c_str());

            // Read Entry From Stream
            if (filename == "*") // Match first PRG
            {
                filename = entryFilename;
                return true;
            }
            else if ( filename == entryFilename ) // Match exact
            {
                return true;
            }
            else if ( wildcard )
            {
                if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                {
                    // Move stream pointer to start track/sector
                    return true;
                }
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool TCRTIStream::seekEntry( uint16_t index )
{
    // Calculate Sector offset & Entry offset
    index--;
    uint16_t entryOffset = 0xE7 + (index * 32);

    //Debug_printv("----------");
    //Debug_printv("index[%d] entryOffset[%d] entry_index[%d]", (index + 1), entryOffset, entry_index);

    containerStream->seek(entryOffset);
    containerStream->read((uint8_t *)&entry, sizeof(entry));

    // uint32_t file_start_address = (0xD8 + (entry.file_start_address[0] << 8 | entry.file_start_address[1]));
    // uint32_t file_size = (entry.file_size[0] | (entry.file_size[1] << 8) | (entry.file_size[2] << 16)) + 2; // 2 bytes for load address
    // uint32_t file_load_address = entry.file_load_address[0] | entry.file_load_address[1] << 8;

    // Debug_printv("file_name[%.16s] file_type[%02X] data_offset[%X] file_size[%d] load_address[%04X]", entry.filename, entry.file_type, file_start_address, file_size, file_load_address);

    entry_index = index + 1;    
    if ( entry.file_type == 0xFF )
        return false;
    else
        return true;
}

uint16_t TCRTIStream::readFile(uint8_t* buf, uint16_t size) {
    uint16_t bytesRead = 0;

    if ( _position < 2)
    {
        Debug_printv("send load address[%4X]", m_load_address);

        buf[0] = m_load_address[_position];
        bytesRead++;
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

    _position += bytesRead;

    return bytesRead;
}

bool TCRTIStream::seekPath(std::string path) {
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if ( seekEntry(path) )
    {
        //auto entry = containerImage->entry;
        auto type = decodeType(mapType(entry.file_type)).c_str();
        Debug_printv("filename [%.16s] type[%s]", entry.filename, type);

        // Calculate file size
        _size = (entry.file_size[0] | (entry.file_size[1] << 8) | (entry.file_size[2] << 16)) + 2; // 2 bytes for load address

        // Load Address
        m_load_address[0] = entry.file_load_address[0];
        m_load_address[1] = entry.file_load_address[1];

        // Set position to beginning of file
        uint32_t file_start_address = (0xD8 + (entry.file_start_address[0] << 8 | entry.file_start_address[1]));
        containerStream->seek(file_start_address);

        Debug_printv("File Size: size[%d] available[%d]", _size, available());
        
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

MStream* TCRTFile::getDecodedStream(std::shared_ptr<MStream> containerIstream) {
    Debug_printv("[%s]", url.c_str());

    return new TCRTIStream(containerIstream);
}


bool TCRTFile::isDirectory() {
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool TCRTFile::rewindDirectory() {
    dirIsOpen = true;
    Debug_printv("streamFile->url[%s]", streamFile->url.c_str());
    auto image = ImageBroker::obtain<TCRTIStream>(streamFile->url);
    if ( image == nullptr )
        Debug_printv("image pointer is null");

    image->resetEntryCounter();

    // Read Header
    image->seekHeader();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.disk_name);
    media_id = "TCRT";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    //mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile* TCRTFile::getNextFileInDir() {

    if(!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<TCRTIStream>(streamFile->url);

    bool r = false;
    do
    {
        r = image->seekNextImageEntry();
    } while ( r && image->mapType(image->entry.file_type) == 0x00); // Skip hidden files
    
    if ( r )
    {
        std::string fileName = mstr::format("%.16s", image->entry.filename);
        mstr::replaceAll(fileName, "/", "\\");
        //Debug_printv( "entry[%s]", (streamFile->url + "/" + fileName).c_str() );
        auto file = MFSOwner::File(streamFile->url + "/" + fileName);
        file->extension = image->decodeType(image->mapType(image->entry.file_type));
        return file;
    }
    else
    {
        //Debug_printv( "END OF DIRECTORY");
        dirIsOpen = false;
        return nullptr;
    }
}


uint32_t TCRTFile::size() {
    //Debug_printv("[%s]", streamFile->url.c_str());
    // use TCRT to get size of the file in image
    auto entry = ImageBroker::obtain<TCRTIStream>(streamFile->url)->entry;

    //size_t blocks = (UINT16_FROM_LE_UINT16(image->entry.load_address) + image->entry.file_size)) / image->block_size;
    //size_t blocks = 1;

    // 9E 60 00
    // 158 96 0

    size_t bytes = (entry.file_size[0] | (entry.file_size[1] << 8) | (entry.file_size[2] << 16)) + 2; // 2 bytes for load address

    return bytes;
}
