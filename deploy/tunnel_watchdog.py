#!/usr/bin/env python3
"""Recover an unconnected cloudflared without restarting the game server.

Run periodically as the deployment user. Loopback probes ignore proxy settings;
the tunnel must fail several consecutive checks before a bounded restart.
"""
import argparse
from dataclasses import dataclass
import fcntl
import json
import math
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.request


@dataclass(frozen=True)
class Policy:
    failures: int = 3
    startup_grace: float = 60
    cooldown: float = 120
    max_cooldown: float = 300
    sample_expiry: float = 90


def decide(previous, sample, now, boot_id, policy=Policy()):
    """Pure decision: persist the returned state before executing its action."""
    state = dict(previous) if previous.get("boot_id") == boot_id else {}
    if now < state.get("sample_at", 0):
        state = {}
    if (sample["pid"] != state.get("pid") or
            now - state.get("sample_at", 0) > policy.sample_expiry):
        state["failures"] = 0
    state.update(boot_id=boot_id, pid=sample["pid"], sample_at=now)
    action = None
    if sample["load_state"] != "loaded":
        event = "unit_missing"
    elif sample["active_state"] not in ("active", "failed"):
        # Respect a deliberate stop; never resurrect a manually stopped unit.
        event = "unit_" + sample["active_state"]
    elif sample["ready"] and sample["active_state"] == "active":
        state["attempts"] = 0
        event = "healthy"
    elif not sample["relay"] or not sample["gateway"]:
        # A broken origin isn't fixed by dropping healthy tunnel connections.
        event = "origin_unhealthy"
    elif (sample["active_state"] == "active" and
          now - sample["started_at"] < policy.startup_grace):
        event = "starting"
    else:
        state["failures"] = state.get("failures", 0) + 1
        attempts = state.get("attempts", 0)
        delay = min(policy.max_cooldown, policy.cooldown * 2 ** min(max(0, attempts - 1), 8))
        if state["failures"] < policy.failures:
            event = "confirming_failure"
        elif "restart_at" in state and now - state["restart_at"] < delay:
            event = "cooldown"
        else:
            state.update(restart_at=now, attempts=attempts + 1, failures=0)
            action = "restart"
            event = "restart_requested"
    if event not in ("confirming_failure", "cooldown", "restart_requested"):
        state["failures"] = 0
    state["event"] = event
    return state, action


def http_get(url):
    # These are trusted loopback endpoints, regardless of desktop VPN proxies.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(url, timeout=2) as reply:
            return reply.status, reply.read(4096)
    except (OSError, urllib.error.URLError, TimeoutError):
        return 0, b""


def relay_ready():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.settimeout(1)
            peer.connect(("127.0.0.1", 3000))
            peer.send(b"!probe")
            return peer.recv(64) == b"!probe-ok"
    except OSError:
        return False


def systemctl(*arguments, timeout=5):
    return subprocess.run(["systemctl", "--user", *arguments], check=True,
                          capture_output=True, text=True, timeout=timeout).stdout


def observe(unit, metrics_port=20241):
    properties = systemctl("show", unit, "--property=LoadState,ActiveState,MainPID,ActiveEnterTimestampMonotonic")
    status = dict(line.split("=", 1) for line in properties.splitlines() if "=" in line)
    code, body = http_get(f"http://127.0.0.1:{metrics_port}/ready")
    try:
        ready = code == 200 and json.loads(body).get("readyConnections", 0) > 0
    except (ValueError, TypeError, AttributeError):
        ready = False
    gateway_code, gateway_body = http_get("http://127.0.0.1:8080/healthz")
    return {"load_state": status.get("LoadState", "not-found"),
            "active_state": status.get("ActiveState", "unknown"),
            "pid": int(status.get("MainPID", "0")),
            "started_at": int(status.get("ActiveEnterTimestampMonotonic", "0")) / 1_000_000,
            "ready": ready, "relay": relay_ready(),
            "gateway": gateway_code == 200 and gateway_body == b"TerMino WebSocket gateway ready\n"}


def load_state(path):
    try:
        value = json.loads(path.read_text())
        if not isinstance(value, dict):
            return {}
        for key in ("sample_at", "restart_at", "failures", "attempts", "pid"):
            if key in value and (not isinstance(value[key], (int, float)) or
                                 not math.isfinite(value[key]) or value[key] < 0):
                return {}
        for key in ("failures", "attempts", "pid"):
            if key in value and not isinstance(value[key], int):
                return {}
        return value
    except (OSError, ValueError):
        return {}


def save_state(path, value):
    temporary = path.with_name(path.name + "." + str(os.getpid()))
    try:
        temporary.write_text(json.dumps(value, sort_keys=True) + "\n")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def run_once(directory, unit, check=False, metrics_port=20241):
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (directory / "check.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return 0
        previous = load_state(directory / "state.json")
        sample = observe(unit, metrics_port)
        boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        state, action = decide(previous, sample, time.monotonic(), boot_id)
        if check or action or state["event"] != previous.get("event"):
            print(json.dumps({"event": state["event"], "sample": sample,
                              "failures": state.get("failures", 0), "check_only": check}), flush=True)
        if check:
            return 0
        save_state(directory / "state.json", state)
        if action:
            # The cooldown is committed even if systemctl fails or times out.
            if sample["active_state"] == "failed":
                systemctl("reset-failed", unit)
            systemctl("restart", unit, timeout=30)
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="print health without writing state or restarting")
    parser.add_argument("--state-dir", type=Path,
                        default=Path(os.environ.get("STATE_DIRECTORY", Path.home() / ".local/state/termino-tunnel-watchdog")))
    parser.add_argument("--unit", default="termino-cloudflare.service")
    parser.add_argument("--metrics-port", type=int, default=20241)
    args = parser.parse_args()
    if not 1 <= args.metrics_port <= 65535:
        parser.error("metrics port must be in 1..65535")
    os.umask(0o077)
    try:
        return run_once(args.state_dir, args.unit, args.check, args.metrics_port)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({"event": "watchdog_error", "error": str(error)}), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
