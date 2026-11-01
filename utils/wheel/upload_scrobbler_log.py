#!/usr/bin/env python3
"""Send a Rockbox .scrobbler.log to Last.fm from a computer.

For players that never get online. Works with the log written by the
Rockbox Y1 Wheel edition (#TZ/UTC) and with classic Rockbox logs
(#TZ/UNKNOWN - pass --utc-offset so the times come out right).

Usage:
    python3 upload_scrobbler_log.py /path/to/.scrobbler-wheel.log
    python3 upload_scrobbler_log.py .scrobbler.log --utc-offset +2   # old Rockbox logs

Credentials are read from lastfm.cfg (same file as on the player) or asked
for. Nothing but the Python standard library is needed. After a complete
upload the log is renamed to .scrobbler.log.sent-YYYYmmdd-HHMMSS.
"""
import argparse, getpass, hashlib, json, os, sys, time, urllib.parse, urllib.request

API = "https://ws.audioscrobbler.com/2.0/"
BATCH = 50


def read_cfg(path):
    cfg = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if v.strip():
                cfg[k.strip().lower()] = v.strip()
    return cfg


def sign(params, secret):
    s = "".join(k + params[k] for k in sorted(params)) + secret
    return hashlib.md5(s.encode("utf-8")).hexdigest()


def call(params, key, secret):
    p = dict(params, api_key=key)
    p["api_sig"] = sign(p, secret)
    p["format"] = "json"
    data = urllib.parse.urlencode(p).encode("utf-8")
    req = urllib.request.Request(API, data=data, headers={"User-Agent": "Rockbox Y1 Wheel uploader"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
    res = json.loads(body)
    if "error" in res:
        raise RuntimeError("Last.fm error %s: %s" % (res["error"], res.get("message")))
    return res


def parse_log(path, offset_s):
    tz_utc, entries = False, []
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if line.startswith("#"):
            if line.upper().startswith("#TZ/UTC"):
                tz_utc = True
            continue
        f = line.split("\t")
        if len(f) < 7:
            continue
        artist, album, title, tracknum, length, rating, ts = f[:7]
        mbid = f[7] if len(f) > 7 else ""
        if rating != "L" or not artist or not title:
            continue
        try:
            t = int(ts)
        except ValueError:
            continue
        if not tz_utc:
            t -= offset_s  # local time written as if it were UTC
        entries.append(dict(artist=artist, album=album, track=title, trackNumber=tracknum,
                            duration=length, timestamp=str(t), mbid=mbid))
    return entries, tz_utc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--cfg", help="lastfm.cfg with api_key/api_secret/username (default: next to the log)")
    ap.add_argument("--utc-offset", type=float, default=0.0,
                    help="hours the player clock was ahead of UTC (only for #TZ/UNKNOWN logs)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    cfg_path = a.cfg or os.path.join(os.path.dirname(os.path.abspath(a.log)), ".rockbox", "lastfm.cfg")
    cfg = read_cfg(cfg_path)
    entries, tz_utc = parse_log(a.log, int(a.utc_offset * 3600))
    two_weeks = time.time() - 14 * 86400
    old = [e for e in entries if int(e["timestamp"]) < two_weeks]
    entries = [e for e in entries if int(e["timestamp"]) >= two_weeks]
    print("%d listens to send%s" % (len(entries), "" if tz_utc else " (log without timezone)"))
    if old:
        print("note: %d listens are older than 14 days; Last.fm no longer accepts those" % len(old))
    if a.dry_run or not entries:
        return

    key = cfg.get("api_key") or input("Last.fm API key: ").strip()
    secret = cfg.get("api_secret") or input("Last.fm API secret: ").strip()
    user = cfg.get("username") or input("Last.fm username: ").strip()
    pw = cfg.get("password") or getpass.getpass("Last.fm password: ")
    sk = cfg.get("session_key") or call({"method": "auth.getMobileSession", "username": user,
                                         "password": pw}, key, secret)["session"]["key"]
    sent = 0
    for i in range(0, len(entries), BATCH):
        p = {"method": "track.scrobble", "sk": sk}
        for j, e in enumerate(entries[i:i + BATCH]):
            for k, v in e.items():
                if v:
                    p["%s[%d]" % (k, j)] = v
        res = call(p, key, secret)
        acc = int(res["scrobbles"]["@attr"]["accepted"])
        sent += acc
        print("  batch %d: %d accepted" % (i // BATCH + 1, acc))
    print("done: %d scrobbles accepted" % sent)
    os.rename(a.log, a.log + time.strftime(".sent-%Y%m%d-%H%M%S"))


if __name__ == "__main__":
    main()
