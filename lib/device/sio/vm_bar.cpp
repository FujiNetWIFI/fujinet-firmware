// vm_bar.cpp — RSX-style "bar command" gate between RunCPM and FujiNet.
//
// FEATURE (RunCPM RSX / Amstrad-CPC "bar commands"):
//   At the CP/M prompt, a line whose first non-space character is '|' is not a
//   CP/M command — RunCPM's CCP (lib/runcpm/ccp.h) routes the rest of the line
//   to vm_bar_command() below.  This lets the Atari trigger host-side FujiNet
//   actions (download a file, etc.) from inside CP/M without a .COM program and
//   without leaving the session.
//
//   Mechanism / trust boundary:
//     - The CCP hook is the ONLY caller.  It passes a plain NUL-terminated
//       command string (a host pointer into the Z80 RAM array, read-only here),
//       the destination directory prefix the calling RunCPM instance uses for
//       its CP/M files (already ending in '/'), and an output buffer.
//     - This gate performs the action and writes back a short status string;
//       the CCP prints it.  The gate never touches Z80 RAM, the console queues,
//       or RunCPM's Status flag, so the same code serves all three RunCPM
//       instances (SIO 'G'/R:, N:CPM://, telnet console).
//     - |wget fetches arbitrary user-supplied URLs into the SD /CPM area.  This
//       is intentional: FujiNet is a network peripheral and the user issuing
//       the command is the operator of the machine.
//
//   New verbs are added by extending the kVerbs[] dispatch table.

#ifdef BUILD_ATARI

#include "vm_bar.h"

#include "../../include/debug.h"

#include "fnFsSD.h"
#include "fnSystem.h"
#include "peoples_url_parser.h"

#include "fnConfig.h" // Config: read-only host/mount slot accessors for |fn mounts

#include "fnFTP.h" // existing FujiNet FTP client, reused by |ftp (no new stack)

#include "utils.h" // util_sam_say(): SAM text-to-speech out the SIO AUDIO line, used by |say

