
#include "d64.h"

//#include "meat_broker.h"
#include "../meat_media.h"
#include "endianness.h"

// D64 Utility Functions

bool D64MStream::seekBlock(uint64_t index, uint8_t offset)
{
    uint16_t sectorOffset = 0;
    uint8_t track = 0;

    // Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Determine actual track & sector from index
    do
    {
        track++;
        uint8_t count = getSectorCount(track);
        if (sectorOffset + count < index)
            sectorOffset += count;
        else
            break;

        // Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", track, speedZone(track), count, sectorOffset);
    } while (true);
    uint8_t sector = index - sectorOffset;

    this->block = index;
    this->track = track;
    this->sector = sector;

    // Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek((index * block_size) + offset);
}

bool D64MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    uint16_t sectorOffset = 0;

    //Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Is this a valid track?
    uint16_t c = partitions[partition].block_allocation_map.size() - 1;
    uint8_t start_track = partitions[partition].block_allocation_map[0].start_track;
    uint8_t end_track = partitions[partition].block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
    {
        Debug_printv("Invalid Track: track[%d] start_track[%d] end_track[%d]", track, start_track, end_track);
        return false;
    }

    // Is this a valid sector?
    c = getSectorCount(track);
    if (sector > c)
    {
        Debug_printv("Invalid Sector: sector[%d] sectorsPerTrack[%d]", sector, c);
        return false;
    }

    // Check for error info
    if (error_info)
    {
        // Look up error for this track/sector
    }

    track--;
    for (uint8_t index = 0; index < track; ++index)
    {
        sectorOffset += getSectorCount(index + 1);
        //Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", (index + 1), speedZone(index), getSectorCount(index + 1), sectorOffset);
    }
    track++;
    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;

    //Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek((sectorOffset * block_size) + offset);
}

bool D64MStream::seekSector(std::vector<uint8_t> trackSectorOffset)
{
    return seekSector(trackSectorOffset[0], trackSectorOffset[1], trackSectorOffset[2]);
}

std::string D64MStream::readBlock(uint8_t track, uint8_t sector)
{
    return "";
}

bool D64MStream::writeBlock(uint8_t track, uint8_t sector, std::string data)
{
    return true;
}

bool D64MStream::allocateBlock(uint8_t track, uint8_t sector)
{
    // int offset;
    // byte bitmask;

    // offset = (track-1) * 4 + 4;							// offset to correct Track-Info
    // if (bam->GetRawSector()[offset] > 0)
    // {
    // 	bam->GetRawSector()[offset] -= 1;				// reduce free Sectors
    // }
    // offset++;											// Move to Bitmask
    // offset += (sector >> 3);							// move to the correct byte (sector div 8)
    // bitmask = (byte)(1 << (sector % 8));				// generate Bitmask for Sector
    // if ((bam->GetRawSector()[offset] & bitmask) == 0)	// was already set to 'used' ?
    // {
    // 	return false;
    // }
    // bitmask ^= 255;										// invert bitmask (0 means "Sector is used")
    // bam->GetRawSector()[offset] &= bitmask;				// clear bit in BAM
    // WriteSector(bam);									// Write back to Image
    return true;
}

bool D64MStream::deallocateBlock(uint8_t track, uint8_t sector)
{
    // int offset;
    // byte bitmask;

    // offset = (track - 1) * 4 + 4;						// offset to correct Track-Info
    // bam->GetRawSector()[offset] += 1;					// increase free Sectors
    // offset++;											// Move to Bitmask
    // offset += (sector >> 3);							// move to the correct byte (sector div 8)
    // bitmask = (byte)(1 << (sector % 8));				// generate Bitmask for Sector
    // if ((bam->GetRawSector()[offset] & bitmask) == 1)	// Sector already free ?
    // {
    // 	return false;
    // }
    // bam->GetRawSector()[offset] |= bitmask;				// clear bit in BAM (1 means "Sector is free")
    // WriteSector(bam);									// Write back to Image
    return true;
}

