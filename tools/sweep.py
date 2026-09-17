"""Every parameter must actually change the picture.

A uniform name that does not match between the C++ and the GLSL is silently
ignored: glGetUniformLocation returns -1, glUniform on -1 is a documented no-op,
and nothing in the build says a word. A control can therefore be completely dead
while everything compiles, links, loads and renders. Nothing else in this repo
catches that.

So: render each parameter at both ends of its range against the same pattern,
and report any that made no difference at all.

    python3 tools/sweep.py

Exit code 1 means something is dead.

------------------------------------------------------------------ the traps

**Most controls only act on one pattern.** The grid controls are invisible on
the bars and the tile controls are invisible on the grid -- correctly. So every
parameter is swept with the pattern that reads it, listed in `CONTEXT`, and a
parameter missing from that table is swept on the defaults and will be reported
dead if the defaults do not read it. That is the table doing its job: a new
control has to be told which pattern it belongs to.

**Every name must be unique.** `--set` finds a parameter by name and takes
the first match, so a second control called Diagonals could never be swept --
which is how the alignment chart's switches came to be called Diagonal Cross
and Centre Marks.

**A dropdown holds its element VALUE.** `gttest --list` prints an option's real
range for exactly this reason.

**The preset row is a whole pattern**; every other row here is one of its
columns, so it is swept from Custom to the last row and is expected to change
the picture by changing everything.

**Never sweep the About block.** Those are buttons that open a web browser, and
sweeping them opens one tab per press. `gttest --list` marks them so.
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = str(ROOT / "build" / "gttest")
SCRATCH = tempfile.mkdtemp(prefix="gtsweep")

WIDTH, HEIGHT = 640, 360
FRAMES = 1

# Parameters that cannot or must not be swept, with the reason.
SKIP = {
    "Text": "free text, set through SetTextParameter; the burn-in check in "
            "gttest --pixels is what proves it draws",
}

# Which pattern each control belongs to, and anything else it needs on to be
# visible. Pattern values: 0 SMPTE, 1 75% bars, 2 100% bars, 3 grid,
# 4 alignment, 5 LED tiles, 6 solid, 7 greyscale, 8 pixel check.
GRID = {"Pattern": 3, "Burn-in": 0}
# Forty-pixel tiles, so a whole cabinet -- and its neighbour of the other
# colour -- fits inside even the 160x90 raster CI sweeps at.
LED = {"Pattern": 5, "Burn-in": 0, "Tile Width": 40, "Tile Height": 40}
SOLID = {"Pattern": 6, "Burn-in": 0}
GREY = {"Pattern": 7, "Burn-in": 0}
PIX = {"Pattern": 8, "Burn-in": 0}
ALIGN = {"Pattern": 4, "Burn-in": 0}

CONTEXT = {
    "Levels": {"Pattern": 0},
    "Grid By": dict(GRID, **{"Pitch": 37}),
    "Pitch": GRID,
    "Divisions X": dict(GRID, **{"Grid By": 1}),
    "Divisions Y": dict(GRID, **{"Grid By": 1}),
    "Line Width": GRID,
    "Major Every": GRID,
    "Major Width": GRID,
    "Diagonals": GRID,
    "Centre Target": GRID,
    "Origin Centre": dict(GRID, **{"Pitch": 37}),
    "Cell Labels": GRID,
    "Line Red": GRID,
    "Line Green": GRID,
    "Line Blue": GRID,
    "Major Red": GRID,
    "Major Green": GRID,
    "Major Blue": GRID,
    "Background Red": GRID,
    "Background Green": GRID,
    "Background Blue": GRID,
    "Tile Width": LED,
    "Tile Height": LED,
    # A module of 1024 in a 192px tile draws no line, same as 0; sweep to a
    # module that fits.
    "Module Width": dict(LED, **{"_high": 20}),
    "Module Height": dict(LED, **{"_high": 20}),
    "Origin X": LED,
    "Origin Y": LED,
    "Checker": LED,
    "Tile Labels": LED,
    "Border Width": LED,
    "Tile A Red": LED,
    "Tile A Green": LED,
    "Tile A Blue": LED,
    "Tile B Red": LED,
    "Tile B Green": LED,
    "Tile B Blue": LED,
    "Border Red": LED,
    "Border Green": LED,
    "Border Blue": LED,
    # Element 10 is Custom, whose default colour is white -- the same field as
    # element 0. Sweep to red.
    "Field": dict(SOLID, **{"_high": 2}),
    "Level": SOLID,
    "Custom Red": dict(SOLID, **{"Field": 10}),
    "Custom Green": dict(SOLID, **{"Field": 10}),
    "Custom Blue": dict(SOLID, **{"Field": 10}),
    "Steps": GREY,
    "Vertical": GREY,
    "Per Channel": GREY,
    "Cell": PIX,
    "Line Bursts": PIX,
    # The corner brackets run along the edges too, and on a small raster
    # they cover all of them, so the border is swept with them off.
    "Edge Border": dict(ALIGN, **{"Corner Markers": 0}),
    "Corner Markers": ALIGN,
    "Corner Size": ALIGN,
    "Safe Area A %": ALIGN,
    "Safe Area B %": ALIGN,
    "Pixel Patches": ALIGN,
    "Centre Marks": ALIGN,
    "Diagonal Cross": ALIGN,
    # Motion, on a plain field so the marker is the only thing that moves.
    "Marker": dict(SOLID, **{"_time": 0.25}),
    "Speed": dict(SOLID, **{"Marker": 1, "_time": 0.25}),
    "Marker Size": dict(SOLID, **{"Marker": 1, "_time": 0.25}),
    "Frame Counter": SOLID,
    "Burn-in": {"Pattern": 6},
    "Show Resolution": {"Pattern": 6, "Text": ""},
    "Show Pattern": {"Pattern": 6, "Text": "", "Show Resolution": 0},
    "Position": {"Pattern": 6},
    "Text Size": {"Pattern": 6},
    "Plate": {"Pattern": 6},
}


def parameters():
    """id, name, kind, low, high from the harness's own declaration."""
    out = subprocess.run([BIN, "--list"], capture_output=True, text=True)
    if out.returncode != 0:
        print("could not list parameters:", out.stdout, out.stderr)
        sys.exit(1)

    found = []
    for line in out.stdout.splitlines():
        m = re.match(
            r"\s*(\d+)\s+(.+?)\s{2,}(\S+)\s+([\d.eE+-]+)\s+\[\s*([\d.eE+-]+)\s*\.\.\s*([\d.eE+-]+)\s*\]",
            line,
        )
        if m:
            found.append(
                (int(m.group(1)), m.group(2).strip(), m.group(3),
                 float(m.group(5)), float(m.group(6)))
            )
        else:
            m = re.match(r"\s*(\d+)\s+(.+?)\s{2,}(about|text)\s", line)
            if m:
                found.append((int(m.group(1)), m.group(2).strip(), m.group(3), 0.0, 0.0))
    return found


