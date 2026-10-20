/**
 * Graticule — browser demo.
 *
 * Test patterns and LED cabinet grids drawn live at the output's own raster
 * (SW Graticule, GT01, a source). The one idea, from AGENTS.md: the shader is
 * the pattern. Every line is a whole number of pixels and nothing is
 * anti-aliased, so a soft edge on the wall means a scaler in the chain, never
 * the pattern. That makes this the one demo in the fleet where the page around
 * the plugin can ruin it: a canvas the browser stretches by a fraction of a
 * pixel shows exactly the soft edges the plugin exists to expose. So this page
 * draws at the screen's own device pixels and says, under the picture,
 * whether it managed to.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `graticule-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Graticule.cpp` — the plugin class, with
 * its constructor and declarations, the preset override (Effective), the
 * conversion of every control into the shader's units (BuildFrame: integer
 * pixels, y = 0 at the top, the bars' layout, the marker, the burn-in's lines
 * and plate), its clock and seconds-or-milliseconds vote — and everything it
 * calls: Render (the program, the font texture, every uniform, the draw),
 * Shaders, Bars (RP 219 on the CPU, up to 48 rectangles), Font (the 5×7
 * glyphs), Clock and Diag, with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping and FFGLShader. Its GL calls go to this page's WebGL2 context
 * through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin, reads its
 *     parameter declarations back through the SDK's host getters — the panel
 *     is built from them, not from a list typed here — and forwards
 *     SetFloatParameter, SetTextParameter (Text), Resize when the raster
 *     changes (FF_RESIZE, as Resolume sends it) and, every frame, SetTime and
 *     ProcessOpenGL with no input, in gttest's order.
 *   - **The raster.** See "Pixels" below.
 *   - **One GL entry point** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte this page's assembly of its copy
 *     (shaders.js, held to source/Shaders.cpp and Render.cpp's Compile() by
 *     demo/tools/check_shaders.py), then applies the kit's `port()` — the
 *     version line and the ES precision defaults, nothing else.
 *   - **The read-outs, the tooltips and the preset note.** The number beside
 *     Level and Speed is Controls.h's conversion, compiled in (gt_convert);
 *     the units and every tooltip are this page's words. What a preset is
 *     drawing with is read from the plugin's own Effective().
 *   - **The About block's links.** The plugin opens them with system(); the
 *     page opens the same addresses itself, read from the plugin's own table.
 *
 * ---------------------------------------------------------------------------
 * Pixels
 * ---------------------------------------------------------------------------
 *
 * The kit sizes its canvas to the column (`width: 100%`), which is a CSS
 * scale, which is a scaler. For every other demo in the fleet that is
 * harmless; here it would draw a soft 1px grid and a grey pixel check and
 * blame the plugin. So:
 *
 *   - **This screen, 1:1** (the default when no `?size=` is given) makes the
 *     composition the picture's own box in DEVICE pixels, measured with a
 *     ResizeObserver on `device-pixel-content-box` — the size the browser will
 *     actually paint the canvas at, after its own snapping — and re-measured
 *     whenever the window, the zoom or the screen changes. The canvas's
 *     raster then is the device raster, by construction. In embed mode the
 *     box is the viewport, so an OBS browser source gets its own raster.
 *   - **A fixed composition** (the kit's 640 × 360 … 1920 × 1080, or
 *     `?size=`) is shown at its raster divided by devicePixelRatio in CSS
 *     pixels, so one canvas pixel is one device pixel, and the stage scrolls
 *     rather than shrinks it. The line under the picture reports the device
 *     box the browser actually gave it, and says so when that is not the
 *     raster (a fractional devicePixelRatio can snap it a pixel out).
 *   - **Scaled to fit** is there on purpose, to show what a scaler does to
 *     the pixel check and the alignment chart's patches. The line under the
 *     picture says that is what is happening.
 *
 * Browsers without devicePixelContentBoxSize (Safari) get an estimate from
 * the CSS box × devicePixelRatio, and the page says it is an estimate.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createGraticule from './graticule-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_RED = 2;
const FF_TYPE_GREEN = 3;
const FF_TYPE_BLUE = 4;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled stage by stage in the order
// the plugin joins the pieces (shaders.js's ASSEMBLY, read out of Render.cpp's
// Compile() and Shaders.cpp's adjacent raw strings by check_shaders.py).
//===========================================================================

const ASSEMBLED = Object.fromEntries(
  Object.entries(S.ASSEMBLY).map(([stage, pieces]) => [stage, pieces.map((piece) => S[piece]).join('')]),
);

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: [], refused: null };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but this page's own assembly for that stage is
 * refused (an empty source, so the plugin's InitGL fails and logs it) rather
 * than compiled.
 */
