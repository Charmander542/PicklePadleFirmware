#!/usr/bin/env python3
"""
PicklePaddle central-node serial debug host.

Connects to the central ESP32 over USB serial, shows incoming node data in a
friendly way, and sends commands using star-topology routing rules:

  - idle / gameplay / tutorial -> broadcast to all nodes
  - swing hit (and most other commands) -> specific node

Examples:
  python tools/paddle_central_serial_debug.py --port COM6
  python tools/paddle_central_serial_debug.py --port /dev/ttyUSB0

Requires:
  pip install pyserial
"""

from __future__ import annotations

import argparse
import queue
import sys
import threading
import time
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "pyserial is required. Install with: pip install pyserial"
    ) from exc


BROADCAST_CMDS = {"idle", "gameplay", "tutorial"}


def ts() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


@dataclass
class NodeStats:
    last_rx_ts: float = 0.0
    msg_count: int = 0
    last_msg: str = ""
    gameplay_impulses: int = 0
    tutorial_rows: int = 0
    button_events: int = 0


def looks_like_float(text: str) -> bool:
    try:
        float(text)
        return True
    except ValueError:
        return False


def looks_like_tutorial_row(text: str) -> bool:
    # tutorial row format: rot_x,rot_y,rot_z,button,impulse
    parts = text.split(",")
    if len(parts) != 5:
        return False
    try:
        float(parts[0])
        float(parts[1])
        float(parts[2])
        int(float(parts[3]))
        float(parts[4])
        return True
    except ValueError:
        return False


def classify_payload(payload: str) -> str:
    p = payload.strip().lower()
    if looks_like_float(payload):
        return "gameplay_impulse"
    if looks_like_tutorial_row(payload):
        return "tutorial_row"
    if p in {"detect btn push", "detect btn hold"}:
        return "button_event"
    return "text"


def parse_serial_line(line: str) -> tuple[str, int | None, str]:
    """
    Returns (kind, node_id, payload)
      kind:
        - "central_log": central-side status like "[cn] ..."
        - "node_data": "<id>:<payload>"
        - "other": anything else
    """
    s = line.strip()
    if not s:
        return ("other", None, "")
    if s.startswith("[cn]"):
        return ("central_log", None, s)
    if ":" in s:
        left, right = s.split(":", 1)
        if left.isdigit():
            return ("node_data", int(left), right)
    return ("other", None, s)