bool D64MStream::getNextFreeBlock(uint8_t startTrack, uint8_t startSector, uint8_t *foundTrack, uint8_t *foundSector)
{
    uint8_t track, sector;
    bool found = false;
    bool wrapped = false;
    track = startTrack;

    sector = startSector;
    while (!found)
    {
        if (!isBlockFree(track, sector))
        {
            if (sector == startSector && track == startTrack)
            {
                sector++; // Skip StartSector
                if (wrapped)
                {
                    break; // already wrapped, exit (no free sector found)
                }
            }
            else
            {
                sector++;
            }

            if (sector > getSectorCount(track))
            {
                track++;
                sector = 0;
                if (track > getTrackCount())
                {
                    track = 1; // Start from the beginning
                    wrapped = true;
                }
            }
        }
        else
        {
            found = true;
        }
    }
    *foundTrack = track;
    *foundSector = sector;
    return found;
}

bool D64MStream::isBlockFree(uint8_t track, uint8_t sector)
{
    // int offset;
    // byte bitmask;

    // offset = (track-1) * 4 + 4;                 // offset to correct Track-Info
    // offset++;									// Move to Bitmask
    // offset += (sector >> 3);                    // move to the correct byte (sector div 8)
    // bitmask = (byte)(1 << (sector % 8));        // generate Bitmask for Sector
    // return (bam->GetRawSector()[offset] & bitmask) == bitmask;
    return true;
}

bool D64MStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        uint16_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename;
            uint8_t i = entryFilename.find_first_of(0xA0);
            entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("index[%d] track[%d] sector[%d] filename[%s] entry.filename[%.16s]", index, track, sector, filename.c_str(), entryFilename.c_str());

            //Debug_printv("filename[%s] entry[%s]", filename.c_str(), entryFilename.c_str());

            if (filename == entryFilename) // Match exact
            {
                return true;
            }
            else if (wildcard) // Wildcard Match
            {
                if (filename == "*") // Match first PRG
                {
                    if (entry.file_type & 0b00000111)
                    {
                        filename = entryFilename;
                        return true;
                    }
                }
                else if (mstr::compare(filename, entryFilename)) // X?XX?X* Wildcard match
                {
                    return true;
                }
            }

            index++;
        }

        Debug_printv("File not found!");
    }

    entry.next_track = 0;
    entry.next_sector = 0;
    entry.blocks = 0;
    entry.filename[0] = '\0';

    return false;
}

bool D64MStream::seekEntry( uint16_t index )
{
    // Calculate Sector offset & Entry offset
    // 8 Entries Per Sector, 32 bytes Per Entry
    index--;
    uint16_t sectorOffset = index / 8;
    uint16_t entryOffset = (index % 8) * 32;

    //Debug_printv("----------");
    //Debug_printv("index[%d] sectorOffset[%d] entryOffset[%d] entry_index[%d]", index, sectorOffset, entryOffset, entry_index);

    if (index == 0 || index != entry_index)
    {
        // Start at first sector of directory
        next_track = 0;
        if (!seekSector(
                partitions[partition].directory_track,
                partitions[partition].directory_sector,
                partitions[partition].directory_offset))
            return false;

        // Find sector with requested entry
        do
        {
            if (next_track)
            {
                //Debug_printv("next_track[%d] next_sector[%d]", entry.next_track, entry.next_sector);
                if (!seekSector(entry.next_track, entry.next_sector))
                    return false;
            }

            readContainer((uint8_t *)&entry, sizeof(entry));
            next_track = entry.next_track;
            next_sector = entry.next_sector;

            //Debug_printv("sectorOffset[%d] -> track[%d] sector[%d]", sectorOffset, track, sector);

        } while (sectorOffset-- > 0);
        if (!seekSector(track, sector, entryOffset))
            return false;
    }
    else
    {
        if (entryOffset == 0)
        {
            if (next_track == 0)
                return false;

            //Debug_printv("Follow link track[%d] sector[%d] entryOffset[%d]", next_track, next_sector, entryOffset);
            if (!seekSector(next_track, next_sector, entryOffset))
                return false;
        }
    }

    readContainer((uint8_t *)&entry, sizeof(entry));

    // If we are at the first entry in the sector then get next_track/next_sector
    if (entryOffset == 0)
    {
        next_track = entry.next_track;
        next_sector = entry.next_sector;
    }

    //std::string e = mstr::toHex((uint8_t *)&entry, sizeof(entry));
    //Debug_printv("file_type[%02X] file_name[%.16s] entry[%s]", entry.file_type, entry.filename, e.c_str());

    // if ( next_track == 0 && next_sector == 0xFF )
    entry_index = index + 1;

    return true;
}