function shaderSource(stage, text) {
  if (ASSEMBLED[stage] !== text) {
    shaderCheck.refused = { stage, length: text.length };
    return '';
  }
  shaderCheck.matched.push(stage);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const REPO = 'https://github.com/stoatworks-labs/graticule';

let core;
try {
  core = await createGraticule({
    graticuleShaderSource: shaderSource,
    print: (line) => console.log(line),
    printErr: (line) => console.warn(line),
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at ${REPO}.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

/** A C string for one call: allocated, handed over, freed. */
function withCString(value, fn) {
  const pointer = core.stringToNewUTF8(value);
  try {
    return fn(pointer);
  } finally {
    core._free(pointer);
  }
}

// Diag.cpp opens its log in InitGL, so its directory must exist in the page's
// memory before the first instance is initialised.
core._gt_prepare_log();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._gt_new();
  const list = [];
  const n = core._gt_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._gt_param_type(h, i);
    const d = { index: i, name: text(core._gt_param_name(h, i)), type, group: text(core._gt_param_group(h, i)) };
    if (type === FF_TYPE_TEXT) d.default = text(core._gt_param_default_text(h, i));
    else d.default = core._gt_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._gt_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._gt_param_element_name(h, i, e)));
        d.values.push(core._gt_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_INTEGER) {
      d.min = core._gt_param_range_min(h, i);
      d.max = core._gt_param_range_max(h, i);
    }
    list.push(d);
  }
  const inputs = [core._gt_min_inputs(h), core._gt_max_inputs(h)];
  core._gt_delete(h);
  return { list, inputs };
}

const DECLARED = declarations();
const BY_INDEX = new Map(DECLARED.list.map((d) => [d.index, d]));

// The About block: its first index, and the address each button opens, from
// the plugin's own table (StoatworksAboutLinks.h, compiled in). A label that
// does not match the declaration is a page bug, not something to paper over.
const ABOUT_FIRST = core._gt_about_first();
const ABOUT_LINKS = new Map();
for (const d of DECLARED.list) {
  if (d.index <= ABOUT_FIRST || d.type !== FF_TYPE_EVENT) continue;
  const button = d.index - ABOUT_FIRST - 1;
  const label = text(core._gt_about_button_label(button));
  if (label !== d.name) throw new Error(`About button ${button} is "${label}" in the plugin's table and "${d.name}" in its declaration`);
  ABOUT_LINKS.set(d.index, text(core._gt_about_button_url(button)));
}
const linkNamed = (label) => [...ABOUT_LINKS.entries()].find(([i]) => BY_INDEX.get(i).name === label)?.[1];

const indexOf = (name) => {
  const d = DECLARED.list.find((x) => x.name === name);
  if (!d) throw new Error(`the plugin declares no "${name}"`);
  return d.index;
};
const PRESET = indexOf('Preset');

//===========================================================================
// The read-outs: Controls.h's number, this page's unit.
//===========================================================================

const UNITS = {
  Level: (x) => `${(100 * x).toFixed(0)}%`,
  Speed: (x) => `${x.toFixed(x < 1 ? 3 : 2)} /s`,
};

function convert(d, v) {
  const x = core._gt_convert(d.index, Math.fround(v));
  if (Number.isNaN(x)) return v.toFixed(2);
  return (UNITS[d.name] ?? ((y) => y.toFixed(3)))(x);
}

