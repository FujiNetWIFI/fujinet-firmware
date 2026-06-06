#!/usr/bin/env python3
# Fragmenting TCP proxy: forwards client<->server, but chops the SERVER->CLIENT
# byte stream into tiny pieces with a small inter-piece delay. This forces the
# TNFS client to observe partial responses (header split from payload, payload
# split mid-message) -- exactly what real WAN TCP segmentation does, and the
# condition the client-side framing fix is meant to handle.
#
#   usage: frag_proxy.py <listen_port> <upstream_host> <upstream_port>
#   env:   FRAG=<bytes per server->client piece>   (default 3)
#          FRAGDELAY=<seconds between pieces>       (default 0.004)
import os
import socket
import sys
import threading
import time

LISTEN = int(sys.argv[1])
UP_HOST = sys.argv[2]
UP_PORT = int(sys.argv[3])
CHUNK = int(os.environ.get("FRAG", "3"))
DELAY = float(os.environ.get("FRAGDELAY", "0.004"))


def pipe_whole(src, dst):
    try:
        while True:
            d = src.recv(65536)
            if not d:
                break
            dst.sendall(d)
    except OSError:
        pass
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def pipe_fragmented(src, dst):
    try:
        while True:
            d = src.recv(65536)
            if not d:
                break
            for i in range(0, len(d), CHUNK):
                dst.sendall(d[i:i + CHUNK])
                time.sleep(DELAY)
    except OSError:
        pass
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


def handle(client):
    try:
        upstream = socket.create_connection((UP_HOST, UP_PORT))
    except OSError as e:
        print("proxy: upstream connect failed:", e, flush=True)
        client.close()
        return
    upstream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    t = threading.Thread(target=pipe_whole, args=(client, upstream), daemon=True)
    t.start()
    pipe_fragmented(upstream, client)  # server -> client, fragmented
    client.close()
    upstream.close()


def main():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", LISTEN))
    s.listen(16)
    print(f"frag-proxy :{LISTEN} -> {UP_HOST}:{UP_PORT}  FRAG={CHUNK}B DELAY={DELAY}s",
          flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
