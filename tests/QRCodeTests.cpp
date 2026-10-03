#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "qrcode/qrcode.h"

#include <cstdlib>

// Encodes text the way QRManager does: version and ECC set first, then init.
static uint8_t encode(QRCode &qr, uint8_t version, uint8_t ecc, const char *text)
{
    qr = QRCode{};
    qr.version = version;
    qr.ecc = ecc;
    return qrcode_initText(&qr, text);
}

TEST_CASE("version 0 picks the smallest symbol that holds the data at the ECC asked for")
{
    QRCode qr;

    SUBCASE("HIGH: 11 alphanumerics overflow 1-H (10), so version 2")
    {
        REQUIRE(encode(qr, VERSION_AUTO, ECC_HIGH, "HELLO WORLD") == 0);
        CHECK(qr.version == 2);
        CHECK(qr.ecc == ECC_HIGH);
    }

    SUBCASE("QUARTILE: 17 alphanumerics overflow 1-Q (16), and 2-H holds them")
    {
        REQUIRE(encode(qr, VERSION_AUTO, ECC_QUARTILE, "HELLO WORLD 12345") == 0);
        CHECK(qr.version == 2);
        CHECK(qr.ecc == ECC_HIGH);
    }

    SUBCASE("LOW: a short text still gets the strongest ECC its symbol can carry")
    {
        REQUIRE(encode(qr, VERSION_AUTO, ECC_LOW, "HELLO") == 0);
        CHECK(qr.version == 1);
        CHECK(qr.ecc == ECC_HIGH);
    }

    SUBCASE("an ECC level above HIGH is refused")
    {
        CHECK(encode(qr, VERSION_AUTO, 4, "HELLO") == 2);
    }

    free(qr.modules);
}

TEST_CASE("an explicit version keeps the ECC asked for")
{
    QRCode qr;
    REQUIRE(encode(qr, 1, ECC_QUARTILE, "HELLO WORLD") == 0);
    CHECK(qr.version == 1);
    CHECK(qr.ecc == ECC_QUARTILE);
    free(qr.modules);
}
