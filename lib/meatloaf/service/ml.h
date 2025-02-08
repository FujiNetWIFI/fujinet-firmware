// ML:// - Meatloaf Server Protocol
// 


#ifndef MEATLOAF_SCHEME_ML
#define MEATLOAF_SCHEME_ML

#include "network/http.h"

#include "peoples_url_parser.h"


/********************************************************
 * FS
 ********************************************************/

class MLMFileSystem: public MFileSystem
{
    MFile* getFile(std::string path) override {
        if ( path.size() == 0 )
            return nullptr;

        //Debug_printv("MLFileSystem::getFile(%s)", path.c_str());
        auto urlParser = PeoplesUrlParser::parseURL( path );
        //std::string code = mstr::toUTF8(urlParser->name);

        //Debug_printv("url[%s]", urlParser.name.c_str());
        std::string ml_url = "https://api.meatloaf.cc/?" + urlParser->name;
        if ( urlParser->query.size() > 0)
            ml_url += "&" + urlParser->query;
        if ( urlParser->fragment.size() > 0)
            ml_url += "#" + urlParser->fragment;
        //Debug_printv("ml_url[%s]", ml_url.c_str());
        
        //Debug_printv("url[%s]", ml_url.c_str());

        return new HTTPMFile(ml_url);
    }

    bool handles(std::string name) {
        std::string pattern = "ml:";
        return mstr::startsWith(name, pattern.c_str(), false);
    }

public:
    MLMFileSystem(): MFileSystem("meatloaf") {};
};


#endif // MEATLOAF_SCHEME_ML