#!/usr/bin/env python3
"""Run the actual tetr executable; never emulate gameplay in this script.

Each child runs in an isolated directory. JSONL contains full distributions;
CSV contains scalar summaries. The engine's raw stderr is retained for audit.
Versus defaults to exactly ten games; --first-to explicitly opts into legacy
win-based stopping. Draws consume a game in the fixed-count protocol.
--completed-versus-games instead counts only natural endings, retaining every
piece-cap truncation and advancing through consecutive seeds up to a safety cap.
"""
import argparse
import collections
import csv
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import tempfile
import time
import unicodedata


def cases(suite):
    zen = dict(name="zen", mode=0, garbage=0)
    layers = [dict(name=f"layer_m{m}", mode=0, garbage=3, mess=m, garbage_goal=100)
              for m in (0.2, 0.4, 0.7, 1.0)]
    timers = [dict(name=f"timer_m{m}_i{i}", mode=0, garbage=4, mess=m, interval=i)
              for m in (0.4, 0.7, 1.0) for i in (6, 4, 3)]
    survival = [dict(name=f"survival_{apm}", mode=0, garbage=5, apm=apm)
                for apm in (30, 40, 50, 60, 70)]
    versus = [dict(name=f"vs_{bot}_turn{quota}", mode=6, garbage=0,
                   opponent=bot, turn_pieces=1, opponent_turn_pieces=quota)
              for bot in (4, 1) for quota in (0.9, 1.0, 1.15, 1.3)]
    if suite == "zen": return [zen]
    if suite == "smoke": return [zen, layers[-1], timers[-1], survival[3]]
    if suite == "solo": return [zen] + layers + timers + survival
    if suite == "versus": return versus
    return [zen] + layers + timers + survival + versus


def config(case, bot, width, turn_pieces=0):
    return f"""[MODEL]
Model = {case['mode']}
[GARBAGE]
Garbage Model = {case['garbage']}
Cheese Model = 1
Num of Hole = 1
Layer Height = 6
Cheese Messiness = {case.get('mess', 0.8)}
Time Interval = {case.get('interval', 3)}
APM for Survival = {case.get('apm', 60)}
[GAME]
Simulation PPS = 1
Turn Pieces = {turn_pieces}
[AUTOPLAY]
Autoplay = {bot}
PPS of Autoplay = 0
Width of Autoplay = {width}
"""


def fields(line):
    return dict(token.split("=", 1) for token in line.split()[1:] if "=" in token)


def summarize(events, result):
    clear_types = collections.Counter()
    spikes, releases, combo_lengths, combo_attacks = (collections.Counter() for _ in range(4))
    spike_attack = combo_length = combo_attack = 0
    last_attack_time = None
    special = clear_moves = 0
    for event in events:
        lines, attack = int(event["lines"]), int(event["attack"])
        spin, mini = int(event["spin"]), int(event["mini"])
        if lines:
            clear_moves += 1
            special += bool(spin or lines >= 4)
            kind = "minispin" if spin and mini else "spin" if spin else "normal"
            piece = unicodedata.normalize("NFKC", event["piece"])
            clear_types[f"{piece}:{kind}:{lines}"] += 1
            combo_length += 1
            combo_attack += attack
            if not spin and lines < 4 and int(event["b2b_before"]) > 0 and int(event["b2b_after"]) == 0:
                # Distribution is actual charge released, including 0 for
                # broken chains below the five-clear charging threshold.
                releases[int(event["b2b_charge"])] += 1
        elif combo_length:
            combo_lengths[combo_length] += 1
            combo_attacks[combo_attack] += 1
            combo_length = combo_attack = 0
        if attack:
            t = float(event["t"])
            if last_attack_time is not None and t - last_attack_time > 1.0 + 1e-9:
                spikes[spike_attack] += 1
                spike_attack = 0
            spike_attack += attack
            last_attack_time = t
    if spike_attack: spikes[spike_attack] += 1
    if combo_length:
        combo_lengths[combo_length] += 1
        combo_attacks[combo_attack] += 1
    pieces = int(result.get("pieces", len(events)))
    attack = int(result.get("attack", sum(int(e["attack"]) for e in events)))
    b2b = int(result.get("b2b", events[-1]["b2b_after"] if events else 0))
    garbage = int(result.get("garbage_cleared", sum(int(e["garbage_lines"]) for e in events)))
    if b2b: releases[b2b] += 1
    return dict(outcome=result.get("outcome", "unknown"), pieces=pieces,
                game_seconds=float(result.get("duration", 0)), attack=attack,
                lines=int(result.get("lines", sum(int(e["lines"]) for e in events))),
                garbage_lines=garbage, remaining_b2b=b2b,
                attack_per_piece=attack / pieces if pieces else 0,
                attack_plus_b2b_per_piece=(attack + b2b) / pieces if pieces else 0,
                pieces_per_garbage_line=pieces / garbage if garbage else None,
                special_clear_fraction=special / clear_moves if clear_moves else 0,
                clear_types=dict(clear_types), spikes=dict(spikes),
                b2b_releases_including_final=dict(releases),
                combo_lengths=dict(combo_lengths), combo_attacks=dict(combo_attacks))


