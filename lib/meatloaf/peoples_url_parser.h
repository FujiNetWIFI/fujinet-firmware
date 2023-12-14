#ifndef MEATLOAF_PUP_H
#define MEATLOAF_PUP_H


#include <string>
#include <vector>
#include <sstream>
#include "utils.h"
#include "string_utils.h"
//#include "../../include/global_defines.h"

class PeoplesUrlParser {
public:
    std::string url;
    std::string scheme;
    std::string user;
    std::string pass;    
    std::string host;
    std::string port;
    std::string path;
    std::string name;
    std::string extension;

private:

    void processHostPort(std::string hostPort) {
        auto byColon = mstr::split(hostPort,':',2);
        host = byColon[0];
        if(byColon.size()>1) {
            port = byColon[1];
        }
    }

    void processAuthorityPath(std::string authorityPath) {
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

    void processUserPass(std::string userPass) {
        // user:pass
        auto byColon = mstr::split(userPass,':',2);
        if(byColon.size()==1) {
            user = byColon[0];
        }
        else {
            user = byColon[0];
            pass = byColon[1];
        }
    }

    void processAuthority(std::string pastTheColon) {
        // //user:pass@/path
        // //user:pass@authority:80/path
        // //          authority:100
        // //          authority:30/path            
        auto byAtSign = mstr::split(pastTheColon,'@', 2);

        if(byAtSign.size()==1) {
            // just address, port, path
            processAuthorityPath(mstr::drop(byAtSign[0],2));
        }
        else {
            // user:pass
            processUserPass(mstr::drop(byAtSign[0],2));
            // address, port, path
            processAuthorityPath(byAtSign[1]);
        }
    }

    void cleanPath() {
        if(path.size() == 0)
            return;

        while(mstr::endsWith(path,"/")) {
            path=mstr::dropLast(path, 1);
        }
        mstr::replaceAll(path, "//", "/");
    }

    void fillInNameExt() {
        if(path.size() == 0)
            return;

        auto pathParts = mstr::split(path,'/');

        if(pathParts.size() > 1)
            name = *(--pathParts.end());
        else
            name = path;

        auto nameParts = mstr::split(name,'.');
        
        if(nameParts.size() > 1)
            extension = *(--nameParts.end());
        else
            extension = "";
    }

public:

    std::string pathToFile(void)
    {
        if (name.size() > 0)
            return path.substr(0, path.size() - name.size() - 1);
        else
            return path;
    }

    std::string root(void)
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
            if ( pass.size() )
                root += ':' + pass;
            root += '@';
        }

        root += host;

        if ( port.size() )
            root += ':' + port;

        //Debug_printv("root[%s]", root.c_str());
        return root;
    }

    std::string base(void)
    {
        // set base URL
        //Debug_printv("base[%s]", (root() + "/" + path).c_str());
        if ( !mstr::startsWith(path, "/") )
            path = "/" + path;

        cleanPath();

        return root() + pathToFile() ;
    }

    std::string rebuildUrl(void)
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

    uint16_t getPort() {
        return std::stoi(port);
    }

    void parseUrl(const std::string &u) {
        url = u;

        //Debug_printv("Before [%s]", url.c_str());

        auto byColon = mstr::split(url, ':', 2);

        scheme = "";
        path = "";
        user = "";
        pass = "";
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
        fillInNameExt();
        rebuildUrl();

        //dump();

        return;        
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

};

#endif