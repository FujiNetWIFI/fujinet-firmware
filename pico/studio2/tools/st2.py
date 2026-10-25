"""st2.py -- the ST2 image format and the cart's page rules, for the tools.

Mirrors firmware/src/s2map.c (the C planner the cart runs); the host test
test_s2map holds the two to the same corpus.
"""

ARENA_PAGE = 0xE4          # FN_ARENA_BASE >> 8: an app's pages stop below it
CLAIM_ADDR = 0x07FC
MAX_BLOCKS = 192           # S2MAP_ST2_BLOCKS: the header's whole page map
WIN_PAGE, WIN_PAGES = 0xC4, 12     # the bank window, $C400-$CFFF (protocol 2)
NBANKS = 16


def in_window(pg):
    return WIN_PAGE <= pg < WIN_PAGE + WIN_PAGES


def page_ok(pg, app=False):
    if pg & 0x0F < 4 or pg in (0x08, 0x09):
        return False
    if app and (pg == 0x0B or pg >= ARENA_PAGE):
        return False
    return True


class Image:
    def __init__(self):
        self.st2 = False
        self.pages = []            # pages the file's blocks name, in order
        self.mem = {}              # console address -> byte (bank 0 in the window)
        self.banks = []            # banks 1.., each {window page: 256 bytes}
        self.claim = False
        self.title = ""


def parse(data):
    """The console's view of an image, as s2map_plan builds it."""
    img = Image()
    if len(data) >= 512 and data[:4] == b"RCA2":
        img.st2 = True
        blocks = data[4]
        if blocks < 2 or blocks > MAX_BLOCKS + 1:
            raise ValueError("%d blocks; an ST2 has 2-%d" % (blocks, MAX_BLOCKS + 1))
        img.title = data[32:64].split(b"\0")[0].decode("latin-1")
        bank, opened = 0, False
        for i in range(blocks - 1):
            pg = data[64 + i]
            img.pages.append(pg)
            if not page_ok(pg):
                continue
            chunk = data[256 * (i + 1):256 * (i + 2)].ljust(256, b"\0")
            if in_window(pg):
                if pg == WIN_PAGE:          # $C4 opens the next bank
                    bank += opened
                    opened = True
                if bank >= NBANKS:
                    raise ValueError("more than %d banks" % NBANKS)
                if bank:
                    while len(img.banks) < bank:
                        img.banks.append({})
                    img.banks[bank - 1][pg] = bytes(chunk)
                    continue
            for k in range(256):
                img.mem[pg * 256 + k] = chunk[k]
    else:
        if len(data) > 0x400:
            raise ValueError("%d bytes; a raw image is at most 1K" % len(data))
        for k, b in enumerate(data):
            img.mem[0x400 + k] = b
        img.pages = sorted({0x04 + k // 256 for k in range(len(data))})
    img.claim = bytes(img.mem.get(CLAIM_ADDR + i, 0) for i in range(4)) == b"FUJI"
    return img


def build(pages, title="", catalogue="", banks=()):
    """An ST2 file from {page: 256 bytes} (bank 0 in the window), in page
    order, then each of `banks` (1..: {window page: bytes}) opened by its $C4."""
    pages = dict(pages)
    if banks:
        pages.setdefault(WIN_PAGE, bytes(256))  # bank 0 must open with $C4
    blocks = [(pg, pages[pg]) for pg in sorted(pages)]
    for b in banks:
        bad = [pg for pg in b if not in_window(pg)]
        if bad:
            raise ValueError("a bank holds pages outside the window: %s"
                             % " ".join("$%02X" % p for p in bad))
        blocks.append((WIN_PAGE, b.get(WIN_PAGE, bytes(256))))
        blocks += [(pg, b[pg]) for pg in sorted(b) if pg != WIN_PAGE]
    if len(blocks) > MAX_BLOCKS:
        raise ValueError("%d blocks; an ST2 holds %d" % (len(blocks), MAX_BLOCKS))
    if 1 + len(banks) > NBANKS:
        raise ValueError("%d banks; the window has %d" % (1 + len(banks), NBANKS))
    hdr = bytearray(256)
    hdr[0:4] = b"RCA2"
    hdr[4] = len(blocks) + 1
    hdr[5] = 1
    cat = catalogue.encode("latin-1")[:9]
    hdr[16:16 + len(cat)] = cat
    t = title.encode("latin-1")[:31]
    hdr[32:32 + len(t)] = t
    for i, (pg, _) in enumerate(blocks):
        hdr[64 + i] = pg
    body = b"".join(bytes(d).ljust(256, b"\0")[:256] for _, d in blocks)
    return bytes(hdr) + body
