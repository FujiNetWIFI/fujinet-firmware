#ifndef AFP_FILE_H
#define AFP_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "global_types.h"

#include "afpSession.h"

struct afp_file_info;

// Paths here are relative to the session's volume and start with '/'.

enum class AfpOpenMode
{
    Read,   // must exist
    Write,  // created if missing, truncated if not
    Append, // created if missing; starts at the end
    Update  // must exist; read and write in place
};

struct AfpDirEntry
{
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
};

// One open file on a session's volume, with its own position.  The session
// must outlive it.
class AfpFile
{
public:
    explicit AfpFile(AfpSession &session) : _session(session) {}
    ~AfpFile();

    AfpFile(const AfpFile &) = delete;
    AfpFile &operator=(const AfpFile &) = delete;

    success_is_true open(const std::string &path, AfpOpenMode mode);
    success_is_true close();

    // Bytes transferred (a read gives 0 at end of file), or a negative errno.
    int read(void *buf, size_t len);
    int write(const void *buf, size_t len);

    void seek(uint64_t offset) { _offset = offset; }
    uint64_t tell() const { return _offset; }
    uint64_t size() const { return _size; }
    bool is_open() const { return _file != nullptr; }

    // 0, or the negative errno of the last failed open or close.
    int error() const { return _error; }

private:
    AfpSession &_session;
    struct afp_file_info *_file = nullptr;
    std::string _path;
    uint64_t _offset = 0;
    uint64_t _size = 0;
    int _error = 0;
};

// Each returns 0 or a negative errno; -ENOTCONN when the session has no volume.
int afp_stat(AfpSession &session, const std::string &path, AfpDirEntry &out);
int afp_list(AfpSession &session, const std::string &path, std::vector<AfpDirEntry> &out);
int afp_remove(AfpSession &session, const std::string &path);
int afp_rename(AfpSession &session, const std::string &from, const std::string &to);
int afp_mkdir(AfpSession &session, const std::string &path);
int afp_rmdir(AfpSession &session, const std::string &path);

#endif /* AFP_FILE_H */
