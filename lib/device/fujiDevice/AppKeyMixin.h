#ifndef APPKEYMIXIN_H
#define APPKEYMIXIN_H

#include "FujiDeviceMixin.h"

#define MAX_APPKEY_LEN 64

enum appkey_mode : int8_t
{
    APPKEYMODE_INVALID = -1,
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_READ_256
};

struct appkey
{
    u16ne_t creator;
    uint8_t app;
    uint8_t key;
    appkey_mode mode;
    uint8_t reserved;
} __attribute__((packed));

class AppKeyMixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers;

    appkey _current_appkey;

protected:
    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void appkey_open(const FUJI_COMMAND_PACKET &packet);
    void appkey_close(const FUJI_COMMAND_PACKET &packet);

    virtual ByteBuffer appkey_read();
    void appkey_read(const FUJI_COMMAND_PACKET &packet);

    success_is_true appkey_write(const ByteBuffer &keydata);
    virtual void appkey_write(const FUJI_COMMAND_PACKET &packet);

 public:
    AppKeyMixin() {
        handlers = {
          { FUJICMD_OPEN_APPKEY,  FM_CMD_HANDLER(appkey_open)  },
          { FUJICMD_CLOSE_APPKEY, FM_CMD_HANDLER(appkey_close) },
          { FUJICMD_READ_APPKEY,  FM_CMD_HANDLER(appkey_read)  },
          { FUJICMD_WRITE_APPKEY, FM_CMD_HANDLER(appkey_write) },
        };
    }
};

#endif /* APPKEYMIXIN_H */
