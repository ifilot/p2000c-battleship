#!/usr/bin/env python3
"""Benchmark: emulated seconds per round (the human's shot with its
animation, then the computer's aim and shot) over the first rounds of a
game, per level. Level 1 does no real thinking, so the difference to it is
what the computer's search costs. Run after make build.

  python3 tools/bench.py [rounds]
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import EMULATOR, HD0, IPL, ROOT, keys_for_battle, keys_for_placement, make_image

MHZ = 4.0


def cycles(level, rounds):
    actions = keys_for_placement() + (keys_for_battle(rounds) if rounds else [])
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0), "--hard-disk-1", str(ROOT / "build/hd1.hda"),
           "--fast-storage", "--chunk-cycles", "2000", "--wait-for", "A>", "--send", "F:ZEESLAG\\r",
           "--run", "12000000", "--send", " ", "--wait-for", "Kies de sterkte", "--send", str(level),
           *actions, "--output", "json"]
    state = json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=900).stdout)
    assert state["status"] == "ok", state.get("message")
    return state["cycles"]


def main():
    make_image(ROOT / "build/ZEESLAG.COM", ROOT / "build")
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    per_round = {}
    for level in (1, 2, 3):
        per_round[level] = (cycles(level, rounds) - cycles(level, 0)) / MHZ / 1e6 / rounds
        print(f"level {level}: {per_round[level]:.2f} s per round over {rounds} rounds (emulated)")
    print(f"level 3 thinks about {per_round[3] - per_round[1]:.2f} s per shot more than level 1")


if __name__ == "__main__":
    main()
