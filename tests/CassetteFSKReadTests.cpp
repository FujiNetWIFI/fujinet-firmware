#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>

#include "FSKTestImages.h"
#include "device/sio/cassetteFSKRead.h"

using namespace fsktest;

// ------------------------------------------------------------------------------------------------
// A cursor-based remote file. A remote READ carries no offset ("read from where the server is"), and
// stdio answers fseek() to the position it already believes it is at without touching the file. A lost
// reply therefore leaves the server ahead of the client: the next sequential read returns bytes from
// the wrong place and nothing reports it.
// ------------------------------------------------------------------------------------------------

namespace
{

struct CursorFile
{
    const std::vector<uint8_t> *bytes = nullptr;
    size_t server = 0;   // where the server reads next
    size_t believed = 0; // where the client thinks the stream is
    uint32_t reads = 0;
    uint32_t real_seeks = 0;

    // A lost reply: the server executed the whole request, the client got `delivered` bytes (the correct
    // ones from the old position, or garbage) and believes it advanced by that much.
    std::function<bool(uint32_t)> lose_reply;
    size_t delivered = 0;
    size_t garbage_from = SIZE_MAX; // delivered bytes from this index on are wrong

    long tell() const { return static_cast<long>(believed); }

    int seek(size_t off)
    {
        if (off == believed)
            return 0; // answered without touching the file
        server = believed = off;
        ++real_seeks;
        return 0;
    }

    size_t read(uint8_t *dst, size_t n)
    {
        const uint32_t no = reads++;
        const size_t avail = server < bytes->size() ? std::min(n, bytes->size() - server) : 0;
        if (lose_reply && lose_reply(no))
        {
            const size_t d = std::min(delivered, avail);
            for (size_t i = 0; i < d; ++i)
                dst[i] = i >= garbage_from ? static_cast<uint8_t>(~(*bytes)[server + i]) : (*bytes)[server + i];
            server += avail;
            believed += d;
            return d;
        }
        std::memcpy(dst, bytes->data() + server, avail);
        server += avail;
        believed += avail;
        return avail;
    }
};

long op_tell(void *ctx) { return static_cast<CursorFile *>(ctx)->tell(); }
int op_seek(void *ctx, size_t off) { return static_cast<CursorFile *>(ctx)->seek(off); }
size_t op_read(void *ctx, uint8_t *dst, size_t n) { return static_cast<CursorFile *>(ctx)->read(dst, n); }

struct Remote
{
    CursorFile file;
    bool suspect = false;
    FSKReadStats stats;
    size_t filesize = 0;

    explicit Remote(const Image &img)
    {
        file.bytes = &img.bytes;
        filesize = img.bytes.size();
    }

