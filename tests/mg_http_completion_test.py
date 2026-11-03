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
ARGS, TEST_ARGS = PARSER.parse_known_args()
BODY = b"fixed\x00body\n"


class HttpCompletionTests(unittest.TestCase):
    def read_request(self, peer, path, reuse=False, method="GET"):
        peer.settimeout(3)
        request = b""
        while b"\r\n\r\n" not in request:
            part = peer.recv(4096)
            self.assertTrue(part)
            request += part
            self.assertLessEqual(len(request), 8192)
        self.assertIn(f"{method} {path} HTTP/1.1\r\n".encode(), request)
        self.assertIn(b"Connection: " + (b"keep-alive" if reuse else b"close") + b"\r\n", request)

    def assert_result(self, lines, body):
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0], "GET_RETURNED")
        fields = lines[1].split()
        first = min(3, len(body))
        self.assertEqual(list(map(int, fields[:8])),
                         [200, len(body), len(body), first, len(body) - first, 0, 0, 1])
        self.assertEqual(fields[8] if body else "", body.hex())

    def run_redirect(self, framing, close_order):
        body = b"redirect\x00final\n"
        with socket.socket() as source, socket.socket() as target:
            for listener in (source, target):
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(3)
            process = subprocess.Popen(
                [ARGS.probe, f"http://127.0.0.1:{source.getsockname()[1]}/redirect", "close"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            try:
                with source.accept()[0] as old:
                    self.read_request(old, "/redirect")
                    status = 302 if framing == "empty" else 301
                    response = (f"HTTP/1.1 {status} Redirect\r\nConnection: close\r\n"
                                f"Location: http://127.0.0.1:{target.getsockname()[1]}/final\r\n").encode()
                    if framing == "chunked":
                        response += b"Transfer-Encoding: chunked\r\n\r\n3\r\nold\r\n0\r\n\r\n"
                    elif framing == "empty":
                        response += b"Content-Length: 0\r\n\r\n"
                    else:
                        response += b"Content-Length: 3\r\n\r\nold"
                    old.sendall(response)
                    if close_order == "immediate":
                        old.shutdown(socket.SHUT_RDWR)
                        old.close()
                    with target.accept()[0] as peer:
                        self.read_request(peer, "/final")
                        if close_order == "overlap":
                            old.shutdown(socket.SHUT_RDWR)
                            old.close()
                        self.assertFalse(select.select([process.stdout], [], [], 0.35)[0],
                                         "GET returned after old close, before redirected response")
                        peer.sendall((f"HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n"
                                      f"Content-Length: {len(body)}\r\n\r\n").encode() + body)
                        output, errors = process.communicate(timeout=3)
                        self.assertEqual(process.returncode, 0, errors.decode())
                        self.assert_result(output.decode().splitlines(), body)
                        print(f"redirect_{framing}_{close_order}: status=200 exact_body=true "
                              "eof_read=0 transaction_done=true target_still_open=true", flush=True)
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=3)

    def run_reuse(self, chunked):
        bodies = (b"first\x00body\n", b"second\x00distinct\n")
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            process = subprocess.Popen(
                [ARGS.probe, f"http://127.0.0.1:{listener.getsockname()[1]}/fixture", "reuse", "twice"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
            )
            try:
                with listener.accept()[0] as peer:
                    for index, body in enumerate(bodies):
                        self.read_request(peer, "/fixture", reuse=True)
                        self.assertFalse(select.select([process.stdout], [], [], 0.15)[0],
                                         "GET returned before its response")
                        response = b"HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n"
                        if chunked:
                            response += (b"Transfer-Encoding: chunked\r\n\r\n" +
                                         f"{len(body):x}\r\n".encode() + body + b"\r\n0\r\n\r\n")
                        else:
                            response += f"Content-Length: {len(body)}\r\n\r\n".encode() + body
                        peer.sendall(response)
                        lines = []
                        for _ in range(2):
                            self.assertTrue(select.select([process.stdout], [], [], 1.5)[0],
                                            "keep-alive response did not complete")
                            lines.append(process.stdout.readline().decode().strip())
                        self.assert_result(lines, body)
                        if index == 0:
                            process.stdin.write(b"\n")
                            process.stdin.flush()
                    output, errors = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, 0, errors.decode())
                    self.assertEqual(output, b"")
                    print(f"reuse_{'chunked' if chunked else 'fixed'}: requests=2 "
                          "same_connection=true exact_bodies=true eof_read=0", flush=True)
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=3)

    def run_response(self, name, body=BODY, split=False, reuse=False, fixed=True,
                     chunked=False, terminal="coalesced", host="127.0.0.1"):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            port = listener.getsockname()[1]
            process = subprocess.Popen(
                [ARGS.probe, f"http://{host}:{port}/fixture", "reuse" if reuse else "close"],
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
                    self.assertIn(f"Host: {host}\r\n".encode(), request)
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

    def test_redirect_fixed_immediate(self):
        self.run_redirect("fixed", "immediate")

    def test_redirect_fixed_overlap(self):
        self.run_redirect("fixed", "overlap")

    def test_redirect_empty_immediate(self):
        self.run_redirect("empty", "immediate")

    def test_redirect_empty_overlap(self):
        self.run_redirect("empty", "overlap")

    def test_redirect_chunked_immediate(self):
        self.run_redirect("chunked", "immediate")

    def test_redirect_chunked_overlap(self):
        self.run_redirect("chunked", "overlap")

    def test_fixed_actual_reuse(self):
        self.run_reuse(chunked=False)

    def test_chunked_actual_reuse(self):
        self.run_reuse(chunked=True)

    def test_hostname_system_resolver(self):
        # Mongoose's own parser rejects 127.1 and 8.8.8.8 answers NXDOMAIN;
        # the system resolver maps it to 127.0.0.1 without any network.
        self.run_response("hostname_system_resolver", host="127.1")

    def test_connection_creation_failure(self):
        result = subprocess.run([ARGS.probe, "", "close"], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        lines = result.stdout.decode().splitlines()
        self.assertEqual(lines, ["GET_RETURNED", "900 0 0 -1 -1 -1 0 1 "])

    def run_no_body(self, method, status, reuse, informational=None):
        length = None if status == 204 else 123
        mode = "head" if method == "HEAD" else "no-body"
        if reuse:
            mode += "-twice"
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            process = subprocess.Popen(
                [ARGS.probe, f"http://127.0.0.1:{listener.getsockname()[1]}/fixture",
                 "reuse" if reuse else "close", mode],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
            )
            try:
                with listener.accept()[0] as peer:
                    self.read_request(peer, "/fixture", reuse, method)
                    response = (f"HTTP/1.1 {status} Final\r\nConnection: keep-alive\r\n"
                                'ETag: "representation"\r\nContent-Type: text/plain\r\n').encode()
                    if length is not None:
                        response += f"Content-Length: {length}\r\n".encode()
                    response += b"\r\n"
                    if informational:
                        interim = b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\n\r\n"
                        if informational == "separate":
                            peer.sendall(interim)
                            self.assertFalse(select.select([process.stdout], [], [], 0.25)[0],
                                             "request completed at informational headers")
                        else:
                            response = interim + response
                    started = time.monotonic()
                    peer.sendall(response)
                    lines = []
                    for _ in range(3):
                        self.assertTrue(select.select([process.stdout], [], [], 1.5)[0],
                                        "no-body response did not complete while peer held open")
                        lines.append(process.stdout.readline().decode().strip())
                    self.assertEqual(lines[0], f"{method}_RETURNED")
                    fields = lines[1].split()
                    self.assertEqual(len(fields), 8, "no-body response exposed body bytes")
                    self.assertEqual(list(map(int, fields)),
                                     [status, length or 0, 0, 0, 0, 0, 0, 1])
                    self.assertEqual(lines[2],
                                     f'{length if length is not None else ""}|"representation"|text/plain|')
                    print(f"{method}_{status}_reuse={reuse}_interim={informational}: "
                          f"complete_before_peer_close=true elapsed_ms={int((time.monotonic()-started)*1000)} "
                          "metadata_preserved=true empty_body=true eof_read=0 transaction_done=true",
                          flush=True)
                    if reuse:
                        process.stdin.write(b"\n")
                        process.stdin.flush()
                        self.read_request(peer, "/fixture", reuse=True)
                        peer.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(BODY)}\r\n\r\n".encode())
                        self.assertFalse(select.select([process.stdout], [], [], 0.25)[0],
                                         "ordinary GET completed before its declared body arrived")
                        peer.sendall(BODY)
                        lines = []
                        for _ in range(3):
                            self.assertTrue(select.select([process.stdout], [], [], 1.5)[0])
                            lines.append(process.stdout.readline().decode().strip())
                        self.assert_result(lines[:2], BODY)
                    output, errors = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, 0, errors.decode())
                    self.assertEqual(output, b"")
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=3)

    def test_no_body(self):
        for method, status in (("GET", 304), ("HEAD", 200), ("GET", 204)):
            for reuse in (False, True):
                with self.subTest(method=method, status=status, reuse=reuse):
                    self.run_no_body(method, status, reuse)

    def test_head_informational(self):
        for reuse in (False, True):
            for informational in ("separate", "coalesced"):
                with self.subTest(reuse=reuse, informational=informational):
                    self.run_no_body("HEAD", 200, reuse, informational)

    def run_response_completion(self, method, status, reuse, close_peer, chunked=False, body=b""):
        mode = "head" if method == "HEAD" else "no-body"
        if close_peer:
            mode += "-close"
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)
            process = subprocess.Popen(
                [ARGS.probe, f"http://127.0.0.1:{listener.getsockname()[1]}/fixture",
                 "reuse" if reuse else "close", mode],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
            )
            try:
                with listener.accept()[0] as peer:
                    self.read_request(peer, "/fixture", reuse, method)
                    response = f"HTTP/1.1 {status} Response\r\n".encode()
                    if status == 101:
                        response += b"Connection: Upgrade\r\nUpgrade: example\r\n"
                    else:
                        response += b"Connection: keep-alive\r\n"
                    if chunked:
                        response += b"Transfer-Encoding: chunked\r\n"
                    response += b'ETag: "completion"\r\n\r\n' + body
                    peer.sendall(response)
                    ready = None
                    if close_peer:
                        peer.shutdown(socket.SHUT_WR)
                    else:
                        deadline = 0.25 if status == 101 else 1.5
                        ready = bool(select.select([process.stdout], [], [], deadline)[0])
                        if not ready:
                            peer.shutdown(socket.SHUT_WR)
                    output, errors = process.communicate(timeout=3)
                    self.assertEqual(process.returncode, 0, errors.decode())
                    lines = output.decode().splitlines()
                    self.assertEqual(len(lines), 3)
                    fields = list(map(int, lines[1].split()[:8]))
                    print(f"response_completion method={method} status={status} reuse={reuse} close={close_peer} "
                          f"chunked={chunked} input_body={body.hex()} ready_held_open={ready} "
                          f"result={fields} metadata={lines[2]}", flush=True)
                    return ready, fields, lines[2]
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=3)

    def test_switching_protocols(self):
        for method in ("GET", "HEAD"):
            for reuse in (False, True):
                for close_peer in (False, True):
                    with self.subTest(method=method, reuse=reuse, close=close_peer):
                        ready, fields, metadata = self.run_response_completion(method, 101, reuse, close_peer)
                        self.assertEqual(fields, [101, -1, 0, 0, 0, 0, 0, 1])
                        self.assertEqual(metadata, '|"completion"||')
                        if not close_peer:
                            self.assertEqual(ready, method == "HEAD" and reuse)

    def test_no_body_transfer_encoding(self):
        for method, status in (("HEAD", 200), ("GET", 304)):
            for reuse in (False, True):
                for close_peer in (False, True):
                    with self.subTest(method=method, status=status, reuse=reuse, close=close_peer):
                        ready, fields, metadata = self.run_response_completion(
                            method, status, reuse, close_peer, chunked=True)
                        self.assertEqual(fields, [status, -1, 0, 0, 0, 0, 0, 1])
                        self.assertEqual(metadata, '|"completion"||chunked')
                        if not close_peer:
                            self.assertTrue(ready, "bodyless Transfer-Encoding waited for peer close")

    def test_truncated_chunked(self):
        for reuse in (False, True):
            for body in (b"3\r\nabc\r\n", b"5\r\nabc"):
                with self.subTest(reuse=reuse, body=body):
                    _, fields, _ = self.run_response_completion("GET", 200, reuse, True, chunked=True, body=body)
                    self.assertEqual(fields[0], 902)
                    self.assertEqual(fields[5:], [-1, 0, 1])


if __name__ == "__main__":
    unittest.main(argv=[__file__] + TEST_ARGS, verbosity=2)
