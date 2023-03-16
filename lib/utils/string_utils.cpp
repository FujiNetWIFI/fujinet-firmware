#include "string_utils.h"
#include "../../include/petscii.h"
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include "../../include/petscii.h"
#include <algorithm>
#include <cstdarg>
#include <cmath>

// Copy string to char buffer
void copyString(const std::string& input, char *dst, size_t dst_size)
{
    strncpy(dst, input.c_str(), dst_size - 1);
    dst[dst_size - 1] = '\0';
}

namespace mstr {

    std::string byteSuffixes[9] = { "", "K", "M", "G", "T", "P", "E", "Z", "Y" };

    // trim from start (in place)
    void ltrim(std::string &s)
    {
        s.erase(
            s.begin(),
            std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
    }

    // trim from end (in place)
    void rtrim(std::string &s)
    {
        s.erase(
            std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
    }
    void rtrimA0(std::string &s)
    {
        s.erase(
            std::find_if(s.rbegin(), s.rend(), [](int ch) { return !isA0Space(ch); }).base(), s.end());
    }

    // trim from both ends (in place)
    void trim(std::string &s)
    {
        ltrim(s);
        rtrim(s);
    }

    // is space or petscii shifted space
    bool isA0Space(int ch)
    {
        return ch == '\xA0' || std::isspace(ch);
    }



    std::string drop(std::string str, size_t count) {
        if(count>str.length())
            return "";
        else
            return str.substr(count);
    }

    std::string dropLast(std::string str, size_t count) {
        if(count>str.length())
            return "";
        else
            return str.substr(0, str.length()-count);
    }

    bool startsWith(std::string s, const char *pattern, bool case_sensitive)
    {
        if (s.empty() && pattern == nullptr)
            return true;
        if (s.empty() || pattern == nullptr)
            return false;
        if(s.length()<strlen(pattern))
            return false;

        std::string ss = s.substr(0, strlen(pattern));
        std::string pp = pattern;

        return equals(ss, pp, case_sensitive);
    }

    bool endsWith(std::string s, const char *pattern, bool case_sensitive)
    {
        if (s.empty() && pattern == nullptr)
            return true;
        if (s.empty() || pattern == nullptr)
            return false;
        if(s.length()<strlen(pattern))
            return false;

        std::string ss = s.substr((s.length() - strlen(pattern)));
        std::string pp = pattern;

        return equals(ss, pp, case_sensitive);
    }


    /*
    * String Comparision
    */
    bool compare_char(char &c1, char &c2)
    {
        if (c1 == c2)
            return true;

        return false;
    }

    bool compare_char_insensitive(char &c1, char &c2)
    {
        if (c1 == c2)
            return true;
        else if (std::toupper(c1) == std::toupper(c2))
            return true;
        return false;
    }

    bool equals(const char* a, const char *b, bool case_sensitive)
    {
        int la = strlen(a);
        int lb = strlen(b);
        if(la != lb) return false;
        for(lb = 0; lb < la; lb++)
        {
            char aa = a[lb];
            char bb = b[lb];

            if(case_sensitive && !compare_char(aa, bb))
                return false;
            else if(!case_sensitive && !compare_char_insensitive(aa, bb))
                return false;
        }
        return true;
    }


    bool equals(std::string &s1, std::string &s2, bool case_sensitive)
    {
        if(case_sensitive)
            return ( (s1.size() == s2.size() ) &&
                std::equal(s1.begin(), s1.end(), s2.begin(), &compare_char) );
        else
            return ( (s1.size() == s2.size() ) &&
                std::equal(s1.begin(), s1.end(), s2.begin(), &compare_char_insensitive) );
    }


    bool equals(std::string &s1, char *s2, bool case_sensitive)
    {
        if(case_sensitive)
            return ( (s1.size() == strlen(s2) ) &&
                std::equal(s1.begin(), s1.end(), s2, &compare_char) );
        else
            return ( (s1.size() == strlen(s2) ) &&
                std::equal(s1.begin(), s1.end(), s2, &compare_char_insensitive) );
    }
    
    bool contains(std::string &s1, const char *s2, bool case_sensitive)
    {
        std::string sn = s2;
        std::string::iterator it;
        if(case_sensitive)
            it = ( std::search(s1.begin(), s1.end(), sn.begin(), sn.end(), &compare_char) );
        else
            it = ( std::search(s1.begin(), s1.end(), sn.begin(), sn.end(), &compare_char_insensitive) );

        return ( it != s1.end() );
    }

    bool compare(std::string &s1, std::string &s2, bool case_sensitive)
    {
        unsigned int index;

        for (index = 0; index < s1.size(); index++) {
            switch (s1[index]) {
                case '*':
                    return true; /* rest is not interesting, it's a match */
                case '?':
                    if (s2[index] == 0xa0) {
                        return false; /* wildcard, but the other is too short */
                    }
                    break;
                case 0xa0: /* This one ends, let's see if the other as well */
                    return (s2[index] == 0xa0);
                default:
                    if (s1[index] != s2[index]) {
                        return false; /* does not match */
                    }
            }
        }

        return true; /* matched completely */
    }

    // convert to lowercase (in place)
    void toLower(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    }

    // convert to uppercase (in place)
    void toUpper(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::toupper(c); });
    }

