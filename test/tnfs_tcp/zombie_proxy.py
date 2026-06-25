#!/usr/bin/env python3
# Like stall_proxy, but models a real NAT/firewall idle drop: when the first
# connection stalls, the UPSTREAM (proxy->tnfsd) socket is kept OPEN forever, so
# the server's session stays bound to that now-dead connection (the client's FIN
# never reaches the server). Reconnects open fresh upstreams, which the server
# rejects with "Session is assigned to another TCP connection" until it reaps the
# zombie. Reproduces the "reconnect never recovers" symptom.
#
#   usage: zombie_proxy.py <listen_port> <upstream_host> <upstream_port>
#   env:   STALL_AFTER=<server->client bytes before black-holing> (default 20)
import os
import socket
import sys
import threading

LISTEN = int(sys.argv[1])
UP_HOST = sys.argv[2]
UP_PORT = int(sys.argv[3])
STALL_AFTER = int(os.environ.get("STALL_AFTER", "20"))

_lock = threading.Lock()
_stalled = False
_zombies = []  # keep dead upstream sockets referenced so they are not GC-closed


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
    except OSError:
        client.close()
        return

    with _lock:
        already = _stalled

    if already:
        threading.Thread(target=pump, args=(client, up), daemon=True).start()
        pump(up, client)
        client.close()
        up.close()
        return

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
        _zombies.append(up)  # keep the upstream alive -> server session stays bound
    print(f"zombie-proxy: black-holed after {sent}B; keeping upstream open (zombie)",
          flush=True)
    discard(client)
    client.close()
    # NOTE: 'up' is intentionally never closed.


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", LISTEN))
    s.listen(16)
    print(f"zombie-proxy :{LISTEN} -> {UP_HOST}:{UP_PORT} STALL_AFTER={STALL_AFTER}B",
          flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