def run_one(args, case, seed, directory):
    label = f"{case['name']}_seed{seed}"
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("TETR_", "BOT_ZZZTOJ_"))}
    env.update(TETR_TEST_BATCH="1", TETR_EXIT_AFTER_GAME="1", TETR_TEST_METRICS="1",
               TETR_TEST_SEED=str(seed), TETR_LOCAL_VERSUS_SEED=str(seed),
               TETR_TEST_MAX_PIECES=str(args.pieces), TETR_LOCAL_VERSUS_SUMMARY="1")
    if "garbage_goal" in case:
        env["TETR_TEST_MAX_GARBAGE_CLEARED"] = str(case["garbage_goal"])
    if case["mode"] == 6:
        env["TETR_TEST_MAX_PIECES_FIRST_ONLY"] = "1"
        env["TETR_LOCAL_VERSUS_BATCH"] = "1"
    if getattr(args, "save_replays", False):
        env["TETR_LOCAL_VERSUS_REPLAY" if case["mode"] == 6 else "TETR_TEST_REPLAY"] = "chatgpt_capture.txt"
    with tempfile.TemporaryDirectory(prefix="tetr_chatgpt_") as temporary:
        work = pathlib.Path(temporary)
        (work / "player1.ini").write_text(config(case, args.bot, args.width,
                                                    case.get('turn_pieces', 0)), encoding="utf-8")
        (work / "player2.ini").write_text(config(case, case.get("opponent", 0), args.width,
                                                    case.get("opponent_turn_pieces", 0)), encoding="utf-8")
        env["TETR_USER_CONFIG"] = str(work / "player1.ini")
        env["TETR_USER_CONFIG_PLAYER2"] = str(work / "player2.ini")
        started = time.perf_counter()
        with (directory / f"{label}.log").open("w", encoding="utf-8") as output:
            process = subprocess.run([str(args.binary)], cwd=work, env=env,
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=output, timeout=args.timeout)
        wall = time.perf_counter() - started
        if getattr(args, "save_replays", False) and process.returncode == 0:
            shutil.copyfile(work / "replay/chatgpt_capture.txt", directory / f"{label}.replay.txt")
    if process.returncode:
        raise RuntimeError(f"{label}: exit {process.returncode}; inspect its .log")
    return parse_run(args, case, seed, directory, wall)


def parse_run(args, case, seed, directory, wall):
    label = f"{case['name']}_seed{seed}"
    events = collections.defaultdict(list)
    decisions = collections.defaultdict(list)
    results = {}
    for line in (directory / f"{label}.log").read_text(encoding="utf-8").splitlines():
        if line.startswith("TETR_BOT_DIAGNOSTIC "):
            diagnostic = fields(line)
            decisions[diagnostic["side"]].append(diagnostic)
        elif line.startswith("TETR_TEST_PLACEMENT "):
            event = fields(line)
            events[event["side"]].append(event)
        elif line.startswith("TETR_TEST_RESULT "):
            result = fields(line)
            results[result.pop("side")] = result
        elif line.startswith("TETR_LOCAL_VERSUS_RESULT "):
            record = fields(line)
            for side in ("first", "second"):
                results[side] = {k[len(side) + 1:]: v for k, v in record.items() if k.startswith(side + "_")}
        elif line.startswith("TETR_TEST_DURATION "):
            record = fields(line)
            for side in ("first", "second"):
                results.setdefault(side, {})["duration"] = record[side]
    if not events or "first" not in results:
        raise RuntimeError(f"{label}: missing engine metrics")
    completion = dict(completed=None, truncated=None, termination_reason=None)
    if case["mode"] == 6:
        completion = versus_completion(results, label)
    records = []
    sides = ("first", "second") if case["mode"] == 6 else events
    for side in sides:
        event_list = events[side]
        if side not in results:
            raise RuntimeError(f"{label}: missing result for {side}")
        metrics = summarize(event_list, results[side])
        if metrics["pieces"] != len(event_list):
            raise RuntimeError(f"{label}: incomplete placement log for {side}")
        if metrics["attack"] != sum(int(event["attack"]) for event in event_list):
            raise RuntimeError(f"{label}: attack total does not match placements for {side}")
        records.append(dict(case=case["name"], parameters=case, seed=seed, side=side,
                            bot=args.bot if side == "first" else case["opponent"],
                            width=args.width, wall_seconds=wall,
                            wall_pps=metrics["pieces"] / wall,
                            diagnostic_decisions=len(decisions[side]),
                            diagnostic_fallbacks=sum(int(d["fallback"]) != 0 for d in decisions[side]),
                            diagnostic_peak_roof=max((int(d["roof"]) for d in decisions[side]), default=None),
                            **completion,
                            **metrics))
    (directory / f"{label}.placements.json").write_text(json.dumps(events, indent=2), encoding="utf-8")
    if any(decisions.values()):
        (directory / f"{label}.decisions.json").write_text(json.dumps(decisions, indent=2), encoding="utf-8")
    return records


