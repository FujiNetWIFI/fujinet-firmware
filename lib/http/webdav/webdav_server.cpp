// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#include "webdav_server.h"

#include <stdio.h>
#include <sstream>
#include <string.h>
#include <errno.h>
#include <cctype>
#include <iomanip>
#include <vector>
#include <memory>

#include <esp_http_server.h>

#include "meatloaf.h"
#include "device/flash.h"
#include "string_utils.h"

using namespace WebDav;

static std::string toLowerCopy(std::string value)
{
    for (char &ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

static std::string getLeafName(const std::string &path)
{
    if (path.empty())
        return "";

    size_t end = path.size();
    while (end > 1 && path[end - 1] == '/')
        end--;

    size_t slashPos = path.rfind('/', end - 1);
    if (slashPos == std::string::npos)
        return path.substr(0, end);

    return path.substr(slashPos + 1, end - slashPos - 1);
}

static bool isJunkLeafName(const std::string &leafName)
{
    if (leafName.empty())
        return false;

    std::string lowerLeaf = toLowerCopy(leafName);

    if (lowerLeaf.rfind("._", 0) == 0)
        return true;

    if (lowerLeaf == ".ds_store" ||
        lowerLeaf == "desktop.ini" ||
        lowerLeaf == "thumbs.db" ||
        lowerLeaf == "thumbs.ini" ||
        lowerLeaf == ".fseventsd" ||
        lowerLeaf == ".temporaryitems" ||
        lowerLeaf == ".trashes" ||
        lowerLeaf == ".volumeicon.icns")
        return true;

    if (lowerLeaf.rfind(".spotlight-v", 0) == 0)
        return true;

    return false;
}

static bool isFilteredJunkPath(const std::string &path)
{
    return isJunkLeafName(getLeafName(path));
}

static int mfile_rm_rf(const std::string &path)
{
    // Use FlashMFile for local paths so .gz files aren't routed to ArchiveMFileSystem,
    // which can't delete the underlying file.
    std::unique_ptr<MFile> mf;
    if (path.find("://") != std::string::npos)
        mf.reset(MFSOwner::File(path));
    else
        mf.reset(new FlashMFile(path));
    if (!mf || !mf->exists())
        return -1;

    if (mf->isDirectory()) {
        mf->rewindDirectory();
        MFile *entry;
        while ((entry = mf->getNextFileInDir()) != nullptr) {
            std::unique_ptr<MFile> e(entry);
            if (e->name == "." || e->name == "..")
                continue;
            mfile_rm_rf(e->path);
        }
        return mf->rmDir() ? 0 : -1;
    }
    return mf->remove() ? 0 : -1;
}

// Smart MFile factory for WebDAV operations.
// - Paths with "://" go directly to MFSOwner (already a resolved network URL).
// - For bare local paths, try MFSOwner first so .config base_url redirects are
//   applied (e.g. /mount/foo resolved to http://foo.com via .config).
//   If MFSOwner resolves to a network scheme, return that.
//   If it resolves to a local/media filesystem (empty scheme, e.g. D64MFile),
//   fall back to FlashMFile so .d64/.gz/etc. check real file existence via stat().
static std::unique_ptr<MFile> webdav_mfile(const std::string &path)
{
    if (path.find("://") != std::string::npos)
        return std::unique_ptr<MFile>(MFSOwner::File(path));

    auto mf = std::unique_ptr<MFile>(MFSOwner::File(path));
    if (mf && !mf->scheme.empty())
        return mf;

    return std::make_unique<FlashMFile>(path);
}

static int mfile_copy_recursive(const std::string &source, const std::string &destination,
                                int recurse, bool overwrite)
{
    auto srcFile = webdav_mfile(source);
    if (!srcFile || !srcFile->exists())
        return -ENOENT;

    auto dstFile = webdav_mfile(destination);
    bool dstExists = dstFile && dstFile->exists();

    if (dstExists && !overwrite)
        return -EEXIST;

    if (!srcFile->isDirectory()) {
        auto srcStream = srcFile->getSourceStream(std::ios_base::in);
        if (!srcStream || !srcStream->isOpen())
            return -ENOENT;

        auto dstStream = dstFile->getSourceStream(std::ios_base::out);
        if (!dstStream || !dstStream->isOpen()) {
            srcStream->close();
            return -EIO;
        }

        const int chunkSize = 8192;
        uint8_t *chunk = (uint8_t *)malloc(chunkSize);
        if (!chunk) {
            srcStream->close();
            dstStream->close();
            return -ENOSPC;
        }
        int ret = 0;
        for (;;) {
            uint32_t r = srcStream->read(chunk, chunkSize);
            if (r == 0)
                break;
            if (dstStream->write(chunk, r) != r) {
                ret = -ENOSPC;
                break;
            }
        }
        free(chunk);
        srcStream->close();
        dstStream->close();
        return ret;
    }

    if (!dstExists && !dstFile->mkDir())
        return -EIO;

    if (recurse <= 0)
        return 0;

    srcFile->rewindDirectory();
    MFile *entry;
    while ((entry = srcFile->getNextFileInDir()) != nullptr) {
        std::unique_ptr<MFile> e(entry);
        if (e->name == "." || e->name == "..")
            continue;
        int ret = mfile_copy_recursive(source + "/" + e->name,
                                       destination + "/" + e->name,
                                       recurse - 1, overwrite);
        if (ret != 0)
            return ret;
    }
    return 0;
}

static void discardRequestBody(Request &req)
{
    int remaining = req.getContentLength();
    if (remaining <= 0)
        return;

    char buffer[512];
    while (remaining > 0)
    {
        int r = req.readBody(buffer, std::min(remaining, static_cast<int>(sizeof(buffer))));
        if (r <= 0)
            break;

        remaining -= r;
    }
}

static bool normalizeAbsolutePath(const std::string &input, std::string &normalized)
{
    if (input.empty() || input[0] != '/')
        return false;

    std::vector<std::string> segments;
    std::string token;

    auto flushToken = [&]() -> bool {
        if (token.empty() || token == ".")
        {
            token.clear();
            return true;
        }

        if (token == "..")
        {
            if (segments.empty())
                return false;

            segments.pop_back();
            token.clear();
            return true;
        }

        segments.push_back(token);
        token.clear();
        return true;
    };

    for (char ch : input)
    {
        if (ch == '\\')
            ch = '/';

        if (ch == '/')
        {
            if (!flushToken())
                return false;
            continue;
        }

        token.push_back(ch);
    }

    if (!flushToken())
        return false;

    normalized = "/";
    for (size_t i = 0; i < segments.size(); i++)
    {
        if (i > 0)
            normalized += '/';
        normalized += segments[i];
    }

    return true;
}

static void splitPathSegments(const std::string &path, std::vector<std::string> &segments)
{
    std::string token;

    for (char ch : path)
    {
        if (ch == '/')
        {
            if (!token.empty())
            {
                segments.push_back(token);
                token.clear();
            }
            continue;
        }

        token.push_back(ch);
    }

    if (!token.empty())
        segments.push_back(token);
}

static bool resolveUnderRoot(const std::string &normalizedRoot, const std::string &relativePath, std::string &resolved)
{
    if (normalizedRoot.empty() || normalizedRoot[0] != '/')
        return false;

    if (relativePath.empty() || relativePath[0] != '/')
        return false;

    std::vector<std::string> segments;
    splitPathSegments(normalizedRoot, segments);
    const size_t rootDepth = segments.size();

    std::string token;
    for (char ch : relativePath)
    {
        if (ch == '\\')
            ch = '/';

        if (ch == '/')
        {
            if (token.empty() || token == ".")
            {
                token.clear();
                continue;
            }

            if (token == "..")
            {
                if (segments.size() <= rootDepth)
                    return false;
                segments.pop_back();
                token.clear();
                continue;
            }

            segments.push_back(token);
            token.clear();
            continue;
        }

        token.push_back(ch);
    }

    if (!token.empty() && token != ".")
    {
        if (token == "..")
        {
            if (segments.size() <= rootDepth)
                return false;
            segments.pop_back();
        }
        else
        {
            segments.push_back(token);
        }
    }

    resolved = "/";
    for (size_t i = 0; i < segments.size(); i++)
    {
        if (i > 0)
            resolved += '/';
        resolved += segments[i];
    }

    return true;
}

static bool isInsideRoot(const std::string &path, const std::string &root)
{
    if (root == "/")
        return !path.empty() && path[0] == '/';

    if (path.size() < root.size())
        return false;

    if (path.compare(0, root.size(), root) != 0)
        return false;

    return path.size() == root.size() || path[root.size()] == '/';
}

Server::Server(std::string rootURI, std::string rootPath) : rootURI(rootURI), rootPath(rootPath)  {}

std::string Server::uriToPath(std::string uri)
{
    std::string normalizedRootPath;
    if (!normalizeAbsolutePath(rootPath, normalizedRootPath))
        return "";

    std::string normalizedRootUri;
    if (!normalizeAbsolutePath(rootURI, normalizedRootUri))
        return "";

    std::string decodedUri = mstr::urlDecode(uri);
    std::string normalizedUri;
    if (!normalizeAbsolutePath(decodedUri, normalizedUri))
        return "";

    bool uriMatchesRoot = false;
    if (normalizedRootUri == "/")
        uriMatchesRoot = true;
    else if (normalizedUri.compare(0, normalizedRootUri.size(), normalizedRootUri) == 0)
        uriMatchesRoot = (normalizedUri.size() == normalizedRootUri.size()) ||
                         (normalizedUri[normalizedRootUri.size()] == '/');

    if (!uriMatchesRoot)
        return "";

    std::string relativePath = normalizedUri.substr(normalizedRootUri.size());
    if (relativePath.empty())
        relativePath = "/";
    else if (relativePath[0] != '/')
        relativePath = "/" + relativePath;

    std::string resolvedPath;
    if (!resolveUnderRoot(normalizedRootPath, relativePath, resolvedPath))
        return "";

    return resolvedPath;
}

std::string Server::pathToURI(std::string path)
{
    std::string normalizedRootPath;
    std::string normalizedRootUri;
    std::string normalizedPath;

    if (!normalizeAbsolutePath(rootPath, normalizedRootPath) ||
        !normalizeAbsolutePath(rootURI, normalizedRootUri) ||
        !normalizeAbsolutePath(path, normalizedPath))
        return "";

    if (!isInsideRoot(normalizedPath, normalizedRootPath))
        return "";

    if (normalizedRootUri == normalizedRootPath)
        return mstr::urlEncode(normalizedPath);

    std::string suffix = normalizedPath.substr(normalizedRootPath.size());
    if (suffix.empty())
        return mstr::urlEncode(normalizedRootUri);

    if (normalizedRootUri == "/")
        return mstr::urlEncode(suffix);

    return mstr::urlEncode(normalizedRootUri + suffix);
}

std::string Server::formatTime(time_t t)
{
    char buf[32];
    struct tm *lt = gmtime(&t);
    // <D:getlastmodified>Tue, 22 Aug 2023 02:37:31 GMT</D:getlastmodified>
    // strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", lt);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", lt);

    return std::string(buf);
}

static void xmlElement(std::ostringstream &s, const char *name, const char *value)
{
    s << "<" << name << ">" << value << "</" << name << ">\r\n";
}

void Server::sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr)
{
    std::ostringstream s;

    s << "<D:response>\r\n";
    xmlElement(s, "D:href", msr.href.c_str());
    s << "<D:propstat>\r\n";

    s << "<D:prop>\r\n";
    for (const auto &p : msr.props)
        xmlElement(s, p.first.c_str(), p.second.c_str());

    xmlElement(s, "D:resourcetype", msr.isCollection ? "<D:collection/>" : "");
    s << "</D:prop>\r\n";

    xmlElement(s, "D:status", msr.status.c_str());
    s << "</D:propstat>\r\n";
    s << "</D:response>\r\n";

    //Debug_printv("[%s]", s.str().c_str());

    resp.sendChunk(s.str().c_str());
}

int Server::sendPropResponse(Response &resp, std::string path, int recurse, MFile* hint)
{
    mstr::replaceAll(path, "//", "/");
    std::string uri = pathToURI(path);

    // Use hint MFile when provided (e.g. from a directory listing that already has
    // is_dir and size set), avoiding an extra network round-trip per entry.
    std::unique_ptr<MFile> owned;
    MFile* mfile;
    if (hint) {
        mfile = hint;
    } else {
        owned.reset(MFSOwner::File(path));
        mfile = owned.get();
    }
    bool exists = mfile && mfile->exists();

    MultiStatusResponse r;
    r.href = uri;

    if (exists)
    {
        r.status = "HTTP/1.1 200 OK";

        r.props["D:creationdate"] = formatTime(mfile->getCreationTime());
        r.props["D:getlastmodified"] = formatTime(mfile->getLastWrite());

        std::string s = path + std::to_string(mfile->getLastWrite());
        r.props["D:getetag"] = mstr::sha1(s);

        r.isCollection = mfile->isDirectory();
        if (!r.isCollection)
        {
            r.props["D:getcontentlength"] = std::to_string(mfile->size);
            r.props["D:getcontenttype"] = HTTPD_TYPE_OCTET;
        }
    }
    else
    {
        r.status = "HTTP/1.1 404 Not Found";
    }

    sendMultiStatusResponse(resp, r);

    if (r.isCollection && recurse > 0)
    {
        if (path == "/")
        {
            auto sdFile = std::unique_ptr<MFile>(MFSOwner::File("/sd"));
            if (sdFile && sdFile->exists())
                sendPropResponse(resp, "/sd", recurse - 1);
        }

        mfile->rewindDirectory();
        MFile *de;
        while ((de = mfile->getNextFileInDir()) != nullptr)
        {
            std::unique_ptr<MFile> entry(de);
            if (entry->name == "." || entry->name == "..")
                continue;
            if (isJunkLeafName(entry->name))
                continue;
            // Use path + name so local WebDAV paths are preserved even when
            // this directory's MFile was redirected to a remote base_url.
            // Pass the entry as a hint: it already has is_dir and size set from
            // the directory listing, so sendPropResponse skips the MFSOwner::File()
            // lookup and the extra network round-trips for exists()/isDirectory().
            sendPropResponse(resp, path + "/" + entry->name, recurse - 1, entry.get());
        }
    }

    return 0;
}

// http entry points
int Server::doCopy(Request &req, Response &resp)
{
    if (req.getDestination().empty())
        return 400;

    std::string source = uriToPath(req.getPath());
    std::string destination = uriToPath(req.getDestination());

    if (source.empty() || destination.empty())
        return 403;

    if (isFilteredJunkPath(source) || isFilteredJunkPath(destination))
    {
        auto dstFile = webdav_mfile(destination);
        bool destinationExists = dstFile && dstFile->exists();
        return destinationExists ? 204 : 201;
    }

    Debug_printv("req[%s] source[%s]", req.getPath().c_str(), source.c_str());

    if (source == destination)
        return 403;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    auto dstCheck = webdav_mfile(destination);
    bool destinationExists = dstCheck && dstCheck->exists();

    int ret = mfile_copy_recursive(source, destination, recurse, req.getOverwrite());

    switch (ret)
    {
    case 0:
        if (destinationExists)
            return 204;

        return 201;

    case -ENOENT:
        return 409;

    case -ENOSPC:
        return 507;

    case -ENOTDIR:
    case -EISDIR:
    case -EEXIST:
        return 412;

    default:
        return 500;
    }

    return 0;
}

int Server::doDelete(Request &req, Response &resp)
{
    if (req.getDepth() != Request::DEPTH_INFINITY)
        return 400;

    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
        return 204;

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    if (mfile_rm_rf(path) < 0)
        return 404;

    return 200;
}

int Server::doGet(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
        return 404;

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    auto mfile = webdav_mfile(path);
    if (!mfile || !mfile->exists())
        return 404;

    if (mfile->isDirectory())
        return 405;

    auto stream = mfile->getSourceStream(std::ios_base::in);
    if (!stream || !stream->isOpen())
        return 404;

    std::string s = path + std::to_string(mfile->getLastWrite());

    resp.setStatus(200);
    resp.setHeader("ETag", mstr::sha1(s));
    resp.setHeader("Last-Modified", formatTime(mfile->getLastWrite()));
    resp.flushHeaders();

    int ret = 0;
    // 16 KB: halves the chunked-send round trips vs 8 KB. malloc lands in
    // PSRAM (above the SPIRAM_MALLOC_ALWAYSINTERNAL threshold).
    const int chunkSize = 16384;
    uint8_t *chunk = (uint8_t *)malloc(chunkSize);
    if (!chunk) {
        stream->close();
        return 500;
    }

    for (;;)
    {
        uint32_t r = stream->read(chunk, chunkSize);
        if (r == 0)
            break;

        if (!resp.sendChunk((char *)chunk, r))
        {
            ret = -1;
            break;
        }
    }

    free(chunk);
    stream->close();
    resp.closeChunk();

    return ret != 0 ? 500 : 200;
}

int Server::doHead(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
        return 404;

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    std::unique_ptr<MFile> mfile;
    if (path.find("://") != std::string::npos)
        mfile.reset(MFSOwner::File(path));
    else
        mfile.reset(new FlashMFile(path));
    if (!mfile || !mfile->exists())
        return 404;

    std::string s = path + std::to_string(mfile->getLastWrite());

    resp.setHeader("ETag", mstr::sha1(s));
    resp.setHeader("Last-Modified", formatTime(mfile->getLastWrite()));

    return 200;
}

int Server::doLock(Request &req, Response &resp)
{
    Debug_printv("req[%s]", req.getPath().c_str());

    static const char *lockToken = "opaquelocktoken:26e57cb3-834d-191a-00de-000042bdecf9";

    resp.setStatus(200);
    resp.setContentType("application/xml;charset=utf-8");
    resp.setHeader("Lock-Token", std::string("<") + lockToken + ">");
    resp.flushHeaders();

    resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");

    std::ostringstream s;

    s << "<D:prop xmlns:D=\"DAV:\">\r\n";
    s << "<D:lockdiscovery>\r\n";
    s << "<D:activelock>\r\n";
    s << "<D:locktype><D:write/></D:locktype>\r\n";
    s << "<D:lockscope><D:exclusive/></D:lockscope>\r\n";
    s << "<D:depth>0</D:depth>\r\n";
    s << "<D:owner><a:href xmlns:a=\"DAV:\">http://meatloaf.cc</a:href></D:owner>\r\n";
    s << "<D:timeout>Second-600</D:timeout>\r\n";
    s << "<D:locktoken>\r\n";
    s << "<D:href>" << lockToken << "</D:href>\r\n";
    s << "</D:locktoken>\r\n";
    s << "</D:activelock>\r\n";
    s << "</D:lockdiscovery>\r\n";
    s << "</D:prop>";

    //Debug_printv("[%s]", s.str().c_str());

    resp.sendChunk(s.str().c_str());
    resp.closeChunk();

    return 200;
}

int Server::doMkcol(Request &req, Response &resp)
{
    if (req.getContentLength() != 0)
        return 415;

    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
        return 201;

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    auto mfile = webdav_mfile(path);
    if (mfile && mfile->exists())
        return 405;

    size_t lastSlash = path.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        auto parentFile = webdav_mfile(path.substr(0, lastSlash));
        if (!parentFile || !parentFile->exists())
            return 409;
    }

    if (!mfile || !mfile->mkDir())
        return 500;

    return 201;
}

