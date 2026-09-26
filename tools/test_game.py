#!/usr/bin/env python3
"""Regression tests: whole games against the computer in the headless emulator.

For each level the human places the fleet by hand (A1, A3, A5, A7, A9, all
horizontal, after one attempt at a spot that touches the carrier, which
must be refused), then fires at every cell in reading order until one fleet
is sunk. After every shot the test waits for the panel's shot counters, so
keys are never sent ahead of the game. At the end both fleets are read from
memory (address from the linker map) and checked against the rules: five
ships of the right lengths that do not touch, no cell fired at twice, the
computer's shots never repeating, hit and sunk counts consistent, the panel
agreeing with memory, and the right winner announced. Doubled keys: Z sent
twice at once (as the keyboard's auto-repeat does while the terminal is busy)
deals one random fleet, the same as a single Z; a second Z after a pause
deals another. Run after `make build`.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import EMULATOR, HD0, IPL, ROOT, keys_for_battle, keys_for_placement, make_image, panel_row

LENGTHS = [5, 4, 3, 3, 2]
FLEET_BYTES = 5 * 3 + 100 + 3
SHOT, SHIP_MASK = 0x80, 0x07
PANEL = 44


def symbol(name):
    text = (ROOT / "build/zeeslag.map").read_text()
    m = re.search(rf"^{name}\s+= \$([0-9A-F]+)", text, re.M)
    return int(m.group(1), 16)


def emulate(level, actions, settle="20000000"):
    fleets = symbol("_fleets")
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0),
           "--hard-disk-1", str(ROOT / "build/hd1.hda"), "--fast-storage", "--wait-cycles", "80000000",
           "--wait-for", "A>", "--send", "F:ZEESLAG\\r", "--run", "12000000", "--send", " ",
           "--wait-for", "Kies de sterkte", "--send", str(level), *actions, "--run", settle,
           "--dump-memory", f"{fleets}:{2 * FLEET_BYTES}", "--output", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    state = json.loads(result.stdout)
    data = bytes.fromhex(state["memory"][0]["bytes"].replace(" ", ""))
    return state, [parse_fleet(data[k * FLEET_BYTES:(k + 1) * FLEET_BYTES]) for k in (0, 1)]


def parse_fleet(b):
    ships = [dict(cell=b[3 * s], vertical=b[3 * s + 1], hits=b[3 * s + 2]) for s in range(5)]
    return dict(ships=ships, grid=list(b[15:115]), afloat=b[115], shots=b[116], hits=b[117])


def check_fleet(name, f, errors):
    occupied = {}
    for s, ship in enumerate(f["ships"]):
        cell, n = ship["cell"], LENGTHS[s]
        r, c = divmod(cell, 10)
        cells = [(r + k, c) if ship["vertical"] else (r, c + k) for k in range(n)]
        if any(rr > 9 or cc > 9 for rr, cc in cells):
            errors.append(f"{name}: ship {s} off the board")
            continue
        for rr, cc in cells:
            if f["grid"][rr * 10 + cc] & SHIP_MASK != s + 1:
                errors.append(f"{name}: grid disagrees with ship {s} at {rr * 10 + cc}")
            occupied[(rr, cc)] = s
        hits = sum(1 for rr, cc in cells if f["grid"][rr * 10 + cc] & SHOT)
        if hits != ship["hits"]:
            errors.append(f"{name}: ship {s} records {ship['hits']} hits, grid shows {hits}")
    for (r, c), s in occupied.items():
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                t = occupied.get((r + dr, c + dc))
                if t is not None and t != s:
                    errors.append(f"{name}: ships {s} and {t} touch at {r * 10 + c}")
    if sum(1 for v in f["grid"] if v & SHIP_MASK) != sum(LENGTHS):
        errors.append(f"{name}: {sum(1 for v in f['grid'] if v & SHIP_MASK)} ship cells")
    shots = sum(1 for v in f["grid"] if v & SHOT)
    hits = sum(1 for v in f["grid"] if v & SHOT and v & SHIP_MASK)
    afloat = sum(1 for s, ship in enumerate(f["ships"]) if ship["hits"] < LENGTHS[s])
    if (shots, hits, afloat) != (f["shots"], f["hits"], f["afloat"]):
        errors.append(f"{name}: counters {f['shots']}/{f['hits']}/{f['afloat']}, grid {shots}/{hits}/{afloat}")


def run_level(level):
    actions = keys_for_placement() + keys_for_battle(100)
    state, (human, computer) = emulate(level, actions)
    flat = "".join(state["screen"])
    rows = [flat[r * 64:(r + 1) * 64] for r in range(21)]
    panel = {r: rows[r][PANEL:].rstrip() for r in range(21)}
    errors = []
    expected = [0, 20, 40, 60, 80]
    if [s["cell"] for s in human["ships"]] != expected or any(s["vertical"] for s in human["ships"]):
        errors.append(f"human fleet not where it was placed: {human['ships']}")
    check_fleet("human", human, errors)
    check_fleet("computer", computer, errors)
    if (human["afloat"] == 0) == (computer["afloat"] == 0):
        errors.append(f"no single winner: afloat {human['afloat']} / {computer['afloat']}")
    status = panel[11]
    won = computer["afloat"] == 0
    if status != ("U hebt gewonnen!" if won else "De P2000C wint"):
        errors.append(f"status '{status}'")
    if computer["shots"] != human["shots"] + (1 if won else 0):
        errors.append(f"shot counts {computer['shots']} (human) / {human['shots']} (computer) do not alternate")
    for label, row, u, p in (("Schoten", 16, computer["shots"], human["shots"]),
                             ("Raak", 17, computer["hits"], human["hits"]),
                             ("Vloot", 19, human["afloat"], computer["afloat"])):
        if panel[row] != panel_row(label, u, p):
            errors.append(f"panel row {row} '{panel[row]}' != '{panel_row(label, u, p)}'")
    print(f"level {level}: {'won' if won else 'lost'} by the human; human fired {computer['shots']}, "
          f"computer fired {human['shots']} ({human['hits']} hits), emulator {state['status']}")
    for e in errors:
        print("   ", e)
    return not errors


def check_repeats():
    """A doubled Z counts once; the same key after a pause counts again.
    Key timing seeds the random numbers, so fleets are only compared within
    one input sequence (a run that stops earlier is a prefix of it)."""
    def fleet(actions, settle="20000000"):
        actions = ["--wait-for", "Kies een plek", "--run", "2000000"] + actions
        return emulate(1, actions, settle)[1][0]["ships"]
    errors = []
    # 0.1 s after "zz" the first fleet is dealt; the second Z cannot have been handled yet.
    if fleet(["--send", "zz"], settle="400000") != fleet(["--send", "zz"]):
        errors.append("a doubled Z dealt a second fleet")
    first = ["--send", "z", "--wait-for", "Vloot gereed!", "--run", "2000000"]
    if fleet(first) == fleet(first + ["--send", "z"]):
        errors.append("a second Z after a pause was dropped")
    print("doubled keys:", "ok" if not errors else "")
    for e in errors:
        print("   ", e)
    return not errors


def main():
    make_image(ROOT / "build/ZEESLAG.COM", ROOT / "build")
    results = [run_level(level) for level in (1, 2, 3)] + [check_repeats()]
    print("PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
