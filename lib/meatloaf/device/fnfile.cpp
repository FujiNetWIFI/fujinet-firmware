#include "fnfile.h"

#include "peoples_url_parser.h"
#include "string_utils.h"

#include <cstdio>


/********************************************************
 * MFileSystem implementations
 ********************************************************/

std::unordered_map<std::string, FnFileMFileSystem::Attached> FnFileMFileSystem::_attached;

std::string FnFileMFileSystem::attach(const std::string &name, fnFile *handle,
                                      const std::string &path, uint32_t size)
{
    std::string url = "fnfile://" + name;
    if ( path.empty() || path[0] != '/' )
        url += "/";
    url += path;

    // Keep the path the way MFile will parse it back out of the URL
    PeoplesUrlParser parsed(url);
    _attached[name] = { handle, parsed.path, size };
    return url;
}

void FnFileMFileSystem::detach(const std::string &name)
{
    _attached.erase(name);
}

const FnFileMFileSystem::Attached *FnFileMFileSystem::find(const std::string &name)
{
    auto entry = _attached.find(name);
    return entry == _attached.end() ? nullptr : &entry->second;
}

fnFile *FnFileMFileSystem::handle(const std::string &name)
{
    const Attached *attached = find(name);
    return attached == nullptr ? nullptr : attached->handle;
}

bool FnFileMFileSystem::handles(std::string name)
{
    return mstr::equals(name, (char *)"fnfile:", false);
}

MFile* FnFileMFileSystem::getFile(std::string path)
{
    return new FnFileMFile(path);
}


/********************************************************
 * MFile implementations
 ********************************************************/

FnFileMFile::FnFileMFile(std::string path): MFile(path)
{
    // The attached file, and paths below it such as LOAD"*" on a mounted PRG, read the handle
    const FnFileMFileSystem::Attached *attached = FnFileMFileSystem::find(host);
    m_isNull = !( attached != nullptr &&
                  ( mstr::equals(this->path, attached->path.c_str(), false) ||
                    mstr::startsWith(this->path, (attached->path + "/").c_str(), false) ) );
    if (!m_isNull)
        size = attached->size;
}

MStream* FnFileMFile::getSourceStream(std::ios_base::openmode mode)
{
    if (m_isNull)
        return nullptr;

    MStream* stream = createStream(mode);
    stream->open(mode);
    return stream;
}

MStream* FnFileMFile::getDecodedStream(std::shared_ptr<MStream> src)
{
    return src.get();
}

MStream* FnFileMFile::createStream(std::ios_base::openmode mode)
{
    MStream* stream = new FnFileMStream(host);
    stream->url = url;
    return stream;
}


/********************************************************
 * MStream implementations
 ********************************************************/

bool FnFileMStream::open(std::ios_base::openmode mode)
{
    const FnFileMFileSystem::Attached *attached = FnFileMFileSystem::find(_name);
    if (attached == nullptr)
        return false;

    _size = attached->size;
    _position = 0;
    _open = true;
    return true;
}

uint32_t FnFileMStream::read(uint8_t* buf, uint32_t size)
{
    fnFile *f = FnFileMFileSystem::handle(_name);
    if (!_open || f == nullptr || buf == nullptr)
        return 0;

    if (size > available())
        size = available();
    if (size == 0 || fnio::fseek(f, _position, SEEK_SET) != 0)
        return 0;

    uint32_t bytesRead = fnio::fread(buf, 1, size, f);
    _position += bytesRead;
    return bytesRead;
}

uint32_t FnFileMStream::write(const uint8_t *buf, uint32_t size)
{
    fnFile *f = FnFileMFileSystem::handle(_name);
    if (!_open || f == nullptr || buf == nullptr || fnio::fseek(f, _position, SEEK_SET) != 0)
        return 0;

    uint32_t written = fnio::fwrite(buf, 1, size, f);
    _position += written;
    if (_position > _size)
        _size = _position;
    return written;
}

bool FnFileMStream::seek(uint32_t pos)
{
    if (!isOpen())
        return false;

    _position = pos;
    return true;
}
