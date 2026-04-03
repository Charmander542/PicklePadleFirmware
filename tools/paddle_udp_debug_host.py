#!/usr/bin/env python3
"""
UDP debug host — mimics the Unreal/game PC side of the PicklePaddle link.

  - Listens on the host port (default 4210) for UDP datagrams FROM the paddle.
  - Sends UDP datagrams TO the paddle on its listen port (default 4211).

Configure the paddle's "Host IP" in the WiFi portal to this machine's LAN address.
No third-party packages (stdlib only).

Paddle → game (logged here)
  Typical payloads: "detect btn push", "detect btn hold", and gameplay jerk as one float
  (m/s^3) e.g. "1523.4" — parse that in UE as an impulse.

Game → paddle (type in this tool or use --send)
  swing hit   → host swing feedback (LED/haptic cue on paddle)
  idle        → UI mode idle
  gameplay    → UI mode gameplay (alias: game)
"""
from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from datetime import datetime, timezone


def _ts() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S") + "Z"


def recv_loop(sock: socket.socket, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            sock.settimeout(0.4)
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        text = data.decode("utf-8", errors="replace").rstrip("\r\n")
        extra = ""
        try:
            float(text)
            extra = "  <-- jerk (m/s^3), gameplay impulse"
        except ValueError:
            pass
        print(f"[{_ts()}] RX {addr[0]}:{addr[1]}  {text!r}{extra}", flush=True)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Log paddle UDP traffic (game / UE side) and send control lines back."
    )
    p.add_argument(
        "--bind",
        default="0.0.0.0",
        help="Address to bind for incoming paddle traffic (default: all interfaces)",
    )
    p.add_argument(
        "--listen-port",
        type=int,
        default=4210,
        help="Port the paddle sends TO (must match Host UDP port in NVS / portal; default 4210)",
    )
    p.add_argument(
        "--paddle",
        default="",
        metavar="IP",
        help="Paddle IP — required for outbound messages unless you only listen",
    )
    p.add_argument(
        "--paddle-port",
        type=int,
        default=4211,
        help="Port the paddle listens on (default 4211)",
    )
    p.add_argument(
        "--send",
        action="append",
        default=[],
        metavar="TEXT",
        help="Send one datagram per --send, then continue (or exit with --no-input)",
    )
    p.add_argument(
        "--no-input",
        action="store_true",
        help="Do not read stdin; quit after --send lines (or run receive-only forever)",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()

    recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    recv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        recv_sock.bind((args.bind, args.listen_port))
    except OSError as e:
        print(f"Bind {args.bind}:{args.listen_port} failed: {e}", file=sys.stderr)
        return 1

    stop = threading.Event()
    t = threading.Thread(target=recv_loop, args=(recv_sock, stop), daemon=True)
    t.start()

    print(
        f"[{_ts()}] Listening for paddle on {args.bind}:{args.listen_port} "
        f"(Host port). Ctrl+C to stop.",
        flush=True,
    )
    if args.paddle:
        print(
            f"[{_ts()}] Outbound target paddle {args.paddle}:{args.paddle_port}",
            flush=True,
        )
    else:
        print(
            f"[{_ts()}] No --paddle IP; receive-only. Add --paddle to send commands.",
            flush=True,
        )

    send_sock: socket.socket | None = None
    if args.paddle:
        send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_line(line: str) -> None:
        if not send_sock or not args.paddle:
            print("Set --paddle IP to send.", file=sys.stderr)
            return
        payload = line.strip().encode("utf-8")
        send_sock.sendto(payload, (args.paddle, args.paddle_port))
        print(f"[{_ts()}] TX {args.paddle}:{args.paddle_port}  {line.strip()!r}", flush=True)

    for line in args.send:
        send_line(line)
        time.sleep(0.02)

    if args.no_input:
        try:
            if args.send:
                time.sleep(0.2)
            else:
                while True:
                    time.sleep(3600)
        except KeyboardInterrupt:
            pass
    else:
        print(
            "Type a line and Enter to send to the paddle. "
            "Examples: swing hit | idle | gameplay",
            flush=True,
        )
        try:
            while True:
                line = sys.stdin.readline()
                if line == "":
                    break
                if not line.strip():
                    continue
                send_line(line)
        except KeyboardInterrupt:
            pass

    stop.set()
    recv_sock.close()
    if send_sock:
        send_sock.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
