#ifndef MEATFILE_PUP_H
#define MEATFILE_PUP_H


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
            // just addres, port, path
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
            path=mstr::dropLast(path,1);
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

        return root;
    }

    std::string base(void)
    {
        // set base URL
        return root() + path;
    }

    std::string pathToFile(void)
    {
        if (name.size() > 0)
            return path.substr(0, path.size() - name.size());
        else
            return path;
    }

    std::string rebuildUrl(void)
    {
        // set full URL
        url = base();
        url += name;
        // if ( query.size() )
        //     url += '?' + query;
        // if ( fragment.size() )
        //     url += '#' + fragment;

        return url;
    }

    void parseUrl(std::string u) {
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
        //rebuildUrl();

        return;        
    }

    // void dump() {
    //     printf("scheme: %s\n", scheme.c_str());
    //     printf("host port: %s -- %s\n", host.c_str(), port.c_str());
    //     printf("path: %s\n", path.c_str());
    //     printf("user pass: %s -- %s\n", user.c_str(), pass.c_str());
    // }

};

#endif