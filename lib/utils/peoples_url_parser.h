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

#ifndef MEATLOAF_PUP_H
#define MEATLOAF_PUP_H

#include <cstdint>
#include <memory>
#include <string>

class PeoplesUrlParser
{
private:
    void processHostPort(std::string hostPort);
    void processAuthorityPath(std::string authorityPath);
    void processUserPass(std::string userPass);
    void processAuthority(std::string pastTheColon);
    void cleanPath();
    void processPath();

protected:
    PeoplesUrlParser() {};

public:
    ~PeoplesUrlParser() {};

    std::string mRawUrl;
    std::string url;
    std::string scheme;
    std::string user;
    std::string password;    
    std::string host;
    std::string port;
    std::string path;
    std::string name;
    std::string base_name;
    std::string extension;
    std::string query;
    std::string fragment;

    std::string pathToFile(void);
    std::string root(void);
    std::string base(void);

    uint16_t getPort();

    static std::unique_ptr<PeoplesUrlParser> parseURL(const std::string &u);
    void resetURL(const std::string u);
    std::string rebuildUrl(void);
    bool isValidUrl();

    void dump() {
        printf("mRawUrl: %s\r\n", mRawUrl.c_str());
        printf("url:     %s\r\n", url.c_str());
        printf("scheme: %s\r\n", scheme.c_str());
        printf("user pass: %s -- %s\r\n", user.c_str(), password.c_str());
        printf("host port: %s -- %s\r\n", host.c_str(), port.c_str());
        printf("path: %s\r\n", path.c_str());
        printf("pathToFile: %s\r\n", pathToFile().c_str());
        printf("name: %s\r\n", name.c_str());
        printf("base_name: %s\r\n", base_name.c_str());
        printf("extension: %s\r\n", extension.c_str());
        printf("root: %s\r\n", root().c_str());
        printf("base: %s\r\n", base().c_str());
        printf("query: %s\r\n", query.c_str());
        printf("fragment: %s\r\n\r\n", fragment.c_str());
    }

};

#endif // MEATLOAF_PUP_H