bool D64MStream::readEntry( uint16_t index ) {
    return seekEntry(index);
}
bool D64MStream::writeEntry( uint16_t index) {
    if ( seekEntry(index - 1) ) {
        return writeContainer((uint8_t*)&entry, sizeof(entry));
    }
    return false;
}

uint16_t D64MStream::blocksFree()
{
    uint16_t free_count = 0;

    for (uint8_t x = 0; x < partitions[partition].block_allocation_map.size(); x++)
    {
        uint8_t bam[partitions[partition].block_allocation_map[x].byte_count];
        // Debug_printv("start_track[%d] end_track[%d]", block_allocation_map[x].start_track, block_allocation_map[x].end_track);

        if (!seekSector(
                partitions[partition].block_allocation_map[x].track,
                partitions[partition].block_allocation_map[x].sector,
                partitions[partition].block_allocation_map[x].offset))
            return 0;

        for (uint8_t i = partitions[partition].block_allocation_map[x].start_track; i <= partitions[partition].block_allocation_map[x].end_track; i++)
        {
            readContainer((uint8_t *)&bam, sizeof(bam));
            if (sizeof(bam) > 3)
            {
                if (i != partitions[partition].directory_track)
                {
                    // Debug_printv("x[%d] track[%d] count[%d] size[%d]", x, i, bam[0], sizeof(bam));
                    free_count += bam[0];
                }
            }
            else
            {
                // D71 tracks 36 - 70 you have to count the 1 bits (0 is allocated)
                uint8_t bit_count = 0;
                bit_count += std::bitset<8>(bam[0]).count();
                bit_count += std::bitset<8>(bam[1]).count();
                bit_count += std::bitset<8>(bam[2]).count();

                // Debug_printv("x[%d] track[%d] count[%d] size[%d] bam0[%d] bam1[%d] bam2[%d] (counting 1 bits)", x, i, bit_count, sizeof(bam), bam[0], bam[1], bam[2]);
                free_count += bit_count;
            }
        }
    }

    return free_count;
}

uint32_t D64MStream::readFile(uint8_t *buf, uint32_t size)
{

    if (sector_offset % block_size == 0)
    {
        // We are at the beginning of the block
        // Read track/sector link
        readContainer((uint8_t *)&next_track, 1);
        readContainer((uint8_t *)&next_sector, 1);
        sector_offset += 2;
        //Debug_printv("next_track[%d] next_sector[%d] sector_offset[%d]", next_track, next_sector, sector_offset);
    }

    uint32_t bytesRead = 0;

    if (size > 0)
    {
        if (size > available())
            size = available();
        
        // Only read up to the bytes remaining in this sector
        size = std::min(size, (uint32_t) (block_size - sector_offset % block_size));

        bytesRead += readContainer(buf, size);
        sector_offset += bytesRead;

        if (next_track && sector_offset % block_size == 0)
        {
            // We are at the end of the block
            // Follow track/sector link to move to next block
            if (!seekSector(next_track, next_sector))
            {
                return 0;
            }
            //Debug_printv("track[%d] sector[%d] sector_offset[%d]", track, sector, sector_offset);
        }
    }

    // if ( !bytesRead )
    // {
    //     sector_offset = 0;
    // }

    return bytesRead;
}

