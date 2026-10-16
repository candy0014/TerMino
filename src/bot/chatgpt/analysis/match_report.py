#!/usr/bin/env python3
"""Report fixed-ten-game batches and historical FT3 in separate protocol pools.

Manifest: {"jobs": [{"phase": "validation", "variant": "terrain_a",
"case": "vs_4_turn1.3", "group": 1, "games": 10, "output": "runlogs/..."}]}.
Paths are relative to the invocation directory. A directory scan is convenient
for development; a frozen manifest also exposes jobs that have not started.
Schema 2 groups protocols -> phases -> variants -> cases; FT3 never contributes
to fixed-ten-game totals. Metadata without stopping fields is historical FT3.
"""

import argparse
import collections
import json
import math
from pathlib import Path
import random
import re
import statistics


CASES = ("vs_4_turn1.0", "vs_4_turn1.3", "vs_1_turn1.0", "vs_1_turn1.3")


def ratio(numerator, denominator):
    return numerator / denominator if denominator else None


def wilson(wins, total):
    if not total:
        return None
    z = 1.959963984540054
    p = wins / total
    denominator = 1 + z * z / total
    center = (p + z * z / (2 * total)) / denominator
    half = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / denominator
    return [max(0.0, center - half), min(1.0, center + half)]


def cluster_interval(series, repetitions=10000):
    """Resample complete independent series; retain draws in the denominator."""
    if len(series) < 2:
        return None
    rng = random.Random(26092601)
    values = []
    for _ in range(repetitions):
        sample = [rng.choice(series) for _ in series]
        values.append(sum(s["wins"] for s in sample) / sum(s["games"] for s in sample))
    values.sort()
    return [values[math.ceil(repetitions * q) - 1] for q in (0.025, 0.975)]


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    return {"count": len(values), "mean": statistics.mean(values),
            "median": statistics.median(values), "min": ordered[0], "max": ordered[-1],
            "p95": ordered[math.ceil(0.95 * len(values)) - 1]}


def stopping_protocol(job, metadata):
    """Prefer observed metadata, flag a conflicting frozen manifest."""
    requested = None
    if job.get("games") is not None:
        requested = f"fixed_{job['games']}"
    elif job.get("first_to") is not None:
        requested = f"ft{job['first_to']}"
    observed = None
    issues = []
    if metadata is not None:
        if metadata.get("versus_games") is not None:
            observed = f"fixed_{metadata['versus_games']}"
            if metadata.get("versus_first_to") is not None:
                issues.append("metadata specifies both fixed games and first-to stopping")
        else:
            observed = f"ft{metadata.get('versus_first_to') or 3}"
    if requested and observed and requested != observed:
        issues.append("stopping protocol differs from manifest")
    protocol = observed or requested or "ft3"
    if protocol not in ("fixed_10", "ft3"):
        issues.append("unsupported stopping protocol; expected fixed_10 or historical ft3")
    return protocol, issues


