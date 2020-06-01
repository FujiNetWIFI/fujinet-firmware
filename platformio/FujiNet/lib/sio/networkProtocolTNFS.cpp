#include "networkProtocolTNFS.h"
#include "../../include/debug.h"

// Function that matches input str with
// given wildcard pattern
bool strmatch(char str[], char pattern[],
              int n, int m)
{
    // empty pattern can only match with
    // empty string
    if (m == 0)
        return (n == 0);

    // lookup table for storing results of
    // subproblems
    bool lookup[n + 1][m + 1];

    // initailze lookup table to false
    memset(lookup, false, sizeof(lookup));

    // empty pattern can match with empty string
    lookup[0][0] = true;

    // Only '*' can match with empty string
    for (int j = 1; j <= m; j++)
        if (pattern[j - 1] == '*')
            lookup[0][j] = lookup[0][j - 1];

    // fill the table in bottom-up fashion
    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            // Two cases if we see a '*'
            // a) We ignore ‘*’ character and move
            //    to next  character in the pattern,
            //     i.e., ‘*’ indicates an empty sequence.
            // b) '*' character matches with ith
            //     character in input
            if (pattern[j - 1] == '*')
                lookup[i][j] = lookup[i][j - 1] ||
                               lookup[i - 1][j];

            // Current characters are considered as
            // matching in two cases
            // (a) current character of pattern is '?'
            // (b) characters actually match
            else if (pattern[j - 1] == '?' ||
                     str[i - 1] == pattern[j - 1])
                lookup[i][j] = lookup[i - 1][j - 1];

            // If characters don't match
            else
                lookup[i][j] = false;
        }
    }

    return lookup[n][m];
}

networkProtocolTNFS::networkProtocolTNFS()
{
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

    if (aux1==6 && filename.empty())
        filename="*";

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

        if (aux1==4)
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
    if (block_write(tx_buf,len))
        return true;

    return false;
}

bool networkProtocolTNFS::status(byte *status_buf)
{
    status_buf[0] = status_buf[1] = 0;
    char tmp[256];

    memset(tmp,0,sizeof(tmp));

    if (aux1 == 0x06)
    {
        if (entryBuf[0] == 0x00)
        {
        skip_entry:
            if (tnfs_readdir(&mountInfo, tmp, 255) != 0)
                return true;

            if (strmatch(tmp, (char *)filename.c_str(), strlen(tmp), filename.length()))
            {
                tmp[strlen(tmp)] = 0x9B;     // EOL
                tmp[strlen(tmp) + 1] = 0x00; // EOS
                strcpy(entryBuf, tmp);
            }
            else
                goto skip_entry;
        }
        status_buf[0] = strlen(entryBuf);
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
    unsigned short total_len=len;
    unsigned short block_len=TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len>0)
    {
        if (total_len>TNFS_MAX_READWRITE_PAYLOAD)
            block_len=TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len=total_len;

        if (tnfs_read(&mountInfo, fileHandle, rx_buf, block_len, &actual_len)!=0)
        {
            return true; // error.
        }
        else
        {
            rx_buf+=block_len;
            total_len-=block_len;
        }
    }
    return false; // no error
}

bool networkProtocolTNFS::block_write(byte *tx_buf, unsigned short len)
{
    unsigned short total_len=len;
    unsigned short block_len=TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len>0)
    {
        if (total_len>TNFS_MAX_READWRITE_PAYLOAD)
            block_len=TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len=total_len;
        
        if (tnfs_write(&mountInfo, fileHandle, tx_buf, block_len, &actual_len)!=0)
        {
            return true; // error.
        }
        else
        {
            tx_buf+=block_len;
            total_len-=block_len;
        }
    }
    return false; // no error
}