#!/usr/bin/env python3
"""Active-work stopwatch for the 24h challenge.

Only time spent actively working counts toward the checkpoint milestones
(10 min / 1 h / 6 h / 12 h / 24 h). Time spent waiting for a usage/rate-limit
window to reset is tracked separately in `cooldown_wait_seconds`.

State lives in ACTIVE_WORK_TIMER.json at the repo root.

Usage (run from anywhere inside the repo):
  python3 Scripts/Tools/work_timer.py start  [--checkpoint CP] [--branch BR] [--next STEP]
  python3 Scripts/Tools/work_timer.py step   --done STEP [--next STEP] [--checkpoint CP] [--branch BR]
  python3 Scripts/Tools/work_timer.py pause  [--reason TEXT]   # stop active clock, start cooldown clock
  python3 Scripts/Tools/work_timer.py resume                   # stop cooldown clock, restart active clock
  python3 Scripts/Tools/work_timer.py stop                     # stop active clock (end of a work session)
  python3 Scripts/Tools/work_timer.py status
"""
from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
STATE_FILE = REPO_ROOT / "ACTIVE_WORK_TIMER.json"

MILESTONES = [
    ("cp-01-10min", 10 * 60),
    ("cp-02-1hr", 60 * 60),
    ("cp-03-6hr", 6 * 3600),
    ("cp-04-12hr", 12 * 3600),
    ("cp-05-24hr", 24 * 3600),
]

DEFAULT_STATE = {
    "active_work_seconds": 0,
    "cooldown_wait_seconds": 0,
    "state": "stopped",  # running | paused_cooldown | stopped
    "segment_started_at": None,  # epoch seconds of current running/cooldown segment
    "current_branch": "main",
    "current_checkpoint": None,
    "last_completed_step": None,
    "next_pending_step": None,
    "pause_reason": None,
    "history": [],
}


def _now() -> int:
    return int(time.time())


def _git_branch() -> str | None:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=REPO_ROOT, capture_output=True, text=True, check=True,
        )
        return out.stdout.strip() or None
    except Exception:
        return None


def load() -> dict:
    if STATE_FILE.exists():
        data = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        for k, v in DEFAULT_STATE.items():
            data.setdefault(k, v if not isinstance(v, list) else list(v))
        return data
    return json.loads(json.dumps(DEFAULT_STATE))


def save(state: dict) -> None:
    STATE_FILE.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _close_segment(state: dict) -> None:
    """Fold the currently open segment into the right counter."""
    started = state.get("segment_started_at")
    if started is None:
        return
    elapsed = max(0, _now() - int(started))
    if state["state"] == "running":
        state["active_work_seconds"] += elapsed
    elif state["state"] == "paused_cooldown":
        state["cooldown_wait_seconds"] += elapsed
    state["segment_started_at"] = None


def _log(state: dict, event: str, **extra) -> None:
    entry = {"t": _now(), "event": event, "active": state["active_work_seconds"]}
    entry.update({k: v for k, v in extra.items() if v is not None})
    state["history"].append(entry)


def _apply_meta(state: dict, args) -> None:
    if getattr(args, "checkpoint", None):
        state["current_checkpoint"] = args.checkpoint
    if getattr(args, "branch", None):
        state["current_branch"] = args.branch
    else:
        br = _git_branch()
        if br:
            state["current_branch"] = br
    if getattr(args, "done", None):
        state["last_completed_step"] = args.done
    if getattr(args, "next", None):
        state["next_pending_step"] = args.next


def live_active_seconds(state: dict) -> int:
    total = state["active_work_seconds"]
    if state["state"] == "running" and state.get("segment_started_at"):
        total += max(0, _now() - int(state["segment_started_at"]))
    return total


def fmt(sec: int) -> str:
    h, rem = divmod(int(sec), 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def cmd_start(state, args):
    _close_segment(state)
    state["state"] = "running"
    state["segment_started_at"] = _now()
    state["pause_reason"] = None
    _apply_meta(state, args)
    _log(state, "start", checkpoint=state["current_checkpoint"])


def cmd_step(state, args):
    # Fold elapsed time so the JSON on disk is always current, keep running.
    was = state["state"]
    _close_segment(state)
    if was in ("running", "paused_cooldown"):
        state["segment_started_at"] = _now()
    _apply_meta(state, args)
    _log(state, "step", done=args.done, next=args.next)


def cmd_pause(state, args):
    _close_segment(state)
    state["state"] = "paused_cooldown"
    state["segment_started_at"] = _now()
    state["pause_reason"] = args.reason
    _apply_meta(state, args)
    _log(state, "pause", reason=args.reason)


def cmd_resume(state, args):
    _close_segment(state)
    state["state"] = "running"
    state["segment_started_at"] = _now()
    state["pause_reason"] = None
    _apply_meta(state, args)
    _log(state, "resume")


def cmd_stop(state, args):
    _close_segment(state)
    state["state"] = "stopped"
    _apply_meta(state, args)
    _log(state, "stop")


def cmd_status(state, _args):
    active = live_active_seconds(state)
    print(f"state              : {state['state']}")
    print(f"active work        : {fmt(active)}  ({active}s)")
    print(f"cooldown wait      : {fmt(state['cooldown_wait_seconds'])}")
    print(f"branch / checkpoint: {state['current_branch']} / {state['current_checkpoint']}")
    print(f"last completed     : {state['last_completed_step']}")
    print(f"next pending       : {state['next_pending_step']}")
    for tag, limit in MILESTONES:
        mark = "reached" if active >= limit else f"in {fmt(limit - active)}"
        print(f"  milestone {tag:<12} {fmt(limit)}  -> {mark}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("start", "step", "pause", "resume", "stop", "status"):
        sp = sub.add_parser(name)
        sp.add_argument("--checkpoint")
        sp.add_argument("--branch")
        sp.add_argument("--done")
        sp.add_argument("--next")
        sp.add_argument("--reason")
    args = p.parse_args()

    state = load()
    handler = globals()[f"cmd_{args.cmd}"]
    handler(state, args)
    if args.cmd != "status":
        save(state)
        print(f"[work_timer] {args.cmd}: active={fmt(live_active_seconds(state))} "
              f"cooldown={fmt(state['cooldown_wait_seconds'])} state={state['state']}")


if __name__ == "__main__":
    main()
