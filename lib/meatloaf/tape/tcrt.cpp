#include "tcrt.h"

//#include "meat_broker.h"

/********************************************************
 * Streams
 ********************************************************/

// Translate TCRT file type to standard CBM file type
std::string TCRTMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    std::string type = "PRG";

    // Type
    // 0x00 - 0x3f: An program which can be loaded into the C64 and executed.
    // The load address from the FS is used (not stored in the file!). The load address + size must fit into the C64.
    // 0x00: general program
    // 0x01: game
    // 0x02: utility
    // 0x03: multimedia
    // 0x04: demo
    // 0x05: image
    // 0x06: tune
    // 0x38-0x3f: private use area
    // if ( file_type <= 0x3F )
    //     return "PRG";

    // 0x40 - 0x7f: A bundled file (see below) Same types as 0x00-0x3f (just for a bundle and not a prg)
    // This mode is designed for games that need to load further files or store data like high scores or save games.
    // It also cen be used for subdirectories.
    if ( file_type >= 0x40 && file_type <= 0x7F )
        type = "DIR";

    // 0x80 - 0xef: Data files may not displayed by a browser.
    // 0x80: general data
    // 0x81: text file (lower PETSCII)
    // 0x82: koala image
    // 0x83: hires image
    // 0x84: fli image (multicolor)
    if ( file_type == 0x81 )
        type = "SEQ";
    // else if ( file_type >= 0x80 && file_type <= 0xEF )
    //     return "PRG";

    // 0xf0: separator - this name should be displayed just as a separator. size must be 0, all other info data (start, load address) is undefined
    // if ( file_type == 0xF0 )
    //     return "PRG";

    // 0xfe: System file! This file is (part of) the program that launches at power up.
    // No other program should ever rename/delete this file or relocate the blocks. 
    // (The entry within the FS however can be moved around. This may cover the FS itself, but it's not required.)
    if ( file_type == 0xFE )
       type = "DEL";

    // 0xff: Marker for the first free entry.

    // All types not mentioned above are reserved.
    return " " + type;
}

bool TCRTMStream::seekEntry( std::string filename )
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
            //uint8_t i = entryFilename.find_first_of(0x00); // padded with NUL (0x00)
            entryFilename = entryFilename.substr(0, 16);
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("index[%d] filename[%s] entry.filename[%s] entry.file_type[%d]", index, filename.c_str(), entryFilename.c_str(), entry.file_type);

            if ( filename == entryFilename ) // Match exact
            {
                return true;
            }
            else if ( wildcard ) // Wildcard Match
            {
                if (filename == "*") // Match first PRG
                {
                    if (entry.file_type < 0xFE) // Skip system files
                    {
                        filename = entryFilename;
                        return true;
                    }
                }
                else if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                {
                    return true;
                }
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}

bool TCRTMStream::seekEntry( uint16_t index )
{
    // Calculate Sector offset & Entry offset
    index--;
    uint16_t entryOffset = 0xE7 + (index * 32);

    //Debug_printv("----------");
    //Debug_printv("index[%d] entryOffset[%d] entry_index[%d]", (index + 1), entryOffset, entry_index);

    containerStream->seek(entryOffset);
    containerStream->read((uint8_t *)&entry, sizeof(entry));

    //uint32_t file_start_address = (0xD8 + (entry.file_start_address[0] << 8 | entry.file_start_address[1] << 16));
    //uint32_t file_size = (entry.file_size[0] | (entry.file_size[1] << 8) | (entry.file_size[2] << 16)) + 2; // 2 bytes for load address
    //uint32_t file_load_address = entry.file_load_address[0] | entry.file_load_address[1] << 8;

    //Debug_printv("file_name[%.16s] file_type[%02X] data_offset[%X] file_size[%d] load_address[%04X]", entry.filename, entry.file_type, file_start_address, file_size, file_load_address);

    entry_index = index + 1;    
    if ( entry.file_type == 0xFF )
        return false;
    else
        return true;
}

uint32_t TCRTMStream::readFile(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    if ( _position < 2)
    {
        //Debug_printv("send load address[%4X]", _load_address);

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

bool TCRTMStream::seekPath(std::string path) {
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if ( seekEntry(path) )
    {
        //auto entry = containerImage->entry;
        //auto type = decodeType(entry.file_type).c_str();
        //Debug_printv("filename [%.16s] type[%s]", entry.filename, type);

        // Calculate file size
        _size = (entry.file_size[0] | (entry.file_size[1] << 8) | (entry.file_size[2] << 16)) + 2; // 2 bytes for load address

        // Load Address
        _load_address[0] = entry.file_load_address[0];
        _load_address[1] = entry.file_load_address[1];

        // Set position to beginning of file
        _position = 0;
        uint32_t file_start_address = (0xD8 + (entry.file_start_address[0] << 8 | entry.file_start_address[1] << 16));
        containerStream->seek(file_start_address);

        Debug_printv("File Size: size[%ld] available[%ld]", _size, available());
        
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

bool TCRTMFile::isDirectory() {
    //Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if ( pathInStream == "" )
        return true;
    else
        return false;
};

bool TCRTMFile::rewindDirectory() {
    dirIsOpen = true;
    Debug_printv("streamFile->url[%s]", streamFile->url.c_str());
    auto image = ImageBroker::obtain<TCRTMStream>(streamFile->url);
    if ( image == nullptr )
        Debug_printv("image pointer is null");

    image->resetEntryCounter();

    // Read Header
    image->readHeader();

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

MFile* TCRTMFile::getNextFileInDir() 
{
    bool r = false;

    if(!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<TCRTMStream>(streamFile->url);
    if ( image == nullptr )
        goto exit;

    do
    {
        r = image->getNextImageEntry();
    } while ( r && image->entry.file_type >= 0xFE); // Skip SYSTEM files
    
    if ( r )
    {
        std::string filename = image->entry.filename;
        //uint8_t i = filename.find_first_of(0x00); // padded with NUL (0x00)
        filename = filename.substr(0, 16);
        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (streamFile->url + "/" + filename).c_str() );
        auto file = MFSOwner::File(streamFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);
        file->size = (image->entry.file_size[0] | (image->entry.file_size[1] << 8) | (image->entry.file_size[2] << 16)) + 2; // 2 bytes for load address

        return file;
    }

exit:
    //Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}


