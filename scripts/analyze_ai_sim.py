"""Summarises phantomcrash_ai_sim output (research tool).

Reads one or more batch folders written by phantomcrash_ai_sim (lives.jsonl,
runs.jsonl, stuck.csv) and prints/writes how engaged, quick and mobile the
computer robots were. Usage:

    python scripts/analyze_ai_sim.py research-output/ai-sim/* --json summary.json
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path

MODES = ["idle", "route", "stationary", "engage", "stationary_alt", "withdraw", "route_special"]
HOTSPOT_CELL = 6.0  # world units a stuck-hotspot grid cell spans


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open(encoding="utf-8") as file:
        return [json.loads(line) for line in file if line.strip()]


def pct(values: list[float], q: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(q * (len(ordered) - 1))))
    return ordered[index]


def share(part: float, whole: float) -> float:
    return part / whole if whole > 0 else float("nan")


def load(folders: list[Path]) -> tuple[list[dict], list[dict], list[dict]]:
    lives, runs, stuck = [], [], []
    for folder in folders:
        batch = folder.name
        batch_runs = {r["run"]: r for r in read_jsonl(folder / "runs.jsonl")}
        for run in batch_runs.values():
            run["batch"] = batch
            runs.append(run)
        for life in read_jsonl(folder / "lives.jsonl"):
            run = batch_runs.get(life["run"], {})
            life.update(batch=batch, day=run.get("day"), stage=run.get("stage"),
                        player=run.get("player", False))
            lives.append(life)
        stuck_file = folder / "stuck.csv"
        if stuck_file.exists():
            with stuck_file.open(encoding="utf-8") as file:
                for row in csv.DictReader(file):
                    run = batch_runs.get(int(row["run"]), {})
                    row.update(batch=batch, stage=run.get("stage"))
                    stuck.append(row)
    return lives, runs, stuck


def summarise(lives: list[dict]) -> dict:
    """Time-weighted AI metrics over a set of robot lives."""
    alive = sum(l["alive"] for l in lives)
    minutes = alive / 60.0
    total = lambda key: sum(l[key] for l in lives)  # noqa: E731
    modes = [sum(l["mode"][i] for l in lives) for i in range(len(MODES))]
    reactions = [r for l in lives for r in l["reactions"]]
    first_seen = [l["first_seen"] for l in lives if l["first_seen"] >= 0]
    first_shot = [l["first_shot"] for l in lives if l["first_shot"] >= 0]
    lengths = [l["alive"] for l in lives]
    acquisitions = total("acquisitions")
    ended = defaultdict(int)
    for life in lives:
        ended[life["ended"]] += 1
    # Lives long enough to judge (10 s) that never fired a shot.
    judged = [l for l in lives if l["alive"] >= 10]
    return {
        "lives": len(lives),
        "robot_minutes": round(minutes, 1),
        "funnel": {
            "has_target": share(total("target"), alive),
            "target_visible": share(total("visible"), alive),
            "target_in_reach": share(total("in_reach"), alive),
            "fire_requested": share(total("request"), alive),
            "locked_on_target": share(total("locked"), alive),
            "trigger_held": share(total("trigger"), alive),
        },
        "opportunity": {
            "rival_in_range_and_sight": share(total("opportunity"), alive),
            "of_which_no_target": share(total("opportunity_no_target"), total("opportunity")),
            "of_which_not_firing": share(total("opportunity_holding"), total("opportunity")),
        },
        "modes": {name: share(modes[i], alive) for i, name in enumerate(MODES)},
        "movement": {
            "moving": share(total("moving"), alive),
            "stuck": share(total("stuck"), alive),
            "still_with_target": share(total("still_fighting"), alive),
            "still_idle": share(total("still_idle"), alive),
            "stuck_episodes_per_min": share(total("stuck_episodes"), minutes),
            "long_stuck_per_min": share(total("long_stuck_episodes"), minutes),
            "longest_stuck_p95": pct([l["longest_stuck"] for l in lives], 0.95),
            "longest_stuck_max": max((l["longest_stuck"] for l in lives), default=0),
            "blocked": share(sum(l.get("blocked", 0) for l in lives), alive),
            "long_blocked_per_min": share(sum(l.get("long_blocked_episodes", 0) for l in lives),
                                          minutes),
            "longest_blocked_p95": pct([l.get("longest_blocked", 0) for l in lives], 0.95),
            "longest_blocked_max": max((l.get("longest_blocked", 0) for l in lives), default=0),
            "nav_recoveries_per_min": share(total("recoveries"), minutes),
            "speed_units_per_s": share(total("distance"), alive),
        },
        "timing": {
            "first_sight_p50": pct(first_seen, 0.5),
            "first_sight_p90": pct(first_seen, 0.9),
            "first_shot_p50": pct(first_shot, 0.5),
            "first_shot_p90": pct(first_shot, 0.9),
            "reaction_p50": pct(reactions, 0.5),
            "reaction_p90": pct(reactions, 0.9),
            "acquisitions_unanswered": share(total("unanswered"), acquisitions),
            "never_fired_share": share(sum(1 for l in judged if l["shots"] == 0), len(judged)),
            "nearest_rival_mean": statistics.fmean(
                [l["nearest_mean"] for l in lives if l["nearest_mean"] >= 0] or [float("nan")]),
            "no_rival_within_acquire_range": share(total("no_enemy_near"), alive),
        },
        "combat": {
            "shots_per_min": share(total("shots"), minutes),
            "hits_per_min": share(total("hits"), minutes),
            "damage_dealt_per_min": share(total("damage_dealt"), minutes),
            "kills_per_min": share(total("kills"), minutes),
            "life_seconds_p50": pct(lengths, 0.5),
            "ended": dict(ended),
            "mode_changes_per_min": share(total("transitions"), minutes),
            "dodges_per_min": share(total("dodges"), minutes),
            "pickups_per_min": share(total("pickups"), minutes),
        },
    }


def hotspots(stuck: list[dict], top: int = 8) -> dict[str, list[dict]]:
    cells: dict[str, dict] = defaultdict(lambda: defaultdict(lambda: [0, 0.0, 0.0, 0.0, 0.0]))
    for row in stuck:
        key = (round(float(row["x"]) / HOTSPOT_CELL), round(float(row["z"]) / HOTSPOT_CELL))
        cell = cells[row["stage"]][key]
        cell[0] += 1
        cell[1] += float(row["seconds"])
        cell[2] += float(row["x"])
        cell[3] += float(row["y"])
        cell[4] += float(row["z"])
    out = {}
    for stage, grid in cells.items():
        ranked = sorted(grid.values(), key=lambda c: -c[1])[:top]
        out[stage] = [{"episodes": c[0], "seconds": round(c[1], 1),
                       "x": round(c[2] / c[0], 1), "y": round(c[3] / c[0], 1),
                       "z": round(c[4] / c[0], 1)} for c in ranked]
    return out


def player_summary(runs: list[dict], lives: list[dict]) -> dict:
    with_player = [r for r in runs if r.get("player")]
    if not with_player:
        return {}
    seconds = sum(r["seconds"] for r in with_player)
    firsts = [r["player_first_targeted"] for r in with_player if r["player_first_targeted"] >= 0]
    ai = [l for l in lives if l["player"]]
    ai_alive = sum(l["alive"] for l in ai)
    return {
        "runs": len(with_player),
        "targeted_share": share(sum(r["player_targeted"] for r in with_player), seconds),
        "first_targeted_p50": pct(firsts, 0.5),
        "never_targeted_runs": sum(1 for r in with_player if r["player_first_targeted"] < 0),
        "damage_taken_per_min": share(sum(r["player_damage_taken"] for r in with_player),
                                      seconds / 60),
        "shots_at_player_per_min": share(sum(r["player_shots_at"] for r in with_player),
                                         seconds / 60),
        "ai_time_on_player": share(sum(l["player_target"] for l in ai), ai_alive),
        "ai_time_on_any_target": share(sum(l["target"] for l in ai), ai_alive),
    }


def group(lives: list[dict], key) -> dict[str, dict]:
    buckets: dict[str, list[dict]] = defaultdict(list)
    for life in lives:
        buckets[key(life)].append(life)
    return {name: summarise(items) for name, items in sorted(buckets.items())}


def worst_lives(lives: list[dict], top: int = 10) -> list[dict]:
    keep = ("batch", "run", "stage", "robot", "entrant", "ability", "profile", "name", "alive",
            "stuck", "longest_stuck", "shots", "opportunity", "opportunity_holding", "weapons")
    ranked = sorted(lives, key=lambda l: -(l["stuck"] + l["still_idle"]))[:top]
    return [{k: l[k] for k in keep} for l in ranked]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folders", nargs="+", type=Path)
    parser.add_argument("--json", type=Path, help="write the full summary here")
    args = parser.parse_args()
    lives, runs, stuck = load(args.folders)
    ffa = [l for l in lives if not l["player"]]
    summary = {
        "runs": len(runs),
        "game_seconds": sum(r["seconds"] for r in runs),
        "wall_seconds": sum(r["wall_seconds"] for r in runs),
        "overall": summarise(lives),
        "free_for_all": summarise(ffa),
        "by_stage": group(lives, lambda l: l["stage"]),
        "by_day": group(ffa, lambda l: f"day{l['day']:02d}"),
        "by_ability": group(lives, lambda l: f"{l['ability'] // 25 * 25:02d}-{l['ability'] // 25 * 25 + 24}"),
        "by_profile": group(lives, lambda l: f"profile{l['profile']}"),
        "player": player_summary(runs, lives),
        "stuck_hotspots": hotspots(stuck),
        "worst_lives": worst_lives(lives),
    }
    if args.json:
        args.json.write_text(json.dumps(summary, indent=1, ensure_ascii=False), encoding="utf-8")
    overall = summary["overall"]
    print(f"{summary['runs']} runs, {summary['game_seconds'] / 3600:.1f} game hours, "
          f"{overall['lives']} robot lives, {overall['robot_minutes']} robot-minutes")
    for section in ("funnel", "opportunity", "movement", "timing", "combat"):
        print(f"[{section}]")
        for name, value in overall[section].items():
            print(f"  {name}: {value:.3f}" if isinstance(value, float) else f"  {name}: {value}")
    print("[player]", json.dumps(summary["player"], indent=None))


if __name__ == "__main__":
    main()
