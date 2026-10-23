#!/usr/bin/env python3
"""Inspect UDP datagrams using only Python's standard library.

From firmware/: python3 tools/udp_receiver.py --port 9000
Ctrl+C exits. For a bounded run, add --count 2 --timeout 30.
"""

import argparse
import math
import socket


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="Local IPv4 interface (default: all)")
    parser.add_argument("--port", type=int, default=9000, help="UDP port; 0 selects an available port")
    parser.add_argument("--count", type=int, default=0, help="Stop after this many packets; 0 is unlimited")
    parser.add_argument("--timeout", type=float, help="Exit after this many seconds without a packet")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or args.count < 0:
        parser.error("port must be 0..65535 and count must be nonnegative")
    if args.timeout is not None and (not math.isfinite(args.timeout) or args.timeout <= 0):
        parser.error("timeout must be finite and positive")

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind((args.bind, args.port))
            receiver.settimeout(args.timeout)
            host, port = receiver.getsockname()
            print(f"Listening on {host}:{port} (Ctrl+C to stop)", flush=True)
            count = 0
            while args.count == 0 or count < args.count:
                try:
                    data, sender = receiver.recvfrom(65535)
                except socket.timeout:
                    print("Inactivity timeout; exiting.", flush=True)
                    break
                print(f"Sender {sender[0]}:{sender[1]} | bytes={len(data)}", flush=True)
                print(f"Hex:  {data.hex(' ')}", flush=True)
                print(f"Text: {data.decode('utf-8', errors='backslashreplace')!r}", flush=True)
                count += 1
    except KeyboardInterrupt:
        print("\nStopped.")
    except OSError as error:
        parser.exit(1, f"UDP receiver: {error}\n")


if __name__ == "__main__":
    main()
