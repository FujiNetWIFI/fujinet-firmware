#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "gcrypt.h"
#include "afp_random.h"
}

#include "mbedtls/version.h"   // pulls in the configuration on MbedTLS 2 and 3
#if defined(MBEDTLS_DES_C)
#include "mbedtls/des.h"
#endif

static bool all_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] != 0) return false;
    return true;
}

TEST_CASE("md5: RFC 1321 vector for \"abc\"")
{
    const uint8_t expected[16] = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
                                  0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};
    uint8_t digest[16] = {0};
    gcry_md_hash_buffer(GCRY_MD_MD5, digest, "abc", 3);
    CHECK(std::memcmp(digest, expected, 16) == 0);
    CHECK(gcry_md_get_algo_dlen(GCRY_MD_MD5) == 16);
}

// RFC 2144 appendix B.1, 128-bit key. With a zero IV the first CBC block is
// the plain block cipher.
static const uint8_t cast_key[16] = {0x01, 0x23, 0x45, 0x67, 0x12, 0x34, 0x56, 0x78,
                                     0x23, 0x45, 0x67, 0x89, 0x34, 0x56, 0x78, 0x9A};
static const uint8_t cast_plain[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
static const uint8_t cast_cipher[8] = {0x23, 0x8B, 0x4F, 0xE5, 0x84, 0x7E, 0x44, 0xB2};
static const uint8_t zero_iv[8] = {0};

static gcry_cipher_hd_t cast_open()
{
    gcry_cipher_hd_t h = nullptr;
    REQUIRE(gcry_cipher_open(&h, GCRY_CIPHER_CAST5, GCRY_CIPHER_MODE_CBC, 0) == GPG_ERR_NO_ERROR);
    REQUIRE(gcry_cipher_setkey(h, cast_key, sizeof(cast_key)) == GPG_ERR_NO_ERROR);
    REQUIRE(gcry_cipher_setiv(h, zero_iv, sizeof(zero_iv)) == GPG_ERR_NO_ERROR);
    return h;
}

TEST_CASE("cast5: RFC 2144 vector encrypts and decrypts")
{
    uint8_t out[8] = {0};
    gcry_cipher_hd_t h = cast_open();
    CHECK(gcry_cipher_encrypt(h, out, sizeof(out), cast_plain, sizeof(cast_plain)) == GPG_ERR_NO_ERROR);
    CHECK(std::memcmp(out, cast_cipher, 8) == 0);
    gcry_cipher_close(h);

    uint8_t back[8] = {0};
    h = cast_open();
    CHECK(gcry_cipher_decrypt(h, back, sizeof(back), cast_cipher, sizeof(cast_cipher)) == GPG_ERR_NO_ERROR);
    CHECK(std::memcmp(back, cast_plain, 8) == 0);
    gcry_cipher_close(h);
}

TEST_CASE("cast5: CBC chains blocks and round-trips in place")
{
    uint8_t buf[16];
    std::memcpy(buf, cast_plain, 8);
    std::memcpy(buf + 8, cast_plain, 8);

    gcry_cipher_hd_t h = cast_open();
    CHECK(gcry_cipher_encrypt(h, buf, sizeof(buf), nullptr, 0) == GPG_ERR_NO_ERROR);
    gcry_cipher_close(h);
    CHECK(std::memcmp(buf, cast_cipher, 8) == 0);
    CHECK(std::memcmp(buf, buf + 8, 8) != 0);

    h = cast_open();
    CHECK(gcry_cipher_decrypt(h, buf, sizeof(buf), nullptr, 0) == GPG_ERR_NO_ERROR);
    gcry_cipher_close(h);
    CHECK(std::memcmp(buf, cast_plain, 8) == 0);
    CHECK(std::memcmp(buf + 8, cast_plain, 8) == 0);
}

TEST_CASE("cast5: a length that is not a whole number of blocks is refused")
{
    uint8_t out[16] = {0};
    gcry_cipher_hd_t h = cast_open();
    CHECK(gcry_cipher_encrypt(h, out, 7, cast_plain, 7) != GPG_ERR_NO_ERROR);
    gcry_cipher_close(h);
}

// The classic "Now is t" single-DES vector; checked against OpenSSL.
static const uint8_t des_key[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
static const uint8_t des_plain[8] = {'N', 'o', 'w', ' ', 'i', 's', ' ', 't'};
static const uint8_t des_cipher[8] = {0x3F, 0xA4, 0x0E, 0x8A, 0x98, 0x4D, 0x48, 0x15};

static gcry_cipher_hd_t des_open(const uint8_t *key)
{
    gcry_cipher_hd_t h = nullptr;
    REQUIRE(gcry_cipher_open(&h, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0) == GPG_ERR_NO_ERROR);
    REQUIRE(h != nullptr);
    REQUIRE(gcry_cipher_setkey(h, key, 8) == GPG_ERR_NO_ERROR);
    return h;
}

TEST_CASE("des: published vector encrypts and decrypts")
{
    uint8_t out[8] = {0};
    gcry_cipher_hd_t h = des_open(des_key);
    CHECK(gcry_cipher_encrypt(h, out, sizeof(out), des_plain, sizeof(des_plain)) == GPG_ERR_NO_ERROR);
    CHECK(std::memcmp(out, des_cipher, 8) == 0);

    uint8_t back[8] = {0};
    CHECK(gcry_cipher_decrypt(h, back, sizeof(back), des_cipher, sizeof(des_cipher)) == GPG_ERR_NO_ERROR);
    CHECK(std::memcmp(back, des_plain, 8) == 0);
    gcry_cipher_close(h);
}

TEST_CASE("des: a second published vector, encrypted in place as the login code does")
{
    const uint8_t key[8] = {0x13, 0x34, 0x57, 0x79, 0x9B, 0xBC, 0xDF, 0xF1};
    const uint8_t expected[8] = {0x85, 0xE8, 0x13, 0x54, 0x0F, 0x0A, 0xB4, 0x05};
    uint8_t buf[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

    gcry_cipher_hd_t h = des_open(key);
    CHECK(gcry_cipher_encrypt(h, buf, sizeof(buf), nullptr, 0) == GPG_ERR_NO_ERROR);
    CHECK(std::memcmp(buf, expected, 8) == 0);
    gcry_cipher_close(h);
}

TEST_CASE("des: ECB does not chain blocks")
{
    uint8_t buf[16];
    std::memcpy(buf, des_plain, 8);
    std::memcpy(buf + 8, des_plain, 8);

    gcry_cipher_hd_t h = des_open(des_key);
    CHECK(gcry_cipher_encrypt(h, buf, sizeof(buf), nullptr, 0) == GPG_ERR_NO_ERROR);
    gcry_cipher_close(h);
    CHECK(std::memcmp(buf, des_cipher, 8) == 0);
    CHECK(std::memcmp(buf + 8, des_cipher, 8) == 0);
}

TEST_CASE("des: a key that is not eight bytes is refused")
{
    gcry_cipher_hd_t h = nullptr;
    REQUIRE(gcry_cipher_open(&h, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0) == GPG_ERR_NO_ERROR);
    CHECK(gcry_cipher_setkey(h, des_key, 7) != GPG_ERR_NO_ERROR);
    gcry_cipher_close(h);
}

#if defined(MBEDTLS_DES_C)
TEST_CASE("des: agrees with MbedTLS over many keys and blocks")
{
    uint32_t state = 0x1234abcd;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<uint8_t>(state >> 24);
    };

    for (int round = 0; round < 500; round++)
    {
        uint8_t key[8], plain[8], ours[8] = {0}, theirs[8] = {0};
        for (auto &b : key) b = next();
        for (auto &b : plain) b = next();

        gcry_cipher_hd_t h = des_open(key);
        REQUIRE(gcry_cipher_encrypt(h, ours, sizeof(ours), plain, sizeof(plain)) == GPG_ERR_NO_ERROR);
        gcry_cipher_close(h);

        mbedtls_des_context ctx;
        mbedtls_des_init(&ctx);
        REQUIRE(mbedtls_des_setkey_enc(&ctx, key) == 0);
        REQUIRE(mbedtls_des_crypt_ecb(&ctx, plain, theirs) == 0);
        mbedtls_des_free(&ctx);

        CAPTURE(round);
        REQUIRE(std::memcmp(ours, theirs, 8) == 0);
    }
}
#endif

TEST_CASE("cipher: a combination the shim does not implement is refused")
{
    gcry_cipher_hd_t h = nullptr;
    CHECK(gcry_cipher_open(&h, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_CBC, 0) != GPG_ERR_NO_ERROR);
    CHECK(h == nullptr);
    CHECK(gcry_cipher_open(&h, GCRY_CIPHER_CAST5, GCRY_CIPHER_MODE_ECB, 0) != GPG_ERR_NO_ERROR);
    CHECK(h == nullptr);
    CHECK(gcry_cipher_open(&h, 99, GCRY_CIPHER_MODE_CBC, 0) != GPG_ERR_NO_ERROR);
    CHECK(h == nullptr);
}

TEST_CASE("mpi: 4^13 mod 497 is 445")
{
    const uint8_t base[] = {0x04}, exp[] = {0x0d}, mod[] = {0x01, 0xf1};
    gcry_mpi_t b = nullptr, e = nullptr, m = nullptr;
    REQUIRE(gcry_mpi_scan(&b, GCRYMPI_FMT_USG, base, sizeof(base), nullptr) == GPG_ERR_NO_ERROR);
    REQUIRE(gcry_mpi_scan(&e, GCRYMPI_FMT_USG, exp, sizeof(exp), nullptr) == GPG_ERR_NO_ERROR);
    REQUIRE(gcry_mpi_scan(&m, GCRYMPI_FMT_USG, mod, sizeof(mod), nullptr) == GPG_ERR_NO_ERROR);
    gcry_mpi_t x = gcry_mpi_new(0);
    REQUIRE(x != nullptr);

    gcry_mpi_powm(x, b, e, m);

    uint8_t out[2] = {0};
    size_t written = 0;
    CHECK(gcry_mpi_print(GCRYMPI_FMT_USG, out, sizeof(out), &written, x) == GPG_ERR_NO_ERROR);
    CHECK(written == 2);
    CHECK(out[0] == 0x01);
    CHECK(out[1] == 0xbd);

    gcry_mpi_release(x);
    gcry_mpi_release(b);
    gcry_mpi_release(e);
    gcry_mpi_release(m);
}

TEST_CASE("mpi: print pads to the buffer, add_ui carries")
{
    const uint8_t ff[] = {0xff};
    gcry_mpi_t a = nullptr;
    REQUIRE(gcry_mpi_scan(&a, GCRYMPI_FMT_USG, ff, sizeof(ff), nullptr) == GPG_ERR_NO_ERROR);
    gcry_mpi_t sum = gcry_mpi_new(0);
    REQUIRE(sum != nullptr);
    gcry_mpi_add_ui(sum, a, 1);

    uint8_t out[4] = {0xee, 0xee, 0xee, 0xee};
    size_t written = 0;
    CHECK(gcry_mpi_print(GCRYMPI_FMT_USG, out, sizeof(out), &written, sum) == GPG_ERR_NO_ERROR);
    CHECK(written == 4);
    const uint8_t expected[4] = {0x00, 0x00, 0x01, 0x00};
    CHECK(std::memcmp(out, expected, 4) == 0);

    gcry_mpi_release(sum);
    gcry_mpi_release(a);
}

TEST_CASE("random: fills the buffer and does not repeat")
{
    uint8_t a[32] = {0}, b[32] = {0};
    afp_fill_random(a, sizeof(a));
    afp_fill_random(b, sizeof(b));
    CHECK_FALSE(all_zero(a, sizeof(a)));
    CHECK(std::memcmp(a, b, sizeof(a)) != 0);
}

TEST_CASE("random: a request larger than one generator call is filled to the end")
{
    std::vector<uint8_t> big(4096, 0);
    afp_fill_random(big.data(), big.size());
    CHECK_FALSE(all_zero(big.data(), 64));
    CHECK_FALSE(all_zero(big.data() + 2048, 64));
    CHECK_FALSE(all_zero(big.data() + big.size() - 64, 64));
}

TEST_CASE("random: the gcrypt entry points use it")
{
    uint8_t a[16] = {0}, b[16] = {0};
    gcry_randomize(a, sizeof(a), GCRY_STRONG_RANDOM);
    gcry_create_nonce(b, sizeof(b));
    CHECK_FALSE(all_zero(a, sizeof(a)));
    CHECK_FALSE(all_zero(b, sizeof(b)));
}
