"""st2.py -- the ST2 image format and the cart's page rules, for the tools.

Mirrors firmware/src/s2map.c (the C planner the cart runs); the host test
test_s2map holds the two to the same corpus.
"""

ARENA_PAGE = 0xE4          # FN_ARENA_BASE >> 8: an app's pages stop below it
CLAIM_ADDR = 0x07FC


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
        self.mem = {}              # console address -> byte
        self.claim = False
        self.title = ""


def parse(data):
    """The console's view of an image, as s2map_plan builds it."""
    img = Image()
    if len(data) >= 512 and data[:4] == b"RCA2":
        img.st2 = True
        blocks = data[4]
        if blocks < 2 or blocks > 65:
            raise ValueError("%d blocks; an ST2 has 2-65" % blocks)
        img.title = data[32:64].split(b"\0")[0].decode("latin-1")
        for i in range(blocks - 1):
            pg = data[64 + i]
            img.pages.append(pg)
            if not page_ok(pg):
                continue
            chunk = data[256 * (i + 1):256 * (i + 2)]
            for k in range(256):
                img.mem[pg * 256 + k] = chunk[k] if k < len(chunk) else 0
    else:
        if len(data) > 0x400:
            raise ValueError("%d bytes; a raw image is at most 1K" % len(data))
        for k, b in enumerate(data):
            img.mem[0x400 + k] = b
        img.pages = sorted({0x04 + k // 256 for k in range(len(data))})
    img.claim = bytes(img.mem.get(CLAIM_ADDR + i, 0) for i in range(4)) == b"FUJI"
    return img


def build(pages, title="", catalogue=""):
    """An ST2 file from {page: 256 bytes}, in page order."""
    order = sorted(pages)
    if len(order) > 64:
        raise ValueError("%d pages; an ST2 holds 64" % len(order))
    hdr = bytearray(256)
    hdr[0:4] = b"RCA2"
    hdr[4] = len(order) + 1
    hdr[5] = 1
    cat = catalogue.encode("latin-1")[:9]
    hdr[16:16 + len(cat)] = cat
    t = title.encode("latin-1")[:31]
    hdr[32:32 + len(t)] = t
    for i, pg in enumerate(order):
        hdr[64 + i] = pg
    body = b"".join(bytes(pages[pg]).ljust(256, b"\0")[:256] for pg in order)
    return bytes(hdr) + body
