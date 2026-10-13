#!/usr/bin/env python3
"""Check TerMino UDP reachability without joining or interrupting a match."""

import argparse
import socket
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="IPv4 address or DNS hostname; optional :port")
    args = parser.parse_args()
    host = args.host
    port = 3000
    if ":" in host:
        host, port_text = host.rsplit(":", 1)
        if ":" in host or not port_text.isascii() or not port_text.isdigit() or len(port_text) > 5:
            parser.error("Use IPv4/hostname[:port], with an integer port from 1 to 65535")
        port = int(port_text)
    if not host or not 1 <= port <= 65535:
        parser.error("Use IPv4/hostname[:port], with an integer port from 1 to 65535")
    try:
        addresses = socket.getaddrinfo(host, port, socket.AF_INET,
                                       socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        endpoint = addresses[0][4]
        print(f"Checking {args.host} -> {endpoint[0]}:{port}/UDP", flush=True)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.connect(endpoint)  # Only accept replies from this endpoint.
            client.settimeout(1.0)
            for attempt in range(1, 4):
                started = time.monotonic()
                client.send(b"!probe")
                try:
                    reply = client.recv(64)
                except socket.timeout:
                    print(f"Attempt {attempt}/3: no reply", flush=True)
                    continue
                if reply == b"!probe-ok":
                    elapsed_ms = (time.monotonic() - started) * 1000
                    print(f"OK: TerMino relay replied in {elapsed_ms:.1f} ms")
                    return 0
                print("Unexpected reply: verify that this is the updated TerMino relay.",
                      file=sys.stderr)
                return 1
    except OSError as error:
        print(f"Check failed: {error}", file=sys.stderr)
        return 1
    print(f"No UDP reply. Check server process/version, UDP {port} firewall rules, "
          "gateway forwarding and DNS. Timeout alone cannot identify the cause.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
