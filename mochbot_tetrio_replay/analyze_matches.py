#!/usr/bin/env python3
"""Batch strategy analysis for multi-round TETR.IO .ttrm matches."""

from __future__ import annotations

import argparse
import glob
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


SPIN_KEYS = (
    "minitspinsingles", "tspinsingles",
    "minitspindoubles", "tspindoubles",
    "minitspintriples", "tspintriples",
    "minitspinquads", "tspinquads", "tspinpentas",
)


def n(value: Any) -> int:
    return int(value or 0)


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def confirmed_garbage(player: dict[str, Any]) -> list[tuple[int, int]]:
    """Incoming sent attack on the player's local frame clock."""
    result = []
    for event in player["replay"]["events"]:
        outer = event.get("data", {})
        inner = outer.get("data", {}) if isinstance(outer, dict) else {}
        if (event.get("type") == "ige" and outer.get("type") == "interaction_confirm"
                and inner.get("type") == "garbage"):
            result.append((n(event.get("frame")), n(inner.get("amt"))))
    return result


def outgoing_garbage(
    players: list[dict[str, Any]], player: dict[str, Any]
) -> list[tuple[int, int]]:
    """Delivered attack sourced by player, on the player's source frame clock."""
    game_id = player["replay"]["options"].get("gameid")
    result = []
    for other in players:
        if other is player:
            continue
        for event in other["replay"]["events"]:
            outer = event.get("data", {})
            inner = outer.get("data", {}) if isinstance(outer, dict) else {}
            if (event.get("type") == "ige" and outer.get("type") == "interaction_confirm"
                    and inner.get("type") == "garbage"
                    and inner.get("gameid") == game_id):
                result.append((n(inner.get("frame")), n(inner.get("amt"))))
    return result


def hard_drop_frames(player: dict[str, Any]) -> list[int]:
    return [
        n(event.get("frame"))
        for event in player["replay"]["events"]
        if event.get("type") == "keydown"
        and isinstance(event.get("data"), dict)
        and event["data"].get("key") == "hardDrop"
    ]


def clear_metrics(clears: dict[str, Any]) -> dict[str, int]:
    spin = sum(n(clears.get(key)) for key in SPIN_KEYS)
    spin1 = n(clears.get("minitspinsingles")) + n(clears.get("tspinsingles"))
    spin2 = n(clears.get("minitspindoubles")) + n(clears.get("tspindoubles"))
    spin3 = n(clears.get("minitspintriples")) + n(clears.get("tspintriples"))
    quads = n(clears.get("quads"))
    pentas = n(clears.get("pentas"))
    ordinary = sum(n(clears.get(key)) for key in ("singles", "doubles", "triples"))
    return {
        "spin": spin,
        "spin1": spin1,
        "spin2": spin2,
        "spin3": spin3,
        "quads": quads,
        "pentas": pentas,
        "difficult": spin + quads + pentas,
        "ordinary": ordinary,
        "all_clear": n(clears.get("allclear")),
    }


def consecutive_attack_runs(
    file_name: str,
    round_number: int,
    player: dict[str, Any],
    players: list[dict[str, Any]],
    outcome: str,
    top_combo: int,
    max_spike: int,
    top_b2b: int,
) -> tuple[list[dict[str, Any]], float]:
    drops = hard_drop_frames(player)
    ordinal = {frame: index + 1 for index, frame in enumerate(drops)}
    grouped: dict[int, int] = defaultdict(int)
    for frame, amount in outgoing_garbage(players, player):
        grouped[frame] += amount
    records = sorted(
        (ordinal[frame], frame, amount)
        for frame, amount in grouped.items()
        if frame in ordinal
    )
    match_fraction = len(records) / len(grouped) if grouped else 1.0
    incoming = confirmed_garbage(player)

    runs: list[list[tuple[int, int, int]]] = []
    current: list[tuple[int, int, int]] = []
    for record in records:
        if current and record[0] != current[-1][0] + 1:
            if len(current) >= 3:
                runs.append(current)
            current = []
        current.append(record)
    if len(current) >= 3:
        runs.append(current)

    result = []
    for run in runs:
        start_frame = run[0][1]
        end_frame = run[-1][1]
        result.append({
            "file": file_name,
            "round": round_number,
            "outcome": outcome,
            "start_piece": run[0][0],
            "end_piece": run[-1][0],
            "clears": len(run),
            "seconds": (end_frame - start_frame) / 60.0,
            "delivered_attack": sum(item[2] for item in run),
            "packets": [item[2] for item in run],
            "incoming_previous_5s": sum(
                amount for frame, amount in incoming
                if start_frame - 300 <= frame < start_frame
            ),
            "incoming_during": sum(
                amount for frame, amount in incoming
                if start_frame <= frame <= end_frame
            ),
            "round_top_combo": top_combo,
            "round_max_spike": max_spike,
            "round_top_b2b": top_b2b,
        })
    return result, match_fraction


