"""Range-test map: join the gateway's per-packet log with a GPS track.

The node has no GPS. Carry a phone that records a GPX track (any GPX logger
app) while walking the node away; the gateway log gives the time, seq,
RSSI, SNR and lost packets. Each packet gets the phone's position at that
time, so the map shows where the link was strong, weak or lost.

  python scripts/range_map.py --gpx walk.gpx --log logs/device-monitor-261116-162904.log
        [--log logs/net-192.168.1.6-*.log ...] [--gateway 10.8231,106.6297]
        [--out logs/range-map] [--every 1] [--max-gap 60]

Inputs
  --log   gateway Serial log (logs/device-monitor-YYMMDD-*.log, written by the
          monitor window) or Wi-Fi log (logs/net-<ip>-YYMMDD-*.log). Lines:
          "HH:MM:SS.mmm > TEST node=1 seq=.. RSSI=.. SNR=.. lost=.." and
          "HH:MM:SS.mmm > RX STATUS node=1 ... FERR=.. Hz". The date comes
          from the file name; times are the PC's local time.
  --gpx   track from the phone (UTC times, as GPX requires).
  --gateway lat,lon   adds the gateway marker and a distance column.

Outputs (default logs/range-map-<date>.*)
  .kml  colored points + track: import in Google My Maps
        (mymaps.google.com > Create > Import) or open in Google Earth.
  .csv  one row per packet (also importable in My Maps, lat/lon columns).

Colors use SNR (RSSI saturates when the boards are close):
  green SNR >= 0 dB, yellow -10..0, orange -15..-10, red < -15 (near the
  SF12 limit of about -20 dB), black X = packet lost. Lost packets are
  placed at their estimated send time (between the packets around them).
Packets more than --max-gap seconds away from any GPS point are skipped.
"""
import argparse
import csv
import datetime as dt
import glob
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

LOCAL_TZ = dt.datetime.now().astimezone().tzinfo
LINE = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) > (.*)$")
TEST = re.compile(r"TEST node=(\d+) seq=(\d+) tx=(-?\d+)dBm RSSI=(-?\d+) SNR=(-?[\d.]+) "
                  r"\(\+(-?\d+) dB\) lost=(\d+)")
RX = re.compile(r"RX STATUS node=(\d+) RSSI=(-?\d+) dBm SNR=(-?[\d.]+) dB FERR=(-?\d+) Hz")
NAME_DATE = re.compile(r"-(\d{6})-(\d{6})\.log$")


def parse_log(path):
    """Yield dicts for received and lost packets, in time order."""
    m = NAME_DATE.search(os.path.basename(path))
    if not m:
        sys.exit(f"{path}: file name has no -YYMMDD-HHMMSS date")
    day = dt.datetime.strptime(m.group(1), "%y%m%d").date()
    start = dt.datetime.strptime(m.group(1) + m.group(2), "%y%m%d%H%M%S")
    prev_t = None
    last = {}  # node -> (time, seq)
    pending = None
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for raw in f:
            lm = LINE.match(raw.strip())
            if not lm:
                continue
            h, mi, s, ms, text = lm.groups()
            t = dt.datetime.combine(day, dt.time(int(h), int(mi), int(s), int(ms) * 1000))
            if t < start - dt.timedelta(minutes=1) or (prev_t and t < prev_t - dt.timedelta(hours=1)):
                day += dt.timedelta(days=1)  # passed midnight
                t += dt.timedelta(days=1)
            prev_t = t
            t = t.replace(tzinfo=LOCAL_TZ)
            tm = TEST.search(text)
            if tm:
                node, seq, tx, rssi, snr, off, lost = tm.groups()
                node, seq, lost = int(node), int(seq), int(lost)
                if lost and node in last:
                    t0, s0 = last[node]
                    for i in range(1, lost + 1):  # estimated send times
                        yield {"time": t0 + (t - t0) * i / (lost + 1), "node": node,
                               "seq": (s0 + i) & 0xFFFF, "status": "lost"}
                pending = {"time": t, "node": node, "seq": seq, "status": "ok",
                           "rssi": int(rssi), "snr": float(snr), "tx_dbm": int(tx),
                           "offset_db": int(off)}
                last[node] = (t, seq)
                continue
            rm = RX.search(text)
            if rm and pending and int(rm.group(1)) == pending["node"]:
                pending["ferr_hz"] = int(rm.group(4))
                yield pending
                pending = None
    if pending:
        yield pending