    FSKFileOps ops() { return {&file, &op_tell, &op_seek, &op_read, [](void *) { return false; }}; }
};

// What the loader's read callback did before it was made resilient: seek, then read.
size_t naive_read(void *ctx, size_t off, uint8_t *dst, size_t n)
{
    Remote *r = static_cast<Remote *>(ctx);
    r->file.seek(off);
    return r->file.read(dst, n);
}

size_t resilient_read(void *ctx, size_t off, uint8_t *dst, size_t n)
{
    Remote *r = static_cast<Remote *>(ctx);
    return fsk_resilient_read(r->ops(), r->filesize, r->suspect, r->stats, off, dst, n);
}

// Number of reads a clean sequential load of `img` takes.
uint32_t clean_read_count(const Image &img)
{
    Remote clean(img);
    Rig rig(img, resilient_read, &clean);
    bool ok = false;
    rig.run_all(ok);
    return clean.file.reads;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The field failure, with and without the resilient read
// ------------------------------------------------------------------------------------------------

TEST_CASE("a lost reply shifts the stream when the read is not resilient")
{
    const Image img = make_image(4, 2000, 3);
    Remote remote(img);
    remote.file.lose_reply = [](uint32_t no) { return no == 3; };
    remote.file.delivered = 488; // a positive short read, as seen on the device
    Rig rig(img, naive_read, &remote);

    bool ok = false;
    rig.run_all(ok);
    CHECK_FALSE(ok); // wrong values were published, and nothing reported it
}

TEST_CASE("the same lost reply with the resilient read completes the run byte for byte")
{
    const Image img = make_image(4, 2000, 3);
    Remote remote(img);
    remote.file.lose_reply = [](uint32_t no) { return no == 3; };
    remote.file.delivered = 488;
    Rig rig(img, resilient_read, &remote);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok);
    CHECK(rig.run.pub_values == img.run_values.size());
    CHECK(rig.run.next_offset == img.run_end);
    CHECK(remote.stats.faults == 1);
    CHECK(remote.stats.resyncs == 1);
    CHECK(remote.stats.mismatches == 0);
    CHECK_FALSE(remote.suspect);
}

TEST_CASE("a clean sequential load never re-seeks")
{
    const Image img = make_image(3, 1500, 7);
    Remote remote(img);
    Rig rig(img, resilient_read, &remote);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(remote.file.real_seeks == 1); // the first read positions the stream
    CHECK(remote.stats.faults == 0);
    CHECK(remote.stats.resyncs == 0);
}

TEST_CASE("a lost reply at any read, whatever it delivered, never corrupts or ends the run")
{
    const Image img = make_image(3, 1200, 17);
    const uint32_t total = clean_read_count(img);
    REQUIRE(total > 10);

    for (size_t delivered : {size_t{0}, size_t{1}, size_t{100}, size_t{488}})
    {
        for (uint32_t k = 0; k < total; ++k)
        {
            CAPTURE(delivered);
            CAPTURE(k);
            Remote remote(img);
            remote.file.lose_reply = [k](uint32_t no) { return no == k; };
            remote.file.delivered = delivered;
            Rig rig(img, resilient_read, &remote);

            bool ok = false;
            CHECK(rig.run_all(ok) == FSKStep::done);
            CHECK(ok);
            CHECK(rig.L.faults == 0); // repaired inside the read, invisible to the producer
            CHECK(rig.run.pub_values == img.run_values.size());
            CHECK(rig.run.next_offset == img.run_end);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// fsk_resilient_read on its own
// ------------------------------------------------------------------------------------------------

TEST_CASE("a short read at the true end of the file is not a fault")
{
    const Image img = make_image(1, 50, 1);
    Remote remote(img);
    FSKFileOps ops = remote.ops();

    uint8_t buf[64];
    const size_t off = img.bytes.size() - 10;
    CHECK(fsk_resilient_read(ops, remote.filesize, remote.suspect, remote.stats, off, buf, sizeof(buf)) == 10);
    CHECK(remote.stats.faults == 0);
    CHECK_FALSE(remote.suspect);
    CHECK(std::memcmp(buf, img.bytes.data() + off, 10) == 0);
}

TEST_CASE("a re-read that disagrees with what the faulty attempt delivered is refused")
{
    const Image img = make_image(2, 500, 2);
    Remote remote(img);
    remote.file.lose_reply = [](uint32_t no) { return no == 0; };
    remote.file.delivered = 40;
    remote.file.garbage_from = 0; // the faulty attempt handed over wrong bytes
    FSKFileOps ops = remote.ops();

    uint8_t buf[200];
    CHECK(fsk_resilient_read(ops, remote.filesize, remote.suspect, remote.stats, 100, buf, sizeof(buf)) == 0);
    CHECK(remote.stats.mismatches == 1);
    CHECK(remote.suspect);
}

TEST_CASE("only the first bytes of a faulty attempt are compared, up to the validated limit")
{
    const Image img = make_image(2, 500, 2);
    Remote remote(img);
    // the faulty attempt delivers more than the limit, and its bytes past the limit are wrong
    remote.file.lose_reply = [](uint32_t no) { return no == 0; };
    remote.file.delivered = FSK_READ_OVERLAP_BYTES + 100;
    remote.file.garbage_from = FSK_READ_OVERLAP_BYTES;
    FSKFileOps ops = remote.ops();

    uint8_t buf[300];
    CHECK(fsk_resilient_read(ops, remote.filesize, remote.suspect, remote.stats, 100, buf, sizeof(buf)) == 300);
    CHECK(std::memcmp(buf, img.bytes.data() + 100, 300) == 0);
    CHECK(FSK_READ_OVERLAP_BYTES == 64);
}

TEST_CASE("attempts are bounded, and a request to stop ends them early")
{
    const Image img = make_image(2, 500, 2);

    Remote persistent(img);
    persistent.file.lose_reply = [](uint32_t) { return true; };
    persistent.file.delivered = 0;
    FSKFileOps ops = persistent.ops();
    uint8_t buf[100];
    CHECK(fsk_resilient_read(ops, persistent.filesize, persistent.suspect, persistent.stats, 100, buf, sizeof(buf)) == 0);
    CHECK(persistent.stats.faults == FSK_READ_ATTEMPTS);
    CHECK(persistent.suspect);

    Remote stopped(img);
    stopped.file.lose_reply = [](uint32_t) { return true; };
    stopped.file.delivered = 0;
    ops = stopped.ops();
    ops.stopping = [](void *) { return true; };
    CHECK(fsk_resilient_read(ops, stopped.filesize, stopped.suspect, stopped.stats, 100, buf, sizeof(buf)) == 0);
    CHECK(stopped.stats.faults == 1);
}

// ------------------------------------------------------------------------------------------------
// Loader fault budget and restart over a failing remote
// ------------------------------------------------------------------------------------------------

TEST_CASE("a persistently failing remote fails the run, and a restart continues from the confirmed cursor")
{
    const Image img = make_image(3, 1200, 27);
    Remote remote(img);
    bool healed = false;
    remote.file.lose_reply = [&healed](uint32_t no) { return !healed && no >= 8; };
    remote.file.delivered = 0;
    Rig rig(img, resilient_read, &remote);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    CHECK(ok);
    CHECK(rig.L.faults == FSKRunLoader::FAULT_BUDGET + 1);
    const uint32_t published = rig.run.pub_values;
    CHECK(published > 0);
    CHECK(published < img.run_values.size());

    healed = true;
    remote.suspect = true; // the position is unknown after a restart
    fsk_loader_recover(rig.L);
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok);
    CHECK(rig.run.pub_values == img.run_values.size());
    CHECK(rig.run.next_offset == img.run_end);
    CHECK(rig.run.total_ticks == total_ticks(oracle_portions(img.run_values, SIZE_MAX)));
}