int Server::doMove(Request &req, Response &resp)
{
    if (req.getDestination().empty())
        return 400;

    std::string source = uriToPath(req.getPath());
    if (source.empty())
        return 403;

    Debug_printv("req[%s] source[%s]", req.getPath().c_str(), source.c_str());

    {
        auto srcFile = webdav_mfile(source);
        if (!srcFile || !srcFile->exists())
            return 404;
    }

    std::string destination = uriToPath(req.getDestination());
    if (destination.empty())
        return 403;

    if (isFilteredJunkPath(source) || isFilteredJunkPath(destination))
    {
        auto dstFile = webdav_mfile(destination);
        bool destinationExists = dstFile && dstFile->exists();
        return destinationExists ? 204 : 201;
    }

    auto dstFile = webdav_mfile(destination);
    bool destinationExists = dstFile && dstFile->exists();

    if (destinationExists)
    {
        if (!req.getOverwrite())
            return 412;

        mfile_rm_rf(destination);
    }

    int ret = ::rename(source.c_str(), destination.c_str());

    if (ret == 0)
    {
        if (destinationExists)
            return 204;

        return 201;
    }

    int err = errno;

    switch (err)
    {
    case ENOENT:
        return 409;

    case ENOSPC:
        return 507;

    case ENOTDIR:
    case EISDIR:
    case EEXIST:
        return 412;

    default:
        return 500;
    }
}

