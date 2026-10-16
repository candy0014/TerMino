#!/usr/bin/env python3
"""Count expansion observations passing the existing conditional-root prefilter.

This is not native move enumeration and does not create a model or assign splits.
Use the exact prepare_root implementation used for the original offline pilot.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from moch_imitation import ORACLE_SHA, Rejected, digest, prepare_root, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expansion", type=Path)
    parser.add_argument("--max-roof", type=int, default=15)
    args = parser.parse_args()
    manifest_path = args.expansion / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest["status"] not in ("complete", "complete_with_exclusions") or manifest["reviewedSha256"] != ORACLE_SHA:
        raise ValueError("Expansion must have complete official-engine verification")
    output = args.expansion / "native_preselection.json"
    if output.exists():
        raise ValueError("Prescreen output already exists")
    total, accepted, reasons = 0, 0, Counter()
    files = []
    families, spins, hold_changes = Counter(), Counter(), 0
    for record in manifest["files"]:
        source = args.expansion / record["artifact"]
        if digest(source) != record["artifact_sha256"]:
            raise ValueError("Source export differs from expansion manifest")
        data = json.loads(source.read_text())
        if data["reviewedSha256"] != ORACLE_SHA:
            raise ValueError("Unreviewed reference export")
        file_stats = {"file": record["file"], "pieces": 0, "accepted": 0,
                      "rejected_first_reason": Counter(), "rounds": []}
        for round_record in data["rounds"]:
            round_stats = {"round": round_record["round"], "pieces": 0,
                           "accepted": 0, "rejected_first_reason": Counter()}
            previous = None
            for placement in round_record["placements"]:
                total += 1
                round_stats["pieces"] += 1
                try:
                    root = prepare_root(round_record, placement, previous, args.max_roof)
                except Rejected as error:
                    reason = str(error)
                    reasons[reason] += 1
                    round_stats["rejected_first_reason"][reason] += 1
                else:
                    accepted += 1
                    round_stats["accepted"] += 1
                    families[placement["piece"]["type"]] += 1
                    spins["mini" if root["mini"] else "full" if root["spin"] else "none"] += 1
                    hold_changes += root["observed_hold_or_family_change"]
                previous = placement
            file_stats["pieces"] += round_stats["pieces"]
            file_stats["accepted"] += round_stats["accepted"]
            file_stats["rejected_first_reason"].update(round_stats["rejected_first_reason"])
            file_stats["rounds"].append(round_stats)
        if file_stats["pieces"] != record["pieces"]:
            raise ValueError("Source count differs from manifest")
        files.append(file_stats)
    if total != manifest["completed_pieces"] or total != accepted + sum(reasons.values()):
        raise ValueError("Prefilter counts do not reconcile")
    result = {
        "status": "complete", "scope": "Official observation prefilter only; native reachability and production candidate coverage remain unmeasured.",
        "split_status": "No training/evaluation/holdout assignment; unextracted does not imply scientifically untouched.",
        "max_roof": args.max_roof, "verified_rounds": manifest["completed_rounds"],
        "verified_pieces": total, "accepted_before_native_enumeration": accepted,
        "source_excluded_rounds": manifest.get("excluded_rounds", 0),
        "source_excluded_pieces": manifest.get("excluded_pieces", 0),
        "rejected_first_reason": reasons, "accepted_families": families,
        "accepted_spin_classes": spins,
        "accepted_with_hold_or_family_change": hold_changes,
        "timing_and_visibility": "Lock-time snapshots follow HOLD and movement; conditional selected-family placement only, current plus five previews, no HOLD learning or hidden queue.",
        "filter": "Same prepare_root as original pilot: supported rules; predecessor present; unchanged board/state; no pending pressure, intervening garbage receipt or inserted garbage; roof <= threshold; exact colored pure placement/clear reconstruction.",
        "expansion_manifest_sha256": digest(manifest_path),
        "prepare_tool_sha256": digest(Path(__file__).with_name("moch_imitation.py")),
        "prescreen_tool_sha256": digest(Path(__file__)), "files": files,
    }
    write_json(output, result)
    print(json.dumps({key: result[key] for key in (
        "status", "verified_rounds", "verified_pieces",
        "accepted_before_native_enumeration", "rejected_first_reason")}, indent=2))


if __name__ == "__main__":
    main()
