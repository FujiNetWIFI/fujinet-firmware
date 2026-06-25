// Standalone end-to-end exerciser for the FujiNet TNFS client over TCP.
//
// It links the REAL (fixed) tnfslib + fnTcpClient code and drives a full
// session: mount -> opendirx/readdirx (whole dir) -> open + read the largest
// file in full (checksummed) -> close -> umount. It prints the negotiated
// transport and a PASS/FAIL verdict per server.
//
// Point it at a real server, or (for deterministic stress) at the fragmenting
// TCP proxy in front of a local tnfsd, which chops the server->client stream so
// every response arrives in pieces -- the exact condition the framing fix
// handles.
//
//   usage: tnfs_tcp_test [host port [host port ...]]   (default 127.0.0.1 16384)

#include "tnfslib.h"
#include "tnfslibMountInfo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char *protoname(uint8_t p)
{
    return p == TNFS_PROTOCOL_TCP ? "TCP" : p == TNFS_PROTOCOL_UDP ? "UDP" : "UNKNOWN";
}

static int run_one(const char *host, uint16_t port)
{
    printf("\n========== TNFS test: %s:%u ==========\n", host, port);
    tnfsMountInfo m(host, port);

    int r = tnfs_mount(&m);
    if (r != 0)
    {
        printf(">> MOUNT FAILED rc=%d\n>> RESULT: FAIL\n", r);
        return 1;
    }
    printf(">> mounted: session=0x%04x server_version=0x%04x transport=%s\n",
           m.session, m.server_version, protoname(m.protocol));
    if (m.protocol != TNFS_PROTOCOL_TCP)
        printf(">> NOTE: not using TCP; the framing fix only affects the TCP path\n");

    // --- read the whole root directory (exercises READDIRX variable framing) ---
    r = tnfs_opendirx(&m, "/", 0, 0, nullptr, 0);
    if (r != 0)
    {
        printf(">> OPENDIRX FAILED rc=%d\n>> RESULT: FAIL\n", r);
        tnfs_umount(&m);
        return 1;
    }
    char name[256];
    tnfsStat st;
    std::string biggest;
    uint32_t biggest_sz = 0;
    int count = 0;
    while (true)
    {
        r = tnfs_readdirx(&m, &st, name, sizeof(name));
        if (r == TNFS_RESULT_END_OF_FILE)
            break;
        if (r != 0)
        {
            printf(">> READDIRX FAILED rc=%d after %d entries\n>> RESULT: FAIL\n", r, count);
            tnfs_closedir(&m);
            tnfs_umount(&m);
            return 1;
        }
        count++;
        if (!st.isDir && st.filesize > biggest_sz)
        {
            biggest_sz = st.filesize;
            biggest = name;
        }
        if (count <= 25)
            printf("     %-32s %-6s %u\n", name, st.isDir ? "<dir>" : "file", st.filesize);
    }
    printf(">> directory entries: %d; largest file: '%s' (%u bytes)\n",
           count, biggest.c_str(), biggest_sz);
    tnfs_closedir(&m);

    // --- open + read the largest file in full (exercises READ framing) ---
    if (!biggest.empty())
    {
        std::string path = std::string("/") + biggest;
        int16_t fh = TNFS_INVALID_HANDLE;
        r = tnfs_open(&m, path.c_str(), TNFS_OPENMODE_READ, 0, &fh);
        if (r != 0)
        {
            printf(">> OPEN '%s' FAILED rc=%d\n>> RESULT: FAIL\n", path.c_str(), r);
            tnfs_umount(&m);
            return 1;
        }
        uint32_t total = 0, sum = 0;
        uint8_t buf[512];
        uint16_t got = 0;
        bool ok = true;
        while (true)
        {
            r = tnfs_read(&m, fh, buf, sizeof(buf), &got);
            if (r == TNFS_RESULT_END_OF_FILE || (r == 0 && got == 0))
                break;
            if (r != 0)
            {
                printf(">> READ FAILED rc=%d at offset %u\n", r, total);
                ok = false;
                break;
            }
            for (uint16_t i = 0; i < got; i++)
                sum = (sum + buf[i]) & 0xFFFFFF;
            total += got;
        }
        tnfs_close(&m, fh);
        printf(">> read '%s': %u/%u bytes, checksum=0x%06x %s\n",
               biggest.c_str(), total, biggest_sz, sum,
               (ok && total == biggest_sz) ? "[SIZE OK]" : "[SIZE MISMATCH]");
        if (!ok || total != biggest_sz)
        {
            printf(">> RESULT: FAIL\n");
            tnfs_umount(&m);
            return 1;
        }
    }

    tnfs_umount(&m);
    printf(">> RESULT: PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3)
    {
        int fails = 0;
        for (int i = 1; i + 1 < argc; i += 2)
            fails += run_one(argv[i], (uint16_t)atoi(argv[i + 1]));
        printf("\n========== %s ==========\n", fails ? "OVERALL: FAIL" : "OVERALL: PASS");
        return fails ? 1 : 0;
    }
    int rc = run_one("127.0.0.1", 16384);
    printf("\n========== %s ==========\n", rc ? "OVERALL: FAIL" : "OVERALL: PASS");
    return rc;
}
