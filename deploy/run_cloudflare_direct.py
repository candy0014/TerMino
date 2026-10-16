#!/usr/bin/env python3
"""Resolve and connect Cloudflare Tunnel through the physical IPv4 uplink.

Keep this scoped to the tunnel: do not change system DNS or VPN routing.
cloudflared 2026.10.0's origin-resolver option does not replace edge discovery,
so pass the freshly resolved endpoints through its existing --edge option.
Prefer QUIC to avoid sharing TCP retransmission stalls between game sessions;
cloudflared's auto mode falls back to HTTP/2 when UDP is unavailable.
"""
import argparse
import concurrent.futures
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import sys


def physical_address():
    explicit = os.environ.get("TETR_TUNNEL_BIND_ADDRESS")
    if explicit:
        return str(ipaddress.IPv4Address(explicit))
    routes = json.loads(subprocess.check_output(
        ["ip", "-j", "-4", "route", "show", "table", "main", "default"], text=True))
    for route in sorted(routes, key=lambda entry: entry.get("metric", 0)):
        interface = route.get("dev", "")
        if not interface or interface.lower().startswith(("lo", "tun", "tap", "wg", "meta")):
            continue
        links = json.loads(subprocess.check_output(
            ["ip", "-j", "-4", "addr", "show", "dev", interface], text=True))
        for link in links:
            for address in link.get("addr_info", []):
                if address.get("scope") == "global":
                    return str(ipaddress.IPv4Address(address["local"]))
    raise RuntimeError("No physical IPv4 default-route address is available")


def resolve_region(hostname, bind_address):
    for resolver in ("223.5.5.5", "119.29.29.29"):
        try:
            result = subprocess.run(
                ["dig", "-b", bind_address, "@" + resolver, hostname, "A",
                 "+short", "+time=2", "+tries=1"],
                text=True, capture_output=True, timeout=3)
        except subprocess.TimeoutExpired:
            continue
        addresses = set()
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                try:
                    address = ipaddress.IPv4Address(line.strip())
                except ipaddress.AddressValueError:
                    continue
                # Reject VPN fake IPs and other non-public resolver answers.
                if address.is_global:
                    addresses.add(address)
        if addresses:
            return [str(address) for address in sorted(addresses)]
    raise RuntimeError("Cannot resolve real tunnel endpoints for " + hostname)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Resolve endpoints without starting the tunnel")
    parser.add_argument("--protocol", choices=("auto", "quic", "http2"), default="auto",
                        help="Tunnel transport (default: QUIC with HTTP/2 fallback)")
    parser.add_argument("--route", choices=("direct", "system"), default="direct",
                        help="Use the physical uplink or the system's working VPN/DNS route")
    options = parser.parse_args()
    bind_address = None
    endpoints = []
    regions = []
    hosts = ("region1.v2.argotunnel.com", "region2.v2.argotunnel.com")
    if options.route == "direct":
        bind_address = physical_address()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            regions = list(pool.map(lambda host: resolve_region(host, bind_address), hosts))
        endpoints = [address + ":7844" for region in regions for address in region]
    if options.check:
        print(json.dumps({"route": options.route, "bind_address": bind_address, "protocol": options.protocol,
                          "regions": dict(zip(hosts, regions))}))
        return
    root = Path.home()
    binary = root / "termino-cloudflare/current/cloudflared"
    command = [str(binary), "tunnel", "--no-autoupdate", "--no-prechecks",
               "--protocol", options.protocol, "--edge-ip-version", "4", "--metrics", "127.0.0.1:20241"]
    if bind_address:
        command.extend(["--edge-bind-address", bind_address])
    for endpoint in endpoints:
        command.extend(["--edge", endpoint])
    command.extend(["run", "--token-file", str(root / ".config/termino-cloudflare/token")])
    print(f"Tunnel: route={options.route}, source={bind_address}, protocol={options.protocol}, resolved_endpoints={len(endpoints)}", file=sys.stderr, flush=True)
    os.execv(binary, command)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print("Direct tunnel startup failed: " + str(error), file=sys.stderr)
        sys.exit(1)
