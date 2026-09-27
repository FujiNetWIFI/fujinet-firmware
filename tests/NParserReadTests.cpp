#include <doctest/doctest.h>

#include "NParser.h"

// Built into nquery_output_mode_tests, which provides main() and the link stubs.

namespace {

// Leaves a fixed chunk in the receive buffer on every read, whatever length was
// asked for, the way translation leaves fewer bytes than it read (CR/LF -> EOL).
class ChunkProtocol : public NetworkProtocol
{
public:
    ChunkProtocol(std::string *rx, std::string *tx, std::string *sp, const std::string &chunk)
        : NetworkProtocol(rx, tx, sp), _chunk(chunk)
    {
        receiveBuffer = rx;
        transmitBuffer = tx;
    }

    fujiError_t read(unsigned short) override
    {
        receiveBuffer->append(_chunk);
        return FUJI_ERROR::NONE;
    }

    fujiError_t status(NetworkStatus *) override { return FUJI_ERROR::NONE; }
    size_t available() override { return receiveBuffer->size(); }

private:
    std::string _chunk;
};

} // namespace

TEST_CASE("a short receive buffer yields only the bytes it holds")
{
    // Stale bytes past the end of the buffer, which a read must not hand out.
    std::string rx = "XXXXXXXXXX", tx, sp;
    rx.clear();
    ChunkProtocol proto(&rx, &tx, &sp, "A\x9b");
    NParser parser(&proto);

    std::string out;
    REQUIRE(parser.read(out, 10) == FUJI_ERROR::NONE);
    CHECK(out == "A\x9b");
    CHECK(rx.empty());
}

TEST_CASE("bytes beyond the length asked for stay for the next read")
{
    std::string rx, tx, sp;
    ChunkProtocol proto(&rx, &tx, &sp, "ABCDE");
    NParser parser(&proto);

    std::string out;
    REQUIRE(parser.read(out, 3) == FUJI_ERROR::NONE);
    CHECK(out == "ABC");
    CHECK(rx == "DE");
}
