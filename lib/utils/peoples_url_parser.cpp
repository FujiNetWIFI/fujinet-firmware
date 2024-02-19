// This code uses code from the Meatloaf Project:
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


#include "peoples_url_parser.h"

#include <string>
#include <vector>
#include <sstream>
#include <cstdint>

#include "../../include/debug.h"

#include "string_utils.h"

void PeoplesUrlParser::processHostPort(std::string hostPort) {
    auto byColon = mstr::split(hostPort,':',2);
    host = byColon[0];
    if(byColon.size()>1) {
        port = byColon[1];
    }
}

void PeoplesUrlParser::processAuthorityPath(std::string authorityPath) {
    //             /path
    // authority:80/path
    // authority:100
    // authority
    auto bySlash = mstr::split(authorityPath,'/',2);

    processHostPort(bySlash[0]);
    if(bySlash.size()>1) {
        // hasPath
        path = bySlash[1];
    }

}

void PeoplesUrlParser::processUserPass(std::string userPass) {
    // user:pass
    auto byColon = mstr::split(userPass,':',2);
    if(byColon.size()==1) {
        user = byColon[0];
    }
    else {
        user = byColon[0];
        password = byColon[1];
    }
}

void PeoplesUrlParser::processAuthority(std::string pastTheColon) {
    // //user:password@/path
    // //user:password@host:80/path
    // //          host:100
    // //          host:30/path            
    auto byAtSign = mstr::split(pastTheColon,'@', 2);

    if(byAtSign.size()==1) {
        // just address, port, path
        processAuthorityPath(mstr::drop(byAtSign[0],2));
    }
    else {
        // user:password
        processUserPass(mstr::drop(byAtSign[0],2));
        // address, port, path
        processAuthorityPath(byAtSign[1]);
    }
}

void PeoplesUrlParser::cleanPath() {
    if(path.size() == 0)
        return;

    // apc: keep trailing '/'
    // it's needed to list a directory on N:
    // while(mstr::endsWith(path,"/")) {
    //     path=mstr::dropLast(path, 1);
    // }
    mstr::replaceAll(path, "//", "/");
}

void PeoplesUrlParser::processPath() {
    if(path.size() == 0)
        return;

    auto pathParts = mstr::split(path, '/');
    auto queryParts = mstr::split(*(--pathParts.end()), '?');
    auto fragmentParts = mstr::split(*(--queryParts.end()), '#');

    // path
    if(pathParts.size() > 1)
        name = *(--pathParts.end());
    else
        name = path;

    // filename
    if(queryParts.size() > 1)
        name = queryParts.front();

    auto nameParts = mstr::split(name, '.');

    // base name
    if(nameParts.size() > 1)
        base_name = nameParts.front();

    // extension
    if(nameParts.size() > 1)
        extension = *(--nameParts.end());

    // query
    if(queryParts.size() > 1)
        query = *(fragmentParts.begin());
    
    // fragment
    if(fragmentParts.size() > 1)
        fragment = *(--fragmentParts.end());
}


std::string PeoplesUrlParser::pathToFile(void)
{
    if (name.size() > 0)
        return path.substr(0, path.size() - name.size() - 1);
    else
        return path;
}

std::string PeoplesUrlParser::root(void)
{
    // set root URL
    std::string root;
    if ( scheme.size() )
        root = scheme + ":";

    if ( host.size() )
        root += "//";

    if ( user.size() )
    {
        root += user;
        if ( password.size() )
            root += ':' + password;
        root += '@';
    }

    root += host;

    if ( port.size() )
        root += ':' + port;

    //Debug_printv("root[%s]", root.c_str());
    return root;
}

std::string PeoplesUrlParser::base(void)
{
    // set base URL
    //Debug_printv("base[%s]", (root() + "/" + path).c_str());
    if ( !mstr::startsWith(path, "/") )
        path = "/" + path;

    cleanPath();

    return root() + pathToFile() ;
}



uint16_t PeoplesUrlParser::getPort() {
    return std::stoi(port);
}


PeoplesUrlParser* PeoplesUrlParser::parseURL(const std::string &u) {
    PeoplesUrlParser *url = new PeoplesUrlParser;
    url->resetURL(u);
    return url;
}

void PeoplesUrlParser::resetURL(const std::string u) {

    if ( u.empty() )
        return;

    //Debug_printv("u[%s]", u.c_str());

    url = u;
    mRawUrl = u;

    //Debug_printv("Before [%s]", url.c_str());

    auto byColon = mstr::split(url, ':', 2);

    scheme = "";
    path = "";
    user = "";
    password = "";
    host = "";
    port = "";

    if(byColon.size()==1) {
        // no scheme, good old local path
        path = byColon[0];
    }
    else
    {
        scheme = byColon[0];

        auto pastTheColon = byColon[1]; // don't visualise!

        if(pastTheColon[0]=='/' && pastTheColon[1]=='/') {
            // //user:pass@/path
            // //user:pass@authority:80/path
            // //authority:100
            // //authority:30/path            

            processAuthority(pastTheColon);
        }
        else {
            // we have just a plain old path
            // /path
            // user@server
            // etc.
            path = pastTheColon;
        }            
    }

    // Clean things up before exiting
    cleanPath();
    processPath();
    rebuildUrl();

    //dump();

    return;        
}

std::string PeoplesUrlParser::rebuildUrl(void)
{
    // set full URL
    if ( !mstr::startsWith(path, "/") )
        path = "/" + path;

    cleanPath();

    url = root() + path;
    //Debug_printv("url[%s]", url.c_str());
    // url += name;
    // Debug_printv("url[%s]", url.c_str());
    // if ( query.size() )
    //     url += '?' + query;
    // if ( fragment.size() )
    //     url += '#' + fragment;

    return url;
}


// void dump() {
//     printf("scheme: %s\r\n", scheme.c_str());
//     printf("user pass: %s -- %s\r\n", user.c_str(), pass.c_str());
//     printf("host port: %s -- %s\r\n", host.c_str(), port.c_str());
//     printf("path: %s\r\n", path.c_str());
//     printf("name: %s\r\n", name.c_str());
//     printf("extension: %s\r\n", extension.c_str());
//     printf("root: %s\r\n", root().c_str());
//     printf("base: %s\r\n", base().c_str());
//     printf("pathToFile: %s\r\n", pathToFile().c_str());
// }

bool PeoplesUrlParser::isValidUrl()
{
    dump();
    return !scheme.empty() && !(path.empty() && port.empty());
}
