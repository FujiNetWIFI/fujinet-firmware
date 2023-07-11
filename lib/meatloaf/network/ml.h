// ML:// - Meatloaf Server Protocol
// 


#ifndef MEATLOAF_SCHEME_ML
#define MEATLOAF_SCHEME_ML

#include "network/http.h"

#include "peoples_url_parser.h"


/********************************************************
 * FS
 ********************************************************/

class MLFileSystem: public MFileSystem
{
    MFile* getFile(std::string path) override {
        if ( path.size() == 0 )
            return nullptr;

        //Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
        PeoplesUrlParser urlParser;
        urlParser.parseUrl(path);

        //Debug_printv("url[%s]", urlParser.name.c_str());
        std::string ml_url = "https://api.meatloaf.cc/?" + urlParser.name;
        //Debug_printv("ml_url[%s]", ml_url.c_str());
        
        //Debug_printv("url[%s]", ml_url.c_str());

        return new HttpFile(ml_url);
    }

    bool handles(std::string name) {
        std::string pattern = "ml:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

public:
    MLFileSystem(): MFileSystem("meatloaf") {};
};


#endif // MEATLOAF_SCHEME_ML