/** Tooltips: this page's words, from the README's, AGENTS.md's and the source's account of each control. */
const HINTS = {
  Preset: 'Thirteen whole jobs in one dropdown. An OVERRIDE, as in Resolume: while it is on anything but Custom, the plugin draws with the preset’s values for the columns it sets and the panel keeps showing yours. This page dims those controls and lists what the plugin is actually drawing with; Resolume’s inspector would not.',
  Pattern: 'What to draw. Every line is a whole number of pixels and nothing is anti-aliased.',
  Levels: 'Full 0–255 (media server, GPU, HDMI full) or legal 16–235 (an SDI chain). At full range the −2% pluge patch clips to black and cannot be judged: that is what full range means.',
  'Grid By': 'Pitch: a line every Pitch pixels. Divisions: Divisions X × Y cells, with the last line exactly on the far edge whatever the raster.',
  Pitch: 'Grid pitch in pixels, 2–1024 (FF_TYPE_INTEGER).',
  'Divisions X': 'Cells across, with Grid By on Divisions.',
  'Divisions Y': 'Cells down, with Grid By on Divisions.',
  'Line Width': 'Minor line width, in whole pixels.',
  'Major Every': 'Every Nth line is heavy; 0 for none.',
  'Major Width': 'Heavy line width, in whole pixels.',
  Diagonals: 'Corner-to-corner lines on the grid.',
  'Centre Target': 'A centre cross and four rings on the grid.',
  'Origin Centre': 'Count the pitch from the centre of the raster rather than its top-left corner.',
  'Cell Labels': 'Column,row in every cell; skipped where the cells are too small to read.',
  'Line Colour': 'The grid’s minor lines. Grid, tile and alignment colours are the operator’s RGB and do not go through Levels.',
  'Major Colour': 'The grid’s heavy lines, target and labels, and the alignment chart’s foreground.',
  Background: 'Behind the grid and the alignment chart.',
  'Tile Width': 'One cabinet, in pixels. A 500 mm cabinet at P2.6 is 192.',
  'Tile Height': 'One cabinet, in pixels.',
  'Module Width': 'Module lines inside each cabinet, every this many pixels; 0 for none.',
  'Module Height': 'Module lines inside each cabinet, every this many pixels; 0 for none.',
  'Origin X': 'Where the wall’s top-left cabinet sits in the raster.',
  'Origin Y': 'Where the wall’s top-left cabinet sits in the raster.',
  Checker: 'Alternate Tile A and Tile B.',
  'Tile Labels': 'A lettered column and a numbered row in every cabinet: A1, B1 … Z1, AA1.',
  'Border Width': 'Cabinet borders, in whole pixels; 0 for none.',
  'Tile A Colour': 'Every other cabinet.',
  'Tile B Colour': 'The cabinets between, with Checker on.',
  'Border Colour': 'Cabinet borders and labels; module lines are half of it over the cabinet.',
  Field: 'The flat colour. Custom uses Custom Colour.',
  Level: 'The field’s level as a fraction of full scale (the number beside it). Fields go through Levels.',
  'Custom Colour': 'The field, with Field on Custom.',
  Steps: 'Stepped wedge; 0 is a continuous ramp. Each step is a flat patch of one exact code value.',
  Vertical: 'Ramp top to bottom instead of left to right.',
  'Per Channel': 'Split into neutral, red, green and blue strips.',
  Cell: 'Checker cell size in pixels. 1 is single-pixel alternation, which goes flat grey under any scaler.',
  'Line Bursts': 'Single-pixel horizontal and vertical line bursts beside the checker.',
  'Edge Border': 'A 1px line on the outermost pixels, drawn last: proves nothing is cropped or overscanned.',
  'Corner Markers': 'L-brackets in the corners, with the bracket’s length printed in pixels.',
  'Corner Size': 'The bracket arm’s length in pixels.',
  'Centre Marks': 'A centre cross and rings on the alignment chart.',
  'Safe Area A %': 'An inset rectangle at this percentage of the raster, labelled with the area it encloses; 0 for none.',
  'Safe Area B %': 'A second inset rectangle; 0 for none.',
  'Pixel Patches': 'Four single-pixel checker patches. Under any scaler they go flat grey.',
  'Diagonal Cross': 'Corner-to-corner lines on the alignment chart.',
  Marker: 'A bar sweeping across or down, or a bouncing box, inverting whatever is under it so it shows on every pattern.',
  Speed: 'Raster crossings per second (the number beside it): 0.5 is exactly one a second, with an eighth at one end and eight at the other.',
  'Marker Size': 'The marker’s thickness, in pixels.',
  'Frame Counter': 'Frame number and seconds in the burn-in. On this page they count this browser’s frames and the page’s clock, not a Resolume composition’s.',
  'Burn-in': 'The text plate: resolution, pattern name and your own text.',
  'Show Resolution': 'The raster the plugin is drawing at.',
  'Show Pattern': 'The pattern’s name.',
  Text: 'Your own text (FF_TYPE_TEXT). A line holds 40 characters; printable ASCII only, anything else is drawn as ?.',
  Position: 'Where the burn-in sits. Top left by default, which is why the alignment chart labels its corners bottom-left.',
  'Text Size': 'Glyph scale, in whole pixels per font pixel; 0 picks one from the raster.',
  Plate: 'A darkened plate behind the text.',
  About: 'The plugin’s own About line (FF_TYPE_TEXT, display only).',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_INTEGER]: 'text',
  [FF_TYPE_TEXT]: 'text',
  [FF_TYPE_RED]: 'colour',
  [FF_TYPE_GREEN]: 'colour',
  [FF_TYPE_BLUE]: 'colour',
};

