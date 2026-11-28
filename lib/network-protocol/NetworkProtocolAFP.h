#ifndef NETWORKPROTOCOLAFP_H
#define NETWORKPROTOCOLAFP_H

#include <string>
#include <vector>

#include "FS.h"

#include "afpFile.h"
#include "afpSession.h"

// AFP://host[:port]/Volume/path. The first path component is the volume;
// with none, a directory open lists the server's volumes.
class NetworkProtocolAFP : public NetworkProtocolFS
{
public:
    NetworkProtocolAFP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolAFP();

    fujiError_t rename(PeoplesUrlParser *url) override;
    fujiError_t del(PeoplesUrlParser *url) override;
    fujiError_t mkdir(PeoplesUrlParser *url) override;
    fujiError_t rmdir(PeoplesUrlParser *url) override;
    fujiError_t lock(PeoplesUrlParser *url) override { return FUJI_ERROR::NONE; }
    fujiError_t unlock(PeoplesUrlParser *url) override { return FUJI_ERROR::NONE; }

    off_t seek(off_t offset, int whence) override;

protected:
    fujiError_t open_file_handle() override;
    fujiError_t open_dir_handle() override;
    fujiError_t mount(PeoplesUrlParser *url) override;
    fujiError_t umount() override;
    void fserror_to_error() override;
    fujiError_t read_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t read_dir_entry(char *buf, unsigned short len) override;
    fujiError_t write_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t close_file_handle() override;
    fujiError_t close_dir_handle() override;
    fujiError_t stat() override;

private:
    AfpSession _session;
    AfpFile _file{_session};
    std::vector<AfpDirEntry> _entries;
    size_t _next_entry = 0;
    // 0, or the negative errno of the last lib/afp call that failed.
    int _afp_error = 0;

    // Records err and reports it; FUJI_ERROR::NONE only when err is 0.
    fujiError_t result(int err);
    // mount, one call on the mounted volume, umount.
    template <typename Call> fujiError_t one_shot(PeoplesUrlParser *url, Call call);
};

#endif /* NETWORKPROTOCOLAFP_H */
