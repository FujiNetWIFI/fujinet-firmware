"""Bounded loopback regression for the real mgHttpClient and Mongoose."""
import argparse
import select
import socket
import subprocess
import time
import unittest

PARSER = argparse.ArgumentParser()
PARSER.add_argument("probe")
PARSER.add_argument("--expect-peer-close", action="store_true")
PARSER.add_argument("--expect-chunked-peer-close", action="store_true")
ARGS = PARSER.parse_args()
BODY = b"fixed\x00body\n"


class HttpCompletionTests(unittest.TestCase):
    def run_response(self, name, body=BODY, split=False, reuse=False, fixed=True,
                     chunked=False, terminal="coalesced"):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            port = listener.getsockname()[1]
            process = subprocess.Popen(
                [ARGS.probe, f"http://127.0.0.1:{port}/fixture", "reuse" if reuse else "close"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(3)
                    request = b""
                    while b"\r\n\r\n" not in request:
                        part = peer.recv(4096)
                        self.assertTrue(part)
                        request += part
                        self.assertLessEqual(len(request), 8192)
                    self.assertIn(b"GET /fixture HTTP/1.1\r\n", request)
                    self.assertIn(b"Connection: " + (b"keep-alive" if reuse else b"close") + b"\r\n", request)
                    headers = b"HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n"
                    if chunked:
                        headers += b"Transfer-Encoding: chunked\r\n"
                    elif fixed:
                        headers += f"Content-Length: {len(body)}\r\n".encode()
                    headers += b"\r\n"
                    if chunked:
                        chunks = b""
                        for part in (body[:2], body[2:]):
                            if part:
                                chunks += f"{len(part):x}\r\n".encode() + part + b"\r\n"
                        if terminal == "coalesced":
                            peer.sendall(headers + chunks + b"0\r\n\r\n")
                        else:
                            peer.sendall(headers + chunks)
                            self.assertFalse(select.select([process.stdout], [], [], 0.25)[0],
                                             "GET completed before the terminal chunk arrived")
                            if terminal == "fragmented":
                                peer.sendall(b"0\r\n")
                                self.assertFalse(select.select([process.stdout], [], [], 0.25)[0],
                                                 "GET completed before the final CRLF arrived")
                                peer.sendall(b"\r\n")
                            else:
                                peer.sendall(b"0\r\n\r\n")
                    elif split:
                        peer.sendall(headers + body[:2])
                        self.assertFalse(select.select([process.stdout], [], [], 0.25)[0],
                                         "GET completed before the declared body arrived")
                        peer.sendall(body[2:])
                    else:
                        peer.sendall(headers + body)
                    # sendall flushes the entire response; retain the socket until observation.
                    started = time.monotonic()
                    ready = bool(select.select([process.stdout], [], [], 1.5)[0])
                    expected_wait = ((ARGS.expect_chunked_peer_close and not reuse) if chunked
                                     else not fixed or (ARGS.expect_peer_close and not reuse))
                    self.assertEqual(ready, not expected_wait)
                    if ready:
                        output, errors = process.communicate(timeout=3)
                        self.assertEqual(process.returncode, 0, errors.decode())
                        # Observe body/status/EOF while the fixture peer is still open.
                        print(f"{name}: complete_before_peer_close=true elapsed_ms={int((time.monotonic()-started)*1000)}", flush=True)
                    else:
                        print(f"{name}: complete_before_peer_close=false full_body_flushed=true held_open_ms=1500", flush=True)
                        peer.shutdown(socket.SHUT_RDWR)
                if not ready:
                    output, errors = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, 0, errors.decode())
                lines = output.decode().splitlines()
                self.assertEqual(len(lines), 2)
                self.assertEqual(lines[0], "GET_RETURNED")
                line = lines[1]
                fields = line.split()
                values = list(map(int, fields[:8]))
                first = min(3, len(body))
                self.assertEqual(values, [200, len(body) if fixed or chunked else -1, len(body),
                                          first, len(body)-first, 0, 0, 1])
                self.assertEqual(fields[8] if body else "", body.hex())
                print(f"{name}: status=200 body_hex={body.hex()} eof_read=0 available_after=0 transaction_done=true", flush=True)
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=3)

    def test_fixed_coalesced(self):
        self.run_response("fixed_coalesced")

    def test_fixed_fragmented(self):
        self.run_response("fixed_fragmented", split=True)

    def test_fixed_empty(self):
        self.run_response("fixed_empty", body=b"")

    def test_fixed_reuse_enabled(self):
        self.run_response("fixed_reuse_enabled", reuse=True)

    def test_close_delimited(self):
        self.run_response("close_delimited", fixed=False)

    def test_chunked_coalesced(self):
        self.run_response("chunked_coalesced", chunked=True)

    def test_chunked_empty(self):
        self.run_response("chunked_empty", body=b"", chunked=True)

    def test_chunked_terminal_separate(self):
        self.run_response("chunked_terminal_separate", chunked=True, terminal="separate")

    def test_chunked_terminal_fragmented(self):
        self.run_response("chunked_terminal_fragmented", chunked=True, terminal="fragmented")

    def test_chunked_reuse_enabled(self):
        self.run_response("chunked_reuse_enabled", chunked=True, reuse=True)


if __name__ == "__main__":
    unittest.main(argv=[__file__], verbosity=2)
