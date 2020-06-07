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

bool networkProtocolTNFS::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    strcpy(mountInfo.hostname, urlParser->hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    directory = urlParser->path.substr(0, urlParser->path.find_last_of("/") - 1);
    filename = urlParser->path.substr(urlParser->path.find_last_of("/") + 1);

    if (filename == "*.*" || filename == "-" || filename == "**" || filename == "*")
        filename = "*";

    aux1 = cmdFrame->aux1;

    if (aux1 == 6 && filename.empty())
        filename = "*";

    if (!urlParser->port.empty())
        mountInfo.port = atoi(urlParser->port.c_str());

    if (tnfs_mount(&mountInfo))
        return false; // error

    if (cmdFrame->aux1 == 6)
    {
        // Directory open.
        string dirPath = urlParser->path.substr(0, urlParser->path.find_last_of("/"));

        if (tnfs_opendir(&mountInfo, dirPath.c_str()))
            return false; // error
    }
    else
    {
        // File. Open file handle.
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

bool networkProtocolTNFS::close()
{
    if (aux1 == 6)
        tnfs_closedir(&mountInfo);

    if (fileHandle != 0)
        tnfs_close(&mountInfo, fileHandle);

    if (mountInfo.session != 0)
        tnfs_umount(&mountInfo);

    return false;
}

bool networkProtocolTNFS::read(byte *rx_buf, unsigned short len)
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

bool networkProtocolTNFS::write(byte *tx_buf, unsigned short len)
{
    if (block_write(tx_buf, len))
        return true;

    return false;
}

bool networkProtocolTNFS::status(byte *status_buf)
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

    status_buf[2] = 1;
    status_buf[3] = 1;

    return false;
}

unsigned char networkProtocolTNFS::status_dir()
{
    char tmp[256];
    string entry;
    char tmp2[4];
    string sectorStr;
    int sectors;
    int res;

    memset(tmp, 0, sizeof(tmp));

    if (entryBuf[0] == 0x00)
    {
        res = tnfs_readdir(&mountInfo, tmp, 255);

        while (res == 0)
        {
            if (util_wildcard_match(tmp, (char *)filename.c_str(), strlen(tmp), filename.length()))
            {
                tmp[strlen(tmp)] = 0x00;
                entry = tmp;

                tnfs_stat(&mountInfo, &fileStat, tmp);

                if (aux2 == 128) // extended dir
                {
                    if (fileStat.isDir)
                    {
                        tmp[strlen(tmp)] = '/';
                        tmp[strlen(tmp)] = 0x00;
                    }

                    entry = util_long_entry(tmp, fileStat.filesize);
                }
                else // 8.3 with sectors
                {
                    entry = util_entry(util_crunch(tmp), fileStat.filesize);

                    if (strcmp(tmp, ".") == 0)
                        entry.replace(2, 1, ".");
                    else if (strcmp(tmp, "..") == 0)
                        entry.replace(2, 1, "..");

                    if (fileStat.isDir)
                        entry.replace(10, 3, "DIR");
                }
                
                entry += "\x9b";
                strcpy(entryBuf, entry.c_str());
                return (unsigned char)strlen(entryBuf);
            }
            else
                tnfs_readdir(&mountInfo, tmp, 255);
        }

        if (dirEOF == false)
        {
            dirEOF = true;
            strcpy(entryBuf, "000 FREE SECTORS\x9b");
        }
    }

    return (unsigned char)strlen(entryBuf);
}

bool networkProtocolTNFS::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolTNFS::special_supported_00_command(unsigned char comnd)
{
    return false;
}

bool networkProtocolTNFS::block_read(byte *rx_buf, unsigned short len)
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

        if (tnfs_read(&mountInfo, fileHandle, rx_buf, block_len, &actual_len) != 0)
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

bool networkProtocolTNFS::block_write(byte *tx_buf, unsigned short len)
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