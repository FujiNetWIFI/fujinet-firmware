// SD:// - Secure Digital Card File System
// https://en.wikipedia.org/wiki/SD_card
// https://github.com/arduino-libraries/SD
//
#ifndef TEST_NATIVE
#ifndef MEATLOAF_DEVICE_SD
#define MEATLOAF_DEVICE_SD

#include "meatloaf.h"

#include "flash.h"
#include "fnFsSD.h"

#include "peoples_url_parser.h"

#include <dirent.h>
#include <string.h>

#define _filesystem fnSDFAT


/********************************************************
 * MFileSystem
 ********************************************************/

class SDFileSystem: public MFileSystem 
{
private:
    MFile* getFile(std::string path) override {
        auto url = PeoplesUrlParser::parseURL( path );

        std::string basepath = _filesystem.basepath();
        basepath += std::string("/");
        //Debug_printv("basepath[%s] url.path[%s]", basepath.c_str(), url.path.c_str());

        return new FlashMFile( url->path );
    }

    bool handles(std::string name) {
        std::string pattern = "sd:";
        return mstr::equals(name, pattern, false);
    }
public:
    SDFileSystem(): MFileSystem("sd") {};
};


#endif // MEATLOAF_DEVICE_SD
#endif // TEST_NATIVE