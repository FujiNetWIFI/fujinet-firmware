# TNFS-over-TCP framing test

A standalone, native (PC) regression test for the FujiNet TNFS client's
TCP transport. It exists because TNFS has no length field in its header: over
UDP each datagram is one message, but over TCP (a byte stream) a single response
can arrive split across reads or several can coalesce. The client must therefore
read **exactly one** TNFS message per response (see `_tnfs_tcp_recv()` /
`_tnfs_tcp_response_length()` in `lib/TNFSlib/tnfslib.cpp`). When that framing is
wrong, the stream desyncs and you get the classic symptoms:

```
Timeout after 2000 milliseconds. Retrying
Received delayed response! Rcvd: 15, Expected: 16
TNFS OUT OF ORDER SEQUENCE! ...
```

## What it does

The harness links the **real** `tnfslib` + `fnTcpClient` from the repo (with thin
stubs for the firmware-only deps `fnSystem`/`bus`/`fnUDP`, in `stubs/`) and drives
a full session against a server:

- `MOUNT` (reports negotiated transport + server version)
- `OPENDIRX` + `READDIRX` over the whole root dir (variable-length framing)
- `OPEN` + `READ` the largest file in full, checksummed (the large/most-split
  response)
- `CLOSE` / `UMOUNT`

It prints a `PASS`/`FAIL` per server.

`frag_proxy.py` is a fragmenting TCP proxy: it forwards client↔server but chops
the **server→client** stream into small pieces with a tiny delay, deterministically
reproducing the WAN segmentation that triggered the bug — without needing a flaky
real-world connection.

## Build

```sh
./build.sh        # produces ./tnfs_tcp_test
```

Requires only `g++` (C++17) and `python3`. Nothing ESP/PlatformIO.

## Run

Against any server, host/port pairs:

```sh
./tnfs_tcp_test apps.irata.online 16384 tnfs.tma-3.net 16384
```

Baseline + fragmented stress against one upstream (local `tnfsd` or remote):

```sh
./run.sh 127.0.0.1 16384            # build, baseline, then through the frag proxy
FRAG=1 ./run.sh apps.irata.online 16384   # 1 byte/piece = most brutal
```

Exit code is 0 only if every run passes.

## Reproducing the bug / confirming the fix

Through the fragmenting proxy the fixed client passes with correct data and zero
errors. Building the same harness against the pre-fix `tnfslib.cpp` and running it
through the identical proxy produces garbage `server_version`, `Received delayed
response`, `OUT OF ORDER SEQUENCE`, corrupt directory entries and retry failures —
i.e. the proxy run is a faithful, deterministic reproduction of the field reports.
