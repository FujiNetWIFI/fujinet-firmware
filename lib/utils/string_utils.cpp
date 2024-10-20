#include "string_utils.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

//#include "../../include/petscii.h"
#include "../../include/debug.h"
#include "U8Char.h"


#if defined(_WIN32)
#include "asprintf.h" // use asprintf from libsmb2
#endif

// Copy string to char buffer
void copyString(const std::string& input, char *dst, size_t dst_size)
{
    strncpy(dst, input.c_str(), dst_size - 1);
    dst[dst_size - 1] = '\0';
}

constexpr unsigned int hash(const char *s, int off = 0) {                        
    return !s[off] ? 5381 : (hash(s, off+1)*33) ^ s[off];                           
}

namespace mstr {

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
        // CR
        s.erase(
            std::find_if(s.rbegin(), s.rend(), [](int ch) { return (ch != 0x0D); }).base(), s.end());
        
        // SPACE
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

    // is OSX/Windows junk system file
    bool isJunk(std::string &s)
    {
        std::vector<std::string> names = {
            // OSX
            "/._",
            "/.DS_Store",
            "/.fseventsd",
            "/.Spotlight-V",
            "/.TemporaryItems",
            "/.Trashes",
            "/.VolumeIcon.icns",

            // Windows
            "/Desktop.ini",
            "/Thumbs.ini"
        };

        for (auto it = begin (names); it != end (names); ++it) {
            if (contains(s, it->c_str()))
                return true;
        }
        
        return false;
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
            switch ((unsigned char)s1[index]) {
                case '*':
                    return true; /* rest is not interesting, it's a match */
                case '?':
                    if ((unsigned char)s2[index] == 0xa0) {
                        return false; /* wildcard, but the other is too short */
                    }
                    break;
                case 0xa0: /* This one ends, let's see if the other as well */
                    return ((unsigned char)s2[index] == 0xa0);
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

    // // convert to ascii (in place) - DO NOT USE, use toUtf8 instead!
    // void toASCII(std::string &s)
    // {
    //     std::transform(s.begin(), s.end(), s.begin(),
    //                 [](unsigned char c) { return petscii2ascii(c); });
    // }

    // // convert to petscii (in place) - DO NOT USE, utf8 can't be converted in place!
    // void toPETSCII(std::string &s)
    // {
    //     std::transform(s.begin(), s.end(), s.begin(),
    //                 [](unsigned char c) { return ascii2petscii(c); });
    // }

    // convert PETSCII to UTF8, using methods from U8Char
    std::string toUTF8(const std::string &petsciiInput)
    {
        std::string utf8string;
        for(char petscii : petsciiInput) {
            if(petscii > 0)
            {
                U8Char u8char(petscii);
                utf8string+=u8char.toUtf8();
            }
        }
        return utf8string;
    }

    // convert UTF8 to PETSCII, using methods from U8Char
    std::string toPETSCII2(const std::string &utfInputString)
    {
        std::string petsciiString;
        char* utfInput = (char*)utfInputString.c_str();
        auto end = utfInput + utfInputString.length();

        while(utfInput<end) {
            U8Char u8char(' ');
            size_t skip = u8char.fromCharArray(utfInput);
            petsciiString+=u8char.toPetscii();
            utfInput+=skip;
        }
        return petsciiString;
    }

    // convert bytes to hex
    std::string toHex(const uint8_t *input, size_t size)
    {
        std::stringstream ss;
        for(int i=0; i<size; ++i)
            ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)input[i];
        return ss.str();
    }
    // convert string to hex
    std::string toHex(const std::string &input)
    {
        return toHex((const uint8_t *)input.c_str(), input.size());
    }

    // convert hex char to it's integer value
    char fromHex(char ch)
    {
        return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
    }

    bool isHex(std::string &s)
    {
        return std::all_of(s.begin(), s.end(), 
                        [](unsigned char c) { return ::isxdigit(c); });
    }

    // convert to A0 space to 20 space (in place)
    void A02Space(std::string &s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return (c == 0xa0) ? 0x20: c; });
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

    bool isNumeric(std::string &s)
    {
        return std::all_of(s.begin(), s.end(), 
                        [](unsigned char c) { return ::isdigit(c); });
    }

    void replaceAll(std::string &s, const std::string &search, const std::string &replace) 
    {
        const size_t size = search.size();
        bool size_match = ( size == replace.size() );
        for( size_t pos = 0; ; pos += replace.size() ) {
            // Locate the substring to replace
            pos = s.find( search, pos );
            if( pos == std::string::npos ) break;
            if ( size_match )
            {
                // Faster using replace if they are the same size
                s.replace( pos, size, replace);
            }
            else
            {
                // Replace by erasing and inserting
                s.erase( pos, search.size() );
                s.insert( pos, replace );
            }
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


    std::string urlEncode(const std::string &s) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (std::string::const_iterator i = s.begin(), n = s.end(); i != n; ++i)
        {
            std::string::value_type c = (*i);

            // Change space to '+'
            // if ( c == ' ')
            // {
            //     escaped << '+';
            //     continue;
            // }

            // Keep alphanumeric and other accepted characters intact
            //if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ' ')
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == '+')
            {
                escaped << c;
                continue;
            }

            // Any other characters are percent-encoded
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }

        return escaped.str();
    }

    void urlDecode(char *s, size_t size, bool alter_pluses)
    {
        char ch;
        int i = 0, ii = 0;

        while (s[i] != '\0' && i < size) {
            if (alter_pluses && s[i] == '+')
            {
                s[ii++] = ' ';
                i++;
            } 
            else if ((s[i] == '%') && 
                    isxdigit(s[i + 1]) && 
                    isxdigit(s[i + 2]) &&
                    (i + 2 < size))
            {
                ch = fromHex(s[i + 1]) << 4 | fromHex(s[i + 2]);
                s[ii++] = ch;
                i += 3; // Skip past the percent encoding.
            }
            else
            {
                s[ii++] = s[i++];
            }
        }
        s[ii] = '\0'; // Null-terminate the decoded string.
    }

    void urlDecode(char *s, size_t size)
    {
        urlDecode(s, size, true);
    }

    std::string urlDecode(const std::string& s, bool alter_pluses)
    {
        if (s.empty()) return s;

        size_t size = s.size() + 1; // +1 for null terminator
        char* buffer = new char[size];
        std::copy(s.begin(), s.end(), buffer);
        buffer[s.size()] = '\0'; // Ensure null termination

        urlDecode(buffer, size, alter_pluses);

        std::string result(buffer);
        delete[] buffer;
        return result;
    }

    std::string sha1(const std::string &s)
    {
        unsigned char hash[21] = { 0x00 };
        mbedtls_sha1((const unsigned char *)s.c_str(), s.length(), hash);
        // unsigned char output[64];
        // size_t outlen;
        // mbedtls_base64_encode(output, 64, &outlen, hash, 20);
        std::string o(reinterpret_cast< char const* >(hash));
        return toHex(o);
    }

    std::string urlDecode(const std::string& s)
    {
        return urlDecode(s, true);
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

    std::string formatBytes(uint64_t size)
    {
        std::string byteSuffixes[9] = { "", "K", "M", "G", "T"}; //, "P", "E", "Z", "Y" };
        uint8_t i = 0;
        double n = 0;

        //Debug_printv("bytes[%llu]", size);
        do
        {          
            n = size / std::pow(1024, ++i);
            //Debug_printv("i[%d] n[%llu]", i, n);
        }
        while ( n >= 1 );

        n = size / std::pow(1024, --i);
        return format("%.2f %s", n, byteSuffixes[i].c_str());
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