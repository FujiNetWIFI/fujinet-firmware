
#include "webdav_server.h"

#include <stdio.h>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <cctype>
#include <iomanip>

#include <esp_http_server.h>

#include "file-utils.h"
#include "string_utils.h"

using namespace WebDav;

Server::Server(std::string rootURI, std::string rootPath) : rootURI(rootURI), rootPath(rootPath)  {}

std::string Server::uriToPath(std::string uri)
{
    if ( rootURI == rootPath )
    return mstr::urlDecode(uri);

    if (uri.find(rootURI) != 0)
        return rootPath;

    std::string path = rootPath + uri.substr(rootURI.length());
    mstr::replaceAll(path, "//", "/");
    //Debug_printv("uri[%s] path[%s]", uri.c_str(), path.c_str());
    if ( path.length() > 1 )
    {
        while (path.substr(path.length() - 1, 1) == "/")
            path = path.substr(0, path.length() - 1);
    }
    //Debug_printv("uri[%s] path[%s]", uri.c_str(), path.c_str());
    return mstr::urlDecode(path);
}

std::string Server::pathToURI(std::string path)
{
    if ( rootURI == rootPath )
        return mstr::urlEncode(path);;

    if (path.find(rootPath) != 0)
        return "";

    const char *sep = path[rootPath.length()] == '/' ? "" : "/";
    std::string uri = rootURI + sep + path.substr(rootPath.length());

    return mstr::urlEncode(uri);
}

std::string Server::formatTime(time_t t)
{
    char buf[32];
    struct tm *lt = localtime(&t);
    // <D:getlastmodified>Tue, 22 Aug 2023 02:37:31 GMT</D:getlastmodified>
    // strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", lt);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", lt);

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
    xmlElement(s, "D:status", msr.status.c_str());

    s << "<D:prop>\r\n";
    for (const auto &p : msr.props)
        xmlElement(s, p.first.c_str(), p.second.c_str());

    xmlElement(s, "D:resourcetype", msr.isCollection ? "<D:collection/>" : "");
    s << "</D:prop>\r\n";

    s << "</D:propstat>\r\n";
    s << "</D:response>\r\n";

    //Debug_printv("[%s]", s.str().c_str());

    resp.sendChunk(s.str().c_str());
}

