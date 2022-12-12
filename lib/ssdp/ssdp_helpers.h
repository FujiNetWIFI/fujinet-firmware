
#ifndef SSDP_HELPERS_H
#define SSDP_HELPERS_H

//#include <algorithm>

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



    bool equals(const char* a, const char *b, bool case_sensitive = true)
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

#endif // SSDP_HELPERS_H