#include <map>
#include <string>
#include <vector>

#include "response.h"

using namespace WebDav;

void Response::setDavHeaders() {
    setHeader("DAV", "1,2");
    setHeader("Allow", "COPY,DELETE,GET,HEAD,LOCK,MKCOL,MOVE,OPTIONS,PROPFIND,PROPPATCH,PUT,UNLOCK");
    setHeader("Keep-Alive", "timeout=5, max=100");
    setHeader("Connection", "Keep-Alive");
}

void Response::setHeader(std::string header, std::string value) {
    headers[header] = value;
}

void Response::setHeader(std::string header, size_t value) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%zu", value);
    headers[header] = tmp;
}

void Response::flushHeaders() {
    for (const auto &h: headers)
        writeHeader(h.first.c_str(), h.second.c_str());
    headers.clear();
}