uint32_t D64MStream::writeFile(uint8_t *buf, uint32_t size)
{

    if (sector_offset % block_size == 0)
    {
        // We are at the beginning of the block
        // Read track/sector link
        readContainer((uint8_t *)&next_track, 1);
        readContainer((uint8_t *)&next_sector, 1);
        sector_offset += 2;
        //Debug_printv("next_track[%d] next_sector[%d] sector_offset[%d]", next_track, next_sector, sector_offset);
    }

    uint32_t bytesRead = 0;

    if (size > 0)
    {
        if (size > available())
            size = available();
        
        // Only read up to the bytes remaining in this sector
        size = std::min(size, (uint32_t) (block_size - sector_offset % block_size));

        bytesRead += readContainer(buf, size);
        sector_offset += bytesRead;

        if (next_track && sector_offset % block_size == 0)
        {
            // We are at the end of the block
            // Follow track/sector link to move to next block
            if (!seekSector(next_track, next_sector))
            {
                return 0;
            }
            //Debug_printv("track[%d] sector[%d] sector_offset[%d]", track, sector, sector_offset);
        }
    }

    // if ( !bytesRead )
    // {
    //     sector_offset = 0;
    // }

    return bytesRead;
}

bool D64MStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    next_track = 0;
    next_sector = 0;
    sector_offset = 0;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    // return D64Image.seekFile(containerIStream, path);
    if (mstr::endsWith(path, "#")) // Direct Access Mode
    {
        Debug_printv("Direct Access Mode track[1] sector[0] path[%s]", path.c_str());
        seekCalled = false;
        _size = block_size;
        return seekSector(1, 0);
    }
    else if (seekEntry(path))
    {
        // auto entry = containerImage->entry;
        //auto type = decodeType(entry.file_type).c_str();
        //Debug_printv("filename[%.16s] type[%s] start_track[%d] start_sector[%d]", entry.filename, type, entry.start_track, entry.start_sector);

        // Calculate file size
        uint8_t t = entry.start_track;
        uint8_t s = entry.start_sector;
        _size = seekFileSize(t, s);

        // Set position to beginning of file
        bool r = seekSector(t, s);

        //Debug_printv("blocks[%d] size[%d] available[%d] r[%d]", entry.blocks, _size, available(), r);

        return r;
    }
    else
    {
        Debug_printv("Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool D64MFile::isDirectory()
{
    // Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if (pathInStream == "")
        return true;
    else
        return false;
};

bool D64MFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("streamFile->url[%s]", streamFile->url.c_str());
    auto image = ImageBroker::obtain<D64MStream>(streamFile->url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Read Header
    image->readHeader();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.disk_name);
    mstr::A02Space(media_header);
    media_id = mstr::format("%.5s", image->header.id_dos);
    mstr::A02Space(media_id);
    media_blocks_free = image->blocksFree();
    media_block_size = image->block_size;
    media_image = name;
    // mstr::toUTF8(media_image);

    return true;
}

MFile *D64MFile::getNextFileInDir()
{
    bool r = false;

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<D64MStream>(streamFile->url);
    if (image == nullptr)
        goto exit;

    do
    {
        r = image->getNextImageEntry();
    } while (r && (image->entry.file_type & 0b00000111) == 0x00); // Skip hidden files

    if (r)
    {
        std::string filename = image->entry.filename;
        uint8_t i = filename.find_first_of(0xA0);
        filename = filename.substr(0, i);

        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        // Debug_printv( "entry[%s]", (streamFile->url + "/" + filename).c_str() );
        auto file = MFSOwner::File(streamFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);
        file->size = UINT16_FROM_LE_UINT16(image->entry.blocks);

        return file;
    }

exit:
    // Debug_printv( "END OF DIRECTORY");
    dirIsOpen = false;
    return nullptr;
}

time_t D64MFile::getLastWrite()
{
    return getCreationTime();
}

time_t D64MFile::getCreationTime()
{
    tm *entry_time = 0;
    auto stream = ImageBroker::obtain<D64MStream>(streamFile->url);
    if ( stream != nullptr )
    {
        auto entry = stream->entry;
        entry_time->tm_year = entry.year + 1900;
        entry_time->tm_mon = entry.month;
        entry_time->tm_mday = entry.day;
        entry_time->tm_hour = entry.hour;
        entry_time->tm_min = entry.minute;
    }

    return mktime(entry_time);
}

bool D64MFile::exists()
{
    // here I'd rather use D64 logic to see if such file name exists in the image!
    // Debug_printv("here");
    return true;
}

