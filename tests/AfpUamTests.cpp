#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

extern "C" {
int init_uams(void);
char *get_uam_names_list(void);
}

// Registration only: whether each login method works needs a server.
TEST_CASE("all six UAMs are registered")
{
    REQUIRE(init_uams() == 0);
    const std::string names = get_uam_names_list();

    for (const char *uam : {"No User Authent", "Cleartxt Passwrd", "Randnum Exchange",
                            "2-Way Randnum Exchange", "DHCAST128", "DHX2"})
    {
        CAPTURE(uam);
        CHECK(names.find(uam) != std::string::npos);
    }
}
