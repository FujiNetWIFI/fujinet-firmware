#include "string_utils.h"
#include "../../include/petscii.h"
#include <algorithm>
#include <cstdarg>


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
    
    bool contains(std::string &s1, char *s2, bool case_sensitive)
    {
        std::string sn = s2;
        std::string::iterator it;
        if(case_sensitive)
            it = ( std::search(s1.begin(), s1.end(), sn.begin(), sn.end(), &compare_char) );
        else
            it = ( std::search(s1.begin(), s1.end(), sn.begin(), sn.end(), &compare_char_insensitive) );

        return ( it != s1.end() );
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
            if (c==' ') new_str += '+';   
            else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') new_str += c;
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
}