def render(path, overrides, frames):
    args = [BIN, "--out", path, "--size", f"{WIDTH}x{HEIGHT}", "--frames", str(frames)]
    merged = {k: v for k, v in overrides.items() if not k.startswith("_")}
    for name, value in merged.items():
        args += ["--set", f"{name}={value}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        print("render failed:", " ".join(args), r.stdout, r.stderr)
        sys.exit(1)
    return pathlib.Path(path).read_bytes()


def pixels(png):
    """Raw RGBA out of the harness's own PNG (filter 0 rows), so nothing else
    is a dependency."""
    i = 8
    idat = b""
    width = height = 0
    while i < len(png):
        length = int.from_bytes(png[i:i + 4], "big")
        kind = png[i + 4:i + 8]
        data = png[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(data[0:4], "big")
            height = int.from_bytes(data[4:8], "big")
        elif kind == b"IDAT":
            idat += data
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for row in range(height):
        out += raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)]
    return out


def difference(a, b):
    pa, pb = pixels(a), pixels(b)
    if len(pa) != len(pb):
        return 1.0, len(pa)
    changed = sum(1 for x, y in zip(pa, pb) if x != y)
    return changed / max(len(pa), 1), changed


def sweep_one(job):
    pid, name, low, high, context = job
    frames = context.get("_frames", FRAMES)
    # The marker moves with time; a frame count of 16 puts it a quarter of the
    # way across at 60 fps, where the sweep can see it.
    if "_time" in context:
        frames = int(context["_time"] * 60) + 1

    lo = dict(context)
    hi = dict(context)
    lo[name] = context.get("_low", low)
    hi[name] = context.get("_high", high)

    a = render(f"{SCRATCH}/{pid}_lo.png", lo, frames)
    b = render(f"{SCRATCH}/{pid}_hi.png", hi, frames)
    fraction, count = difference(a, b)
    # Progress as it happens, on stderr, so a run that is cut off by a CI
    # timeout still says how far it got and how long each one took.
    print(f"  swept {pid:3d} {name}", file=sys.stderr, flush=True)
    return pid, name, fraction, count


def main():
    global WIDTH, HEIGHT

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="%dx%d" % (WIDTH, HEIGHT))
    ap.add_argument("--jobs", type=int, default=0)
    args = ap.parse_args()
    if "x" in args.size:
        WIDTH, HEIGHT = (int(v) for v in args.size.split("x", 1))
    jobs = args.jobs or min(8, os.cpu_count() or 1)

    if not pathlib.Path(BIN).exists():
        print(f"{BIN} is not built")
        return 1

    skipped = []
    work = []
    for pid, name, kind, low, high in parameters():
        if kind == "about":
            skipped.append((name, "a button that opens a web browser"))
            continue
        if kind == "text" or name in SKIP:
            skipped.append((name, SKIP.get(name, "free text")))
            continue
        context = CONTEXT.get(name, {})
        work.append((pid, name, low, high, context))

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for r in pool.map(sweep_one, work):
            results.append(r)

    dead = []
    for pid, name, fraction, count in sorted(results):
        if count == 0:
            dead.append(name)
            print(f"DEAD  {pid:4d}  {name}")
        else:
            print(f"ok    {pid:4d}  {name}  ({count} subpixels, {fraction * 100:.2f}%)")

    print()
    for name, why in skipped:
        print(f"skip  {name}: {why}")

    print(f"\n{len(results)} swept, {len(dead)} dead, {len(skipped)} skipped, {jobs} at a time")
    if dead:
        print("\nDEAD CONTROLS: " + ", ".join(dead))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