def serial_reader(ser: serial.Serial, out_q: queue.Queue[str], stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            raw = ser.readline()
        except Exception as exc:  # pragma: no cover
            out_q.put(f"[{ts()}] [host] serial read error: {exc}")
            break
        if not raw:
            continue
        try:
            text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        except Exception:
            text = repr(raw)
        out_q.put(text)


def write_cmd(ser: serial.Serial, cmd: str) -> None:
    ser.write((cmd.strip() + "\n").encode("utf-8"))
    ser.flush()


def print_help() -> None:
    print("")
    print("Commands:")
    print("  idle | gameplay | tutorial")
    print("      Broadcast mode command to all nodes")
    print("  swing <id>")
    print("      Send 'swing hit' to one node")
    print("  send <id> <text...>")
    print("      Send arbitrary text to one node")
    print("  all <text...>")
    print("      Broadcast arbitrary text to all nodes")
    print("  list | mac")
    print("      Forward directly to central node")
    print("  stats")
    print("      Show per-node counters from this host session")
    print("  raw <exact text>")
    print("      Send exact serial line to central node")
    print("  help | quit")
    print("")


def print_stats(stats: dict[int, NodeStats]) -> None:
    if not stats:
        print(f"[{ts()}] [host] no node data seen yet")
        return
    print(f"[{ts()}] [host] node stats:")
    for node_id in sorted(stats.keys()):
        st = stats[node_id]
        age_ms = int((time.time() - st.last_rx_ts) * 1000) if st.last_rx_ts else -1
        print(
            f"  node {node_id:>2} | msgs={st.msg_count:<5} "
            f"impulses={st.gameplay_impulses:<4} tutorial={st.tutorial_rows:<4} "
            f"buttons={st.button_events:<3} last={age_ms:>5}ms ago | {st.last_msg[:60]}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="UE-style serial debug host for PicklePaddle central node."
    )
    parser.add_argument(
        "--port",
        required=False,
        default="",
        help="Serial port (e.g. COM6, /dev/ttyUSB0). If omitted, available ports are listed.",
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default 115200).")
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.2,
        help="Serial read timeout seconds (default 0.2).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.port:
        ports = list(list_ports.comports())
        if not ports:
            print("No serial ports found. Plug in central node and retry with --port.")
            return 1
        print("Available serial ports:")
        for p in ports:
            print(f"  {p.device:12}  {p.description}")
        print("")
        print("Run again with --port, e.g.:")
        print(f"  python {sys.argv[0]} --port {ports[0].device}")
        return 1

    try:
        ser = serial.Serial(args.port, args.baud, timeout=args.timeout)
    except Exception as exc:
        print(f"Failed to open serial {args.port} @ {args.baud}: {exc}", file=sys.stderr)
        return 1

    print(f"[{ts()}] [host] connected to {args.port} @ {args.baud}")
    print_help()

    stop = threading.Event()
    rx_q: queue.Queue[str] = queue.Queue()
    t = threading.Thread(target=serial_reader, args=(ser, rx_q, stop), daemon=True)
    t.start()

    stats: dict[int, NodeStats] = defaultdict(NodeStats)

    try:
        while True:
            # Drain serial lines first so telemetry stays responsive.
            while True:
                try:
                    line = rx_q.get_nowait()
                except queue.Empty:
                    break
                kind, node_id, payload = parse_serial_line(line)
                if kind == "central_log":
                    print(f"[{ts()}] {payload}")
                    continue
                if kind == "node_data" and node_id is not None:
                    cls = classify_payload(payload)
                    st = stats[node_id]
                    st.last_rx_ts = time.time()
                    st.msg_count += 1
                    st.last_msg = payload
                    if cls == "gameplay_impulse":
                        st.gameplay_impulses += 1
                        print(f"[{ts()}] [node {node_id:02}] IMPULSE  {payload}")
                    elif cls == "tutorial_row":
                        st.tutorial_rows += 1
                        # Keep tutorial output compact; row can be long at high rate.
                        print(f"[{ts()}] [node {node_id:02}] TUTORIAL {payload}")
                    elif cls == "button_event":
                        st.button_events += 1
                        print(f"[{ts()}] [node {node_id:02}] BUTTON   {payload}")
                    else:
                        print(f"[{ts()}] [node {node_id:02}] MSG      {payload}")
                    continue
                print(f"[{ts()}] [raw] {line}")

            # Non-blocking command prompt cadence.
            try:
                if sys.platform.startswith("win"):
                    # Windows-friendly line input cadence.
                    if msvcrt_kbhit_line_ready():
                        user = input("> ").strip()
                    else:
                        time.sleep(0.05)
                        continue
                else:
                    # Keep it simple on POSIX: blocking input in small cadence window.
                    user = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("")
                break

            if not user:
                continue

            low = user.lower()
            if low in {"quit", "exit", "q"}:
                break
            if low in {"help", "h", "?"}:
                print_help()
                continue
            if low == "stats":
                print_stats(stats)
                continue

            # Broadcast mode commands by default.
            if low in BROADCAST_CMDS:
                wire = f"*:{low}"
                write_cmd(ser, wire)
                print(f"[{ts()}] [tx] BROADCAST {wire}")
                continue

            if low.startswith("swing "):
                parts = user.split(maxsplit=1)
                if len(parts) != 2 or not parts[1].isdigit():
                    print("Usage: swing <node_id>")
                    continue
                nid = int(parts[1])
                wire = f"{nid}:swing hit"
                write_cmd(ser, wire)
                print(f"[{ts()}] [tx] NODE {nid} {wire}")
                continue

            if low.startswith("send "):
                parts = user.split(maxsplit=2)
                if len(parts) < 3 or not parts[1].isdigit():
                    print("Usage: send <node_id> <text...>")
                    continue
                nid = int(parts[1])
                wire = f"{nid}:{parts[2]}"
                write_cmd(ser, wire)
                print(f"[{ts()}] [tx] NODE {nid} {wire}")
                continue

            if low.startswith("all "):
                text = user[4:].strip()
                if not text:
                    print("Usage: all <text...>")
                    continue
                wire = f"*:{text}"
                write_cmd(ser, wire)
                print(f"[{ts()}] [tx] BROADCAST {wire}")
                continue

            if low in {"list", "mac"}:
                write_cmd(ser, low)
                print(f"[{ts()}] [tx] CENTRAL {low}")
                continue

            if low.startswith("raw "):
                raw_line = user[4:].strip()
                if raw_line:
                    write_cmd(ser, raw_line)
                    print(f"[{ts()}] [tx] RAW {raw_line}")
                continue

            # Convenience fallback:
            #   "<id>:<text>" passes through untouched.
            #   "swing hit 2" -> node-targeted.
            if ":" in user and user.split(":", 1)[0].isdigit():
                write_cmd(ser, user)
                print(f"[{ts()}] [tx] RAW {user}")
                continue

            if low.startswith("swing hit "):
                tail = user[len("swing hit ") :].strip()
                if tail.isdigit():
                    nid = int(tail)
                    wire = f"{nid}:swing hit"
                    write_cmd(ser, wire)
                    print(f"[{ts()}] [tx] NODE {nid} {wire}")
                    continue

            print("Unknown command. Type 'help'.")

    finally:
        stop.set()
        try:
            ser.close()
        except Exception:
            pass
        print(f"[{ts()}] [host] disconnected")

    return 0


def msvcrt_kbhit_line_ready() -> bool:
    """Windows helper so telemetry prints while waiting for Enter."""
    if not sys.platform.startswith("win"):
        return True
    try:
        import msvcrt
    except ImportError:
        return True

    # If no key pressed, keep loop non-blocking.
    if not msvcrt.kbhit():
        return False
    # Consume nothing here; input() will read the full line from stdin buffer.
    return True


if __name__ == "__main__":
    raise SystemExit(main())