def analyze_round(
    file_name: str,
    round_number: int,
    players: list[dict[str, Any]],
    player: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    replay = player["replay"]
    result = replay["results"]
    stats = result["stats"]
    garbage = stats["garbage"]
    clear = clear_metrics(stats["clears"])
    pieces = n(stats.get("piecesplaced"))
    duration = float(stats.get("finaltime", 0.0)) / 1000.0
    top_combo = n(stats.get("topcombo"))
    top_b2b = n(stats.get("topbtb"))
    max_spike = n(garbage.get("maxspike"))
    outcome = result.get("gameoverreason", "unknown")
    bursts, attack_frame_match = consecutive_attack_runs(
        file_name, round_number, player, players, outcome,
        top_combo, max_spike, top_b2b)
    row = {
        "file": file_name,
        "round": round_number,
        "player": player["username"],
        "opponent": next((p["username"] for p in players if p is not player), "?"),
        "outcome": outcome,
        "won": outcome == "winner",
        "duration_seconds": duration,
        "pieces": pieces,
        "lines": n(stats.get("lines")),
        "raw_attack": n(garbage.get("attack")),
        "sent": n(garbage.get("sent")),
        "received": n(garbage.get("received")),
        "garbage_cleared": n(garbage.get("cleared")),
        "max_spike": max_spike,
        "max_spike_nomult": n(garbage.get("maxspike_nomult")),
        "top_combo": top_combo,
        "top_b2b": top_b2b,
        "final_b2b": n(stats.get("btb")),
        "holds": n(stats.get("holds")),
        "inputs": n(stats.get("inputs")),
        "pps": float(result["aggregatestats"].get("pps", 0.0)),
        "apm": float(result["aggregatestats"].get("apm", 0.0)),
        "vs": float(result["aggregatestats"].get("vsscore", 0.0)),
        "attack_frame_match": attack_frame_match,
        **clear,
    }
    row.update({
        "app": row["raw_attack"] / pieces if pieces else 0.0,
        "sent_per_piece": row["sent"] / pieces if pieces else 0.0,
        "received_per_piece": row["received"] / pieces if pieces else 0.0,
        "garbage_cleared_per_piece": row["garbage_cleared"] / pieces if pieces else 0.0,
        "pieces_per_difficult": pieces / row["difficult"] if row["difficult"] else None,
        "pieces_per_spin": pieces / row["spin"] if row["spin"] else None,
    })
    return row, bursts


def sum_rows(rows: Iterable[dict[str, Any]]) -> dict[str, Any]:
    rows = list(rows)
    sum_keys = (
        "duration_seconds", "pieces", "lines", "raw_attack", "sent", "received",
        "garbage_cleared", "spin", "spin1", "spin2", "spin3", "quads", "pentas",
        "difficult", "ordinary", "all_clear", "holds", "inputs",
    )
    out = {key: sum(row[key] for row in rows) for key in sum_keys}
    pieces = out["pieces"]
    difficult = out["difficult"]
    spin = out["spin"]
    total_clears = difficult + out["ordinary"]
    out.update({
        "rounds": len(rows),
        "wins": sum(row["won"] for row in rows),
        "pps": pieces / out["duration_seconds"] if out["duration_seconds"] else 0.0,
        "apm": out["raw_attack"] * 60.0 / out["duration_seconds"]
            if out["duration_seconds"] else 0.0,
        "app": out["raw_attack"] / pieces if pieces else 0.0,
        "sent_per_piece": out["sent"] / pieces if pieces else 0.0,
        "received_per_piece": out["received"] / pieces if pieces else 0.0,
        "garbage_cleared_per_piece": out["garbage_cleared"] / pieces if pieces else 0.0,
        "pieces_per_difficult": pieces / difficult if difficult else None,
        "pieces_per_spin": pieces / spin if spin else None,
        "spin1_share": out["spin1"] / spin if spin else 0.0,
        "spin2_share": out["spin2"] / spin if spin else 0.0,
        "quad_share_difficult": out["quads"] / difficult if difficult else 0.0,
        "ordinary_share_clears": out["ordinary"] / total_clears if total_clears else 0.0,
        "holds_per_piece": out["holds"] / pieces if pieces else 0.0,
        "inputs_per_piece": out["inputs"] / pieces if pieces else 0.0,
        "top_b2b_max": max((row["top_b2b"] for row in rows), default=0),
        "top_combo_max": max((row["top_combo"] for row in rows), default=0),
        "max_spike_max": max((row["max_spike"] for row in rows), default=0),
    })
    return out


def pressure_bin(row: dict[str, Any]) -> str:
    value = row["received_per_piece"]
    if value < 0.50:
        return "low"
    if value < 1.00:
        return "medium"
    return "high"


def analyze(paths: list[Path]) -> dict[str, Any]:
    rounds: list[dict[str, Any]] = []
    bursts: list[dict[str, Any]] = []
    matches = []
    for path in paths:
        document = json.loads(path.read_text(encoding="utf-8"))
        replay_rounds = document["replay"].get("rounds", [])
        match_rows = []
        for round_number, players in enumerate(replay_rounds, 1):
            if len(players) != 2:
                continue
            for player in players:
                row, player_bursts = analyze_round(
                    path.name, round_number, players, player)
                rounds.append(row)
                match_rows.append(row)
                if player["username"].casefold().startswith("moch"):
                    bursts.extend(player_bursts)
        moch_rows = [r for r in match_rows if r["player"].casefold().startswith("moch")]
        opponent_rows = [r for r in match_rows if not r["player"].casefold().startswith("moch")]
        leaderboard = document["replay"].get("leaderboard", [])
        matches.append({
            "file": path.name,
            "timestamp": document.get("ts"),
            "score": {entry["username"]: n(entry.get("wins")) for entry in leaderboard},
            "moch": sum_rows(moch_rows),
            "opponents": sum_rows(opponent_rows),
        })

    moch = [r for r in rounds if r["player"].casefold().startswith("moch")]
    other = [r for r in rounds if not r["player"].casefold().startswith("moch")]
    pressure = {}
    for label in ("low", "medium", "high"):
        selected = [row for row in moch if pressure_bin(row) == label]
        pressure[label] = sum_rows(selected) if selected else {"rounds": 0}

    combo4 = [row for row in moch if row["top_combo"] >= 3]
    charge_combo = []
    for burst in bursts:
        packets = burst["packets"]
        peak = max(packets)
        peak_index = packets.index(peak)
        # Canonical B2B cash-out signature: a packet approximately as large as
        # the round's mature chain, followed immediately by at least two more
        # attacking clears.  The last condition avoids mistaking an isolated
        # T-spin/quad spike for a charge-to-combo conversion.
        if (len(packets) >= 4 and burst["seconds"] <= 5.0
                and peak >= max(8, burst["round_top_b2b"] - 3)
                and len(packets) - peak_index - 1 >= 2):
            charge_combo.append(burst)
    burst_ranked = sorted(
        bursts,
        key=lambda x: (x["delivered_attack"], x["incoming_previous_5s"], x["clears"]),
        reverse=True,
    )
    counter_ranked = sorted(
        bursts,
        key=lambda x: (x["incoming_previous_5s"], x["delivered_attack"], x["clears"]),
        reverse=True,
    )
    charge_ranked = sorted(
        charge_combo,
        key=lambda x: (x["delivered_attack"], x["incoming_previous_5s"]),
        reverse=True,
    )
    return {
        "files": len(paths),
        "matches": matches,
        "moch": sum_rows(moch),
        "opponents": sum_rows(other),
        "pressure_bins_received_per_piece": {
            "definition": {"low": "<0.50", "medium": "0.50-0.99", "high": ">=1.00"},
            **pressure,
        },
        "combo4_rounds": sum_rows(combo4) if combo4 else {"rounds": 0},
        "charge_combo_summary": {
            "runs": len(charge_combo),
            "wins": sum(run["outcome"] == "winner" for run in charge_combo),
            "delivered_attack": sum(run["delivered_attack"] for run in charge_combo),
            "incoming_previous_5s": sum(
                run["incoming_previous_5s"] for run in charge_combo),
            "max_delivered_attack": max(
                (run["delivered_attack"] for run in charge_combo), default=0),
        },
        "charge_combo_runs": charge_ranked[:20],
        "top_consecutive_attack_runs": burst_ranked[:20],
        "top_counterattack_runs": counter_ranked[:20],
        "attack_frame_match_p05": percentile(
            [row["attack_frame_match"] for row in moch], 0.05),
        "rounds": rounds,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    paths = args.paths or [Path(p) for p in sorted(glob.glob("tetrio_replay/*.ttrm"))]
    print(json.dumps(analyze(paths), ensure_ascii=False,
                     indent=None if args.compact else 2))


if __name__ == "__main__":
    main()
