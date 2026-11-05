#!/usr/bin/env python3
"""Mock services for testing the Y1 Wheel Java helpers on a PC.

  * an Audioscrobbler 2.0 endpoint (POST /2.0/) that checks api_sig exactly
    like Last.fm does and keeps a log of every call
  * an LRCLIB-like lyrics API (GET /api/get, /api/search)
  * an OBEX Object Push receiver on a TCP port (stands in for a phone)

Control: GET /control/log (JSON), /control/reset, /control/expire_session,
/control/approve_token, /control/obex_reject=1|0
Start: mock_services.py HTTP_PORT OBEX_PORT OUT_DIR
"""
import hashlib, json, os, socket, struct, sys, threading, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

API_KEY, SECRET = "testkey", "testsecret"
USER, PASSWORD = "natalia", "s3cret"

state = {"calls": [], "sk": "SK-1", "token_ok": False, "obex": [], "obex_reject": False,
         "fail_scrobble_once": 0}
lock = threading.Lock()


def lfm_error(code, msg):
    return {"error": code, "message": msg}


def check_sig(p):
    sig = p.get("api_sig", "")
    raw = "".join(k + p[k] for k in sorted(p) if k not in ("api_sig", "format", "callback")) + SECRET
    return hashlib.md5(raw.encode("utf-8")).hexdigest() == sig


def audioscrobbler(p):
    m = p.get("method", "")
    with lock:
        state["calls"].append(p)
    if p.get("api_key") != API_KEY:
        return lfm_error(10, "Invalid API key")
    if not check_sig(p):
        return lfm_error(13, "Invalid method signature supplied")
    if m == "auth.getMobileSession":
        if p.get("username") == USER and p.get("password") == PASSWORD:
            return {"session": {"name": USER, "key": state["sk"], "subscriber": 0}}
        return lfm_error(4, "Authentication Failed")
    if m == "auth.getToken":
        return {"token": "TOKEN-123"}
    if m == "auth.getSession":
        if p.get("token") != "TOKEN-123":
            return lfm_error(4, "Invalid authentication token")
        if not state["token_ok"]:
            return lfm_error(14, "This token has not been authorized")
        return {"session": {"name": USER, "key": state["sk"], "subscriber": 0}}
    # everything below needs a session
    if p.get("sk") != state["sk"]:
        return lfm_error(9, "Invalid session key - Please re-authenticate")
    if m == "track.updateNowPlaying":
        if not p.get("artist") or not p.get("track"):
            return lfm_error(6, "Invalid parameters")
        return {"nowplaying": {"track": {"#text": p["track"]}, "artist": {"#text": p["artist"]}}}
    if m == "track.scrobble":
        with lock:
            if state["fail_scrobble_once"] > 0:
                state["fail_scrobble_once"] -= 1
                return lfm_error(16, "There was a temporary error processing your request")
        n = 0
        while "artist[%d]" % n in p:
            if not p.get("track[%d]" % n) or not p.get("timestamp[%d]" % n):
                return lfm_error(6, "Invalid parameters")
            n += 1
        if n == 0 or n > 50:
            return lfm_error(6, "Invalid parameters (batch of %d)" % n)
        return {"scrobbles": {"@attr": {"accepted": n, "ignored": 0}, "scrobble": []}}
    if m == "track.love":
        return {}
    return lfm_error(3, "Invalid Method")


LYRICS = {  # original test text, not real lyrics
    ("test artist", "test song"): {
        "id": 1, "trackName": "Test Song", "artistName": "Test Artist", "albumName": "Tests",
        "duration": 180, "instrumental": False,
        "plainLyrics": "first line\nsecond line\n",
        "syncedLyrics": "[00:01.00] first line\n[00:05.50] second line\n"},
    ("plain artist", "plain song"): {
        "id": 2, "trackName": "Plain Song", "artistName": "Plain Artist", "duration": 120,
        "instrumental": False, "plainLyrics": "only plain words\n", "syncedLyrics": None},
}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send_json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        if not self.path.startswith("/2.0"):
            return self.send_json({"error": "not found"}, 404)
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n).decode("utf-8")
        p = {k: v[0] for k, v in urllib.parse.parse_qs(body, keep_blank_values=True).items()}
        r = audioscrobbler(p)
        self.send_json(r, 400 if "error" in r and r["error"] in (6, 10, 13) else 200)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        if u.path == "/control/log":
            with lock:
                return self.send_json({"calls": state["calls"], "obex": state["obex"]})
        if u.path == "/control/reset":
            with lock:
                state["calls"].clear(); state["obex"].clear(); state["token_ok"] = False
            return self.send_json({})
        if u.path == "/control/expire_session":
            state["sk"] = "SK-2"
            return self.send_json({})
        if u.path == "/control/approve_token":
            state["token_ok"] = True
            return self.send_json({})
        if u.path == "/control/fail_scrobble_once":
            state["fail_scrobble_once"] = 1
            return self.send_json({})
        if u.path == "/control/obex_reject":
            state["obex_reject"] = q.get("on") == "1"
            return self.send_json({})
        if u.path == "/sonora/episode":
            # 300 KB of a repeating pattern, no Content-Length shortcuts
            data = bytes((i * 7) & 0xff for i in range(300 * 1024))
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if u.path == "/sonora/redirect":
            self.send_response(302)
            self.send_header("Location", "/sonora/episode?via=redirect")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if u.path == "/api/get":
            key = (q.get("artist_name", "").lower(), q.get("track_name", "").lower())
            if key in LYRICS:
                return self.send_json(LYRICS[key])
            return self.send_json({"code": 404, "name": "TrackNotFound"}, 404)
        if u.path == "/api/search":
            t = q.get("track_name", "").lower()
            return self.send_json([v for k, v in LYRICS.items() if t and t in k[1]])
        self.send_json({"error": "not found"}, 404)