int Server::sendPropResponse(Response &resp, std::string path, int recurse)
{
    mstr::replaceAll(path, "//", "/");
    std::string uri = pathToURI(path);
    //Debug_printv("uri[%s] path[%s] recurse[%d]", uri.c_str(), path.c_str(), recurse);

    // bool exists = (path == rootPath) ||
    //               (access(path.c_str(), R_OK) == 0);
    struct stat sb;
    int i = stat(path.c_str(), &sb);
    bool exists (i == 0);

    MultiStatusResponse r;

    r.href = uri;

    if ( exists )
    {
        r.status = "HTTP/1.1 200 OK";

        r.props["D:creationdate"] = formatTime(sb.st_ctime);
        r.props["D:getlastmodified"] = formatTime(sb.st_mtime);
        //r.props["D:displayname"] = mstr::urlEncode(basename(path.c_str()));

        std::string s = path + std::to_string(sb.st_mtime);
        r.props["D:getetag"] = mstr::sha1(s);

        r.isCollection = ((sb.st_mode & S_IFMT) == S_IFDIR);
        if ( !r.isCollection )
        {
            r.props["D:getcontentlength"] = std::to_string(sb.st_size);
            r.props["D:getcontenttype"] = HTTPD_TYPE_OCTET;
        }
        //Debug_printv("Found!");
    }
    else
    {
        r.status = "HTTP/1.1 404 Not Found";
        //Debug_printv("Not Found!");
    }

    sendMultiStatusResponse(resp, r);

    if (r.isCollection && recurse > 0)
    {
        DIR *dir = opendir(path.c_str());
        if (dir)
        {
            struct dirent *de;

            while ((de = readdir(dir)))
            {
                if (strcmp(de->d_name, ".") == 0 ||
                    strcmp(de->d_name, "..") == 0)
                    continue;

                std::string rpath = path + "/" + de->d_name;
                sendPropResponse(resp, rpath, recurse - 1);
            }
            closedir(dir);
        }

        // If we are at root and SD card is mounted send entry
        if (path == "/")
        {
            i = stat("/sd", &sb);
            if (i == 0)
                sendPropResponse(resp, "/sd", recurse - 1);
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

    Debug_printv("req[%s] source[%s]", req.getPath().c_str(), source.c_str());

    if (source == destination)
        return 403;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    bool destinationExists = access(destination.c_str(), F_OK) == 0;

    int ret = copy_recursive(source, destination, recurse, req.getOverwrite());

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

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    int ret = rm_rf(path.c_str());
    if (ret < 0)
        return 404;

    return 200;
}

int Server::doGet(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    struct stat sb;
    int ret = stat(path.c_str(), &sb);
    if (ret < 0)
        return 404;

    // Method Not Allowed
    if ((sb.st_mode & S_IFMT) == S_IFDIR)
        return 405;

    // Send File
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return 404;

    std::string s = path + std::to_string(sb.st_mtime);

    resp.setHeader("Content-Length", sb.st_size);
    resp.setHeader("ETag", mstr::sha1(s));
    resp.setHeader("Last-Modified", formatTime(sb.st_mtime));

    ret = 0;

    const int chunkSize = 8192;
    char *chunk = (char *)malloc(chunkSize);

    for (;;)
    {
        size_t r = fread(chunk, 1, chunkSize, f);
        if (r <= 0)
            break;

        if (!resp.sendChunk(chunk, r))
        {
            ret = -1;
            break;
        }
    }

    free(chunk);
    fclose(f);
    resp.closeChunk();

    if (ret != 0)
        return 500;

    return 200;
}

int Server::doHead(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    struct stat sb;
    int ret = stat(path.c_str(), &sb);
    if (ret < 0)
        return 404;

    std::string s = path + std::to_string(sb.st_mtime);

    resp.setHeader("Content-Length", sb.st_size);
    resp.setHeader("ETag", mstr::sha1(s));
    resp.setHeader("Last-Modified", formatTime(sb.st_mtime));

    return 200;
}

int Server::doLock(Request &req, Response &resp)
{
    Debug_printv("req[%s]", req.getPath().c_str());

    resp.setStatus(200);
    resp.setContentType("application/xml;charset=utf-8");
    resp.setHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
    resp.flushHeaders();

    resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");

    std::ostringstream s;

    s << "<D:prop xmls:D=\"DAV:\">\r\n";
    s << "<D:lockdiscovery>\r\n";
    s << "<D:activelock>\r\n";
    s << "<D:locktype><D:write/></D:locktype>\r\n";
    s << "<D:lockscope><D:exclusive/></D:lockscope>\r\n";
    s << "<D:depth>0</D:depth>\r\n";
    s << "<D:owner><a:href xmlns:a=\"DAV:\">http://meatloaf.cc</a:href></D:owner>\r\n";
    s << "<D:timeout>Second-600</D:timeout>\r\n";
    s << "<D:locktoken>\r\n";
    s << "<D:href>opaquelocktoken:89f0e324-e19d-4efd-bafe-e383829cf5a8</D:href>\r\n";
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

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    int ret = mkdir(path.c_str(), 0755);
    if (ret == 0)
        return 201;

    switch (errno)
    {
    case EEXIST:
        return 405;

    case ENOENT:
        return 409;

    default:
        return 500;
    }
}

int Server::doMove(Request &req, Response &resp)
{
    if (req.getDestination().empty())
        return 400;

    std::string source = uriToPath(req.getPath());

    //Debug_printv("req[%s] source[%s]", req.getPath().c_str(), source.c_str());

    struct stat sourceStat;
    int ret = stat(source.c_str(), &sourceStat);
    if (ret < 0)
        return 404;

    std::string destination = uriToPath(req.getDestination());
    bool destinationExists = access(destination.c_str(), F_OK) == 0;

    if (destinationExists)
    {
        if (!req.getOverwrite())
            return 412;

        rm_rf(destination.c_str());
    }

    ret = rename(source.c_str(), destination.c_str());

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
}

int Server::doOptions(Request &req, Response &resp)
{
    //Debug_printv("req[%s]", req.getPath().c_str());
    return 200;
}

int Server::doPropfind(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    // bool exists = (path == rootPath) ||
    //               (access(path.c_str(), R_OK) == 0);
    struct stat sb;
    int i = stat(path.c_str(), &sb);
    bool exists (i == 0);

    if (!exists)
        return 404;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    resp.setStatus(207);
    resp.setContentType("application/xml;charset=utf-8");
    resp.flushHeaders();

    resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");
    resp.sendChunk("<D:multistatus xmlns:D=\"DAV:\">\r\n");
    sendPropResponse(resp, path, recurse);
    resp.sendChunk("</D:multistatus>\r\n");
    resp.closeChunk();

    return 207;
}

int Server::doProppatch(Request &req, Response &resp)
{
    // std::string path = uriToPath(req.getPath());

    // Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    // bool exists = access(path.c_str(), R_OK) == 0;

    // if (!exists)
    //     return 404;

    // return 501;
    return doPropfind(req, resp);
}

int Server::doPut(Request &req, Response &resp)
{
    std::string path = uriToPath(req.getPath());

    //Debug_printv("req[%s] path[%s]", req.getPath().c_str(), path.c_str());

    struct stat sb;
    int i = stat(path.c_str(), &sb);
    bool exists (i == 0);

    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return 404;

    // // Do we need to continue to get the data?
    // if (req.getHeader("Expect").contains("100-continue") )
    // {
    //     Debug_printv("continue");
    //     req.sendContinue();
    // }

    int remaining = req.getContentLength();

    const int chunkSize = 8192;
    char *chunk = (char *)malloc(chunkSize);

    int ret = 0;

    while (remaining > 0)
    {
        int r, w;
        r = req.readBody(chunk, std::min(remaining, chunkSize));
        if (r <= 0)
            break;

        w = fwrite(chunk, 1, r, f);
        if (w != r)
        {
            ret = -errno;
            break;
        }

        remaining -= w;
    }

    free(chunk);
    fclose(f);

    if (ret < 0)
        return 500;

    if (!exists)
        return 201;

    return 200;
}

int Server::doUnlock(Request &req, Response &resp)
{
    Debug_printv("req[%s]", req.getPath().c_str());
    resp.setHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
    return 204;
}
