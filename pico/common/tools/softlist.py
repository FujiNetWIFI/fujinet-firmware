"""softlist.py -- what the carts' mkmapdb.py read from a MAME software list (hash/*.xml).

    from softlist import carts
    for name, slot, ram, crc in carts("hash/sms.xml", "sms_cart"): ...
"""
import xml.etree.ElementTree as ET


def gf2_times(mat, vec):
    s = 0
    i = 0
    while vec:
        if vec & 1:
            s ^= mat[i]
        vec >>= 1
        i += 1
    return s


def gf2_square(mat):
    return [gf2_times(mat, mat[n]) for n in range(32)]


def crc32_combine(crc1, crc2, len2):
    """zlib's crc32_combine: the CRC of A+B from crc(A), crc(B), len(B)."""
    if len2 == 0:
        return crc1
    odd = [0xEDB88320] + [1 << n for n in range(31)]
    even = gf2_square(odd)
    odd = gf2_square(even)
    while True:
        even = gf2_square(odd)
        if len2 & 1:
            crc1 = gf2_times(even, crc1)
        len2 >>= 1
        if not len2:
            break
        odd = gf2_square(even)
        if len2 & 1:
            crc1 = gf2_times(odd, crc1)
        len2 >>= 1
        if not len2:
            break
    return crc1 ^ crc2


def num(s):
    return int(s, 0)


def carts(path, interface):
    """(name, slot or None, cart RAM bytes, CRC-32) for each part on `interface`. A
    multi-chip part's CRC is its chips' joined in offset order, which is what a
    single-file dump of the same cart hashes to."""
    for sw in ET.parse(path).getroot().iter("software"):
        for part in sw.iter("part"):
            if part.get("interface") != interface:
                continue
            slot = None
            for f in part.iter("feature"):
                if f.get("name") == "slot":
                    slot = f.get("value")
            roms, ram = [], 0
            for da in part.iter("dataarea"):
                if da.get("name") == "rom":
                    for r in da.iter("rom"):
                        if r.get("crc") and r.get("size"):
                            roms.append((num(r.get("offset", "0")), num(r.get("size")),
                                         int(r.get("crc"), 16)))
                elif da.get("name") == "ram":
                    ram = num(da.get("size"))
            if not roms:
                continue
            roms.sort()
            crc = roms[0][2]
            for _, size, c in roms[1:]:
                crc = crc32_combine(crc, c, size)
            yield sw.get("name"), slot, ram, crc
