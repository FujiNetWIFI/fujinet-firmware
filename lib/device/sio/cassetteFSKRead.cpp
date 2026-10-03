#include "cassetteFSKRead.h"

#include <cstring>

size_t fsk_resilient_read(const FSKFileOps &f, size_t filesize, bool &suspect, FSKReadStats &st,
                          size_t off, uint8_t *dst, size_t n, unsigned attempts)
{
    uint8_t kept[FSK_READ_OVERLAP_BYTES];
    size_t kept_n = 0;
    for (unsigned a = 0; a < attempts; ++a)
    {
        if (suspect)
        {
            // Real absolute re-seek: away (outside any buffer window) and back.
            const size_t away = off >= 1024 ? 0 : off + 1024;
            (void)f.seek(f.ctx, away);
            ++st.resyncs;
            if (f.seek(f.ctx, off) != 0)
            {
                ++st.faults;
                if (f.stopping(f.ctx))
                    return 0;
                continue;
            }
        }
        else if (f.tell(f.ctx) != static_cast<long>(off))
        {
            if (f.seek(f.ctx, off) != 0)
            {
                suspect = true;
                ++st.faults;
                continue;
            }
        }

        const size_t r = f.read(f.ctx, dst, n);
        if (r == n)
        {
            if (kept_n != 0 && std::memcmp(kept, dst, kept_n) != 0)
            {
                // The position is proven wrong or the data unstable: hand nothing over.
                ++st.mismatches;
                suspect = true;
                return 0;
            }
            suspect = false;
            return n;
        }
        if (off + r >= filesize)
        {
            suspect = false;
            return r; // the true end of the file
        }

        // Short before the end of the file: a transport fault. What it did deliver is kept only to be
        // compared with the re-read.
        ++st.faults;
        suspect = true;
        if (kept_n == 0 && r > 0)
        {
            kept_n = r < sizeof(kept) ? r : sizeof(kept);
            std::memcpy(kept, dst, kept_n);
        }
        if (f.stopping(f.ctx))
            return 0;
    }
    return 0;
}
