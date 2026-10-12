// Stand-in for lib/config/fnPassword.h: no device password is set.
#pragma once
#include <string>

struct FnPasswordStub
{
    bool is_set() { return false; }
    bool check_session(const std::string &) { return false; }
    bool check_basic_auth(const char *) { return false; }
};
inline FnPasswordStub fnPassword;
