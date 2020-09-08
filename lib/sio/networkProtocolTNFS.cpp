#include "networkProtocolTNFS.h"
#include "../../include/debug.h"
#include "utils.h"

networkProtocolTNFS::networkProtocolTNFS()
{
    memset(entryBuf, 0, sizeof(entryBuf));
}

networkProtocolTNFS::~networkProtocolTNFS()
{
}

bool networkProtocolTNFS::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame, enable_interrupt_t enable_interrupt)
{
    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    path = urlParser->path;
    directory = urlParser->path.substr(0, urlParser->path.find_last_of("/") - 1);
    filename = urlParser->path.substr(urlParser->path.find_last_of("/") + 1);

    if (filename == "*.*" || filename == "-" || filename == "**" || filename == "*")
        filename = "*";

    aux1 = cmdFrame->aux1;
    aux2 = cmdFrame->aux2;

    if (aux1 == 6 && filename.empty())
        filename = "*";

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (tnfs_mount(&mountInfo))
        return false; // error

    // This is a directory open request
    if (cmdFrame->aux1 == 6)
    {
        // Disable interrupts
        enable_interrupt(false);

        string dirPath = urlParser->path.substr(0, urlParser->path.find_last_of("/"));

        if (tnfs_opendirx(&mountInfo, dirPath.c_str()))
        {
            enable_interrupt(true);
            return false; // error
        }
    }
    // This is a file open request
    else
    {
        int mode = 1;
        int create_perms = 0;

        switch (cmdFrame->aux1)
        {
        case 4:
            mode = 1;
            break;
        case 8:
            mode = 0x103;
            create_perms = 0x1FF;
            break;
        case 9:
            mode = 0x10B;
            create_perms = 0x1FF;
            break;
        case 12:
            mode = 0x103;
            create_perms = 0x1FF;
            break;
        }

        if (aux1 == 4)
        {
            if (tnfs_stat(&mountInfo, &fileStat, urlParser->path.c_str()))
                return false; // error
        }

        if (tnfs_open(&mountInfo, urlParser->path.c_str(), mode, create_perms, &fileHandle))
            return false; // error
    }

    return true;
}

bool networkProtocolTNFS::close(enable_interrupt_t enable_interrupt)
{
    if (aux1 == 6)
    {
        tnfs_closedir(&mountInfo);
        enable_interrupt(true);
    }


    if (fileHandle != 0)
        tnfs_close(&mountInfo, fileHandle);

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return true;
}

bool networkProtocolTNFS::read(uint8_t *rx_buf, unsigned short len)
{

    if (aux1 == 6) // are we reading directory?
    {
        if (len == 0)
            return true;

        strcpy((char *)rx_buf, entryBuf);
        memset(entryBuf, 0, sizeof(entryBuf));
    }
    else
    {
        // Reading from a file
        if (block_read(rx_buf, len))
        {
            return true;
        }
        else
        {
            fileStat.filesize -= len;
        }
    }
    return false;
}

bool networkProtocolTNFS::write(uint8_t *tx_buf, unsigned short len)
{
    if (block_write(tx_buf, len))
        return true;

    return false;
}

bool networkProtocolTNFS::status(uint8_t *status_buf)
{
    status_buf[0] = status_buf[1] = 0;

    if (aux1 == 0x06)
    {
        status_buf[0] = status_dir();
        status_buf[1] = 0;
    }
    else
    {
        // File
        status_buf[0] = fileStat.filesize & 0xFF;
        status_buf[1] = fileStat.filesize >> 8;
    }

    status_buf[2] = 0;
    status_buf[3] = (fileStat.filesize == 0 ? 136 : 1);

    return false;
}

