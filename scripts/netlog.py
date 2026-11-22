"""Read a Hybrid-LoRa board's log over Wi-Fi (no USB cable needed).

The firmware (v0.5.4+) serves its log on TCP port 23 and announces
hybrid-node-NN.local / hybrid-gateway.local over mDNS.

  python scripts/netlog.py 192.168.1.5                 # live, type commands
  python scripts/netlog.py 192.168.1.5 --seconds 60    # read 60 s and exit

Use the IP: the board prints it ('Net log: ... (IP x.x.x.x)') and the node
reports it as nodeIp on ThingsBoard. <name>.local did not resolve on the
Windows PC (tested 2026-11-16).

Lines typed here are sent to the board (TEST, TEST 0, QUIET 1, ISO...).
Output is also saved to logs/net-<host>-<YYMMDD-HHMMSS>.log with timestamps.
Reconnects by itself if the board restarts or Wi-Fi drops.
"""
import argparse
import datetime
import os
import socket
import sys
import threading
import time

PORT = 23
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", help="board IP (see Net log line or nodeIp)")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N s (0 = run until Ctrl+C)")
    args = ap.parse_args()

    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    stamp = datetime.datetime.now().strftime("%y%m%d-%H%M%S")
    safe = args.host.replace(":", "_")
    path = os.path.join(ROOT, "logs", f"net-{safe}-{stamp}.log")
    log = open(path, "a", encoding="utf-8")
    print(f"Log file: {path}", flush=True)

    end = time.time() + args.seconds if args.seconds else None
    state = {"sock": None}

    def typed():  # forward keyboard lines to the board
        for line in sys.stdin:
            s = state["sock"]
            if s:
                try:
                    s.sendall(line.rstrip("\r\n").encode() + b"\n")
                except OSError:
                    pass

    if args.seconds == 0 and sys.stdin.isatty():
        threading.Thread(target=typed, daemon=True).start()

    pending = ""
    while end is None or time.time() < end:
        try:
            s = socket.create_connection((args.host, PORT), timeout=5)
        except OSError as e:
            print(f"[netlog] cannot connect to {args.host}:{PORT} ({e}); retry in 3 s", flush=True)
            time.sleep(3)
            continue
        s.settimeout(1)
        state["sock"] = s
        print(f"[netlog] connected to {args.host}:{PORT}", flush=True)
        try:
            while end is None or time.time() < end:
                try:
                    data = s.recv(1024)
                except socket.timeout:
                    continue
                if not data:
                    raise OSError("closed by board")
                pending += data.decode("utf-8", "replace")
                while "\n" in pending:
                    line, pending = pending.split("\n", 1)
                    line = line.rstrip("\r")
                    t = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    print(f"{t} > {line}", flush=True)
                    log.write(f"{t} > {line}\n")
                    log.flush()
        except OSError as e:
            print(f"[netlog] disconnected ({e}); reconnecting", flush=True)
        finally:
            state["sock"] = None
            s.close()
    log.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
