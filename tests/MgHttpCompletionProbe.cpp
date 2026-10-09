#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include "fnSystem.h"
#include "mgHttpClient.h"
#include "utils.h"

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
    if (argc != 3)
        return 2;
    mgHttpClient client;
    if (!client.begin(argv[1]))
        return 3;
    client.set_keep_alive(std::string(argv[2]) == "reuse");
    const int status = client.GET();
    const int available = client.available();
    uint8_t body[128] = {};
    const int first = client.read(body, 3);
    const int rest = client.read(body + std::max(first, 0), 125);
    const int eof = client.read(body + 127, 1);
    std::cout << status << ' ' << client.content_length() << ' ' << available << ' '
              << first << ' ' << rest << ' ' << eof << ' ' << client.available() << ' '
              << client.is_transaction_done() << ' ';
    for (int i = 0; i < std::max(first, 0) + std::max(rest, 0); ++i)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(body[i]);
    std::cout << std::endl;
}
