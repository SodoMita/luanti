#!/usr/bin/env python3
"""Summarise a Luanti profiler log into a table of averaged timings.

The Luanti profiler prints one block per second; each entry is
`<name>_..._ <samples>x <value>`, where <samples> is the number of times the
value was recorded in that interval -- i.e. frames per second for per-frame
counters.
"""
import re
import sys
from collections import defaultdict


LINE_RE = re.compile(
    r"^\S*\s*\d{4}-\d\d-\d\d \d\d:\d\d:\d\d: \w+\[\w+\]:   (.*?)_*\s+(\d+)x\s+([-\d.eE+]+)\s*$"
)

# keys we care about, in output order
INTERESTING = [
    "Irr: drawcalls",
    "Irr: primitives per drawcall",
    "Irr: HW buffers active",
    "Irr: HW buffers uploaded",
    "Game::updateFrame(): update frame [ms]",
    "Draw scene [us]",
    "Time non-rendering [us]",
    "Sleep [us]",
    "renderMap(SOLID): drawcalls [#]",
    "renderMap(SOLID): material swaps [#]",
    "renderMap(SOLID): draw meshes [ms]",
    "renderMap(SOLID): collecting [ms]",
    "renderMap(SOLID): vertices drawn [#]",
    "renderMap(SOLID): merged buffers [#]",
    "renderMap(TRANS): drawcalls [#]",
    "renderMap(TRANS): draw meshes [ms]",
    "MapBlocks drawn [#]",
    "ActiveObjectMgr: CAO count [#]",
    "Client: Mesh making (sum) [ms]",
    "Server::AsyncRunStep() [ms]",
    "Server::RunStep() (max) [ms]",
    "ServerEnv: Run SAO::step() [ms]",
    "EmergeThread: Mapgen::makeChunk [ms]",
    "Sky::render() [us]",
]


def parse(path):
    rows = []
    cur = {}
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LINE_RE.match(line.rstrip("\n"))
            if m:
                name = m.group(1).strip()
                samples = int(m.group(2))
                val = float(m.group(3))
                cur[name] = (samples, val)
                continue
            if "Profiler:" in line:
                if cur:
                    rows.append(cur)
                cur = {}
    if cur:
        rows.append(cur)
    return rows


def fps_of(row):
    """Frames per profiler interval ~ frames per second."""
    for key in ("Irr: drawcalls", "Game::updateFrame(): update frame [ms]"):
        if key in row:
            return row[key][0]
    return None


def main():
    path = sys.argv[1]
    warmup = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    rows = parse(path)
    if not rows:
        print("NO PROFILER DATA FOUND")
        print(open(path, errors="replace").read()[-2000:])
        return 1
    rows = rows[warmup:] if len(rows) > warmup + 3 else rows

    agg = defaultdict(list)
    for r in rows:
        for k, (n, v) in r.items():
            agg[k].append((n, v))

    fps_vals = [fps_of(r) for r in rows]
    fps_vals = [f for f in fps_vals if f]
    print(f"{len(rows)} profiler intervals analysed (warmup {warmup})")
    if fps_vals:
        fps_vals.sort()
        avg = sum(fps_vals) / len(fps_vals)
        print(f"  {'FPS (from sample counts)':60s} mean={avg:8.2f}  med={fps_vals[len(fps_vals)//2]:6.1f}"
              f"  min={min(fps_vals):6.1f}  max={max(fps_vals):6.1f}")

    keys = [k for k in INTERESTING if k in agg]
    for k in sorted(set(agg) - set(keys)):
        keys.append(k)

    for k in keys:
        vals = [v for (n, v) in agg[k]]
        if not vals:
            continue
        cnt = sum(n for (n, v) in agg[k]) / len(agg[k])
        print(f"  {k:60s} mean={sum(vals)/len(vals):10.3f}  min={min(vals):10.3f}"
              f"  max={max(vals):10.3f}  (n={cnt:.1f}/interval)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