def parse_gpx(path):
    pts = []
    for el in ET.parse(path).iter():
        if el.tag.endswith("trkpt") or el.tag.endswith("wpt") or el.tag.endswith("rtept"):
            tt = next((c.text for c in el if c.tag.endswith("time")), None)
            if not tt:
                continue
            tt = tt.strip().replace("Z", "+00:00")
            when = dt.datetime.fromisoformat(tt)
            if when.tzinfo is None:
                when = when.replace(tzinfo=dt.timezone.utc)
            pts.append((when, float(el.get("lat")), float(el.get("lon"))))
    pts.sort()
    if not pts:
        sys.exit(f"{path}: no timed track points")
    return pts


def position(pts, t, max_gap):
    """Linear interpolation between the GPS points around t."""
    lo, hi = 0, len(pts)
    while lo < hi:
        mid = (lo + hi) // 2
        if pts[mid][0] < t:
            lo = mid + 1
        else:
            hi = mid
    before = pts[lo - 1] if lo > 0 else None
    after = pts[lo] if lo < len(pts) else None
    if before and after:
        span = (after[0] - before[0]).total_seconds()
        if span > 2 * max_gap:
            near = min((before, after), key=lambda p: abs((p[0] - t).total_seconds()))
            return (near[1], near[2]) if abs((near[0] - t).total_seconds()) <= max_gap else None
        k = 0 if span == 0 else (t - before[0]).total_seconds() / span
        return before[1] + (after[1] - before[1]) * k, before[2] + (after[2] - before[2]) * k
    near = before or after
    return (near[1], near[2]) if abs((near[0] - t).total_seconds()) <= max_gap else None


def distance_m(a, b):
    r = 6371000.0
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dp, dl = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(h))


def bucket(p):
    if p["status"] == "lost":
        return "lost"
    s = p["snr"]
    return "good" if s >= 0 else "fair" if s >= -10 else "weak" if s >= -15 else "edge"