/** The kit reads `embed`, `size`, `clip` and `bg` from the query string itself. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_INTEGER: return String(Math.round(d.default));
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

const PARAMS = DECLARED.list.map((d) => {
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (p.type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = (v) => convert(d, v);
  if (ABOUT_LINKS.has(d.index)) p.hint = `Opens ${ABOUT_LINKS.get(d.index)} — the address the plugin’s own button opens.`;
  return p;
});
const PARAM_BY_INDEX = new Map(PARAMS.map((p) => [p.ff.index, p]));
{
  const ids = new Set();
  for (const p of PARAMS) {
    if (ids.has(p.id)) throw new Error(`two parameters address as "${p.id}"`);
    ids.add(p.id);
  }
}

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v) {
  const d = p.ff;
  if (d.index === ABOUT_FIRST) return null;// display only
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_INTEGER: {
      const n = Number.parseInt(String(v).trim(), 10);
      if (!Number.isFinite(n)) return null;
      return Math.min(d.max, Math.max(d.min, n));
    }
    case FF_TYPE_TEXT: return String(v);
    default: return Math.fround(v);
  }
}

/** A value the plugin holds, said the way its control shows it. */
function spoken(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return d.elements[d.values.findIndex((x) => x === v)] ?? String(v);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 'On' : 'Off';
    case FF_TYPE_INTEGER: return String(Math.round(v));
    case FF_TYPE_STANDARD: return convert(d, v);
    default: return String(v);
  }
}

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let raster = null;
  let pushed = new Map();
  let frames = 0;
  let overridesDirty = true;
  let overrides = [];
  const grabs = [];

  let failure = null;

  function start(width, height) {
    instance = core._gt_new();
    pushed = new Map();
    frames = 0;
    if (!core._gt_init_gl(instance, width, height)) {
      core._gt_delete(instance);
      instance = 0;
      const why = shaderCheck.refused
        ? `the ${shaderCheck.refused.stage} shader text the compiled plugin handed to GL (${shaderCheck.refused.length} characters) is not this page’s checked copy of source/Shaders.cpp, so it was not compiled`
        : `its InitGL failed — the pattern shader did not compile or link in this browser’s WebGL2, which offers ${gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS)} fragment uniform vectors (WebGL2 promises only 224, and the plugin’s bar, text and line arrays take most of that). Its log line under the picture says what the plugin saw`;
      // Kept, and thrown again by every later frame (a resize asks for one),
      // so the page goes on saying why rather than something less true.
      failure = new GLError(`SW Graticule could not start: ${why}.`);
      throw failure;
    }
    raster = { width, height };
  }

  function push(params) {
    let changed = false;
    for (const p of PARAMS) {
      const value = hostValue(p, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      changed = true;
      if (typeof value === 'string') withCString(value, (s) => core._gt_set_text(instance, p.ff.index, s));
      else core._gt_set_float(instance, p.ff.index, value);
    }
    if (changed) overridesDirty = true;
  }

  /** Which controls the current preset overrides, through the plugin's Effective(). */
  function readOverrides() {
    overridesDirty = false;
    const preset = core._gt_get_float(instance, PRESET);
    overrides = [];
    if (Math.round(preset) <= 0) return;
    for (const d of DECLARED.list) {
      if (!core._gt_overridden(instance, d.index)) continue;
      const own = core._gt_get_float(instance, d.index);
      const effective = core._gt_effective(instance, d.index);
      overrides.push({ d, own, effective, differs: own !== effective });
    }
  }

  return {
    render({ params, width, height, time }) {
      if (failure) throw failure;
      core.GL.makeContextCurrent(handle);
      if (!instance) start(width, height);
      else if (raster.width !== width || raster.height !== height) {
        core._gt_resize(instance, width, height);
        raster = { width, height };
      }
      push(params);
      if (overridesDirty) {
        readOverrides();
        window.dispatchEvent(new CustomEvent('graticule:overrides'));
      }
      if (!core._gt_process(instance, time)) throw new GLError('The plugin’s ProcessOpenGL failed.');
      frames += 1;
      // For anything checking the page from outside: frames this instance drew.
      gl.canvas.dataset.frames = String(frames);
      // A read-back the same frame it is drawn (the drawing buffer is not
      // preserved), for a comparison with gttest. Reads; changes nothing.
      while (grabs.length) {
        const resolve = grabs.shift();
        const pixels = new Uint8Array(width * height * 4);
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        resolve({ width, height, pixels });
      }
    },
    /** An About button: the press and the release at once (the plugin acts on the press). */
    click(index) {
      if (!instance) return;
      core._gt_set_float(instance, index, 1);
      core._gt_set_float(instance, index, 0);
    },
    /** The next frame's pixels, bottom row first, as GL reads them. */
    grab() {
      return new Promise((resolve) => grabs.push(resolve));
    },
    get overrides() { return overrides; },
    get raster() { return raster; },
    log() {
      try {
        return core.FS.readFile(text(core._gt_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

let renderer = null;

const mounted = mountDemo({
  name: 'Graticule',
  pluginId: 'GT01',
  kind: 'source',
  tagline:
    'Test patterns and LED cabinet grids for Resolume, drawn live at the output’s own raster: SMPTE RP 219 bars, plain bars, a pitch or division grid, an alignment chart, lettered LED cabinets, solid fields, greyscale and a single-pixel check, with a burn-in, a motion marker and a frame counter. Every line is a whole number of pixels and nothing is anti-aliased, so a soft edge on the wall means a scaler in the chain, never the pattern.',
  repo: REPO,
  page: linkNamed('Project page'),

  // The stock banner says the page runs "on generated clips". Graticule is a
  // source with no input: it draws the pattern from nothing.
  blurb:
    'It is the plugin’s own C++ — its parameter table and preset override, the conversion of every control into whole pixels, the RP 219 layout and the 5×7 font — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. It reads no video: the picture is the pattern. By default it draws at this screen’s own device pixels, one plugin pixel to one screen pixel, and the line under the picture says whether the browser kept it that way. The host is this page, and its clock is the browser’s.',

  // A source: nothing to feed it. The kit's Clip and "Use my own…" controls
  // are removed below rather than left present and inert.
  sources: [],

  params: PARAMS,
  showBackdrop: false,

  differences: [
    'This page runs the plugin rather than a port of it. graticule-core.wasm is source/Graticule.cpp — the plugin class: its constructor and declarations, the preset override, BuildFrame’s conversion of every control into the shader’s units (whole pixels, y = 0 at the top: the bars’ rectangles, the marker, the burn-in’s lines and plate), its clock and unit vote — and everything it calls (Render, Shaders, Bars, Font, Clock, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping and FFGLShader, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). Of source/ only SourcePlugin.cpp, the registration and build stamp, is left out, and with it the SDK’s plugMain (FFGL.cpp) and CFFGLPluginInfo: the page constructs the plugin itself, as gttest does. Nothing is ported to JavaScript. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside Level and Speed are Controls.h’s conversions, compiled in; their units and every tooltip are this page’s.',
    'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp — the vertex shader and the fragment shader’s three raw strings, in the order Render.cpp compiles them — and the repository’s verify script fails if a character differs or if the WebAssembly was built from sources that have since changed. At start-up the page also requires each text the compiled plugin hands to glShaderSource to equal its own assembly of that copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults (highp float and int). GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and one entry point is the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port.',
    'Pixels are the point of this plugin, so the page does not let the browser scale the canvas unless asked to. “This screen, 1:1”, the default, makes the composition the picture’s own box in device pixels, measured by a ResizeObserver on the device-pixel content box (the size the browser will paint the canvas at, after its own snapping) and re-measured on every resize, zoom or move to another screen; the raster is the device raster by construction, and the burn-in prints it. A fixed composition (the kit’s sizes, or ?size=) is drawn at that raster and shown at raster ÷ devicePixelRatio CSS pixels, so one canvas pixel lands on one device pixel and the stage scrolls rather than shrinking it; the line under the picture reports the device box the browser actually gave it and says so if that is not the raster, which a fractional devicePixelRatio can do. “Scaled to fit” is there on purpose, to show what a scaler does to the pixel check. Safari does not report device-pixel boxes, so there the size is estimated from the CSS box × devicePixelRatio and the page says it is an estimate. Device pixels are what the browser hands the operating system: a desktop set to a scaled resolution (macOS “More Space” and its kin) resamples them again on the way to the panel, and nothing in a web page can see that. That is the case Graticule is for, on the real output.',
    'The clock is the browser’s: SetTime is handed seconds summed from the display’s frames (a frame capped at 0.1 s), and the plugin’s own vote settles on seconds within four frames, running on its wall clock (here performance.now()) until it does — the page does not force the unit, as gttest does, because a host cannot. The marker moves against that clock, and the Frame Counter counts the plugin’s ProcessOpenGL calls at this browser’s refresh rate, a redraw while paused included; neither is a Resolume composition’s timing, so the latency measurement the counter is for needs the real output. Pause is no time passing, Step advances 1/60 s, and Restart sets the clock back to 0, which the counter’s seconds follow and its frame number, the plugin’s own count, does not.',
    'Graticule is a source with no input (SetMinInputs and SetMaxInputs are both 0), so the page offers no clip and no “Use my own…”; ProcessOpenGL is called with no input textures. The output is opaque and goes straight into this page’s 8-bit canvas, antialiasing off, as the plugin draws straight into the host’s framebuffer. A change of composition is sent as FF_RESIZE, as Resolume sends it, and the plugin redraws at the new raster.',
    `The kit has no integer, event or colour-triple control of its own, so: the integers (Pitch, Tile Width and the rest, FF_TYPE_INTEGER) are number fields clamped to their declared ranges; each RED/GREEN/BLUE run is one swatch named after its red component, as Resolume draws it, and a colour picked there is 8-bit, so it reaches the plugin in steps of 1/255 (the plugin’s own defaults, such as the grid line’s 0.24, 0.49, 1.0, are sent as declared until you touch the swatch); Text is a text field sent on every keystroke, where Resolume sends it on commit; and the About block (a text line and ${ABOUT_LINKS.size} buttons, FF_TYPE_EVENT) is on the panel because Resolume shows it. Pressing a button sends the press to the plugin, as Resolume would, and the plugin’s handler opens the link by asking the operating system (std::system), which a browser does not have — so nothing happens there, and the page opens the same address itself, read from the plugin’s own About table (StoatworksAboutLinks.h, compiled in).`,
    'Compared once with the plugin’s own harness, gttest, on the development machine (an M4 Max, headless Chrome 155, 2026-10-20): twenty settings drawn by this page at fixed compositions and read back — RP 219 at both ranges, both plain bars, the grid by pitch and by divisions with labels, the alignment chart, two LED presets (one at 3840 × 2160, one with an offset origin), solid fields, the ramp and the wedge, the pixel check, a typed burn-in and the marker after a run of Steps — were identical to gttest’s, pixel for pixel, on ANGLE over Metal. On Chrome’s software renderer two were one code value out where a level lands exactly half-way between two (the wedge’s 0.3 step, the burn-in plate), which is rounding GL leaves to the driver. A screenshot of the page matched the canvas exactly at device pixel ratios of 1, 1.25, 1.5 and 2. Your GPU and your browser are not that machine’s; gttest’s own pixel checks, not this page, are the reason to believe the pattern.',
    'Preset is the plugin’s own control and behaves as it does in Resolume: an override, not a write. While it is on anything but Custom the plugin draws with the preset’s values for the columns it sets, and the panel keeps your values, as Resolume’s inspector does. Unlike Resolume, this page dims the controls the preset is overriding and says, under the Preset control, what the plugin is drawing with instead — read from the plugin’s own Effective(), probed by setting a control to two values and seeing whether Effective() follows, then putting it back.',
  ],

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The raster, the controls the kit has no type for, the About block, the
// preset note, and the plugin's log.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const { params, state } = mounted;
  const canvas = document.querySelector('.stage__gl');
  const wrap = canvas?.parentElement;

  //-------------------------------------------------------------------------
  // Pixels: the composition's raster against the screen's.
  //-------------------------------------------------------------------------

  /** The kit takes `?size=` when it is a preset id or WxH within 8K. Mirrors demo.js's pickResolution. */
  function kitTakesSize() {
    const wanted = query.get('size');
    if (!wanted) return false;
    if (['640', '960', '1280', '1920'].includes(wanted)) return true;
    const match = /^(\d{2,5})x(\d{2,5})$/.exec(wanted);
    return Boolean(match) && Number(match[1]) > 0 && Number(match[2]) > 0 && Number(match[1]) <= 7680 && Number(match[2]) <= 4320;
  }

  const display = {
    composition: kitTakesSize() ? 'fixed' : 'screen',
    mode: 'exact',// or 'fit'
    dpr: window.devicePixelRatio || 1,
    device: null,// [w, h]: the device-pixel box the browser gives the canvas
    measured: false,// true when that came from devicePixelContentBoxSize
    reported: null,// [w, h]: the last devicePixelContentBoxSize, trusted or not
    css: null,// [w, h]: its CSS box
  };
  const MAX = [7680, 4320];

  function layout() {
    display.dpr = window.devicePixelRatio || 1;
    if (!canvas || embed) return;// embed: kit.css fills the viewport, and the screen raster IS the viewport
    const s = canvas.style;
    if (display.composition === 'screen' || display.mode === 'fit') {
      s.width = '100%';
      s.maxWidth = '100%';
      s.height = 'auto';
      s.aspectRatio = display.composition === 'screen' ? '16 / 9' : '';
      wrap.style.overflow = 'hidden';
    } else {
      const { width, height } = state.resolution;
      s.width = `${width / display.dpr}px`;
      s.height = `${height / display.dpr}px`;
      s.maxWidth = 'none';
      s.aspectRatio = '';
      wrap.style.overflow = 'auto';
    }
  }

  function adoptScreenRaster() {
    if (display.composition !== 'screen' || !display.device) return;
    const width = Math.max(16, Math.min(MAX[0], display.device[0]));
    const height = Math.max(16, Math.min(MAX[1], display.device[1]));
    const r = state.resolution;
    if (r && r.id === 'screen' && r.width === width && r.height === height) return;
    state.resolution = { id: 'screen', name: `${width} × ${height}`, width, height };
    mounted.redraw();
  }

  if (canvas && 'ResizeObserver' in window) {
    const observer = new ResizeObserver((entries) => {
      const entry = entries[entries.length - 1];
      const box = entry.contentBoxSize?.[0];
      const dev = entry.devicePixelContentBoxSize?.[0];
      display.css = box ? [box.inlineSize, box.blockSize] : [entry.contentRect.width, entry.contentRect.height];
      const dpr = window.devicePixelRatio || 1;
      const estimate = [Math.round(display.css[0] * dpr), Math.round(display.css[1] * dpr)];
      // The browser's own device-pixel box, unless it disagrees with the CSS
      // box × devicePixelRatio by more than snapping can explain. Chrome's
      // DevTools device emulation is the case met: it reports CSS pixels there
      // while painting at the emulated ratio.
      display.reported = dev ? [dev.inlineSize, dev.blockSize] : null;
      if (dev && Math.abs(dev.inlineSize - estimate[0]) <= 2 && Math.abs(dev.blockSize - estimate[1]) <= 2) {
        display.device = [dev.inlineSize, dev.blockSize];
        display.measured = true;
      } else {
        display.device = estimate;
        display.measured = false;
      }
      if ((window.devicePixelRatio || 1) !== display.dpr) layout();
      adoptScreenRaster();
      report();
    });
    try {
      observer.observe(canvas, { box: 'device-pixel-content-box' });
    } catch {
      observer.observe(canvas);
    }
  }

  // A zoom or a move to another screen changes devicePixelRatio; the CSS size
  // of a fixed composition depends on it. The observer normally reports the
  // new device box itself; where the last report no longer fits the new ratio
  // (DevTools' device emulation does not re-report), estimate until it does.
  const watchRatio = () => {
    const media = window.matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
    media.addEventListener('change', () => {
      layout();
      const dpr = window.devicePixelRatio || 1;
      if (canvas && display.css && display.device) {
        const estimate = [Math.round(display.css[0] * dpr), Math.round(display.css[1] * dpr)];
        const fits = (box) => box && Math.abs(box[0] - estimate[0]) <= 2 && Math.abs(box[1] - estimate[1]) <= 2;
        if (fits(display.reported)) {
          // The browser's last report already fits: it did not change, so it
          // was not re-sent.
          display.device = display.reported;
          display.measured = true;
          adoptScreenRaster();
        } else if (!fits(display.device)) {
          display.device = estimate;
          display.measured = false;
          adoptScreenRaster();
        }
      }
      report();
      watchRatio();
    }, { once: true });
  };
  watchRatio();

  // Square corners. kit.css rounds the stage's frame and clips the canvas to
  // it, which hides a few pixels in each corner of the raster, and the corners
  // are part of this plugin's pattern: the alignment chart's edge border and
  // brackets live there.
  if (wrap && !embed) wrap.style.borderRadius = '0';
  layout();
  if (display.composition === 'screen' && canvas) {
    // A first estimate from the CSS box, so the first frame is drawn at about
    // the right raster rather than the kit's 960 × 540. The observer's
    // measurement replaces it before the second.
    const box = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    if (box.width > 0 && box.height > 0) {
      display.device = [Math.round(box.width * dpr), Math.round(box.height * dpr)];
      adoptScreenRaster();
      display.device = null;
    }
  }

  // For anything checking the page from outside (a headless browser reading
  // the plugin's own accessors): the kit's params and state, the renderer,
  // the shader check and what the page measured of the screen.
  window.graticuleDemo = { params, state, redraw: mounted.redraw, shaderCheck, display, get renderer() { return renderer; } };

  let status = null;

  function report() {
    if (!status) return;
    const r = state.resolution;
    const dev = display.device;
    const dpr = +display.dpr.toFixed(3);
    if (!r || !dev) { status.textContent = ''; return; }
    const one = dev[0] === r.width && dev[1] === r.height;
    const measured = display.measured
      ? 'the browser reports'
      : 'this browser does not report its device pixels (or reports them inconsistently), so this is an estimate from the CSS size: about';
    const css = display.css ? `${+display.css[0].toFixed(2)} × ${+display.css[1].toFixed(2)} CSS pixels` : 'its CSS box';
    let say;
    if (display.composition === 'screen') {
      say = `Composition ${r.width} × ${r.height}: this screen’s own raster. The picture is ${css} at a device pixel ratio of ${dpr}, and ${measured} ${dev[0]} × ${dev[1]} device pixels`;
      say += one
        ? ', so the plugin draws one pixel for every device pixel and nothing between it and the screen is scaling.'
        : ` — over the 8K this page caps a composition at, so it is drawn at ${r.width} × ${r.height} and scaled.`;
    } else if (display.mode === 'exact') {
      say = `Composition ${r.width} × ${r.height}, shown one to one: ${+(r.width / dpr).toFixed(2)} × ${+(r.height / dpr).toFixed(2)} CSS pixels at a device pixel ratio of ${dpr}, and ${measured} ${dev[0]} × ${dev[1]} device pixels`;
      say += one
        ? '. Nothing is scaling it.'
        : `, so the browser IS scaling it: soft edges and a grey pixel check here are this page, not the plugin. “This screen, 1:1” avoids it.`;
      if (one && wrap && (wrap.scrollWidth > wrap.clientWidth + 1 || wrap.scrollHeight > wrap.clientHeight + 1)) {
        say += ' It is bigger than this column, so it scrolls rather than shrinks.';
      }
    } else {
      const k = dev[0] / r.width;
      say = `Composition ${r.width} × ${r.height}, scaled to fit: ${measured} ${dev[0]} × ${dev[1]} device pixels, ×${k.toFixed(3)}. `;
      say += one
        ? 'That happens to be one to one at this size.'
        : 'Soft edges, uneven lines and a grey pixel check are that scaling — exactly what Graticule exists to catch on a real output — not the plugin.';
    }
    status.textContent = say;
    status.dataset.oneToOne = String(one);
  }

  if (!embed) {
    const button = (label, title) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.textContent = label;
      if (title) b.title = title;
      return b;
    };
    const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
      ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

    for (const p of PARAMS) {
      const row = rowOf(p);
      if (!row) continue;

      // FF_TYPE_EVENT: only the About buttons. Each sends its press and opens
      // the address the plugin's own press would have opened.
      if (p.ff.type === FF_TYPE_EVENT) {
        const toggle = row.querySelector('.prow__toggle');
        const url = ABOUT_LINKS.get(p.ff.index);
        if (!url) throw new Error(`the plugin declares an event, "${p.name}", that is not an About button`);
        const press = document.createElement('a');
        press.href = url;
        press.target = '_blank';
        press.rel = 'noopener';
        press.textContent = 'Open';
        press.title = p.hint;
        press.className = 'prow__toggle';
        press.dataset.press = p.id;
        press.style.textAlign = 'center';
        press.style.textDecoration = 'none';
        // The press goes to the plugin as Resolume sends it, press and release
        // at once (the plugin acts on the press). Its handler asks the
        // operating system to open the link, which does nothing in a browser;
        // the link itself opens the same address.
        press.addEventListener('click', () => renderer?.click(p.ff.index));
        toggle?.replaceWith(press);
        continue;
      }

      const field = row.querySelector('.prow__text');
      if (!field) continue;

      // The About line: the plugin's, display only.
      if (p.ff.index === ABOUT_FIRST) {
        field.readOnly = true;
        field.tabIndex = -1;
        continue;
      }

      // FF_TYPE_INTEGER: a number field in the declared range.
      if (p.ff.type === FF_TYPE_INTEGER) {
        field.type = 'number';
        field.min = String(p.ff.min);
        field.max = String(p.ff.max);
        field.step = '1';
        field.inputMode = 'numeric';
      }
      if (p.ff.type === FF_TYPE_TEXT) {
        field.maxLength = 120;
        field.placeholder = 'burn-in text';
      }
    }

    //-----------------------------------------------------------------------
    // The transport: a source has no clip and takes no file.
    //-----------------------------------------------------------------------
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') field.remove();
    }
    document.querySelector('.transport__file')?.remove();

    // The kit's Composition dropdown, with "This screen, 1:1" first. A change
    // to it is caught before the kit's own handler, which knows only its four
    // sizes; any other choice goes on to the kit as before.
    const compositionSelect = [...document.querySelectorAll('.transport__field')]
      .find((f) => f.querySelector('.transport__label')?.textContent === 'Composition')
      ?.querySelector('select');
    if (compositionSelect) {
      const screen = new Option('This screen, 1:1', 'screen');
      screen.title = 'The picture’s own box, in this screen’s device pixels: one plugin pixel to one screen pixel.';
      compositionSelect.prepend(screen);
      if (display.composition === 'fixed' && ![...compositionSelect.options].some((o) => o.value === state.resolution.id)) {
        compositionSelect.append(new Option(state.resolution.name, state.resolution.id));
      }
      compositionSelect.value = display.composition === 'screen' ? 'screen' : state.resolution.id;
      compositionSelect.addEventListener('change', (event) => {
        if (compositionSelect.value === 'screen') {
          event.stopImmediatePropagation();
          display.composition = 'screen';
          layout();
          adoptScreenRaster();
          mounted.redraw();
        } else if (!['640', '960', '1280', '1920'].includes(compositionSelect.value)) {
          // The ?size= entry, which the kit's handler would not find.
          event.stopImmediatePropagation();
          display.composition = 'fixed';
          layout();
          mounted.redraw();
        } else {
          display.composition = 'fixed';
          // After the kit's handler has set state.resolution.
          queueMicrotask(() => { layout(); report(); });
        }
        report();
      }, { capture: true });
    }

    // How the raster reaches the screen.
    const bar = document.querySelector('.transport');
    if (bar) {
      const label = document.createElement('label');
      label.className = 'transport__field';
      const span = document.createElement('span');
      span.className = 'transport__label';
      span.textContent = 'Pixels';
      const select = document.createElement('select');
      select.className = 'transport__select';
      select.id = 'graticule-pixels';
      select.append(new Option('1:1', 'exact'), new Option('Scaled to fit', 'fit'));
      select.value = display.mode;
      label.title = '1:1 shows a fixed composition one canvas pixel to one device pixel, scrolling if it has to. Scaled to fit lets the browser scale it into the column — soft on purpose, to show what a scaler does. “This screen, 1:1” is one to one either way.';
      select.addEventListener('change', () => {
        display.mode = select.value;
        layout();
        report();
      });
      label.append(span, select);
      bar.append(label);
    }

    //-----------------------------------------------------------------------
    // Under the picture: the raster against the screen, and the plugin's own
    // log (Diag.cpp). Both report; neither measures the pattern.
    //-----------------------------------------------------------------------
    const stage = document.querySelector('.stage');
    if (stage) {
      status = document.createElement('p');
      status.className = 'stage__status';
      status.id = 'graticule-raster';
      status.setAttribute('aria-live', 'polite');
      const log = document.createElement('p');
      log.className = 'stage__status';
      log.id = 'graticule-log';
      log.setAttribute('aria-live', 'off');
      log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
      log.style.fontSize = '12px';
      log.style.whiteSpace = 'pre-wrap';
      stage.append(status, log);
      report();
      let lastLog = '';
      setInterval(() => {
        const all = renderer?.log() ?? '';
        if (all === lastLog) return;
        lastLog = all;
        const lines = all.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) graticule: /, '$1 '));
        log.textContent = lines.length
          ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}`
          : '';
      }, 250);
    }

    //-----------------------------------------------------------------------
    // The preset override: what the plugin is drawing with instead.
    //-----------------------------------------------------------------------
    const presetRow = rowOf(PARAM_BY_INDEX.get(PRESET));
    const note = document.createElement('p');
    note.className = 'inspector__note';
    note.id = 'graticule-preset-note';
    note.style.margin = '0';
    note.style.padding = '2px 14px 6px';
    presetRow?.after(note);
    const dimmed = new Set();
    window.addEventListener('graticule:overrides', () => {
      for (const row of dimmed) {
        row.style.opacity = '';
        row.removeAttribute('data-overridden');
      }
      dimmed.clear();
      const list = renderer?.overrides ?? [];
      if (!list.length) {
        note.textContent = '';
        note.style.display = 'none';
        return;
      }
      const d = BY_INDEX.get(PRESET);
      const presetName = d.elements[Math.round(params.get(PARAM_BY_INDEX.get(PRESET).id))];
      for (const o of list) {
        const row = rowOf(PARAM_BY_INDEX.get(o.d.index));
        if (!row) continue;
        row.style.opacity = '0.5';
        row.dataset.overridden = spoken(o.d, o.effective);
        dimmed.add(row);
      }
      const differ = list.filter((o) => o.differs).map((o) => `${o.d.name} ${spoken(o.d, o.effective)}`);
      note.style.display = '';
      note.textContent = `“${presetName}” overrides the ${list.length} dimmed controls (an override, as in Resolume, whose inspector would go on showing your values).`
        + (differ.length ? ` The plugin is drawing with ${differ.join(', ')}.` : ' Each already holds the preset’s value.');
    });
  }
}
