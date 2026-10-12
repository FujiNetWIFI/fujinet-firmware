// Stand-in for lib/http/fnSession.h: no session cookie is ever present.
#pragma once
#include <string>

#define FN_SESSION_COOKIE "fnsession"

namespace fnSession
{
inline std::string cookie_value(const char *, const char *) { return ""; }
}
