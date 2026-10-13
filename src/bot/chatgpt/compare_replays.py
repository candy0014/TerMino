#!/usr/bin/env python3
"""Reproduce an observational comparison, without running or changing any BOT.

Run from any directory. Inputs are the retained, explicitly versioned cohorts;
they are never pooled across versions or treated as matched TETR.IO benchmarks.
Only a new --output file is written. Uses the existing replay analyzer unchanged.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path

from profile_report import summarize


ROOT = Path(__file__).resolve().parents[3]
COHORTS = {
    "stable_zen_v031": "v0.3.1 delivered: ZEN runtime acceptance",
    "stable_layer_v031": "v0.3.1 delivered: layer 1.0 runtime acceptance",
    "acceptance_survival_60_v03": "v0.3 historical control: APM60",
    "acceptance_survival_70_v03": "v0.3 historical control: APM70",
    "acceptance_vs_1_turn1.3_v03": "v0.3 historical control: while1 quota 1.3",
    "acceptance_survival_70_v04": "REJECTED candidate F: APM70",
    "acceptance_vs_1_turn1.3_v04": "REJECTED candidate F: while1 quota 1.3",
    "final_layer1_v03": "v0.3 paired control for B: layer 1.0",
    "final_layer1_v04": "REJECTED candidate B: layer 1.0",
    "final_s60_v03": "v0.3 paired control for B: APM60",
    "final_s60_v04": "REJECTED candidate B: APM60",
}
OPTIONS = (
    "version", "g", "spinbonuses", "b2bcharging", "b2bcharge_base",
    "allclear_garbage", "allclear_b2b", "allclear_b2b_sends", "allow180",
    "nolockout", "garbagespecialbonus", "openerphase", "garbagemultiplier",
    "garbagemargin", "garbageincrease",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ratio(a, b):
    return a / b if b else None


def metrics(pieces, attack, spin, spin1, quads, difficult, clears):
    return dict(app=ratio(attack, pieces), special_clear_fraction=ratio(difficult, clears),
                special_per_100_pieces=ratio(100 * difficult, pieces),
                spin_per_100_pieces=ratio(100 * spin, pieces),
                quad_per_100_pieces=ratio(100 * quads, pieces),
                spin1_share=ratio(spin1, spin))


def native_cohort(directory, label, hashes):
    def source(path):
        hashes[str(path.relative_to(ROOT))] = sha(path)
        return path.read_text(encoding="utf-8")

    metadata = json.loads(source(directory / "metadata.json"))
    rows = [json.loads(line) for line in source(directory / "results.jsonl").splitlines()
            if line.strip()]
    rows = [r for r in rows if r["side"] == "first" and r["bot"] == 9]
    assert rows and len({r["case"] for r in rows}) == 1
    counts = collections.Counter()
    peak_b2b = 0
    for row in rows:
        path = directory / f'{row["case"]}_seed{row["seed"]}.placements.json'
        events = json.loads(source(path))["first"]
        assert len(events) == row["pieces"]
        for key in ("attack", "lines", "garbage_lines"):
            assert sum(int(e[key]) for e in events) == row[key], (path, key)
        assert sum(int(k.rsplit(":", 1)[1]) * v for k, v in row["clear_types"].items()) == row["lines"]
        counts.update(row["clear_types"])
        peak_b2b = max(peak_b2b, *(int(e[k]) for e in events for k in ("b2b_before", "b2b_after")))
    pieces = sum(r["pieces"] for r in rows)
    attack = sum(r["attack"] for r in rows)
    spin = spin1 = quads = difficult = 0
    for key, count in counts.items():
        _, kind, lines = key.split(":")
        is_spin = kind in ("spin", "minispin")
        spin += count * is_spin
        spin1 += count * (is_spin and int(lines) == 1)
        quads += count * (kind == "normal" and int(lines) == 4)
        difficult += count * (is_spin or int(lines) >= 4)
    return dict(label=label, directory=str(directory.relative_to(ROOT)),
                recorded_binary_sha256=metadata["binary_sha256"],
                simulation_pps=metadata["simulation_pps"],
                autoplay_pps=metadata["autoplay_pps"],
                versus_protocol=metadata.get("versus_protocol"),
                parameters=rows[0]["parameters"], seeds=[r["seed"] for r in rows],
                pieces_each=[r["pieces"] for r in rows], pieces=pieces, attack=attack,
                garbage_lines=sum(r["garbage_lines"] for r in rows),
                outcomes=dict(collections.Counter(r["outcome"] for r in rows)),
                whole_process_pps=pieces / sum(r["wall_seconds"] for r in rows),
                clear_counts=dict(counts), spin=spin, spin1=spin1, quads=quads,
                difficult=difficult, clears=sum(counts.values()),
                peak_active_b2b_native=peak_b2b,
                comparison_metrics=metrics(pieces, attack, spin, spin1, quads, difficult, sum(counts.values())),
                profiles=summarize(rows))


def build_report():
    analyzer_path = ROOT / "mochbot_tetrio_replay/analyze_matches.py"
    spec = importlib.util.spec_from_file_location("moch_analysis", analyzer_path)
    analyzer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(analyzer)
    paths = sorted((ROOT / "mochbot_tetrio_replay").glob("*.ttrm"))
    assert paths, "No replay inputs"
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in
              [Path(__file__), analyzer_path, Path(__file__).with_name("profile_report.py"),
               ROOT / "doc_test_bot_chatgpt.txt", *paths]}
    replay = analyzer.analyze(paths)
    event_types = collections.Counter()
    option_values = {key: collections.Counter() for key in OPTIONS}
    audited_players = 0
    # Line totals independently validate inclusion of all clearing spin counters.
    weights = {"singles": 1, "doubles": 2, "triples": 3, "quads": 4, "pentas": 5}
    weights.update({prefix + key: value for prefix in ("tspin", "minitspin")
                    for key, value in list(weights.items())})
    for path in paths:
        doc = json.loads(path.read_text(encoding="utf-8"))
        for players in doc["replay"]["rounds"]:
            assert len(players) == 2, (path, "unsupported round")
            assert sum(p["username"].casefold().startswith("moch") for p in players) == 1
            assert sum(p["replay"]["results"]["gameoverreason"] == "winner" for p in players) == 1
            for player in players:
                r = player["replay"]
                stats = r["results"]["stats"]
                assert sum(stats["clears"].get(k, 0) * w for k, w in weights.items()) == stats["lines"]
                audited_players += 1
                for event in r["events"]:
                    kind = event["type"]
                    if kind == "ige":
                        kind += ":" + str(event.get("data", {}).get("type"))
                    event_types[kind] += 1
                if player["username"].casefold().startswith("moch"):
                    for key in OPTIONS:
                        value = r["options"].get(key, "<omitted; default not inferred>")
                        option_values[key][json.dumps(value, ensure_ascii=False)] += 1
    mismatches = []
    for match in replay["matches"]:
        if sum(match["score"].values()) != match["moch"]["rounds"]:
            mismatches.append(dict(file=match["file"], scoreboard=match["score"],
                                   stored_rounds=match["moch"]["rounds"]))
    summaries = [replay["moch"], replay["opponents"]]
    summaries += [match[side] for match in replay["matches"] for side in ("moch", "opponents")]
    summaries += [replay["pressure_bins_received_per_piece"][key] for key in ("low", "medium", "high")]
    for s in summaries:
        if s["rounds"]:
            s["comparison_metrics"] = metrics(s["pieces"], s["raw_attack"], s["spin"],
                                              s["spin1"], s["quads"], s["difficult"], s["difficult"] + s["ordinary"])
    native = {key: native_cohort(ROOT / "runlogs" / ("chatgpt_stage4_" + key), label, hashes)
              for key, label in COHORTS.items()}
    native["historical_botris_v02"] = native_cohort(
        ROOT / "runlogs/chatgpt_stage2_vs_botris_v02", "v0.2 historical: botris quota 1.0, NOT current", hashes)
    return dict(
        purpose="observational comparison, not matched performance or a strength ranking",
        limitations=[
            "Current v0.3.1 has ZEN/layer acceptance only; v0.3 survival/versus are historical controls.",
            "TTRM combines different opponents/dates; no proof that mochbot or zzztoj versions are identical.",
            "TTRM real-time PPS is not native simulated turn quota or measured computational throughput.",
            "Attack/B2B/mini conventions and omitted rule defaults are not fully cross-engine aligned.",
            "End maxspike/topbtb/topcombo and delivered-packet runs are NOT complete raw per-lock distributions.",
            "Existing analyzer charge_combo is a heuristic signature, NOT measured B2B cash-out.",
            "Pressure bins use whole-round received/pieces; not board urgency or causal evidence.",
            "No native controlled ZEN/cheese/survival counterpart is present in these versus replays.",
        ],
        validation=dict(audited_replay_player_records=audited_players, event_types=dict(event_types),
                        scoreboard_mismatches=mismatches,
                        moch_rounds_with_zero_received=sum(r["received"] == 0 for r in replay["rounds"]
                                                          if r["player"].casefold().startswith("moch"))),
        replay_moch_option_values=option_values, replay=replay, native=native, source_sha256=hashes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = build_report()
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    print(json.dumps(dict(files=report["replay"]["files"],
                          moch_rounds=report["replay"]["moch"]["rounds"],
                          native_cohorts=len(report["native"]),
                          validation=report["validation"]), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