int Server::doOptions(Request &req, Response &resp)
{
    Debug_printv("req[%s]", req.getPath().c_str());
    return 200;
}

int Server::doPropfind(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
        return 404;

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    auto mfile = webdav_mfile(path);
    if (!mfile || !mfile->exists())
        return 404;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    Debug_printv("depth[%d] recurse[%d]", req.getDepth(), recurse);

    resp.setStatus(207);
    resp.setContentType("application/xml;charset=utf-8");
    resp.flushHeaders();

    resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
    resp.sendChunk("<D:multistatus xmlns:D=\"DAV:\">\r\n");
    // Pass mfile as hint so sendPropResponse doesn't create a second MFile
    // for the same path, avoiding a duplicate exists()/isDirectory() round-trip.
    sendPropResponse(resp, path, recurse, mfile.get());
    resp.sendChunk("</D:multistatus>\r\n");
    resp.closeChunk();

    return 207;
}

int Server::doProppatch(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    if (isFilteredJunkPath(path))
    {
        discardRequestBody(req);
        return 404;
    }

    {
        auto mfile = webdav_mfile(path);
        if (!mfile || !mfile->exists())
        {
            discardRequestBody(req);
            return 404;
        }
    }

    discardRequestBody(req);

    std::string href = pathToURI(path);
    if (href.empty())
        return 403;

    resp.setStatus(207);
    resp.setContentType("application/xml;charset=utf-8");
    resp.flushHeaders();

    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    s << "<D:multistatus xmlns:D=\"DAV:\">\r\n";
    s << "<D:response>\r\n";
    xmlElement(s, "D:href", href.c_str());
    s << "<D:propstat>\r\n";
    s << "<D:prop/>\r\n";
    xmlElement(s, "D:status", "HTTP/1.1 200 OK");
    s << "</D:propstat>\r\n";
    s << "</D:response>\r\n";
    s << "</D:multistatus>\r\n";

    resp.sendChunk(s.str().c_str());
    resp.closeChunk();

    return 207;
}

