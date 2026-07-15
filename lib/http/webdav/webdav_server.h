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

#pragma once

#include "request.h"
#include "response.h"
#include "meatloaf.h"

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
        int sendPropResponse(Response &resp, std::string path, int recurse, MFile* hint = nullptr);
        void sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr);
};

} // namespace