// NOTE: the HTTP client header MUST come after the FujiNet bus headers above.
// On Windows/FujiNet-PC, mgHttpClient.h pulls in mongoose.h, which does
// `#define poll(a,b,c) WSAPoll(...)`.  bus headers (fnSystem.h -> bus.h ->
// NetSIO.h) declare a method `bool poll(int ms);` — if mongoose's macro is
// already active, that declaration fails with "too few arguments provided to
// function-like macro invocation".  Including the bus headers first lets
// NetSIO.h parse cleanly before the macro is defined.
#ifdef ESP_PLATFORM
#include "fnHttpClient.h"
#define HTTP_CLIENT_CLASS fnHttpClient
#else
#include "mgHttpClient.h"
#define HTTP_CLIENT_CLASS mgHttpClient
#endif

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h" // heap_caps_malloc(MALLOC_CAP_DMA): see do_wget SD buffer
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Skip leading spaces/tabs.
const char *skip_spaces(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

// Copy the next whitespace-delimited token from *pp into tok (NUL-terminated,
// at most toksize-1 chars).  Advances *pp past the token.  Returns the token
// length (0 if no token remains).
int next_token(const char **pp, char *tok, int toksize)
{
    const char *p = skip_spaces(*pp);
    int n = 0;
    while (*p && *p != ' ' && *p != '\t')
    {
        if (n < toksize - 1)
            tok[n] = *p;
        ++n;
        ++p;
    }
    if (n > toksize - 1)
        n = toksize - 1;
    tok[n] = '\0';
    *pp = p;
    return n;
}

// Build an uppercase CP/M 8.3 filename (NAME.EXT, name<=8, ext<=3) from a
// source filename, dropping path separators and characters illegal in a CP/M
// FCB.  Returns false if nothing usable remains.
bool to_cpm_83(const char *src, char *out, int outsize)
{
    // Keep only the basename (strip any path the URL parser left in).
    const char *base = src;
    for (const char *q = src; *q; ++q)
        if (*q == '/' || *q == '\\' || *q == ':')
            base = q + 1;

    char name[9] = {0};
    char ext[4] = {0};
    int ni = 0, ei = 0;
    bool in_ext = false;

    for (const char *q = base; *q; ++q)
    {
        char c = *q;
        if (c == '.')
        {
            // Last dot starts the extension; treat earlier dots as name chars
            // by restarting the extension capture.
            in_ext = true;
            ei = 0;
            continue;
        }
        // CP/M FCB illegal characters; replace with '_' rather than drop so the
        // result stays a single recognisable token.
        if (c <= ' ' || strchr("<>.,;:=?*[]/\\|", c))
            c = '_';
        c = (char)toupper((unsigned char)c);
        if (!in_ext)
        {
            if (ni < 8)
                name[ni++] = c;
        }
        else
        {
            if (ei < 3)
                ext[ei++] = c;
        }
    }

    if (ni == 0)
        return false;

    int w = 0;
    for (int i = 0; i < ni && w < outsize - 1; ++i)
        out[w++] = name[i];
    if (ei > 0 && w < outsize - 1)
    {
        out[w++] = '.';
        for (int i = 0; i < ei && w < outsize - 1; ++i)
            out[w++] = ext[i];
    }
    out[w] = '\0';
    return w > 0;
}

// Write a buffer to f converting line endings to CP/M's CRLF convention.  Any
// of LF (Unix), CR (classic Mac), or CRLF (DOS) in the input is normalised to a
// single CRLF, so files fetched from Unix hosts display correctly under CP/M
// TYPE (which expects CR before each LF).  *pending_cr carries a CR seen at the
// very end of the previous buffer, whose matching LF may begin this one (CRLF
// can straddle the 1 KB read boundary).  Returns the number of bytes written,
// or -1 on a write error.
long write_text(FILE *f, const uint8_t *buf, int len, bool *pending_cr)
{
    long w = 0;
    for (int i = 0; i < len; ++i)
    {
        uint8_t c = buf[i];
        if (*pending_cr)
        {
            // A CR was buffered last time; emit the CRLF it represents now.
            if (fputc('\r', f) == EOF || fputc('\n', f) == EOF)
                return -1;
            w += 2;
            *pending_cr = false;
            if (c == '\n')
                continue; // CRLF pair: this LF was already accounted for
            if (c == '\r')
            {
                *pending_cr = true; // another bare CR; decide on the next byte
                continue;
            }
            if (fputc(c, f) == EOF)
                return -1;
            ++w;
        }
        else if (c == '\r')
        {
            *pending_cr = true; // could be a lone CR or the CR of a CRLF
        }
        else if (c == '\n')
        {
            if (fputc('\r', f) == EOF || fputc('\n', f) == EOF)
                return -1;
            w += 2; // bare LF -> CRLF
        }
        else
        {
            if (fputc(c, f) == EOF)
                return -1;
            ++w;
        }
    }
    return w;
}

// Pad a freshly-written CP/M file up to the next 128-byte record boundary with
// the soft-EOF byte 0x1A (^Z).  RunCPM's disk layer (_sys_readseq in the
// abstraction) reads whole 128-byte records only, so a file whose length is not
// a multiple of 128 would have its final partial record dropped, and a
// zero-length file would read as immediate EOF.  This is the single source of
// truth for that convention: both |wget and |ftp call it so their on-disk
// layout is identical.  128 == RunCPM BlkSZ.  Returns bytes padded, or -1 on a
// write error.
long pad_cpm_record(FILE *f, long written)
{
    long pad = (128 - (written % 128)) % 128;
    if (written == 0)
        pad = 128; // never leave a zero-length (unreadable) CP/M file
    for (long i = 0; i < pad; ++i)
        if (fputc(0x1A, f) == EOF)
            return -1;
    return pad;
}

// ---------------------------------------------------------------------------
// Verb: wget
// ---------------------------------------------------------------------------

int do_wget(const char *args, const char *dir_prefix, char *outmsg,
            int outmsg_size, const vm_bar_io *io)
{
    (void)io; // wget is non-interactive

    // Options precede the URL: any whitespace-delimited token starting with '-'.
    // Currently only -a (ASCII/text mode: convert line endings to CP/M CRLF).
    bool ascii = false;
    char url[256];
    url[0] = '\0';
    for (;;)
    {
        char t[256];
        if (next_token(&args, t, sizeof(t)) == 0)
            break; // ran out of tokens without finding a URL
        if (t[0] == '-' && t[1] != '\0')
        {
            for (const char *o = t + 1; *o; ++o)
            {
                if (*o == 'a' || *o == 'A')
                    ascii = true;
                else
                {
                    snprintf(outmsg, outmsg_size, "wget: unknown option -%c\r\n", *o);
                    return 1;
                }
            }
            continue; // keep scanning for more options / the URL
        }
        snprintf(url, sizeof(url), "%s", t); // first non-option token = URL
        break;
    }
    if (url[0] == '\0')
    {
        snprintf(outmsg, outmsg_size, "wget: usage: |wget [-a] URL [DEST]\r\n");
        return 1;
    }

    char destarg[64];
    next_token(&args, destarg, sizeof(destarg)); // optional explicit destination

    // Decide the CP/M 8.3 destination filename.
    char dest83[16];
    if (destarg[0] != '\0')
    {
        if (!to_cpm_83(destarg, dest83, sizeof(dest83)))
        {
            snprintf(outmsg, outmsg_size, "wget: bad dest name\r\n");
            return 1;
        }
    }
    else
    {
        std::unique_ptr<PeoplesUrlParser> up = PeoplesUrlParser::parseURL(url);
        const std::string &nm = up ? up->name : std::string();
        if (nm.empty() || !to_cpm_83(nm.c_str(), dest83, sizeof(dest83)))
        {
            snprintf(outmsg, outmsg_size, "wget: cannot derive name, give DEST\r\n");
            return 1;
        }
    }

    // Full host path = directory prefix (ends in '/') + 8.3 name.
    char path[160];
    snprintf(path, sizeof(path), "%s%s", dir_prefix, dest83);

    // Ensure the CP/M drive/user directory exists (create_path wants the
    // directory path without a trailing slash).
    char dir[160];
    snprintf(dir, sizeof(dir), "%s", dir_prefix);
    size_t dlen = strlen(dir);
    if (dlen > 1 && dir[dlen - 1] == '/')
        dir[dlen - 1] = '\0';
    fnSDFAT.create_path(dir);

    FILE *f = fnSDFAT.file_open(path, "w");
    if (!f)
    {
        snprintf(outmsg, outmsg_size, "wget: open failed: %s\r\n", dest83);
        return 1;
    }

    // ------------------------------------------------------------------------
    // BUG: |wget over the loopback telnet console (vm_telnet.cpp) failed with
    //      "wget: read error after 0 bytes", preceded in the ESP32 log by:
    //          E dma_utils: esp_dma_capable_malloc(125): Not enough heap memory
    //          E diskio_sdmmc: sdmmc_write_blocks failed (0x101)
    //      i.e. the HTTP GET and the SD file_open both succeeded, but the very
    //      first fwrite() to the SD card failed.
    //
    // ROOT CAUSE: this gate runs on the "cpmtel" worker task, whose 32 KB stack
    //      is allocated from INTERNAL DRAM — which on the ESP32 is the same
    //      region the SDMMC/SDSPI driver needs for DMA.  Between that stack,
    //      lwIP, and the live fnHttpClient (~3 KB internal), DMA-capable
    //      internal RAM is down to a few KB during the transfer.  newlib
    //      allocates the FILE's stdio buffer lazily on the first write; by then
    //      internal RAM is exhausted, so it falls back to a PSRAM buffer.  The
    //      SD driver cannot DMA straight from PSRAM, so it tries to allocate an
    //      internal bounce buffer (esp_dma_capable_malloc) — which also fails
    //      because internal RAM is gone — and the block write aborts.
    //
    // FIX: give the FILE a DMA-capable internal stdio buffer up front, here —
    //      right after open() and BEFORE the HTTP client grabs internal RAM,
    //      when a few KB of internal heap is still free.  With the stdio buffer
    //      already in DMA-capable memory the SD driver writes from it directly
    //      and never needs a bounce buffer.  If the small allocation fails we
    //      fall back to default buffering (no regression vs. the old path).
    //      ESP-only: the PC/Mongoose build has no DMA constraint.
    char *sd_iobuf = nullptr;
#ifdef ESP_PLATFORM
    const size_t kSdIoBufSize = 2048; // 4 SD sectors; keeps DMA footprint small
    sd_iobuf = (char *)heap_caps_malloc(kSdIoBufSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (sd_iobuf != nullptr)
        setvbuf(f, sd_iobuf, _IOFBF, kSdIoBufSize);
#endif

    HTTP_CLIENT_CLASS http;
    if (!http.begin(std::string(url)))
    {
        fclose(f);
        free(sd_iobuf); // safe after fclose; free(nullptr) is a no-op
        fnSDFAT.remove(path);
        snprintf(outmsg, outmsg_size, "wget: begin failed\r\n");
        return 1;
    }

    int code = http.GET();
    if (code < 200 || code >= 300)
    {
        http.close();
        fclose(f);
        free(sd_iobuf); // safe after fclose; free(nullptr) is a no-op
        fnSDFAT.remove(path);
        snprintf(outmsg, outmsg_size, "wget: HTTP %d\r\n", code);
        return 1;
    }

    // Drain the response body to the file (mirrors lib/network-protocol/HTTP.cpp).
    // total   = raw bytes received from the server.
    // written = bytes actually placed in the file (differs from total in ASCII
    //           mode, where each line ending may grow from 1 byte to 2).
    uint8_t buf[1024];
    long total = 0;
    long written = 0;
    bool pending_cr = false; // ASCII mode: a CR awaiting its possible LF
    bool ok = true;
    while (!http.is_transaction_done() || http.available() > 0)
    {
        int len = http.available();
        if (len > 0)
        {
            if (len > (int)sizeof(buf))
                len = (int)sizeof(buf);
            int got = http.read(buf, len);
            if (got <= 0)
            {
                ok = false;
                break;
            }
            if (ascii)
            {
                long w = write_text(f, buf, got, &pending_cr);
                if (w < 0)
                {
                    ok = false;
                    break;
                }
                written += w;
            }
            else
            {
                if ((int)fwrite(buf, 1, got, f) != got)
                {
                    ok = false;
                    break;
                }
                written += got;
            }
            total += got;
        }
        else if (len == 0)
        {
            fnSystem.delay(10); // wait for more body data
        }
        else // len < 0
        {
            ok = false;
            break;
        }
    }

    http.close();

    // ASCII mode: a stream ending on a bare CR leaves a pending CRLF to emit.
    if (ok && ascii && pending_cr)
    {
        if (fputc('\r', f) == EOF || fputc('\n', f) == EOF)
            ok = false;
        else
            written += 2;
    }

    // Pad up to the next 128-byte CP/M record boundary so TYPE and other tools
    // can read back exactly what was written (see pad_cpm_record).
    if (ok && pad_cpm_record(f, written) < 0)
        ok = false;

    fclose(f);
    free(sd_iobuf); // safe after fclose; free(nullptr) is a no-op

    if (!ok)
    {
        fnSDFAT.remove(path);
        snprintf(outmsg, outmsg_size, "wget: read error after %ld bytes\r\n", total);
        return 1;
    }

    snprintf(outmsg, outmsg_size, "Saved %s (%ld bytes)\r\n", dest83, written);
    return 0;
}

// ---------------------------------------------------------------------------
// Verb: fn  (FujiNet info subcommands)
// ---------------------------------------------------------------------------

// Append src to outmsg at *off, never exceeding outmsg_size.  Returns false
// (and stops the caller) once the buffer is full so a long listing degrades
// gracefully instead of overflowing.
bool append_msg(char *outmsg, int outmsg_size, int *off, const char *src)
{
    int avail = outmsg_size - *off;
    if (avail <= 1)
        return false;
    int n = snprintf(outmsg + *off, avail, "%s", src);
    if (n < 0)
        return false;
    if (n >= avail)
    {
        *off = outmsg_size - 1; // truncated; no room for more
        return false;
    }
    *off += n;
    return true;
}

// |fn mounts — read-only listing of the FujiNet drive slots D1-D8, mirroring
// the host:path (mode) column of the web/config UI (httpServiceBrowser.cpp
// browse_listdrives()).  Uses only Config getters, so it performs no mount,
// eject, write, reconnect, WiFi, SD-card, or network side effects.
int do_fn_mounts(char *outmsg, int outmsg_size)
{
    outmsg[0] = '\0';
    int off = 0;

    for (int slot = 0; slot < MAX_MOUNT_SLOTS && slot < 8; ++slot)
    {
        int host_slot = Config.get_mount_host_slot(slot);
        std::string path = Config.get_mount_path(slot);

        char line[320];
        if (host_slot == HOST_SLOT_INVALID && path.empty())
        {
            // Nothing configured for this slot.
            snprintf(line, sizeof(line), "D%d: <empty>\r\n", slot + 1);
        }
        else
        {
            // Host name from the referenced host slot; fall back safely if the
            // slot index is invalid/unknown but a path is still present.
            std::string host = (host_slot != HOST_SLOT_INVALID)
                                   ? Config.get_host_name((uint8_t)host_slot)
                                   : std::string();
            const char *hoststr = host.empty() ? "<unknown-host>" : host.c_str();

            fnConfig::mount_mode_t m = Config.get_mount_mode(slot);
            const char *modestr =
                (m == fnConfig::mount_modes::MOUNTMODE_READ)    ? "R"
                : (m == fnConfig::mount_modes::MOUNTMODE_WRITE) ? "W"
                                                                : "?";

            snprintf(line, sizeof(line), "D%d: %s:%s (%s)\r\n",
                     slot + 1, hoststr, path.c_str(), modestr);
        }

        if (!append_msg(outmsg, outmsg_size, &off, line))
            break;
    }
    return 0;
}

int do_fn(const char *args, const char *dir_prefix, char *outmsg,
          int outmsg_size, const vm_bar_io *io)
{
    (void)dir_prefix;
    (void)io; // fn is non-interactive

    char sub[16];
    next_token(&args, sub, sizeof(sub));
    for (char *s = sub; *s; ++s)
        *s = (char)tolower((unsigned char)*s);

    // No subcommand, or an explicit help request -> list the |fn subcommands.
    if (sub[0] == '\0' || strcmp(sub, "help") == 0 || strcmp(sub, "--help") == 0)
    {
        snprintf(outmsg, outmsg_size,
                 "FNC Bar Commands:\r\n"
                 "|fn mounts - show mounted FujiNet drive slots D1-D8\r\n"
                 "|fn help   - this list\r\n");
        return 0;
    }

    if (strcmp(sub, "mounts") == 0)
    {
        // Per-command help: |fn mounts --help (or -h / help).
        char opt[16];
        next_token(&args, opt, sizeof(opt));
        for (char *o = opt; *o; ++o)
            *o = (char)tolower((unsigned char)*o);
        if (strcmp(opt, "--help") == 0 || strcmp(opt, "-h") == 0 ||
            strcmp(opt, "help") == 0)
        {
            snprintf(outmsg, outmsg_size,
                     "|fn mounts - show currently mounted FujiNet drive slots "
                     "D1-D8\r\n");
            return 0;
        }
        // Any other extra arguments are ignored (as |wget ignores trailing
        // tokens) since the listing takes no parameters.
        return do_fn_mounts(outmsg, outmsg_size);
    }

    snprintf(outmsg, outmsg_size, "?fn: %s (try |fn help)\r\n", sub);
    return 1;
}

// ---------------------------------------------------------------------------
// Verb: say  (speak text via the SAM voice synth, out the SIO AUDIO line)
// ---------------------------------------------------------------------------
//
// Reuses the firmware's built-in SAM (Software Automatic Mouth) synthesizer via
// util_sam_say() (lib/utils/utils.cpp) — the same engine that announces disk
// swaps.  The synthesized 8-bit/22050 Hz PCM is played out the FujiNet DAC onto
// the Atari SIO AUDIO IN line.  util_sam_say() copies the text into SAM's own
// buffer, so this never writes back into Z80 RAM (the gate's contract holds).
// Playback is synchronous (it blocks the CP/M worker task for the speech
// duration, exactly like the disk-swap announcements).

int do_say(const char *args, const char *dir_prefix, char *outmsg,
           int outmsg_size, const vm_bar_io *io)
{
    (void)dir_prefix;
    (void)io; // non-interactive: speech is hardware audio, not console I/O

    const char *text = skip_spaces(args);

    if (text[0] == '\0' || strcmp(text, "help") == 0 ||
        strcmp(text, "--help") == 0)
    {
        snprintf(outmsg, outmsg_size,
                 "|say <text> - speak text aloud via the SAM voice synth\r\n"
                 "              (audio out the Atari SIO AUDIO line)\r\n");
        return text[0] == '\0' ? 1 : 0;
    }

    // English text -> SAM's reciter converts it to phonemes (phonetic=false).
    util_sam_say(text);

    snprintf(outmsg, outmsg_size, "OK\r\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Verb: help
// ---------------------------------------------------------------------------

int do_help(const char *args, const char *dir_prefix, char *outmsg,
            int outmsg_size, const vm_bar_io *io)
{
    (void)args;
    (void)dir_prefix;
    (void)io; // help is non-interactive
    snprintf(outmsg, outmsg_size,
             "Bar commands:\r\n"
             "|wget [-a] URL [DEST] - download URL to current dir\r\n"
             "                       (-a: convert text to CP/M CRLF)\r\n"
             "|ftp HOST             - interactive anonymous FTP client\r\n"
             "|fn                   - FujiNet Network Configuration Tools\r\n"
             "|apt                  - Advanced Packaging Tool\r\n"
             "|say <text>           - Speak aloud using SAM voice\r\n"
             "|help                 - this list\r\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Verb: ftp  (interactive anonymous FTP client)
// ---------------------------------------------------------------------------
//
// Unlike the other verbs, |ftp is INTERACTIVE: it opens a small shell that
// reads commands from, and prints results to, the CP/M terminal through the
// vm_bar_io callbacks the CCP hook supplies.  It reuses the existing FujiNet
// FTP client (fnFTP) for all networking and fnSDFAT for SD writes — no new FTP
// stack and no new file I/O.  Scope is deliberately small: anonymous login,
// browse (ls/cd/pwd), and download (get).  There is no put, no remote mutation,
// and no credential handling.

// Map a CP/M user number (0..15) to RunCPM's directory hex character ('0'..'9',
// 'A'..'F'), matching ccp.h's tohex()/toupper() so |ftp writes to the same
// /CPM/<drive>/<user>/ folder DIR and TYPE read from.
char ftp_hexuser(int u) { return (char)(u < 10 ? '0' + u : 'A' + (u - 10)); }

// Interactive FTP session state.
struct FtpState
{
    std::string remote_cwd; // current remote directory, absolute, starts '/'
    std::string root;       // local CP/M host root prefix, e.g. "/CPM/"
    char drive;             // local CP/M drive letter A..P
    int user;               // local CP/M user number 0..15
    bool binary;            // true = binary (default), false = ASCII text
};

// Build the host directory for the current local CP/M drive/user, ending in '/'
// (e.g. root "/CPM/" + drive 'B' + user 3 -> "/CPM/B/3/").
std::string ftp_local_dir(const FtpState &st)
{
    std::string d = st.root;
    d += st.drive;
    d += '/';
    d += ftp_hexuser(st.user);
    d += '/';
    return d;
}

// Parse a CP/M drive/user spec like "A", "A:", "A0", "A0:", "A15:".  Drive must
// be A..P; user (when present) must be 0..15 and defaults to 0.  Returns false
// on a bad drive letter, an out-of-range user, or trailing garbage.
bool parse_lcd(const char *tok, char *drive, int *user)
{
    if (!tok || !tok[0])
        return false;
    char d = (char)toupper((unsigned char)tok[0]);
    if (d < 'A' || d > 'P')
        return false;
    const char *p = tok + 1;
    int u = 0;
    bool has_digit = false;
    while (*p >= '0' && *p <= '9')
    {
        u = u * 10 + (*p - '0');
        if (u > 15)
            return false;
        has_digit = true;
        ++p;
    }
    if (*p == ':')
        ++p;
    if (*p != '\0')
        return false; // trailing junk (e.g. "A0X")
    *drive = d;
    *user = has_digit ? u : 0;
    return true;
}

// Resolve an FTP path argument against the current remote working directory.
// Absolute args (leading '/') replace cwd; relative args extend it.  '.', '..'
// and duplicate slashes collapse.  The result always begins with '/'.
std::string ftp_resolve(const std::string &cwd, const std::string &arg)
{
    std::string in = (!arg.empty() && arg[0] == '/') ? arg : (cwd + "/" + arg);
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < in.size())
    {
        size_t j = in.find('/', i);
        if (j == std::string::npos)
            j = in.size();
        std::string seg = in.substr(i, j - i);
        if (seg.empty() || seg == ".")
        {
            // skip
        }
        else if (seg == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else
        {
            parts.push_back(seg);
        }
        i = j + 1;
    }
    std::string out = "/";
    for (size_t k = 0; k < parts.size(); ++k)
    {
        out += parts[k];
        if (k + 1 < parts.size())
            out += "/";
    }
    return out;
}

// Read one line from the interactive terminal into buf (NUL-terminated, at most
// bufsize-1 chars).  Echoes printable input through io, supports BS/DEL line
// editing, and treats Ctrl-C as "cancel this line".  CR or LF ends the line.
// Returns the line length, or -1 if cancelled with Ctrl-C.
int ftp_readline(const vm_bar_io *io, char *buf, int bufsize)
{
    int n = 0;
    for (;;)
    {
        int c = io->getch() & 0xFF;
        if (c == 0x03) // Ctrl-C
        {
            io->puts_("^C\r\n");
            return -1;
        }
        if (c == '\r' || c == '\n')
        {
            io->puts_("\r\n");
            break;
        }
        if (c == 0x08 || c == 0x7F) // BS / DEL
        {
            if (n > 0)
            {
                --n;
                io->puts_("\b \b");
            }
            continue;
        }
        if (c < 0x20) // ignore other control characters
            continue;
        if (n < bufsize - 1)
        {
            buf[n++] = (char)c;
            io->putch(c); // echo
        }
    }
    buf[n] = '\0';
    return n;
}

// |ftp 'get' — RETR a remote file into the current local CP/M directory using a
// safe temp-then-rename sequence.  All status is printed through io.
void do_ftp_get(fnFTP &ftp, FtpState &st, const char *args, const vm_bar_io *io)
{
    bool use_ascii = !st.binary; // per-transfer mode starts from the session's
    bool force = false;
    char remote[256];
    remote[0] = '\0';
    char localarg[64];
    localarg[0] = '\0';

    // Options (-a ascii, -b binary, -f force) precede the remote name.
    const char *q = args;
    for (;;)
    {
        char t[256];
        if (next_token(&q, t, sizeof(t)) == 0)
            break;
        if (t[0] == '-' && t[1] != '\0')
        {
            for (const char *o = t + 1; *o; ++o)
            {
                if (*o == 'a' || *o == 'A')
                    use_ascii = true;
                else if (*o == 'b' || *o == 'B')
                    use_ascii = false;
                else if (*o == 'f' || *o == 'F')
                    force = true;
                else
                {
                    io->puts_("?get: unknown option\r\n");
                    return;
                }
            }
            continue;
        }
        snprintf(remote, sizeof(remote), "%s", t);
        break;
    }
    if (remote[0] == '\0')
    {
        io->puts_("?USAGE: get [-a|-b|-f] remote [local]\r\n");
        return;
    }
    next_token(&q, localarg, sizeof(localarg)); // optional explicit local name

    std::string rpath = ftp_resolve(st.remote_cwd, remote);

    // Derive the CP/M 8.3 local name (explicit arg wins, else the remote
    // basename).  No silent truncation — reject a name that won't fit.
    char name83[16];
    const char *src = localarg[0] ? localarg : rpath.c_str();
    if (!to_cpm_83(src, name83, sizeof(name83)))
    {
        io->puts_("?BAD CP/M NAME - USE: get remote-file LOCAL.EXT\r\n");
        return;
    }

    std::string dir = ftp_local_dir(st);
    std::string finalpath = dir + name83;
    std::string tmp = dir + "$FTPTEMP.$$$";

    if (fnSDFAT.exists(finalpath.c_str()) && !force)
    {
        io->puts_("?FILE EXISTS\r\n");
        return;
    }

    // Ensure the CP/M drive/user directory exists (create_path wants no trailing
    // slash).
    std::string dirNoSlash = dir;
    if (dirNoSlash.size() > 1 && dirNoSlash.back() == '/')
        dirNoSlash.pop_back();
    fnSDFAT.create_path(dirNoSlash.c_str());

    FILE *f = fnSDFAT.file_open(tmp.c_str(), "w");
    if (!f)
    {
        io->puts_("?SD WRITE ERROR\r\n");
        return;
    }

    // Give the FILE a DMA-capable internal stdio buffer before the FTP data
    // connection grabs internal RAM, so SD block writes never need a bounce
    // buffer that can't be allocated.  Same root cause and fix as |wget above
    // (see the big comment in do_wget): on the "cpmtel" task the 32 KB stack
    // + lwIP starve DMA-capable internal RAM, so a PSRAM stdio buffer would
    // make sdmmc_write_blocks fail with "Not enough heap memory".  ESP-only.
    char *sd_iobuf = nullptr;
#ifdef ESP_PLATFORM
    const size_t kSdIoBufSize = 2048; // 4 SD sectors; keeps DMA footprint small
    sd_iobuf = (char *)heap_caps_malloc(kSdIoBufSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (sd_iobuf != nullptr)
        setvbuf(f, sd_iobuf, _IOFBF, kSdIoBufSize);
#endif

    if (ftp.open_file(rpath, false) != FUJI_ERROR::NONE)
    {
        fclose(f);
        free(sd_iobuf); // safe after fclose; free(nullptr) is a no-op
        fnSDFAT.remove(tmp.c_str());
        io->puts_("?NO SUCH FILE\r\n");
        return;
    }

    // Drain the data connection.  Mirrors lib/FileSystem/fnFsFTP.cpp
    // cache_file(): read whatever the data socket has buffered, clamped to what
    // fnFTP::read_file() can satisfy in one call, until the server closes the
    // data connection (data_connected() == NONE).
    uint8_t buf[1024];
    long total = 0;
    long written = 0;
    bool pending_cr = false; // ASCII: a CR awaiting its possible LF
    int last_raw = -1;       // last raw byte received (ASCII soft-EOF decision)
    bool ok = true;
    bool done = false;
    while (!done)
    {
        int available = ftp.data_available();
        if (ftp.data_connected() == FUJI_ERROR::NONE) // transfer complete
            break;
        if (available == 0)
        {
            fnSystem.delay(10); // server still sending; wait for the next burst
            continue;
        }
        if (available < 0)
        {
            ok = false;
            break;
        }
        while (available > 0)
        {
            if (ftp.data_connected() == FUJI_ERROR::NONE)
            {
                done = true;
                break;
            }
            int to_read = available > (int)sizeof(buf) ? (int)sizeof(buf) : available;
            if (ftp.read_file(buf, (unsigned short)to_read) != FUJI_ERROR::NONE)
            {
                ok = false;
                break;
            }
            last_raw = buf[to_read - 1];
            if (use_ascii)
            {
                long w = write_text(f, buf, to_read, &pending_cr);
                if (w < 0)
                {
                    ok = false;
                    break;
                }
                written += w;
            }
            else
            {
                if ((int)fwrite(buf, 1, to_read, f) != to_read)
                {
                    ok = false;
                    break;
                }
                written += to_read;
            }
            total += to_read;
            available = ftp.data_available();
        }
        if (!ok)
            break;
    }

    ftp.close();

    // ASCII: flush a trailing bare CR, then guarantee a CP/M soft-EOF (^Z),
    // appending one only if the text doesn't already end with it.
    if (ok && use_ascii && pending_cr)
    {
        if (fputc('\r', f) == EOF || fputc('\n', f) == EOF)
            ok = false;
        else
            written += 2;
    }
    if (ok && use_ascii && last_raw != 0x1A)
    {
        if (fputc(0x1A, f) == EOF)
            ok = false;
        else
            ++written;
    }

    // Pad to the next 128-byte CP/M record boundary (same convention as |wget).
    if (ok && pad_cpm_record(f, written) < 0)
        ok = false;

    fclose(f);
    free(sd_iobuf); // safe after fclose; free(nullptr) is a no-op

    if (!ok)
    {
        fnSDFAT.remove(tmp.c_str()); // never leave a partial/corrupt file
        io->puts_("?TRANSFER FAILED\r\n");
        return;
    }

    // Safe publish: the temp is closed; only now remove the old final (when
    // forcing) and rename temp -> final.  On rename failure the temp is removed
    // and the existing final is left untouched.
    if (force && fnSDFAT.exists(finalpath.c_str()))
        fnSDFAT.remove(finalpath.c_str());
    if (!fnSDFAT.rename(tmp.c_str(), finalpath.c_str()))
    {
        fnSDFAT.remove(tmp.c_str());
        io->puts_("?SD WRITE ERROR\r\n");
        return;
    }

    char line[64];
    snprintf(line, sizeof(line), "Got %s (%ld bytes)\r\n", name83, written);
    io->puts_(line);
    (void)total;
}

int do_ftp(const char *args, const char *dir_prefix, char *outmsg,
           int outmsg_size, const vm_bar_io *io)
{
    // |ftp is interactive and needs the terminal-I/O callbacks.  A
    // non-interactive caller (no io) gets a clear refusal instead of a crash.
    if (!io || !io->getch || !io->putch || !io->puts_)
    {
        snprintf(outmsg, outmsg_size, "?INTERACTIVE I/O UNAVAILABLE\r\n");
        return 1;
    }
    outmsg[0] = '\0'; // the shell prints everything through io itself

    char host[128];
    if (next_token(&args, host, sizeof(host)) == 0)
    {
        io->puts_("?USAGE: |ftp HOST\r\n");
        return 1;
    }

    // Initialise session state.  Parse dir_prefix ("/CPM/A/0/") into the local
    // root, drive letter and user number so 'get' lands where DIR/TYPE look.
    FtpState st;
    st.remote_cwd = "/";
    st.binary = true; // binary is the default transfer mode
    st.drive = 'A';
    st.user = 0;
    st.root = "/CPM/";
    {
        std::string dp = dir_prefix ? dir_prefix : "/CPM/A/0/";
        if (!dp.empty() && dp.back() == '/')
            dp.pop_back(); // "/CPM/A/0"
        size_t s2 = dp.find_last_of('/');
        if (s2 != std::string::npos && s2 > 0)
        {
            std::string useg = dp.substr(s2 + 1);
            std::string rest = dp.substr(0, s2);
            size_t s1 = rest.find_last_of('/');
            if (s1 != std::string::npos && !useg.empty())
            {
                std::string dseg = rest.substr(s1 + 1);
                char u = (char)toupper((unsigned char)useg[0]);
                int un = (u >= '0' && u <= '9')   ? (u - '0')
                         : (u >= 'A' && u <= 'F') ? (10 + u - 'A')
                                                  : -1;
                if (!dseg.empty())
                {
                    char d = (char)toupper((unsigned char)dseg[0]);
                    if (d >= 'A' && d <= 'P' && un >= 0)
                    {
                        st.drive = d;
                        st.user = un;
                        st.root = rest.substr(0, s1 + 1); // "/CPM/"
                    }
                }
            }
        }
    }

    // Connect + anonymous login.  Reuses the codebase default anonymous
    // credentials; there is no user/password prompt and nothing is stored.
    fnFTP ftp;
    if (ftp.login("anonymous", "fujinet@fujinet.online", std::string(host)) !=
        FUJI_ERROR::NONE)
    {
        io->puts_("?LOGIN FAILED\r\n");
        return 1;
    }

    {
        char banner[200];
        snprintf(banner, sizeof(banner),
                 "\r\nConnected to %s (anonymous).\r\n"
                 "Type 'help' for commands, 'bye' to quit.\r\n",
                 host);
        io->puts_(banner);
    }

    for (;;)
    {
        io->puts_("ftp> ");
        char line[256];
        int n = ftp_readline(io, line, sizeof(line));
        if (n < 0)
            continue; // Ctrl-C cancels the current line

        const char *p = line;
        char cmd[16];
        if (next_token(&p, cmd, sizeof(cmd)) == 0)
            continue;
        for (char *c = cmd; *c; ++c)
            *c = (char)tolower((unsigned char)*c);

        if (strcmp(cmd, "bye") == 0 || strcmp(cmd, "quit") == 0)
        {
            ftp.logout();
            break;
        }
        else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
        {
            io->puts_(
                "Commands:\r\n"
                "  ls [path]    list remote dir     cd path  change remote dir\r\n"
                "  pwd          show remote dir      lcd D[u] set local CP/M dir\r\n"
                "  lpwd         show local dir       type     show transfer mode\r\n"
                "  ascii        text mode (CRLF+^Z)  binary   binary mode\r\n"
                "  get [-a|-b|-f] remote [local]     bye      quit\r\n");
        }
        else if (strcmp(cmd, "pwd") == 0)
        {
            char b[280];
            snprintf(b, sizeof(b), "%s\r\n", st.remote_cwd.c_str());
            io->puts_(b);
        }
        else if (strcmp(cmd, "lpwd") == 0)
        {
            char b[16];
            snprintf(b, sizeof(b), "%c%c:\r\n", st.drive, ftp_hexuser(st.user));
            io->puts_(b);
        }
        else if (strcmp(cmd, "type") == 0)
        {
            io->puts_(st.binary ? "binary\r\n" : "ascii\r\n");
        }
        else if (strcmp(cmd, "binary") == 0)
        {
            st.binary = true;
            io->puts_("binary\r\n");
        }
        else if (strcmp(cmd, "ascii") == 0)
        {
            st.binary = false;
            io->puts_("ascii\r\n");
        }
        else if (strcmp(cmd, "lcd") == 0)
        {
            char arg[32];
            if (next_token(&p, arg, sizeof(arg)) == 0)
            {
                char b[16];
                snprintf(b, sizeof(b), "%c%c:\r\n", st.drive, ftp_hexuser(st.user));
                io->puts_(b);
            }
            else
            {
                char d;
                int u;
                if (parse_lcd(arg, &d, &u))
                {
                    st.drive = d;
                    st.user = u;
                }
                else
                {
                    io->puts_("?BAD LOCAL NAME\r\n");
                }
            }
        }
        else if (strcmp(cmd, "cd") == 0)
        {
            char arg[256];
            if (next_token(&p, arg, sizeof(arg)) == 0)
            {
                io->puts_("?USAGE: cd PATH\r\n");
            }
            else
            {
                std::string target = ftp_resolve(st.remote_cwd, arg);
                // Validate by listing; open_directory stops its own data socket,
                // so no extra cleanup is needed here.
                if (ftp.open_directory(target, "") != FUJI_ERROR::NONE)
                    io->puts_("?NO SUCH FILE\r\n");
                else
                    st.remote_cwd = target;
            }
        }
        else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0)
        {
            char arg[256];
            std::string target = st.remote_cwd;
            if (next_token(&p, arg, sizeof(arg)) != 0)
                target = ftp_resolve(st.remote_cwd, arg);
            if (ftp.open_directory(target, "") != FUJI_ERROR::NONE)
            {
                io->puts_("?NO SUCH FILE\r\n");
            }
            else
            {
                // read_directory ends iteration on a non-NONE result OR an empty
                // name (the "no more entries" sentinel); the last real entry may
                // still arrive together with the terminating non-NONE.
                for (;;)
                {
                    std::string name;
                    long sz = 0;
                    bool isdir = false;
                    fujiError_t r = ftp.read_directory(name, sz, isdir);
                    if (!name.empty())
                    {
                        io->puts_(name.c_str());
                        if (isdir)
                            io->puts_("/");
                        io->puts_("\r\n");
                    }
                    if (r != FUJI_ERROR::NONE || name.empty())
                        break;
                }
            }
        }
        else if (strcmp(cmd, "get") == 0)
        {
            do_ftp_get(ftp, st, p, io);
        }
        else
        {
            char b[48];
            snprintf(b, sizeof(b), "?%s\r\n", cmd);
            io->puts_(b);
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Verb: apt  (Advanced Packaging Tool — STUB / scaffolding only)
// ---------------------------------------------------------------------------
//
// This is intentionally a command-dispatch SKELETON for a future package
// manager.  It parses the apt subcommand and its arguments, validates the
// usage shape, and prints "to be implemented" for every recognized command.
// It performs NO package management: no network access, no SD writes, no
// downloads, no archive extraction, no package database, and the
// "to=<destination>" option (valid only for `install`) is parsed but not
// acted upon.  All real functionality is deferred to a later stage.
//
// Recognized subcommands:
//   update                              refresh package index   (stub)
//   install <package> [to=<dest>]       install a package       (stub)
//   upgrade                             upgrade everything       (stub)
//   remove  <package>                   remove a package         (stub)
//   list                                list packages            (stub)
//   search  <text>                      search packages          (stub)
//   info    <package>                   show package details     (stub)
int do_apt(const char *args, const char *dir_prefix, char *outmsg,
           int outmsg_size, const vm_bar_io *io)
{
    (void)dir_prefix;
    (void)io; // non-interactive stub

    const char *p = skip_spaces(args);
    char sub[16];
    next_token(&p, sub, sizeof(sub));

    // Subcommand match is case-insensitive.
    for (char *s = sub; *s; ++s)
        *s = (char)tolower((unsigned char)*s);

    // No subcommand, or explicit help -> print the apt usage banner.
    if (sub[0] == '\0' || strcmp(sub, "help") == 0 ||
        strcmp(sub, "--help") == 0)
    {
        snprintf(outmsg, outmsg_size,
                 "APT Bar Commands:\r\n"
                 "\r\n"
                 "usage:\r\n"
                 "  |apt update\r\n"
                 "  |apt install <package> [to=<destination>]\r\n"
                 "  |apt upgrade\r\n"
                 "  |apt remove <package>\r\n"
                 "  |apt list\r\n"
                 "  |apt search <text>\r\n"
                 "  |apt info <package>\r\n"
                 "\r\n"
                 "examples:\r\n"
                 "  |apt install aztec-c\r\n"
                 "  |apt install aztec-c to=A1:\r\n");
        return 0;
    }

    // Commands that take NO arguments.
    if (strcmp(sub, "update") == 0 || strcmp(sub, "upgrade") == 0 ||
        strcmp(sub, "list") == 0)
    {
        char extra[64];
        next_token(&p, extra, sizeof(extra));
        if (extra[0] != '\0')
        {
            snprintf(outmsg, outmsg_size, "USE: |apt %s\r\n", sub);
            return 1;
        }
        snprintf(outmsg, outmsg_size, "to be implemented\r\n");
        return 0;
    }

    // install <package> [to=<destination>]
    if (strcmp(sub, "install") == 0)
    {
        char pkg[64];
        next_token(&p, pkg, sizeof(pkg));
        if (pkg[0] == '\0')
        {
            snprintf(outmsg, outmsg_size,
                     "USE: |apt install <package> [to=<destination>]\r\n");
            return 1;
        }
        // Optional second token must be "to=<destination>".
        char opt[80];
        next_token(&p, opt, sizeof(opt));
        if (opt[0] != '\0')
        {
            if (strncmp(opt, "to=", 3) != 0 || opt[3] == '\0')
            {
                snprintf(outmsg, outmsg_size,
                         "USE: |apt install <package> [to=<destination>]\r\n");
                return 1;
            }
            // Reject anything after the to= token.
            char trailing[16];
            next_token(&p, trailing, sizeof(trailing));
            if (trailing[0] != '\0')
            {
                snprintf(outmsg, outmsg_size,
                         "USE: |apt install <package> [to=<destination>]\r\n");
                return 1;
            }
            // "to=<destination>" is parsed but not acted upon (stub).
        }
        snprintf(outmsg, outmsg_size, "to be implemented\r\n");
        return 0;
    }

    // remove <package> / info <package> — exactly one argument; no to=.
    if (strcmp(sub, "remove") == 0 || strcmp(sub, "info") == 0)
    {
        char pkg[64];
        next_token(&p, pkg, sizeof(pkg));
        if (pkg[0] == '\0')
        {
            snprintf(outmsg, outmsg_size, "USE: |apt %s <package>\r\n", sub);
            return 1;
        }
        char extra[64];
        next_token(&p, extra, sizeof(extra));
        if (extra[0] != '\0')
        {
            // Trailing tokens (e.g. a stray to=) are malformed here:
            // to=<destination> is only valid for `install`.
            snprintf(outmsg, outmsg_size, "USE: |apt %s <package>\r\n", sub);
            return 1;
        }
        snprintf(outmsg, outmsg_size, "to be implemented\r\n");
        return 0;
    }

    // search <text> — requires a search term.
    if (strcmp(sub, "search") == 0)
    {
        char term[64];
        next_token(&p, term, sizeof(term));
        if (term[0] == '\0')
        {
            snprintf(outmsg, outmsg_size, "USE: |apt search <text>\r\n");
            return 1;
        }
        snprintf(outmsg, outmsg_size, "to be implemented\r\n");
        return 0;
    }

    snprintf(outmsg, outmsg_size, "?UNKNOWN APT COMMAND\r\nUSE: |apt help\r\n");
    return 1;
}

// ---------------------------------------------------------------------------
// Dispatch table (extension point for new verbs)
// ---------------------------------------------------------------------------

struct BarVerb
{
    const char *name;
    int (*handler)(const char *args, const char *dir_prefix, char *outmsg,
                   int outmsg_size, const vm_bar_io *io);
};

const BarVerb kVerbs[] = {
    {"wget", do_wget},
    {"ftp", do_ftp},
    {"fn", do_fn},
    {"apt", do_apt},
    {"say", do_say},
    {"help", do_help},
};

} // namespace

// ---------------------------------------------------------------------------
// Gate entry point
// ---------------------------------------------------------------------------

extern "C" int vm_bar_command(const char *cmd, const char *dir_prefix,
                               char *outmsg, int outmsg_size,
                               const vm_bar_io *io)
{
    if (!outmsg || outmsg_size <= 0)
        return 1;
    outmsg[0] = '\0';

    if (!cmd)
    {
        snprintf(outmsg, outmsg_size, "?bar\r\n");
        return 1;
    }

    const char *p = skip_spaces(cmd);
    char verb[16];
    next_token(&p, verb, sizeof(verb));
    if (verb[0] == '\0')
    {
        snprintf(outmsg, outmsg_size, "?bar (try |help)\r\n");
        return 1;
    }

    // Verb match is case-insensitive.
    for (char *v = verb; *v; ++v)
        *v = (char)tolower((unsigned char)*v);

    const char *prefix = dir_prefix ? dir_prefix : "/CPM/";

    for (size_t i = 0; i < sizeof(kVerbs) / sizeof(kVerbs[0]); ++i)
    {
        if (strcmp(verb, kVerbs[i].name) == 0)
            return kVerbs[i].handler(p, prefix, outmsg, outmsg_size, io);
    }

    snprintf(outmsg, outmsg_size, "?bar: %s (try |help)\r\n", verb);
    return 1;
}

#endif // BUILD_ATARI