# ------------------------------ OBEX OPP ------------------------------

def read_exact(c, n):
    b = b""
    while len(b) < n:
        x = c.recv(n - len(b))
        if not x:
            raise EOFError
        b += x
    return b


def read_packet(c):
    h = read_exact(c, 3)
    ln = struct.unpack(">H", h[1:3])[0]
    return h[0], read_exact(c, ln - 3)


def headers(data):
    out, i = {}, 0
    while i < len(data):
        hi = data[i]
        kind = hi & 0xC0
        if kind in (0x00, 0x40):
            ln = struct.unpack(">H", data[i + 1:i + 3])[0]
            out.setdefault(hi, b"")
            out[hi] += data[i + 3:i + ln]
            i += ln
        elif kind == 0x80:
            out[hi] = data[i + 1]; i += 2
        else:
            out[hi] = struct.unpack(">I", data[i + 1:i + 5])[0]; i += 5
    return out


def obex_session(c, out_dir):
    op, data = read_packet(c)
    assert op == 0x80, "expected CONNECT"
    version, flags, maxlen = data[0], data[1], struct.unpack(">H", data[2:4])[0]
    # SUCCESS, version 1.0, our max packet 1024 (small on purpose: tests chunking),
    # and a connection id the client must echo
    resp = bytes([0x10, 0, 0x04, 0x00]) + bytes([0xCB]) + struct.pack(">I", 7)
    c.sendall(bytes([0xA0]) + struct.pack(">H", 3 + len(resp)) + resp)
    cur = None
    while True:
        op, data = read_packet(c)
        if op == 0x81:  # DISCONNECT
            c.sendall(b"\xA0\x00\x03")
            return
        if op in (0x02, 0x82):
            h = headers(data)
            if 0xCB in h and h[0xCB] != 7:
                c.sendall(b"\xC3\x00\x03")  # forbidden: wrong connection id
                return
            if cur is None:
                name = h.get(0x01, b"").decode("utf-16-be").rstrip("\x00")
                cur = {"name": name, "type": h.get(0x42, b"").rstrip(b"\x00").decode(),
                       "length": h.get(0xC3), "data": b"", "max": maxlen}
                if state["obex_reject"]:
                    c.sendall(b"\xC3\x00\x03")
                    cur = None
                    continue
            cur["data"] += h.get(0x48, b"") + h.get(0x49, b"")
            if op == 0x82:
                path = os.path.join(out_dir, cur["name"])
                with open(path, "wb") as f:
                    f.write(cur["data"])
                with lock:
                    state["obex"].append({"name": cur["name"], "type": cur["type"],
                                          "length": cur["length"], "received": len(cur["data"]),
                                          "md5": hashlib.md5(cur["data"]).hexdigest()})
                cur = None
                c.sendall(b"\xA0\x00\x03")
            else:
                c.sendall(b"\x90\x00\x03")
            continue
        c.sendall(b"\xD1\x00\x03")  # not implemented


def obex_server(port, out_dir):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port)); s.listen(4)
    while True:
        c, _ = s.accept()
        try:
            obex_session(c, out_dir)
        except Exception as e:
            print("obex:", e, file=sys.stderr)
        finally:
            c.close()


if __name__ == "__main__":
    http_port, obex_port, out_dir = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    os.makedirs(out_dir, exist_ok=True)
    threading.Thread(target=obex_server, args=(obex_port, out_dir), daemon=True).start()
    ThreadingHTTPServer(("127.0.0.1", http_port), H).serve_forever()
