"""fujibus.py -- a FujiBus (FEP-004) client over fujinet-pc's BoIP socket.

The wire format follows lib/bus/rs232/FujiBusPacket.cpp: SLIP frames of
{device, command, u16 length, checksum, descriptor} plus params and payload.
"""
import os
import socket
import struct

END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD
FIELD_SIZE = [0, 1, 1, 1, 1, 2, 2, 4]
NUM_FIELDS = [0, 1, 2, 3, 4, 1, 2, 1]

FUJI = 0x70
DISK = 0x31
ACK, NAK = 0x06, 0x15


def checksum(buf):
    chk = 0
    for b in buf:
        chk += b
        chk = (chk >> 8) + (chk & 0xFF)
    return chk & 0xFF


def packet(dev, cmd, params=(), payload=b""):
    """params is [(size, value)]; one descriptor per param, as the RP does."""
    descs, pdata = [], b""
    for size, val in params:
        descs.append({1: 1, 2: 5, 4: 7}[size])
        pdata += struct.pack({1: "B", 2: "<H", 4: "<I"}[size], val)
    if not descs:
        descs = [0]
    for i in range(len(descs) - 1):
        descs[i] |= 0x80
    body = bytes(descs[1:]) + pdata + bytes(payload)
    pkt = bytearray([dev, cmd, 0, 0, 0, descs[0]]) + body
    struct.pack_into("<H", pkt, 2, len(pkt))
    pkt[4] = checksum(pkt)
    return bytes(pkt)


def slip(pkt):
    out = bytearray([END])
    for b in pkt:
        out += {END: bytes([ESC, ESC_END]), ESC: bytes([ESC, ESC_ESC])}.get(b, bytes([b]))
    out.append(END)
    return bytes(out)


def unslip(body):
    out, i = bytearray(), 0
    while i < len(body):
        if body[i] == ESC and i + 1 < len(body):
            i += 1
            out.append(END if body[i] == ESC_END else ESC)
        else:
            out.append(body[i])
        i += 1
    return bytes(out)


def decode(pkt):
    """-> (device, command, params, payload); raises ValueError if malformed."""
    if len(pkt) < 6:
        raise ValueError("short packet")
    length = pkt[2] | pkt[3] << 8
    if length != len(pkt):
        raise ValueError("length %d != %d" % (length, len(pkt)))
    z = bytearray(pkt)
    z[4] = 0
    if checksum(z) != pkt[4]:
        raise ValueError("bad checksum")
    descs, off = [pkt[5]], 6
    while descs[-1] & 0x80:
        descs.append(pkt[off])
        off += 1
    params = []
    for d in descs:
        n, sz = NUM_FIELDS[d & 7], FIELD_SIZE[d & 7]
        for _ in range(n):
            params.append(int.from_bytes(pkt[off:off + sz], "little"))
            off += sz
    return pkt[0], pkt[1], params, pkt[off:]


def endpoint():
    host, _, port = os.environ.get("FUJINET_TCP", "127.0.0.1:1995").rpartition(":")
    return host or "127.0.0.1", int(port)


class Bus:
    def __init__(self, addr=None, timeout=10.0):
        self.s = socket.create_connection(addr or endpoint(), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.rx = bytearray()

    def _frame(self):
        while True:
            start = self.rx.find(END)
            if start >= 0:
                stop = self.rx.find(END, start + 1)
                if stop == start + 1:           # back-to-back ENDs: resync
                    del self.rx[:start + 1]
                    continue
                if stop > 0:
                    body = bytes(self.rx[start + 1:stop])
                    del self.rx[:stop + 1]
                    return unslip(body)
            chunk = self.s.recv(4096)
            if not chunk:
                raise EOFError("fujinet-pc closed the connection")
            self.rx += chunk

    def call(self, dev, cmd, params=(), payload=b""):
        """-> (ok, payload)"""
        self.s.sendall(slip(packet(dev, cmd, params, payload)))
        d, c, _, data = decode(self._frame())
        if d != dev:
            raise ValueError("reply from %02X to a request for %02X" % (d, dev))
        return c == ACK, data

    def mount(self, slot, host, path, mode=1):
        """Puts `path` on `host` into device slot `slot` and mounts it (mode 1 = read, 2 = write)."""
        if not self.call(FUJI, 0xF9, [(1, host)])[0]:
            raise RuntimeError("MOUNT_HOST %d refused" % host)
        ok, _ = self.call(FUJI, 0xE2, [(1, slot), (1, host), (1, mode)], path.encode() + b"\0")
        if not ok:
            raise RuntimeError("SET_DEVICE_FULLPATH %s refused" % path)
        ok, _ = self.call(FUJI, 0xF8, [(1, slot), (1, mode)])
        if not ok:
            raise RuntimeError("MOUNT_IMAGE slot %d refused" % slot)

    def unmount(self, slot):
        return self.call(FUJI, 0xE9, [(1, slot)])[0]