def versus_completion(results, label):
    outcomes = tuple(results.get(side, {}).get("outcome") for side in ("first", "second"))
    # Local Versus has no racing/blitz goal. This runner clears inherited test
    # limits and sets only MAX_PIECES (first side only), so GoalReached denotes
    # that cap. The opponent normally observes LeaveRound as opponent_left.
    # Do not infer truncation from piece counts: a natural death at the cap is
    # handled before the cap check in run_piece_loop and remains a valid game.
    if (outcomes[0] == "goal" and outcomes[1] in ("opponent_left", "dead", "won", "goal")):
        return dict(completed=False, truncated=True, termination_reason="piece_cap")
    if outcomes in (("won", "dead"), ("dead", "won"), ("dead", "dead")):
        return dict(completed=True, truncated=False, termination_reason="natural")
    raise RuntimeError(f"{label}: unrecognized versus ending {outcomes}; cannot count as completed")


def write_csv(directory, records):
    scalars = [{k: v for k, v in record.items() if not isinstance(v, dict)} for record in records]
    with (directory / "summary.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(scalars[0]))
        writer.writeheader()
        writer.writerows(scalars)


def continue_case(case, played, wins, repetitions, versus_games=10, first_to=None,
                  completed_versus_games=None, completed=0, max_versus_games=100):
    if case["mode"] != 6:
        return played < repetitions
    if completed_versus_games is not None:
        return completed < completed_versus_games and played < max_versus_games
    if first_to is None:
        return played < versus_games
    # Explicit compatibility mode for historical FT3 experiments only.
    return played < repetitions or max(wins.values(), default=0) < first_to


def versus_score(records, completed_target=None, max_games=100):
    first = [record for record in records if record["side"] == "first"]
    second = [record for record in records if record["side"] == "second"]
    games = len(first)
    wins = collections.Counter(record["side"] for record in records if record["outcome"] == "won")
    completed = sum(record["completed"] for record in first)
    truncated = sum(record["truncated"] for record in first)
    valid_wins = collections.Counter(record["side"] for record in records
                                     if record["completed"] and record["outcome"] == "won")
    required_wins = completed_target // 2 + 1 if completed_target is not None else None
    seeds = [record["seed"] for record in first]
    consecutive = seeds == list(range(seeds[0], seeds[0] + games))
    return dict(first=wins["first"], second=wins["second"], games=games,
                draws=games - sum(wins.values()),
                first_bot=first[0]["bot"], second_bot=second[0]["bot"],
                first_win_rate=wins["first"] / games,
                second_win_rate=wins["second"] / games,
                completed=completed, truncated=truncated,
                completed_first_wins=valid_wins["first"],
                completed_second_wins=valid_wins["second"],
                completed_draws=completed - sum(valid_wins.values()),
                completed_first_win_rate=valid_wins["first"] / completed if completed else None,
                completed_second_win_rate=valid_wins["second"] / completed if completed else None,
                completed_target=completed_target,
                max_total_games=max_games if completed_target is not None else None,
                required_first_wins=required_wins,
                completed_target_reached=completed >= completed_target if completed_target is not None else None,
                acceptance_passed=(completed == completed_target and consecutive and
                                   valid_wins["first"] >= required_wins)
                    if completed_target is not None else None,
                safety_limit_reached=(games >= max_games and completed < completed_target)
                    if completed_target is not None else False,
                seeds=seeds, consecutive_seeds=consecutive)


def write_scores(directory, records, completed_target=None, max_games=100):
    matchups = collections.defaultdict(list)
    for record in records:
        if record["parameters"]["mode"] == 6:
            matchups[record["case"]].append(record)
    scores = {}
    for name, games in matchups.items():
        scores[name] = versus_score(games, completed_target, max_games)
        (directory / f"{name}_score.json").write_text(json.dumps(scores[name], indent=2), encoding="utf-8")
    return scores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, default=pathlib.Path("build/tetr"))
    parser.add_argument("--binary-source-metadata", type=pathlib.Path,
                        help="Original metadata for an archived comparison binary (SHA256 must match)")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--suite", choices=("zen", "smoke", "solo", "versus", "full"), default="zen")
    parser.add_argument("--case", help="Run only one named case from the selected suite")
    parser.add_argument("--bot", type=int, default=9)
    parser.add_argument("--width", type=int, default=300)
    parser.add_argument("--pieces", type=int, default=1000)
    parser.add_argument("--repetitions", type=int, default=3)
    stopping = parser.add_mutually_exclusive_group()
    stopping.add_argument("--versus-games", type=int,
                          help="Fixed games per matchup (default 10); wins never stop the batch early")
    stopping.add_argument("--first-to", type=int,
                          help="Explicit legacy win-based stopping; not used by current fixed-game tests")
    stopping.add_argument("--completed-versus-games", type=int,
                          help="Natural endings per matchup; retain but exclude piece-cap games, never stop at enough wins")
    parser.add_argument("--max-versus-games", type=int,
                        help="Safety cap on total games with --completed-versus-games (default 100)")
    parser.add_argument("--seed", type=int, default=2026092401)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--save-replays", action="store_true", help="Retain the engine's native replay alongside each run")
    parser.add_argument("--reparse", action="store_true", help="Rebuild summaries from this runner's existing raw logs")
    args = parser.parse_args()
    previous_metadata = {}
    if args.reparse and (args.output / "metadata.json").exists():
        previous_metadata = json.loads((args.output / "metadata.json").read_text(encoding="utf-8"))
    if args.versus_games is None and args.first_to is None and args.completed_versus_games is None:
        args.versus_games = previous_metadata.get("versus_games")
        args.first_to = previous_metadata.get("versus_first_to")
        args.completed_versus_games = previous_metadata.get("versus_completed_games")
    if args.versus_games is None and args.first_to is None and args.completed_versus_games is None:
        args.versus_games = 10
    if args.max_versus_games is None:
        args.max_versus_games = previous_metadata.get("versus_max_games", 100)
    if ((args.versus_games is not None and args.versus_games < 1) or
            (args.first_to is not None and args.first_to < 1) or
            (args.completed_versus_games is not None and args.completed_versus_games < 1) or
            args.max_versus_games < 1):
        parser.error("positive versus stopping counts required")
    if args.completed_versus_games is not None and args.max_versus_games < args.completed_versus_games:
        parser.error("max-versus-games must be at least completed-versus-games")
    if args.reparse:
        previous = [json.loads(line) for line in (args.output / "results.jsonl").read_text().splitlines()]
        records = []
        for first in (record for record in previous if record["side"] == "first"):
            args.bot, args.width = first["bot"], first["width"]
            records.extend(parse_run(args, first["parameters"], first["seed"], args.output, first["wall_seconds"]))
        (args.output / "results.jsonl").write_text("".join(json.dumps(record) + "\n" for record in records), encoding="utf-8")
        write_csv(args.output, records)
        write_scores(args.output, records, args.completed_versus_games, args.max_versus_games)
        return
    args.binary = args.binary.resolve(strict=True)
    selected_cases = [case for case in cases(args.suite) if not args.case or case["name"] == args.case]
    if not selected_cases: parser.error("--case does not match the selected suite")
    if args.repetitions < 1 or args.pieces < 1: parser.error("positive pieces/repetitions required")
    args.output.mkdir(parents=True, exist_ok=False)
    repo = pathlib.Path(__file__).resolve().parents[3]
    def git_value(*arguments):
        return subprocess.check_output(["git", *arguments], cwd=repo, text=True).strip()
    sources = [*sorted((repo / "src/bot/chatgpt").glob("*.h")),
               repo / "src/bot/bot_chatgpt.cpp", repo / "src/bot/bot_zzztoj.cpp",
               repo / "include/tetr/bot/bot_chatgpt.h", repo / "src/legacy/bot.cpp",
               repo / "include/tetr/bot/environment.h", repo / "include/tetr/rules/opener_phase.h",
               repo / "src/engine/piece_spawn.cpp", repo / "src/engine/simulation_time.cpp",
               repo / "src/engine/game_loop_adapter.cpp", repo / "src/app/local_versus.cpp",
               repo / "src/rules/attack.cpp", repo / "src/rules/chain.cpp", repo / "src/rules/spin.cpp",
               repo / "zzztoj_standalone/upstream/src/ai_zzz.cpp",
               repo / "zzztoj_standalone/upstream/src/tetris_core.h",
               repo / "zzztoj_standalone/src/rule_tetr_srs_plus.cpp", pathlib.Path(__file__).resolve()]
    metadata = dict(platform=platform.platform(), processor=platform.processor(),
                    cpu_affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
                    binary=str(args.binary), binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                    command=os.sys.argv, simulation_pps=1, autoplay_pps=0,
                    spike_definition="positive attacks linked by gaps <= 1 game second; no minimum size",
                    b2b_definition="actual charge on break; final nonzero B2B count added separately",
                    throughput_definition="pieces / whole child wall time, including startup/countdown",
                    versus_piece_limit="first side only; opponent has no independent piece cap",
                    versus_protocol="turn-pieces-v3-completed" if args.completed_versus_games is not None else "turn-pieces-v2",
                    versus_games=args.versus_games,
                    versus_first_to=args.first_to,
                    versus_completed_games=args.completed_versus_games,
                    versus_max_games=args.max_versus_games,
                    versus_completion_definition="won/dead or dead/dead are natural; first goal is piece-cap truncation; other endings rejected",
                    versus_legacy_draws_definition="score.draws retains all non-win endings; completed_draws excludes truncations",
                    versus_note="native alternating Local Versus; Simulation PPS=1 for both; first Turn Pieces=1, opponent quota varies",
                    test_spec_sha256=hashlib.sha256((repo / "doc_test_bot_chatgpt.txt").read_bytes()).hexdigest(),
                    git_head=git_value("rev-parse", "HEAD"),
                    upstream_head=git_value("-C", "zzztoj_standalone/upstream", "rev-parse", "HEAD"),
                    source_hash_scope="working tree at invocation; archived binary provenance is in binary_source_metadata when supplied",
                    source_sha256={str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    if args.binary_source_metadata:
        reference = json.loads(args.binary_source_metadata.read_text(encoding="utf-8"))
        if reference["binary_sha256"] != metadata["binary_sha256"]:
            parser.error("--binary-source-metadata does not match the supplied executable")
        metadata["binary_source_metadata"] = reference
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    all_records = []
    for case in selected_cases:
        wins = collections.Counter(first=0, second=0)
        repetition = 0
        completed = 0
        while continue_case(case, repetition, wins, args.repetitions,
                            args.versus_games, args.first_to, args.completed_versus_games,
                            completed, args.max_versus_games):
            if case["mode"] == 6 and args.first_to is not None and repetition >= 100:
                raise RuntimeError(f"{case['name']}: 100 games without the requested win-based result")
            records = run_one(args, case, args.seed + repetition, args.output)
            with (args.output / "results.jsonl").open("a", encoding="utf-8") as output:
                for record in records: output.write(json.dumps(record) + "\n")
            all_records.extend(records)
            completed += sum(bool(record["completed"]) for record in records if record["side"] == "first")
            for record in records:
                if record["outcome"] == "won": wins[record["side"]] += 1
                print(f"{record['case']} seed={record['seed']} {record['side']}: "
                      f"{record['outcome']} pieces={record['pieces']} attack={record['attack']} "
                      f"completed={record['completed']} truncated={record['truncated']} "
                      f"APP={record['attack_per_piece']:.4f} effective={record['attack_plus_b2b_per_piece']:.4f} "
                      f"wall_pps={record['wall_pps']:.2f}", flush=True)
            repetition += 1
            # Keep the audit usable even if a later child times out or fails.
            write_csv(args.output, all_records)
            write_scores(args.output, all_records, args.completed_versus_games, args.max_versus_games)
        if case["mode"] == 6:
            score = write_scores(args.output, all_records, args.completed_versus_games,
                                 args.max_versus_games)[case["name"]]
            print(f"{case['name']} final: first_bot={args.bot} "
                  f"wins={wins['first']} losses={wins['second']} draws={score['draws']} "
                  f"games={repetition} completed={score['completed']} truncated={score['truncated']} "
                  f"completed_wins={score['completed_first_wins']} acceptance_passed={score['acceptance_passed']} "
                  f"win_rate={score['first_win_rate']:.1%}", flush=True)
            if score["safety_limit_reached"]:
                raise RuntimeError(f"{case['name']}: safety cap of {args.max_versus_games} total games reached "
                                   f"with only {score['completed']}/{args.completed_versus_games} natural endings; "
                                   "logs and summaries retained, acceptance incomplete")
    write_csv(args.output, all_records)


if __name__ == "__main__":
    main()
