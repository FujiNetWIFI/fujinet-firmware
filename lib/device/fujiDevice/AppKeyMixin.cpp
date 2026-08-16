#include "AppKeyMixin.h"
#include "fnFsSD.h"
#include "debug.h"
#include "utils.h"

#include <algorithm>

static char *_generate_appkey_filename(appkey *info)
{
    static char filenamebuf[30];

    snprintf(filenamebuf, sizeof(filenamebuf), "/FujiNet/%04hx%02hhx%02hhx.key",
             (uint16_t) info->creator, info->app, info->key);
    return filenamebuf;
}

/*
 Opens an "app key".  This just sets the needed app key parameters (creator, app, key, mode)
 for the subsequent expected read/write command. We could've added this information as part
 of the payload in a WRITE_APPKEY command, but there was no way to do this for READ_APPKEY.
 Requiring a separate OPEN command makes both the read and write commands behave similarly
 and leaves the possibity for a more robust/general file read/write function later.
*/
void AppKeyMixin::appkey_open(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    Debug_print("Fuji cmd: OPEN APPKEY\n");

    appkey key;

    // The data expected for this command
    if (!SYSTEM_BUS.transaction_get(&key, sizeof(key)))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // We're only supporting writing to SD, so return an error if there's no SD mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - returning error");
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Basic check for valid data
    if (key.creator == 0 || key.mode == APPKEYMODE_INVALID)
    {
        Debug_println("Invalid app key data");
        SYSTEM_BUS.transaction_error();
        return;
    }

    _current_appkey = key;

    Debug_printf("App key creator = 0x%04hx, app = 0x%02hhx, key = 0x%02hhx, mode = %hhu, "
                 "filename = \"%s\"\n",
                 (int) _current_appkey.creator, _current_appkey.app, _current_appkey.key,
                 _current_appkey.mode, _generate_appkey_filename(&_current_appkey));

    SYSTEM_BUS.transaction_success();
}

/*
  The app key close operation is a placeholder in case we want to provide more robust file
  read/write operations. Currently, the file is closed immediately after the read or write
  operation.
*/
void AppKeyMixin::appkey_close(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("Fuji cmd: CLOSE APPKEY\n");
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;
    SYSTEM_BUS.transaction_success();
}

ByteBuffer AppKeyMixin::appkey_read()
{
    char *filename;
    FILE *fIn;
    size_t count;
    ByteBuffer keydata(MAX_APPKEY_LEN, 0);

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't read app key");
        goto done;
    }

    // Make sure we have valid app key information, and the mode is not WRITE
    if (_current_appkey.creator == 0 || _current_appkey.mode == APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        goto done;
    }

    filename = _generate_appkey_filename(&_current_appkey);
    Debug_printf("Reading appkey from \"%s\"\n", filename);

    fIn = fnSDFAT.file_open(filename, FILE_READ);
    if (fIn == nullptr)
    {
        Debug_printf("Failed to open input file: errno=%d\n", errno);
        goto done;
    }

    count = fread(keydata.data(), 1, keydata.size(), fIn);
    keydata.resize(count);
    Debug_printf("Read %u bytes from input file\n", (unsigned)count);
    fclose(fIn);

#ifdef DEBUG
    {
        std::string msg = util_hexdump(keydata.data(), keydata.size());
        Debug_printf("\n%s\n", msg.c_str());
    }
#endif

 done:
    return keydata;
}

void AppKeyMixin::appkey_read(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_println("Fuji cmd: READ APPKEY");
    SYSTEM_BUS.transaction_send(appkey_read());
}

success_is_true AppKeyMixin::appkey_write(const ByteBuffer &keydata)
{
    Debug_printf("Fuji cmd: WRITE APPKEY (keylen = %hu)\n", keydata.size());

    // Store at most MAX_APPKEY_LEN bytes.
    size_t storelen = std::min<size_t>(keydata.size(), MAX_APPKEY_LEN);

    // Make sure we have valid app key information
    if (_current_appkey.creator == 0 || _current_appkey.mode != APPKEYMODE_WRITE)
    {
        Debug_println("Invalid app key metadata - aborting");
        RETURN_ERROR_AS_FALSE();
    }

    // Make sure we have an SD card mounted
    if (fnSDFAT.running() == false)
    {
        Debug_println("No SD mounted - can't write app key");
        RETURN_ERROR_AS_FALSE();
    }

    char *filename = _generate_appkey_filename(&_current_appkey);

    // Reset the app key data so we require calling APPKEY OPEN before another attempt
    _current_appkey.creator = 0;
    _current_appkey.mode = APPKEYMODE_INVALID;

    Debug_printf("Writing appkey to \"%s\"\n", filename);

    // Make sure we have a "/FujiNet" directory, since that's where we're putting these files
    fnSDFAT.create_path("/FujiNet");

    FILE *fOut = fnSDFAT.file_open(filename, FILE_WRITE);
    if (fOut == nullptr)
    {
        Debug_printf("Failed to open/create output file: errno=%d\n", errno);
        RETURN_ERROR_AS_FALSE();
    }
    size_t count = fwrite(keydata.data(), 1, storelen, fOut);
    fclose(fOut);

    if ((size_t)count != storelen)
    {
        Debug_printf("Only wrote %u bytes of expected %zu, errno=%d\n",
                     count, storelen, errno);
        RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}

void AppKeyMixin::appkey_write(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (!appkey_write(*packet.data()))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }
    SYSTEM_BUS.transaction_success();
}
