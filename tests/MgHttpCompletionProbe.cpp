#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include "fnSystem.h"
#include "mgHttpClient.h"
#include "utils.h"

namespace
{
mg_mgr *http_manager(mgHttpClient &client);
mg_connection *active_connection(mgHttpClient &client);
int response_status(mgHttpClient &client);

// [temp.explicit] permits private member names in explicit instantiations.
// Keep close observation test-only without changing the production class definition.
template<auto Manager, auto Active, auto Status>
struct CompletionProbeAccess
{
    friend mg_mgr *http_manager(mgHttpClient &client)
    {
        return (client.*Manager).get();
    }
    friend mg_connection *active_connection(mgHttpClient &client)
    {
        return client.*Active;
    }
    friend int response_status(mgHttpClient &client)
    {
        return client.*Status;
    }
};

template struct CompletionProbeAccess<&mgHttpClient::_handle, &mgHttpClient::_active,
                                      &mgHttpClient::_status_code>;
}

// Only the platform clock and string helper are substituted; HTTP and TCP are real.
SystemManager::SystemManager() {}
SystemManager fnSystem;
uint64_t SystemManager::millis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string util_tolower(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4)
        return 2;
    mgHttpClient client;
    if (!client.begin(argv[1]))
        return 3;
    client.set_keep_alive(std::string(argv[2]) == "reuse");
    const std::string mode = argc == 4 ? argv[3] : "";
    const int requests = mode == "twice" || mode == "no-body-twice" || mode == "head-twice" ? 2 : 1;
    const bool metadata = mode == "no-body" || mode == "head" ||
                          mode == "no-body-twice" || mode == "head-twice" ||
                          mode == "no-body-close" || mode == "head-close";
    if (metadata)
        client.create_empty_stored_headers({"Content-Length", "ETag", "Content-Type", "Transfer-Encoding"});
    for (int request = 0; request < requests; ++request)
    {
        const bool head = request == 0 && (mode == "head" || mode == "head-twice" || mode == "head-close");
        int status = head ? client.HEAD() : client.GET();
        std::cout << (head ? "HEAD_RETURNED" : "GET_RETURNED") << std::endl;
        if (mode == "no-body-close" || mode == "head-close")
        {
            const uint64_t deadline = fnSystem.millis() + 1500;
            while (active_connection(client) != nullptr && fnSystem.millis() < deadline)
                mg_mgr_poll(http_manager(client), 10);
            if (active_connection(client) != nullptr)
                return 5;
            status = response_status(client);
        }
        const int available = client.available();
        uint8_t body[128] = {};
        const int first = client.read(body, 3);
        const int rest = client.read(body + std::max(first, 0), 125);
        const int eof = client.read(body + 127, 1);
        std::cout << std::dec << status << ' ' << client.content_length() << ' ' << available << ' '
                  << first << ' ' << rest << ' ' << eof << ' ' << client.available() << ' '
                  << client.is_transaction_done() << ' ';
        for (int i = 0; i < std::max(first, 0) + std::max(rest, 0); ++i)
            std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(body[i]);
        std::cout << std::endl;
        if (metadata)
            std::cout << client.get_header("Content-Length") << '|'
                      << client.get_header("ETag") << '|'
                      << client.get_header("Content-Type") << '|'
                      << client.get_header("Transfer-Encoding") << std::endl;
        if (request + 1 < requests)
        {
            char next;
            if (!std::cin.get(next) || next != '\n')
                return 4;
        }
    }
}