def read_series(job):
    directory = Path(job["output"])
    metadata_file = directory / "metadata.json"
    metadata = json.loads(metadata_file.read_text()) if metadata_file.exists() else None
    protocol, issues = stopping_protocol(job, metadata)
    records = []
    if (directory / "INVALID.json").exists():
        return dict(output=str(directory), group=job.get("group"), protocol=protocol, games=0,
                    wins=0, losses=0, draws=0, complete=False,
                    issues=["INVALID.json excludes this series from all statistics"],
                    binary_sha256=None, seeds=[], records=[], decisions=[])
    source = directory / "results.jsonl"
    if source.exists():
        for line in source.read_text().splitlines():
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                issues.append("unfinished or malformed results.jsonl")
                break
    else:
        issues.append("results missing")
    if any(r["case"] != job["case"] for r in records):
        issues.append("record case mismatch")
    first = [r for r in records if r["side"] == "first"]
    second = {r["seed"]: r for r in records if r["side"] == "second"}
    if len({r["seed"] for r in first}) != len(first):
        issues.append("duplicate game seed within series")
    wins = losses = draws = 0
    for row in first:
        opponent = second.get(row["seed"])
        if opponent is None:
            issues.append("opponent result missing")
        elif row["outcome"] == "won":
            wins += 1
            if opponent["outcome"] == "won":
                issues.append("both sides reported a win")
        elif opponent["outcome"] == "won":
            losses += 1
        else:
            draws += 1
        expected_quota = float(job["case"].split("turn")[1])
        parameters = row.get("parameters", {})
        if (row.get("bot") != 9 or row.get("width") != 300 or
                parameters.get("turn_pieces") != 1 or
                parameters.get("opponent_turn_pieces") != expected_quota):
            issues.append("game parameters differ from required protocol")
    score_file = directory / (job["case"] + "_score.json")
    if score_file.exists():
        score = json.loads(score_file.read_text())
        if (score.get("first"), score.get("second")) != (wins, losses):
            issues.append("score/result mismatch")
        if protocol == "fixed_10":
            if len(first) != 10 or score.get("games") != 10:
                issues.append("fixed-ten-game group is incomplete or has extra games")
            if score.get("draws") != draws:
                issues.append("draw score/result mismatch")
        elif protocol == "ft3":
            if max(wins, losses) != 3 or min(wins, losses) >= 3:
                issues.append("series is not FT3")
            if "games" in score and score["games"] != len(first):
                issues.append("game count score/result mismatch")
            if "draws" in score and score["draws"] != draws:
                issues.append("draw score/result mismatch")
    else:
        issues.append("final group score missing")
    binary = None
    if metadata is not None:
        binary = metadata.get("binary_sha256")
        if (metadata.get("simulation_pps") != 1 or metadata.get("autoplay_pps") != 0 or
                metadata.get("versus_protocol") != "turn-pieces-v2"):
            issues.append("metadata differs from required protocol")
        if job.get("binary_sha256") and job["binary_sha256"] != binary:
            issues.append("binary differs from manifest")
    else:
        issues.append("metadata missing")
    decisions = []
    for row in first:
        path = directory / f"{job['case']}_seed{row['seed']}.decisions.json"
        if path.exists():
            decisions.extend(json.loads(path.read_text()).get("first", []))
    if len(decisions) != sum(r.get("diagnostic_decisions", 0) for r in first):
        issues.append("decision records missing")
    return dict(output=str(directory), group=job.get("group"), protocol=protocol, games=len(first),
                wins=wins, losses=losses, draws=draws, complete=bool(first) and not issues,
                issues=sorted(set(issues)), binary_sha256=binary,
                seeds=[r["seed"] for r in first], records=first, decisions=decisions)


