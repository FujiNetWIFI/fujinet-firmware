#pragma once

#include "request.h"
#include "response.h"

namespace WebDav {

class Server {
public:
        Server(std::string rootURI, std::string rootPath);
        ~Server() {};

        std::string pathToURI(std::string path);
        std::string uriToPath(std::string uri);

        int doCopy(Request &req, Response &resp);
        int doDelete(Request &req, Response &resp);
        int doGet(Request &req, Response &resp);
        int doHead(Request &req, Response &resp);
        int doLock(Request &req, Response &resp);
        int doMkcol(Request &req, Response &resp);
        int doMove(Request &req, Response &resp);
        int doOptions(Request &req, Response &resp);
        int doPropfind(Request &req, Response &resp);
        int doProppatch(Request &req, Response &resp);
        int doPut(Request &req, Response &resp);
        int doUnlock(Request &req, Response &resp);

private:
        std::string rootURI, rootPath;

        std::string formatTime(time_t t);
        int sendPropResponse(Response &resp, std::string path, int recurse);
        void sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr);
};

} // namespace