int Server::doPut(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    if (path.empty())
        return 403;

    // Use FlashMFile for local paths so .gz files aren't routed to ArchiveMFileSystem
    // (ArchiveMFile::exists() always returns true, and it can't open a write stream).
    std::unique_ptr<MFile> mfile;
    if (path.find("://") != std::string::npos)
        mfile.reset(MFSOwner::File(path));
    else
        mfile.reset(new FlashMFile(path));

    if (isFilteredJunkPath(path))
    {
        bool exists = mfile && mfile->exists();
        discardRequestBody(req);
        return exists ? 200 : 201;
    }

    Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    bool exists = mfile && mfile->exists();

    auto stream = mfile ? mfile->getSourceStream(std::ios_base::out) : nullptr;
    if (!stream || !stream->isOpen())
        return 404;

    int remaining = req.getContentLength();

    // 16 KB: fewer read/write iterations per PUT; buffer lives in PSRAM.
    const int chunkSize = 16384;
    uint8_t *chunk = (uint8_t *)malloc(chunkSize);
    if (!chunk) {
        stream->close();
        return 500;
    }

    int ret = 0;

    while (remaining > 0)
    {
        int r = req.readBody((char *)chunk, std::min(remaining, chunkSize));
        if (r <= 0)
            break;

        if ((int)stream->write(chunk, r) != r)
        {
            ret = -1;
            break;
        }

        remaining -= r;
    }

    free(chunk);
    stream->close();

    if (ret < 0)
        return 500;

    return exists ? 200 : 201;
}

int Server::doUnlock(Request &req, Response &resp)
{
    Debug_printv("req[%s]", req.getPath().c_str());
    resp.setHeader("Lock-Token", "<opaquelocktoken:26e57cb3-834d-191a-00de-000042bdecf9>");
    return 204;
}
