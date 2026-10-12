// One HTTP exchange through the fake esp_http_server.
#pragma once
#include <esp_http_server.h>
#include <map>
#include <string>

struct Exchange
{
    std::map<std::string, std::string> req_headers; // lower-case field names
    std::string req_body;
    size_t req_pos = 0;
    std::string status = "200 OK";
    std::map<std::string, std::string> resp_headers;
    std::string resp_body;
};

// Dispatches like esp_http_server: no route for the URI is 404, a route for the
// URI but not this method is 405. Returns the numeric status, or -1 when the
// handler failed.
int request(http_method method, const std::string &uri, Exchange &e);
