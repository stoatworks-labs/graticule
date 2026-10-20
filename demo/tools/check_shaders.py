"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

The page at graticule-demo.stoatworks-labs.com carries two things that are
copies of this repository, and copies drift quietly -- a grid whose last line
is a pixel off, or a checker whose phase has flipped, still looks like a test
pattern, and a test pattern that is quietly wrong is worse than none.

1. **The GLSL.** `demo/shaders.js` holds the two shaders of
   `source/Shaders.cpp`, kVertexShader and kFragmentShader, the fragment
   shader as the three adjacent raw strings it is written as (MSVC caps one
   literal at about 16 KB), AND the order the plugin joins them in: C++
   concatenates adjacent literals, and Render.cpp's InitGL compiles one
   program, `mProgram.Compile( kVertexShader, kFragmentShader )`. This script
   reads that call and the pieces out of the C++ and writes them into
   shaders.js as `ASSEMBLY`. The page builds its expected text for each stage
   from that table, and at run time REQUIRES the text the compiled plugin
   hands to glShaderSource to equal it before the kit's port() is applied
   (demo/wasm/gl_shim.cpp). The pieces are compared exactly -- no whitespace
   normalisation, no comment stripping: a comment updated on one side only is
   drift worth catching. Then:

     - every `const char* const k... =` in Shaders.cpp must be a shader this
       script knows, and Shaders.h must declare exactly those, so a new one
       cannot be added to the plugin and silently left out of the page;
     - Render.cpp must compile exactly one program, from exactly the two
       symbols ASSEMBLY names, in that order;
     - and shaders.js must be byte for byte what `--write` produces, so
       nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js escapes it as \\` (one comment in the fragment
   shader quotes `cell` in backticks). Any other backslash, or a `${`, on the
   JS side is rejected; the C++ bodies hold neither.

2. **The WebAssembly.** `demo/graticule-core.wasm` is the plugin's C++
   compiled by `demo/tools/build-wasm.sh`, and committed, because the deploy
   has no build step. Building it needs emscripten, which verify.sh cannot
   assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the seven plugin .cpp files, the SDK files, the glue, the shim
   and the build script) and of both outputs, and the SDK commit. A source
   file changed since the last build -- or an output that is not the one the
   build wrote -- fails here, and the fix is to rerun build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js, the page around the
plugin: its panel is built from the plugin's own declarations at run time, but
its read-out units, its tooltips, its raster handling and its layout are the
page's.
"""
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# C++ symbol (source/Shaders.cpp) -> the JS name its pieces are exported as,
# in the order they are declared there. A shader written as several adjacent
# raw strings becomes NAME_1, NAME_2, ...; one written as one stays NAME.
SHADERS = [
    ("kVertexShader", "VERTEX_SHADER"),
    ("kFragmentShader", "FRAGMENT_SHADER"),
]
JS_BASE = dict(SHADERS)

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/graticule-core.js", "demo/graticule-core.wasm")

# `const char* const kX =` followed by one or more raw strings, with the C++
# comments that may sit between them, up to the terminating semicolon.
DECLARATION = re.compile(
    r'const char\* const (k\w+) =((?:\s*(?://[^\n]*\n\s*)*R"\((?:.*?)\)")+)\s*;', re.S
)
PIECE = re.compile(r'R"\((.*?)\)"', re.S)


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def declared(cpp):
    """Every `const char* const k... =` in Shaders.cpp, in order."""
    return re.findall(r"const char\* const (k\w+)\s*=", cpp)


def pieces_of(cpp):
    """symbol -> its raw-string pieces, in source order."""
    out = {}
    for match in DECLARATION.finditer(cpp):
        out[match.group(1)] = PIECE.findall(match.group(2))
    return out


def js_names(symbol, count):
    base = JS_BASE[symbol]
    return [base] if count == 1 else [f"{base}_{i + 1}" for i in range(count)]


def unknown_symbols(cpp, shaders_h):
    """Shaders in Shaders.cpp or Shaders.h that this script does not copy."""
    found = declared(cpp) + re.findall(r"extern const char\* const (k\w+);", shaders_h)
    return sorted(set(s for s in found if s not in JS_BASE))


def assembly(cpp, render_cpp, shaders_h):
    """The plugin's own assembly, in JS names: the one Compile() call in
    Render.cpp, each stage the pieces of its symbol in source order."""
    pieces = pieces_of(cpp)
    for symbol, _ in SHADERS:
        if symbol not in pieces:
            raise Stale(f"{symbol} is not a run of raw strings in source/Shaders.cpp")
        if not re.search(r"extern const char\* const " + symbol + r";", shaders_h):
            raise Stale(f"source/Shaders.h does not declare {symbol}")
    undeclared = [s for s in declared(cpp) if s not in pieces]
    if undeclared:
        raise Stale(f"{', '.join(undeclared)} in source/Shaders.cpp could not be read as raw strings")

    calls = re.findall(r"\.Compile\(\s*([^)]*?)\s*\)", render_cpp)
    if len(calls) != 1:
        raise Stale(f"source/Render.cpp compiles {len(calls)} programs, not one")
    stages = [a.strip() for a in calls[0].split(",")]
    if len(stages) != 2:
        raise Stale(f"Render.cpp's Compile() takes {len(stages)} stages ({calls[0]}), not a vertex and a fragment")
    table = []
    for stage, symbol in zip(("vertex", "fragment"), stages):
        if symbol not in JS_BASE:
            raise Stale(f"Render.cpp compiles {symbol} as the {stage} stage, a shader this script does not copy")
        table.append((stage, js_names(symbol, len(pieces[symbol]))))
    return table


def render(cpp, render_cpp, shaders_h):
    """shaders.js as --write produces it."""
    pieces = pieces_of(cpp)
    out = [HEADER]
    for symbol, _ in SHADERS:
        if symbol not in pieces:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        names = js_names(symbol, len(pieces[symbol]))
        for i, (name, body) in enumerate(zip(names, pieces[symbol])):
            if "\\" in body or "${" in body:
                raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            where = "" if len(names) == 1 else f", raw string {i + 1} of {len(names)}"
            out.append(f"\n// {symbol}, source/Shaders.cpp{where}\n"
                       f"export const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// Render.cpp's InitGL compiles one program, Compile( vertex, fragment ),\n"
        "// and C++ joins a shader's adjacent raw strings in the order they are written.\n"
        "export const ASSEMBLY = {\n"
    )
    for stage, names in assembly(cpp, render_cpp, shaders_h):
        out.append(f"  {stage}: [{', '.join(repr(n) for n in names)}],\n")
    out.append("};\n")
    return "".join(out)


def from_js(source, name):
    match = re.search(r"^export const " + name + r" = `(.*?)`;$", source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, render_cpp, shaders_h, js):
    problems = 0

    for symbol in unknown_symbols(cpp, shaders_h):
        print(f"FAIL  {symbol} is a shader of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    pieces = pieces_of(cpp)
    for symbol, _ in SHADERS:
        if symbol not in pieces:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
            continue
        for name, cpp_text in zip(js_names(symbol, len(pieces[symbol])), pieces[symbol]):
            js_text, complaint = from_js(js, name)
            if complaint is not None:
                print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
                problems += 1
            elif js_text is None:
                print(f"FAIL  {name} not found in demo/shaders.js")
                problems += 1
            elif cpp_text == js_text:
                print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars)")
            else:
                problems += 1
                print(f"FAIL  {name} has drifted from {symbol}")
                where = first_difference(cpp_text, js_text)
                if where:
                    print(f"        first difference at line {where[0]}")
                    print(f"          C++: {where[1]}")
                    print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, render_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for stage, names in table:
        want = ", ".join(repr(n) for n in names)
        if rows.get(stage) == want:
            print(f"ok    {'ASSEMBLY.' + stage:<20} {' + '.join(names)}")
        else:
            print(f"FAIL  ASSEMBLY.{stage} is [{rows.get(stage, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for stage in sorted(set(rows) - set(s for s, _ in table)):
        print(f"FAIL  ASSEMBLY.{stage} is no stage of the plugin's")
        problems += 1

    # Anything else typed into the file.
    try:
        expected = render(cpp, render_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    pinned = False
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            pinned = True
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/graticule-core.wasm was built")
            problems += 1

    if not pinned:
        print(f"FAIL  {MANIFEST} does not record the FFGL SDK's commit")
        problems += 1

    # A source file added to source/ since the build is an input the build
    # never saw: every header, and every .cpp but the registration.
    for name in sorted(os.listdir(os.path.join(REPO, "source"))):
        path = f"source/{name}"
        if name == "SourcePlugin.cpp" or path in recorded:
            continue
        if name.endswith(".h") or name.endswith(".cpp"):
            print(f"FAIL  {path} is newer than the .wasm's manifest -- rebuild")
            problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<20} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    render_cpp = read("source", "Render.cpp")
    shaders_h = read("source", "Shaders.h")
    if "--write" in argv:
        unknown = unknown_symbols(cpp, shaders_h)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: shader(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, render_cpp, shaders_h)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, render_cpp, shaders_h, js) + check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    count = sum(len(pieces) for pieces in pieces_of(cpp).values())
    stages = len(assembly(cpp, render_cpp, shaders_h))
    print(f"all {count} shader pieces and the order the plugin's {stages} stages join them in are the plugin's,"
          " and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
