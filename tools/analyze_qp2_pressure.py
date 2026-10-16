#!/usr/bin/env python3
"""Reproduce a stratified QP2 public-score sample and its pressure statistics.

Only the documented Tetra Channel API is used. Full replay events are not
included in this endpoint: a run's average is not a per-floor incoming rate.
"""
import argparse
import json
import math
import statistics
import time
import urllib.parse
import urllib.request
from pathlib import Path

ANCHORS = [25, 100, 225, 375, 550, 750, 975, 1225, 1500, 2000, 3000, 4000, 5000]


def download(directory, weeks):
    directory.mkdir(parents=True, exist_ok=True)
    for week in weeks:
        for anchor in ANCHORS:
            path = directory / f"{week}_{anchor}.json"
            if path.exists():
                continue
            query = urllib.parse.urlencode({"limit": 100, "after": f"{anchor}:0:0"})
            url = f"https://ch.tetr.io/api/records/zenith_global@{week}?{query}"
            request = urllib.request.Request(url, headers={
                "User-Agent": "TerMino-QP2-pressure-study/1.0",
                "X-Session-ID": "termino-qp2-pressure-20261012",
            })
            with urllib.request.urlopen(request, timeout=30) as response:
                data = json.load(response)
            if not data.get("success"):
                raise RuntimeError(data.get("error"))
            path.write_text(json.dumps(data), encoding="utf-8")
            print(f"{week} <{anchor}m: {len(data['data']['entries'])} records", flush=True)
            time.sleep(1)  # Respect the public endpoint; cached files are reused.


def percentile(values, p):
    ordered = sorted(values)
    x = (len(ordered)-1)*p
    lo = math.floor(x)
    hi = math.ceil(x)
    return ordered[lo]+(ordered[hi]-ordered[lo])*(x-lo)


def summarize(directory):
    records = {}
    for path in sorted(directory.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        for record in data.get("data", {}).get("entries", []):
            records[record["_id"]] = record
    buckets = {}
    mix_bands = [[0]*5 for _ in range(6)]
    unmodded_total = 0
    for record in records.values():
        if record["extras"]["zenith"]["mods"]:
            continue
        unmodded_total += 1
        stats = record["results"]["stats"]
        minutes = stats["finaltime"]/60000
        if minutes <= 0:
            continue
        altitude = stats["zenith"]["altitude"]
        band = 0 if altitude < 150 else 1 if altitude < 450 else 2 if altitude < 850 else 3 if altitude < 1650 else 4 if altitude < 3500 else 5
        clears = stats["clears"]
        counts = [clears["singles"]+clears["doubles"], clears["triples"]+clears["minitspinsingles"],
                  clears["tspinsingles"]+clears["minitspindoubles"], clears["quads"]+clears["tspindoubles"],
                  clears["tspintriples"]]
        mix_bands[band] = [a+b for a, b in zip(mix_bands[band], counts)]
        anchor = min(ANCHORS, key=lambda x: abs(x-altitude))
        # The upper-bound query can run below its intended band in sparse tails.
        if abs(anchor-altitude) > max(75, anchor*.15):
            continue
        buckets.setdefault(anchor, []).append({
            "altitude": altitude, "minutes": minutes,
            "apm": record["results"]["aggregatestats"]["apm"],
            "sent": stats["garbage"]["sent"]/minutes,
            "received": stats["garbage"]["received"]/minutes,
        })
    summary = {"source": "https://ch.tetr.io/api/records/:leaderboard",
               "sampled_records": len(records), "unmodded_total": unmodded_total,
               "unmodded_records": sum(map(len, buckets.values())),
               "sampling": "100 PBs immediately below each altitude anchor, two completed weekly leaderboards; not a population-random sample",
               "metric": "whole-run outgoing and received rows/minute; incoming includes official reception adjustments and Windup clipping",
               "bands": [], "clear_mix": [
                   {"sizes": [1, 2, 3, 5, 7], "counts": counts,
                    "weights": [round(x/sum(counts), 4) for x in counts]}
                   for counts in mix_bands]}
    for anchor, rows in sorted(buckets.items()):
        def describe(field):
            values = [row[field] for row in rows]
            return {"mean": statistics.mean(values), "median": statistics.median(values),
                    "p10": percentile(values, .1), "p90": percentile(values, .9)}
        summary["bands"].append({"anchor": anchor, "n": len(rows),
                                  "altitude_mean": statistics.mean(row["altitude"] for row in rows),
                                  "apm": describe("apm"), "sent": describe("sent"),
                                  "received": describe("received")})
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--weeks", nargs="+", default=["2026w40", "2026w41"])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.download:
        download(args.directory, args.weeks)
    summary = summarize(args.directory)
    text = json.dumps(summary, ensure_ascii=False, indent=2)+"\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text)
