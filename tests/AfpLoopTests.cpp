#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <csignal>
#include <thread>

#include <pthread.h>

extern "C" {
struct libafpclient;
void libafpclient_register(struct libafpclient *client);
int afp_main_quick_startup(pthread_t *thread);
}

static void host_handler(int) {}

// FujiNet-PC installs its own SIGINT and SIGTERM handlers in main().
TEST_CASE("starting the AFP event loop leaves the host's signal handlers alone")
{
    std::signal(SIGINT, host_handler);
    std::signal(SIGTERM, host_handler);

    libafpclient_register(nullptr);
    REQUIRE(afp_main_quick_startup(nullptr) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    CHECK(std::signal(SIGINT, SIG_DFL) == host_handler);
    CHECK(std::signal(SIGTERM, SIG_DFL) == host_handler);
}