unsigned char networkProtocolTNFS::status_dir()
{
    char tmp[256];
    string path_fixed;
    string entry;
    size_t fix_pos;

    path_fixed = path;
    fix_pos = path_fixed.find("*");

    if (fix_pos != string::npos)
    {
        path_fixed = path_fixed.substr(0, fix_pos);
    }

    memset(tmp, 0, sizeof(tmp));

    if (entryBuf[0] == 0x00)
    {
        while (tnfs_readdirx(&mountInfo, &fileStat, tmp, 255) == 0)
        {
            Debug_printf("tnfs::status_dir got \"%s\"\n", tmp);
            if (util_wildcard_match(tmp, filename.c_str()))
            {
                Debug_printf("tnfs::status_dir path: %s - tmp: %s\n", path.c_str(), tmp);

                entry = "/" + path_fixed + tmp;

                if (aux2 & 0x80) // extended dir
                {
                    if (fileStat.isDir)
                    {
                        tmp[strlen(tmp)] = '/';
                        tmp[strlen(tmp)] = 0x00;
                    }

                    entry = tmp;
                    entry = util_long_entry(entry, fileStat.filesize);
                }
                else // 8.3 with sectors
                {
                    entry = util_entry(util_crunch(tmp), fileStat.filesize);

                    if (strcmp(tmp, ".") == 0)
                        entry.replace(2, 1, ".");
                    else if (strcmp(tmp, "..") == 0)
                        entry.replace(2, 2, "..");

                    if (fileStat.isDir)
                        entry.replace(10, 3, "DIR");
                }

                entry += "\x9b";
                Debug_printf("tnfs::status_dir entry: \"%s\"\n", entry.c_str());
                strcpy(entryBuf, entry.c_str());
                return (unsigned char)strlen(entryBuf);
            }
        }

        if (dirEOF == false)
        {
            dirEOF = true;
            strcpy(entryBuf, "999+FREE SECTORS\x9b");
        }
    }

    return (unsigned char)strlen(entryBuf);
}

bool networkProtocolTNFS::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolTNFS::special_supported_00_command(unsigned char comnd)
{
    return false;
}

bool networkProtocolTNFS::special_supported_40_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 0x26: // NOTE
        return true;
    }
    return false;
}

bool networkProtocolTNFS::special_supported_80_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 0x25: // POINT
        return true;
    }
    return false;
}

bool networkProtocolTNFS::block_read(uint8_t *rx_buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        int result = tnfs_read(&mountInfo, fileHandle, rx_buf, block_len, &actual_len);
        if (result != 0 && result != TNFS_RESULT_END_OF_FILE)
        {
            return true; // error.
        }
        else
        {
            rx_buf += block_len;
            total_len -= block_len;
        }
    }
    return false; // no error
}

bool networkProtocolTNFS::block_write(uint8_t *tx_buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        if (tnfs_write(&mountInfo, fileHandle, tx_buf, block_len, &actual_len) != 0)
        {
            return true; // error.
        }
        else
        {
            tx_buf += block_len;
            total_len -= block_len;
        }
    }
    return false; // no error
}

bool networkProtocolTNFS::rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    int ret = 0;

    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    directory = urlParser->path.substr(0, urlParser->path.find_last_of("/") + 1);
    filename = urlParser->path.substr(urlParser->path.find_last_of("/") + 1);
    comma_pos = filename.find_first_of(",");

    if (comma_pos == string::npos)
        return false;

    rnTo = directory + filename.substr(comma_pos + 1);
    filename = directory + filename.substr(0, comma_pos);

    if (tnfs_mount(&mountInfo))
        return false; // error

    ret = tnfs_rename(&mountInfo, filename.c_str(), rnTo.c_str());

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return ret;
}

bool networkProtocolTNFS::del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    int ret = 0;

    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (tnfs_mount(&mountInfo))
        return false; // error

    ret = tnfs_unlink(&mountInfo, urlParser->path.c_str());

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return ret;
}

bool networkProtocolTNFS::mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    int ret = 0;

    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (tnfs_mount(&mountInfo))
        return false; // error

    ret = tnfs_mkdir(&mountInfo, urlParser->path.c_str());

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return ret;
}

bool networkProtocolTNFS::rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    int ret = 0;

    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (tnfs_mount(&mountInfo))
        return false; // error

    ret = tnfs_rmdir(&mountInfo, urlParser->path.c_str());

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return ret;
}

bool networkProtocolTNFS::note(uint8_t *rx_buf)
{
    uint32_t pos;
    bool ret;

    ret = tnfs_lseek(&mountInfo, fileHandle, 0, SEEK_CUR, &pos);

    if (ret == 0x00)
    {
        pos &= 0xFFFFFF; // 24 bit value.

        memcpy(rx_buf, &pos, 3);
        return true;
    }

    return false;
}

bool networkProtocolTNFS::point(uint8_t *tx_buf)
{
    uint32_t pos;

    memcpy(&pos, tx_buf, 3);

    return tnfs_lseek(&mountInfo, fileHandle, pos, SEEK_SET, NULL);
}