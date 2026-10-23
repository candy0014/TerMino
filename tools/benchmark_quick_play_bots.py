#!/usr/bin/env python3
"""Paired, headless QP altitude benchmarks using the real shared game engine.

Build tetr_bot_tests first. The benchmark module never reads/writes player data.
Each before/after pair runs on the same worker/core, with identical visible
settings and RNG seeds. Timing is measured, not replaced with a fixed search cost.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import random
import signal
import subprocess
import sys
import threading

FAMILIES = (
    ("EX", "rEX"), ("NH", "rNH"), ("MS", "rMS"), ("GV", "rGV"),
    ("VL", "rVL"), ("DH", "rDH"), ("IN", "rIN"),
    ("AS", "rAS", "AS3"), ("BF", "rBF"), ("SE", "rSE"),
    ("SB", "rSB"), ("PN", "rPN", "F26", "rF26"),
)


def cases(suite: str) -> list[str]:
    singles = ["BASE"] + [mod for family in FAMILIES for mod in family]
    if suite == "singles":
        return singles
    pairs = [a + "+" + b for i, family in enumerate(FAMILIES)
             for other in FAMILIES[i + 1:] for a in family for b in other]
    if suite == "pairs":
        return singles + pairs
    # Each family level (including OFF) occurs equally often. 60 is divisible
    # by 3, 4 and 5, so extra AS/Fool variants have equal within-family coverage.
    rng = random.Random(20261021)
    columns = []
    for family in FAMILIES:
        values = [""] + list(family)
        column = values * (60 // len(values))
        rng.shuffle(column)
        columns.append(column)
    mixed = ["+".join(column[row] for column in columns if column[row]) or "BASE"
             for row in range(60)]
    if suite == "mixed":
        return mixed
    return singles + mixed if suite == "performance" else singles + pairs + mixed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", nargs="?", type=Path, default=Path("build/tests/tetr_bot_tests"))
    parser.add_argument("--before", type=Path, help="Frozen pre-change suite binary")
    parser.add_argument("--suite", choices=("singles", "pairs", "mixed", "performance", "all"), default="singles")
    parser.add_argument("--case", action="append", help="Explicit combination; repeat to select several")
    parser.add_argument("--list-cases", action="store_true")
    parser.add_argument("--seed", type=int, action="append")
    parser.add_argument("--bot", type=int, default=9, help="Runtime BOT ID; 0 runs all ten")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--pieces", type=int, default=0, help="Default: ceil(seconds*10)+1")
    parser.add_argument("--width", type=int, default=300)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--cores", help="Optional Linux CPU IDs, one per worker")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    names = args.case or cases(args.suite)
    if args.list_cases:
        print("\n".join(names))
        return 0
    if not args.output:
        parser.error("--output is required unless --list-cases is used")
    if args.seconds <= 0 or args.workers <= 0 or not 0 <= args.bot <= 10:
        parser.error("positive seconds/workers and a BOT ID between 0 and 10 are required")
    cores = list(map(int, args.cores.split(","))) if args.cores else []
    if cores and (len(cores) != args.workers or not hasattr(os, "sched_setaffinity")):
        parser.error("--cores requires Linux and one core ID per worker")
    revisions = [("after", args.binary.resolve())]
    if args.before:
        revisions.insert(0, ("before", args.before.resolve()))
    pieces = args.pieces or int(args.seconds * 10) + 1
    jobs = [(seed, name) for seed in (args.seed or [123, 456, 789]) for name in names]
    groups = [jobs[i::args.workers] for i in range(args.workers)]
    stop = threading.Event()

    def interrupt(signum, frame):
        stop.set()
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, interrupt)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as output:
        def worker(index: int):
            for number, (seed, name) in enumerate(groups[index]):
                if stop.is_set():
                    return
                # Reverse order on alternate pairs to reduce warmup/order bias.
                order = revisions if (number + index) % 2 == 0 else revisions[::-1]
                for revision, binary in order:
                    if stop.is_set():
                        return
                    command = [str(binary), "bot_quick_play_altitude_benchmark", str(pieces),
                               str(seed), str(args.bot), name, str(args.width), str(args.seconds)]
                    if cores:
                        command = ["taskset", "-c", str(cores[index])] + command
                    run = subprocess.run(command, capture_output=True, text=True)
                    if run.returncode:
                        rows = [dict(case=name, bot=args.bot, seed=seed, supported=None,
                                     height=None, ended="error", error=run.stderr.strip(),
                                     returncode=run.returncode)]
                    else:
                        rows = [json.loads(line) for line in run.stderr.splitlines()]
                    for row in rows:
                        row.update(revision=revision, width=args.width, time_limit=args.seconds)
                    yield rows

        # Workers return streams through a queue; only the main thread writes.
        import queue
        completed: queue.Queue = queue.Queue()

        def collect(index: int):
            for rows in worker(index):
                completed.put(rows)

        with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
            futures = [pool.submit(collect, i) for i in range(args.workers)]
            expected = len(jobs) * len(revisions)
            failures = 0
            for count in range(expected):
                while True:
                    try:
                        rows = completed.get(timeout=.2)
                        break
                    except queue.Empty:
                        for future in futures:
                            if future.done() and future.exception():
                                raise future.exception()
                for row in rows:
                    output.write(json.dumps(row, ensure_ascii=False) + "\n")
                    failures += row["revision"] == "after" and "error" in row
                output.flush()
                print(f"{count + 1}/{expected}: {rows[0]['revision']} {rows[0]['case']} seed={rows[0]['seed']}",
                      file=sys.stderr, flush=True)
            for future in futures:
                future.result()
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
