#!/usr/bin/env python3
# Simulates a TCP connection that goes stale mid-session (NAT/firewall drops the
# path during idle, no RST): the first client connection forwards normally for a
# while, then black-holes -- the client socket stays "connected" but no further
# responses flow. The upstream is closed immediately so the tnfsd session frees
# its cli_fd. Subsequent client connections (the reconnect) are served normally.
#
#   usage: stall_proxy.py <listen_port> <upstream_host> <upstream_port>
#   env:   STALL_AFTER=<server->client bytes on conn #1 before black-holing> (def 20)
import os
import socket
import sys
import threading

LISTEN = int(sys.argv[1])
UP_HOST = sys.argv[2]
UP_PORT = int(sys.argv[3])
STALL_AFTER = int(os.environ.get("STALL_AFTER", "20"))

_lock = threading.Lock()
_stalled = False  # has the one-time mid-session stall fired yet?


def discard(sock):
    try:
        while sock.recv(65536):
            pass
    except OSError:
        pass


def pump(src, dst):
    try:
        while True:
            d = src.recv(65536)
            if not d:
                break
            dst.sendall(d)
    except OSError:
        pass


def handle(client):
    global _stalled
    try:
        up = socket.create_connection((UP_HOST, UP_PORT))
    except OSError as e:
        print("stall-proxy: upstream connect failed:", e, flush=True)
        client.close()
        return

    with _lock:
        already_stalled = _stalled

    if already_stalled:
        # The reconnect: behave like a normal pass-through proxy.
        threading.Thread(target=pump, args=(client, up), daemon=True).start()
        pump(up, client)
        client.close()
        up.close()
        return

    # Forward server->client until STALL_AFTER bytes have flowed, then black-hole
    # the connection (the first connection that actually carries data triggers it,
    # so a zero-byte health-check probe doesn't count). Upstream recv has a
    # timeout so an idle probe connection doesn't block this thread forever.
    threading.Thread(target=pump, args=(client, up), daemon=True).start()
    up.settimeout(5)
    sent = 0
    try:
        while sent < STALL_AFTER:
            d = up.recv(65536)
            if not d:
                break
            take = min(len(d), STALL_AFTER - sent)
            client.sendall(d[:take])
            sent += take
    except OSError:
        client.close()
        up.close()
        return

    with _lock:
        _stalled = True
    print(f"stall-proxy: black-holing connection after {sent} bytes", flush=True)
    # Free the tnfsd session by closing upstream; keep the client socket open but
    # silent so the client believes it is still connected until it gives up.
    up.close()
    discard(client)
    client.close()


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", LISTEN))
    s.listen(16)
    print(f"stall-proxy :{LISTEN} -> {UP_HOST}:{UP_PORT} STALL_AFTER={STALL_AFTER}B",
          flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
