"""Every parameter must actually change the picture.

A uniform name that does not match between the C++ and the GLSL is silently
ignored: glGetUniformLocation returns -1, glUniform on -1 is a documented no-op,
and nothing in the build says a word. A control can therefore be completely dead
while everything compiles, links, loads and renders. Nothing else in this repo
catches that.

So: render each parameter at both ends of its range against the same pattern,
and report any that made no difference at all.

    python3 tools/sweep.py [--size WxH] [--jobs N] [--timeout S]

Exit code 1 means something is dead, a negative control failed, or a render
hung.

------------------------------------------------------------------ the traps

**One process per batch, never one per render.** Each new GL program pays for
its shader on its first draw: milliseconds on a GPU, but over 20 s under the
software renderer GitHub's macOS runner falls back to. This used to start a
fresh `gttest --out` for each of its ~140 renders and ran the CI step out of
its 25 minutes, under three quarters of the way down the list. Now
`gttest --batch` draws every render in a batch with one compiled program, and
`--jobs` is the number of batches -- each one a compile. `GTTEST_SOFTWARE=1`
reproduces the runner's renderer on a Mac with a GPU.

**A sweep that cannot report a dead control proves nothing.** So every run
carries three controls of its own, and fails if any of them does:
  - the same job is rendered first and last in every batch and must come out
    identical -- otherwise something carries over from job to job and every
    control would look alive;
  - a control the pattern does not read (Pitch on a solid field) must read dead;
  - one setting is rendered in a batch and through `gttest --out`, the host's
    ProcessOpenGL path in a fresh process, and the two must be identical.

**A hang fails with a name.** Each batch has `--timeout` seconds; past that it
is killed and the job it was in is reported.

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
import threading
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = str(ROOT / "build" / "gttest")
SCRATCH = tempfile.mkdtemp(prefix="gtsweep")

WIDTH, HEIGHT = 640, 360
FRAMES = 1
# Seconds per gttest process. A batch at 160x90 takes about 7 s under
# GTTEST_SOFTWARE=1 on an M4 Max, nearly all of it the one compile, and the
# CI runner is two to three times slower; on a GPU it is under a second.
TIMEOUT = 300

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
    "Line Colour": GRID,
    "Line Green": GRID,
    "Line Blue": GRID,
    "Major Colour": GRID,
    "Major Green": GRID,
    "Major Blue": GRID,
    "Background": GRID,
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
    "Tile A Colour": LED,
    "Tile A Green": LED,
    "Tile A Blue": LED,
    "Tile B Colour": LED,
    "Tile B Green": LED,
    "Tile B Blue": LED,
    "Border Colour": LED,
    "Border Green": LED,
    "Border Blue": LED,
    # Element 10 is Custom, whose default colour is white -- the same field as
    # element 0. Sweep to red.
    "Field": dict(SOLID, **{"_high": 2}),
    "Level": SOLID,
    "Custom Colour": dict(SOLID, **{"Field": 10}),
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

# The sweep's own controls; see the docstring.
REPEAT = ({"Pattern": 0}, 1)
KNOWN_DEAD = ("Pitch", SOLID, 2, 1024)
REFERENCE = ({"Pattern": 0, "Marker": 1, "Frame Counter": 1, "Text": "GT01"}, 16)


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


def job(path, overrides, frames, label):
    sets = {k: v for k, v in overrides.items() if not k.startswith("_")}
    return {"path": path, "sets": sets, "frames": frames, "label": label}


def batch_line(j):
    """One line of `gttest --batch`: tab-separated, since names have spaces."""
    fields = [j["path"], f"{WIDTH}x{HEIGHT}", str(j["frames"])]
    fields += [f"{name}={value}" for name, value in j["sets"].items()]
    return "\t".join(fields) + "\n"


def run_batch(index, chunk, timeout):
    """Every job in `chunk` through one gttest process -- one shader compile.
    Returns None, or what went wrong."""
    listing = pathlib.Path(f"{SCRATCH}/batch{index}.tsv")
    listing.write_text("".join(batch_line(j) for j in chunk))
    by_path = {j["path"]: j for j in chunk}

    proc = subprocess.Popen([BIN, "--batch", str(listing)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    killed = threading.Event()

    def kill():
        killed.set()
        proc.kill()

    timer = threading.Timer(timeout, kill)
    timer.start()
    finished, output = 0, []
    try:
        for text in proc.stdout:
            output.append(text)
            word, _, rest = text.rstrip("\n").partition(" ")
            if word == "gl" and index == 0:
                print(f"  renderer: {rest}", file=sys.stderr, flush=True)
            if word != "done":
                continue
            took, _, path = rest.partition(" ")
            j = by_path.get(path, {})
            if finished == 0:
                # The first draw carries the shader compile.
                print(f"  batch {index}: first frame {took} s", file=sys.stderr, flush=True)
            elif "swept" in j:
                # Progress as it happens, on stderr, so a run that is cut off
                # still says how far it got and how long each one took.
                pid, name = j["swept"]
                print(f"  swept {pid:3d} {name}  ({took} s)", file=sys.stderr, flush=True)
            finished += 1
        proc.wait()
    finally:
        timer.cancel()

    if killed.is_set():
        where = chunk[finished]["label"] if finished < len(chunk) else "after the last job"
        return (f"batch {index} HUNG: killed after {timeout} s in '{where}' "
                f"({finished} of {len(chunk)} jobs done)")
    if proc.returncode != 0:
        return f"batch {index} failed (exit {proc.returncode}):\n" + "".join(output[-20:])
    return None


def run_reference(timeout):
    """REFERENCE through `gttest --out`: ProcessOpenGL, in a fresh process."""
    sets, frames = REFERENCE
    args = [BIN, "--out", f"{SCRATCH}/reference_out.png", "--size", f"{WIDTH}x{HEIGHT}",
            "--frames", str(frames)]
    for name, value in sets.items():
        args += ["--set", f"{name}={value}"]
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return f"the reference render HUNG: gttest --out killed after {timeout} s"
    if r.returncode != 0:
        return "the reference render failed: " + " ".join(args) + "\n" + r.stdout + r.stderr
    return None


def compare(a, b):
    return difference(pathlib.Path(a).read_bytes(), pathlib.Path(b).read_bytes())


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


def plan(key, name, low, high, context):
    """The low and high renders of one control, as batch jobs."""
    frames = context.get("_frames", FRAMES)
    # The marker moves with time; a frame count of 16 puts it a quarter of the
    # way across at 60 fps, where the sweep can see it.
    if "_time" in context:
        frames = int(context["_time"] * 60) + 1

    lo = dict(context)
    hi = dict(context)
    lo[name] = context.get("_low", low)
    hi[name] = context.get("_high", high)
    return (job(f"{SCRATCH}/{key}_lo.png", lo, frames, f"{name} low"),
            job(f"{SCRATCH}/{key}_hi.png", hi, frames, f"{name} high"))


def main():
    global WIDTH, HEIGHT

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="%dx%d" % (WIDTH, HEIGHT))
    ap.add_argument("--jobs", type=int, default=0,
                    help="batches, one gttest process and one shader compile each")
    ap.add_argument("--timeout", type=float, default=TIMEOUT,
                    help="seconds a batch may take before it is killed as hung")
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

    # Dealt round-robin, so the sixteen-frame marker controls do not all land
    # in one batch.
    chunks = [[] for _ in range(max(1, min(jobs, len(work))))]
    swept = []
    for k, (pid, name, low, high, context) in enumerate(work):
        lo, hi = plan(pid, name, low, high, context)
        hi["swept"] = (pid, name)
        chunks[k % len(chunks)] += [lo, hi]
        swept.append((pid, name, lo["path"], hi["path"]))

    dead_name, dead_context, dead_low, dead_high = KNOWN_DEAD
    known_dead = plan("known_dead", dead_name, dead_low, dead_high, dead_context)
    reference = job(f"{SCRATCH}/reference_batch.png", REFERENCE[0], REFERENCE[1], "the reference")
    chunks[0] += [*known_dead, reference]

    repeats = []
    for i, chunk in enumerate(chunks):
        first = job(f"{SCRATCH}/repeat{i}_first.png", REPEAT[0], REPEAT[1], "the repeat, first")
        last = job(f"{SCRATCH}/repeat{i}_last.png", REPEAT[0], REPEAT[1], "the repeat, last")
        chunk.insert(0, first)
        chunk.append(last)
        repeats.append((first["path"], last["path"]))

    start = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(chunks) + 1) as pool:
        futures = [pool.submit(run_batch, i, c, args.timeout) for i, c in enumerate(chunks)]
        futures.append(pool.submit(run_reference, args.timeout))
        errors = [e for e in (f.result() for f in futures) if e]
    elapsed = time.monotonic() - start
    if errors:
        for e in errors:
            print(e)
        return 1

    dead = []
    for pid, name, lo, hi in sorted(swept):
        fraction, count = compare(lo, hi)
        if count == 0:
            dead.append(name)
            print(f"DEAD  {pid:4d}  {name}")
        else:
            print(f"ok    {pid:4d}  {name}  ({count} subpixels, {fraction * 100:.2f}%)")

    print()
    broken = []

    def control(count, what, why):
        ok = count == 0
        line = f"{what}: {count} subpixels" + ("" if ok else f" -- {why}")
        print(f"{'ok  ' if ok else 'FAIL'}  control  {line}")
        if not ok:
            broken.append(line)

    for i, (first, last) in enumerate(repeats):
        control(compare(first, last)[1], f"batch {i}, the same job first and last",
                "something carries over between jobs, so every control would look alive")
    control(compare(*(j["path"] for j in known_dead))[1],
            f"{dead_name} on a solid field, which does not read it",
            "the sweep cannot see a dead control")
    control(compare(reference["path"], f"{SCRATCH}/reference_out.png")[1],
            "a batch render against the same through ProcessOpenGL (--out)",
            "the batch no longer draws what a host would")

    print()
    for name, why in skipped:
        print(f"skip  {name}: {why}")

    print(f"\n{len(swept)} swept, {len(dead)} dead, {len(skipped)} skipped, "
          f"{len(chunks)} batches, {elapsed:.1f} s")
    if dead:
        print("\nDEAD CONTROLS: " + ", ".join(dead))
    if broken:
        print("\nSWEEP CONTROLS FAILED: " + "; ".join(broken))
    return 1 if dead or broken else 0


if __name__ == "__main__":
    sys.exit(main())