    // convert to ascii (in place)
    void toASCII(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return petscii2ascii(c); });
    }

    // convert to petscii (in place)
    void toPETSCII(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return ascii2petscii(c); });
    }

    // convert to A0 space to 20 space (in place)
    void A02Space(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return (c == '\xA0') ? '\x20': c; });
    }

    bool isText(std::string &s) 
    {
        // extensions
        if(equals(s, (char*)"txt", false))
            return true;
        if(equals(s, (char*)"htm", false))
            return true;
        if(equals(s, (char*)"html", false))
            return true;

        // content types
        if(equals(s, (char*)"text/html", false))
            return true;
        if(equals(s, (char*)"text/plain", false))
            return true;
        if(contains(s, (char*)"text", false))
            return true;
        if(contains(s, (char*)"json", false))
            return true;
        if(contains(s, (char*)"xml", false))
            return true;

        return false;
    };

    void replaceAll(std::string &s, const std::string &search, const std::string &replace) 
    {
        for( size_t pos = 0; ; pos += replace.length() ) {
            // Locate the substring to replace
            pos = s.find( search, pos );
            if( pos == std::string::npos ) break;
            // Replace by erasing and inserting
            s.erase( pos, search.length() );
            s.insert( pos, replace );
        }
    }


    std::vector<std::string> split(std::string toSplit, char ch, int limit) {
        std::vector<std::string> parts;

        limit--;

        while(limit > 0 && toSplit.size()>0) {
            auto pos = toSplit.find(ch);
            if(pos == std::string::npos) {
                parts.push_back(toSplit);
                return parts;
            }
            parts.push_back(toSplit.substr(0, pos));

            toSplit = toSplit.substr(pos+1);

            limit--;
        }
        parts.push_back(toSplit);

        return parts;
    }

    std::string joinToString(std::vector<std::string>::iterator* start, std::vector<std::string>::iterator* end, std::string separator) {
        std::string res;

        if((*start)>=(*end))
        {
            //Debug_printv("start >= end");
            return std::string();
        }
            

        for(auto i = (*start); i<(*end); i++) 
        {
            //Debug_printv("b %d res [%s]", i, res.c_str());
            res+=(*i);
            if(i<(*end))
                res+=separator;

            //Debug_printv("a %d res [%s]", i, res.c_str());
        }
        //Debug_printv("res[%s] length[%d] size[%d]", res.c_str(), res.length(), res.size());

        return res.erase(res.length()-1,1);
    }

    std::string joinToString(std::vector<std::string> strings, std::string separator) {
        auto st = strings.begin();
        auto ed = strings.end();
        return joinToString(&st, &ed, separator);
    }


    std::string urlEncode(std::string s) {
        std::string new_str = "";
        char c;
        int ic;
        const char* chars = s.c_str();
        char bufHex[10];
        int len = strlen(chars);

        for(int i=0;i<len;i++){
            c = chars[i];
            ic = c;
            // uncomment this if you want to encode spaces with +
            // if (c==' ') new_str += '+';
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') new_str += c;
            else {
                sprintf(bufHex,"%X",c);
                if(ic < 16) 
                    new_str += "%0"; 
                else
                    new_str += "%";
                new_str += bufHex;
            }
        }
        return new_str;
    }

    std::string urlDecode(std::string s){
        std::string ret;
        char ch;
        int i, ii, len = s.length();

        for (i=0; i < len; i++){
            if(s[i] != '%'){
                if(s[i] == '+')
                    ret += ' ';
                else
                    ret += s[i];
            }else{
                sscanf(s.substr(i + 1, 2).c_str(), "%x", &ii);
                ch = static_cast<char>(ii);
                ret += ch;
                i = i + 2;
            }
        }
        return ret;
    }

    std::string format(const char *format, ...)
    {
        // Format our string
        va_list args;
        va_start(args, format);
        char text[vsnprintf(NULL, 0, format, args) + 1];
        vsnprintf(text, sizeof text, format, args);
        va_end(args);

        return text;
    }

    std::string formatBytes(uint64_t value)
    {
        uint8_t i = 0;
        double n = 0;
        char *f = NULL;

        //Debug_printv("bytes[%llu]", value);

        do
        {          
            n = value / std::pow(1024, ++i);
            //Debug_printv("i[%d] n[%llu]", i, n);
        }
        while ( n >= 1 );

        n = value / std::pow(1024, --i);
        asprintf(&f, "%.2f %s", n, byteSuffixes[i].c_str());
        return f;
    }


    void cd( std::string &path, std::string newDir) 
    {
        //Debug_printv("cd requested: [%s]", newDir.c_str());

        // OK to clarify - coming here there should be ONLY path or magicSymbol-path combo!
        // NO "cd:xxxxx", no "/cd:xxxxx" ALLOWED here! ******************
        //
        // if you want to support LOAD"CDxxxxxx" just parse/drop the CD BEFORE calling this function
        // and call it ONLY with the path you want to change into!

        if(newDir[0]=='/' && newDir[1]=='/') {
            if(newDir.size()==2) {
                // user entered: CD:// or CD//
                // means: change to the root of roots
                path = "/"; // chedked, works ad flash root!
                return;
            }
            else {
                // user entered: CD://DIR or CD//DIR
                // means: change to a dir in root of roots
                path = mstr::drop(newDir,2);
                return;
            }
        }
        // else if(newDir[0]=='/' || newDir[0]=='^') {
        //     if(newDir.size()==1) {
        //         // user entered: CD:/ or CD/
        //         // means: change to container root
        //         // *** might require a fix for flash fs!
        //         //return MFSOwner::File(streamPath);
        //         return MFSOwner::File("/");
        //     }
        //     else {
        //         // user entered: CD:/DIR or CD/DIR
        //         // means: change to a dir in container root
        //         return localRoot(mstr::drop(newDir,1));
        //     }
        // }
        else if(newDir[0]=='_') {
            if(newDir.size()==1) {
                // user entered: CD:_ or CD_
                // means: go up one directory
                path = parent(path);
                return;
            }
            else {
                // user entered: CD:_DIR or CD_DIR
                // means: go to a directory in the same directory as this one
                path = parent(path, mstr::drop(newDir,1));
                return;
            }
        }

        if(newDir[0]=='.' && newDir[1]=='.') {
            if(newDir.size()==2) {
                // user entered: CD:.. or CD..
                // means: go up one directory
                path = parent(path);
                return;
            }
            else {
                // user entered: CD:..DIR or CD..DIR
                // meaning: Go back one directory
                path = localParent(path, mstr::drop(newDir,2));
                return;
            }
        }

        //Debug_printv("> url[%s] newDir[%s]", url.c_str(), newDir.c_str());
                // Add new directory to path
        if ( !mstr::endsWith(path, "/") && newDir.size() )
            path.push_back('/');

        path += newDir;
        return;
    }


    std::string parent(std::string path, std::string plus) 
    {
        //Debug_printv("url[%s] path[%s]", url.c_str(), path.c_str());

        // drop last dir
        // add plus
        if(path.empty()) {
            // from here we can go only to flash root!
            return "/";
        }
        else {
            int lastSlash = path.find_last_of('/');
            if ( lastSlash == path.size() - 1 ) {
                lastSlash = path.find_last_of('/', path.size() - 2);
            }
            std::string newDir = mstr::dropLast(path, path.size() - lastSlash);
            if(!plus.empty())
                newDir+= ("/" + plus);
            return newDir;
        }
    }

    std::string localParent(std::string path, std::string plus) 
    {
        //Debug_printv("url[%s] path[%s]", url.c_str(), path.c_str());
        // drop last dir
        // check if it isn't shorter than streamFile
        // add plus
        int lastSlash = path.find_last_of('/');
        if ( lastSlash == path.size() - 1 ) {
            lastSlash = path.find_last_of('/', path.size() - 2);
        }
        std::string parent = mstr::dropLast(path, path.size() - lastSlash);
        // if(parent.length()-streamFile->url.length()>1)
        //     parent = streamFile->url;
        return parent + "/" + plus;
    }
}