def summarize(protocol, phase, case, series, historical_required_series):
    rows = [r for s in series for r in s["records"]]
    decisions = [d for s in series for d in s["decisions"]]
    finished = [s for s in series if s["complete"]]
    games = len(rows)
    wins, losses, draws = (sum(s[k] for s in series) for k in ("wins", "losses", "draws"))
    pieces = sum(r["pieces"] for r in rows)
    attack = sum(r["attack"] for r in rows)
    clear_types = collections.Counter()
    for row in rows:
        clear_types.update(row.get("clear_types", {}))
    special = sum(count for kind, count in clear_types.items()
                  if kind.split(":")[1] != "normal" or int(kind.split(":")[2]) >= 4)
    all_seeds = [seed for s in series for seed in s["seeds"]]
    hashes = sorted({s["binary_sha256"] for s in series if s["binary_sha256"]})
    issues = []
    if len(all_seeds) != len(set(all_seeds)):
        issues.append("series overlap in game seeds")
    if len(hashes) > 1:
        issues.append("variant contains multiple binaries")
    required_series = 1 if protocol == "fixed_10" else historical_required_series
    complete = len(finished) >= required_series and len(finished) == len(series) and not issues
    win_fraction = ratio(wins, games)
    threshold = 0.95 if case.endswith("turn1.0") else 0.5
    target_met = win_fraction is not None and (win_fraction >= threshold if threshold == 0.95
                                               else win_fraction > threshold)
    status = ("incomplete" if not complete else "development_only" if protocol == "ft3" and phase != "validation"
              else "sample_target_met" if target_met else "below_target")
    return dict(status=status, protocol=protocol, independent_validation=phase == "validation",
                sample_target_met=target_met if complete else None,
                planned_series=len(series), complete_series=len(finished),
                required_series=required_series, n=games, games=games, wins=wins, losses=losses, draws=draws,
                win_fraction=win_fraction, decisive_win_fraction=ratio(wins, wins + losses),
                game_win_wilson_95=wilson(wins, games),
                complete_series_cluster_bootstrap_95=cluster_interval(finished),
                series_wins=sum(s["wins"] > s["losses"] for s in finished),
                series_win_wilson_95=wilson(sum(s["wins"] > s["losses"] for s in finished), len(finished)),
                target={"comparison": ">=" if threshold == 0.95 else ">", "win_fraction": threshold,
                        "required_wins_per_10_games": 10 if threshold == 0.95 else 6},
                pieces=pieces, attack=attack, attack_per_piece=ratio(attack, pieces),
                special_clear_events=special, clear_events=sum(clear_types.values()),
                special_clear_per_piece=ratio(special, pieces),
                special_clear_fraction=ratio(special, sum(clear_types.values())),
                fallback=sum(r.get("diagnostic_fallbacks", 0) for r in rows),
                diagnostic_decisions=sum(r.get("diagnostic_decisions", 0) for r in rows),
                search_ms=distribution([float(d["search_ms"]) for d in decisions]),
                pieces_at_game_end=distribution([r["pieces"] for r in rows]),
                game_seconds=distribution([r["game_seconds"] for r in rows]),
                pieces_at_death=distribution([r["pieces"] for r in rows if r["outcome"] == "dead"]),
                outcomes=dict(collections.Counter(r["outcome"] for r in rows)),
                binary_sha256=hashes, issues=issues,
                series=[{k: v for k, v in s.items() if k not in ("records", "decisions")}
                        for s in series])


def report(jobs, historical_required_series=5):
    if historical_required_series < 5:
        raise ValueError("Historical FT3 validation requires at least five series per case")
    groups = collections.defaultdict(list)
    for job in jobs:
        if job["case"] not in CASES:
            raise ValueError(f"unsupported matchup: {job['case']}")
        series = read_series(job)
        groups[(series["protocol"], job.get("phase", "dev"), job["variant"], job["case"])].append(series)
    result = {}
    for protocol, phase, variant in sorted({key[:3] for key in groups}):
        phases = result.setdefault(protocol, {"phases": {}})["phases"]
        phases.setdefault(phase, {})[variant] = {
            case: summarize(protocol, phase, case, groups[(protocol, phase, variant, case)],
                            historical_required_series) for case in CASES}
    return dict(schema=2, protocols=result, notes=[
        "All win fractions used for targets include draws in the denominator.",
        "One complete fixed-ten-game group permits a measured target assessment: equal quota needs 10/10 wins, 1.3 quota needs at least 6/10.",
        "Development and validation remain separate; sample_target_met describes this sample, not population strength or independent confirmation.",
        "Historical FT3 and fixed-ten-game data are never pooled; historical FT3 retains its old five-series validation gate.",
        "Wilson game intervals ignore within-group dependence; historical FT3 additionally has outcome-dependent stopping. Cluster intervals resample whole complete groups.",
        "End-of-game survival is censored by winning; zero-variation cluster bootstrap intervals can be degenerate.",
        "Validation labeling and independence from tuning must be guaranteed by the frozen experiment manifest.",
    ])


def discover(root):
    jobs = []
    pattern = re.compile(r"^(dev|developer|development|validation)_(.+)_(vs_[14]_turn1\.[03])(?:_group(\d+))?$")
    for directory in sorted(root.iterdir()):
        match = pattern.fullmatch(directory.name)
        if directory.is_dir() and match:
            phase, variant, case, group = match.groups()
            jobs.append(dict(phase=phase, variant=variant, case=case, group=group,
                             output=str(directory)))
    return jobs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path("runlogs/chatgpt_moch_20260926"))
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    jobs = json.loads(args.manifest.read_text())["jobs"] if args.manifest else discover(args.root)
    output = json.dumps(report(jobs), indent=2, ensure_ascii=False) + "\n"
    if args.output:
        with args.output.open("x") as stream:
            stream.write(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