STYLES = {  # KML colors are aabbggrr
    "good": ("ff00c000", "SNR >= 0 dB"),
    "fair": ("ff00e0ff", "SNR -10..0 dB"),
    "weak": ("ff0080ff", "SNR -15..-10 dB"),
    "edge": ("ff0000ff", "SNR < -15 dB"),
    "lost": ("ff000000", "lost"),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gpx", required=True)
    ap.add_argument("--log", required=True, action="append", help="gateway log (glob ok), repeatable")
    ap.add_argument("--gateway", help="lat,lon of the gateway")
    ap.add_argument("--out", help="output path without extension")
    ap.add_argument("--every", type=int, default=1, help="keep every Nth received packet (lost: all)")
    ap.add_argument("--max-gap", type=float, default=60, help="max seconds to the nearest GPS point")
    args = ap.parse_args()

    files = sorted({f for pat in args.log for f in glob.glob(pat)})
    if not files:
        sys.exit("no log file matches --log")
    packets = sorted((p for f in files for p in parse_log(f)), key=lambda p: p["time"])
    track = parse_gpx(args.gpx)
    gw = tuple(float(x) for x in args.gateway.split(",")) if args.gateway else None

    rows, kept_ok, no_fix = [], 0, 0
    for p in packets:
        if p["status"] == "ok":
            kept_ok += 1
            if (kept_ok - 1) % args.every:
                continue
        pos = position(track, p["time"].astimezone(dt.timezone.utc), args.max_gap)
        if not pos:
            no_fix += 1
            continue
        p["lat"], p["lon"] = pos
        p["dist_m"] = round(distance_m(gw, pos)) if gw else ""
        p["class"] = bucket(p)
        rows.append(p)
    if not rows:
        sys.exit(f"no packet falls inside the GPS track ({len(packets)} packets, "
                 f"log {packets[0]['time']:%H:%M}..{packets[-1]['time']:%H:%M} local, "
                 f"GPX {track[0][0].astimezone(LOCAL_TZ):%H:%M}..{track[-1][0].astimezone(LOCAL_TZ):%H:%M} local)"
                 if packets else "no packets in the log")

    out = args.out or os.path.join("logs", f"range-map-{rows[0]['time']:%y%m%d-%H%M}")
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)

    with open(out + ".csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["time_local", "node", "seq", "status", "class", "lat", "lon", "dist_m",
                    "rssi_dbm", "snr_db", "ferr_hz", "tx_dbm", "offset_db"])
        for p in rows:
            w.writerow([p["time"].strftime("%Y-%m-%d %H:%M:%S.%f")[:-3], p["node"], p["seq"],
                        p["status"], p["class"], f"{p['lat']:.6f}", f"{p['lon']:.6f}", p["dist_m"],
                        p.get("rssi", ""), p.get("snr", ""), p.get("ferr_hz", ""),
                        p.get("tx_dbm", ""), p.get("offset_db", "")])

    k = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<kml xmlns="http://www.opengis.net/kml/2.2"><Document>',
         f"<name>Hybrid-LoRa range {rows[0]['time']:%Y-%m-%d %H:%M}</name>"]
    for name, (color, _) in STYLES.items():
        icon = ("http://maps.google.com/mapfiles/kml/shapes/forbidden.png" if name == "lost"
                else "http://maps.google.com/mapfiles/kml/shapes/shaded_dot.png")
        k.append(f'<Style id="{name}"><IconStyle><color>{color}</color><scale>0.6</scale>'
                 f"<Icon><href>{icon}</href></Icon></IconStyle></Style>")
    k.append('<Style id="track"><LineStyle><color>80ff8000</color><width>2</width></LineStyle></Style>')
    k.append("<Placemark><name>GPS track</name><styleUrl>#track</styleUrl><LineString><coordinates>"
             + " ".join(f"{lon:.6f},{lat:.6f}" for _, lat, lon in track)
             + "</coordinates></LineString></Placemark>")
    if gw:
        k.append(f"<Placemark><name>Gateway</name><Point><coordinates>{gw[1]},{gw[0]}"
                 "</coordinates></Point></Placemark>")
    for name, (_, label) in STYLES.items():
        k.append(f"<Folder><name>{label}</name>")
        for p in (r for r in rows if r["class"] == name):
            desc = f"{p['time']:%H:%M:%S} seq {p['seq']}"
            if p["status"] == "ok":
                desc += (f" | RSSI {p['rssi']} dBm, SNR {p['snr']} dB, FERR {p.get('ferr_hz', '?')} Hz"
                         f" (tx {p['tx_dbm']} dBm, +{p['offset_db']} dB)")
            else:
                desc += " | LOST"
            if p["dist_m"] != "":
                desc += f" | {p['dist_m']} m from gateway"
            k.append(f"<Placemark><name>{p['seq']}</name><description>{desc}</description>"
                     f"<TimeStamp><when>{p['time'].isoformat()}</when></TimeStamp>"
                     f"<styleUrl>#{name}</styleUrl><Point><coordinates>{p['lon']:.6f},{p['lat']:.6f}"
                     "</coordinates></Point></Placemark>")
        k.append("</Folder>")
    k.append("</Document></kml>")
    with open(out + ".kml", "w", encoding="utf-8") as f:
        f.write("\n".join(k))

    counts = {n: sum(r["class"] == n for r in rows) for n in STYLES}
    ok = sum(r["status"] == "ok" for r in rows)
    print(f"{len(rows)} points ({ok} received, {counts['lost']} lost) from {len(files)} log(s); "
          f"{no_fix} skipped without GPS fix")
    print("  " + ", ".join(f"{STYLES[n][1]}: {c}" for n, c in counts.items()))
    if gw:
        far_ok = max((r["dist_m"] for r in rows if r["status"] == "ok"), default=0)
        print(f"  farthest received packet: {far_ok} m from the gateway")
    print(f"Wrote {out}.kml and {out}.csv")


if __name__ == "__main__":
    main()
