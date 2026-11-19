# DEMIURGE — tools

Zero-dependency Node tools for seeing and testing the game in a real browser. Everything here was
measured on the build machine (Windows 10, Edge 157, NVIDIA RTX 3060, Node 24).

| file | what it is |
|------|------------|
| `tools/shot.js` | headless-browser rig: drives Microsoft Edge over the DevTools protocol. Module and CLI. |
| `tools/server.js` | static dev server for the project root + `POST /__save` |
| `tools/smoke/smoke.js`, `webgl.html` | WebGL2 capability smoke test → JSON report + `shots/smoke.png` |
| `tools/smoke/rigtest.js` (+ `rig.html`, `throws.html`) | self-test of the rig and the server (30 tests, ~45 s) |
| `tools/smoke/example.js`, `example-scenario.js` | the two examples below, runnable as they are |
| `shots/` | where screenshots, `/__save` uploads and page downloads (`shots/downloads/`) land |

Run everything from the project root. Quote the project path in shells: it contains a space.

```
node tools/shot.js index.html --out title.png            # one-shot: load, wait 1.5 s, screenshot, report errors
node tools/shot.js --run tools/smoke/example-scenario.js   # scripted session
node tools/smoke/smoke.js                                  # is WebGL2 healthy on this machine?
node tools/server.js                                       # http://127.0.0.1:5174/
```

## 1. `tools/shot.js` as a module

```js
// tools/smoke/example.js — run it with: node tools/smoke/example.js
const { launch, decodePng } = require('../shot.js');   // from tests/ or dev/ this is '../tools/shot.js'

(async () => {
  const browser = await launch({ width: 1280, height: 720 });
  try {
    const page = await browser.page();
    await page.goto('tools/smoke/webgl.html');                    // path relative to the project root (or absolute, or a URL)
    await page.waitFor('window.SMOKE && SMOKE.done', 15000);      // poll a JS expression until it is truthy
    const info = await page.eval('({ renderer: SMOKE.report.renderer, fps: SMOKE.report.raf.fps })');
    await page.eval('SMOKE.freeze(0.5)');                         // put the page into a known state …
    await page.wait(100);                                         // … and give it a few frames to draw it
    const file = await page.shot('example.png');                  // → <project>/shots/example.png
    const centre = decodePng(file).px(640, 360);                  // [r, g, b, a] of one pixel of the PNG
    console.log(JSON.stringify({ info, file, centre, errors: page.errors }, null, 2));
    if (page.errors.length) process.exitCode = 1;
  } finally {
    await browser.close();                                        // always: kills the browser, deletes its profile
  }
})().catch(e => { console.error(e); process.exitCode = 1; });
```

### launch

`launch(opts) → Promise<browser>`. Every launch is a private Edge instance with its own temporary
profile and its own debugging port, so any number can run at once (12 at a time is tested).

| option | default | meaning |
|--------|---------|---------|
| `width`, `height` | 1280, 720 | viewport in CSS pixels |
| `dpr` | 1 | device pixel ratio (`devicePixelRatio`; screenshots are `width·dpr × height·dpr`) |
| `headless` | true | `false` opens a visible Edge window (for watching; behaves the same) |
| `timeout` | 120000 | **whole-session deadline in ms.** When it passes the browser is killed and every pending and later call rejects with `session timeout`. Raise it for long scenarios; `0` = none. |
| `mobile`, `touch` | false | initial values for `page.setViewport` |
| `ignore` | `[]` | RegExp, string or array. Error messages matching one are filed in `page.logs` as `[ignored error] …` instead of `page.errors`. Also `browser.ignore.push(/re/)` at any time. |
| `network` | false | `true` lets the browser reach non-loopback hosts (see Limitations) |
| `args` | `[]` | extra browser command-line flags (a later flag overrides an earlier one) |
| `exe` | auto | browser executable; the `SHOT_BROWSER` environment variable does the same |

`browser.page()` → a page. The first call uses the browser's initial tab; each further call opens
another window (all of them keep animating). `browser.close()` kills the process tree and deletes
the profile; it is safe to call twice. Also: `browser.errors` / `browser.logs` (all pages
together), `browser.pages`, `browser.downloads`, `browser.version`, `browser.pid`.

### page

| call | notes |
|------|-------|
| `await page.goto(target, { timeout = 30000 })` | `target`: `http(s)://`, `file://`, `data:` or `about:` URL, or a plain path — absolute, or relative to the current directory or the project root; spaces are fine; `?query` and `#hash` may be appended (`'dev/play.html?bare=1'`). A directory means its `index.html`. Resolves with the final URL after the `load` event; rejects if the file is missing or the load fails. |
| `await page.eval(js)` | Runs an expression or statements in the page and returns the completion value **by value**. Promises are awaited; top-level `await` and a top-level `return` both work; `{ a: 1 }` is read as an object literal. Typed arrays come back as arrays, `Map` as `[[k, v], …]`, `Set` as an array, `Date` as an ISO string, a DOM node as `'<div#id.class>'`; functions are dropped, cycles become `'[Circular]'`. `const`/`let` may be redeclared from one eval to the next. A page-side exception rejects with an error named `PageError` whose message is the page's message and stack. Failed evals are *not* added to `page.errors`. |
| `await page.wait(ms)` | sleeps in Node; the page keeps running |
| `await page.waitFor(js, timeoutMs = 10000, intervalMs = 50)` | polls until truthy and resolves with that value. An expression that throws counts as "not yet". Rejects with a `TimeoutError` (message includes the last exception, if any). |
| `await page.shot(name, { clip })` | PNG of the viewport, including WebGL canvases. A relative name is written under `shots/` (sub-folders are created); returns the absolute path. A `.jpg` or `.webp` extension changes the format. `clip: { x, y, width, height }` in CSS pixels. |
| `await page.png({ clip })` | the same as a `Buffer`, nothing written |
| `await page.mouse(type, x, y, opts)` | `type`: `'move'`, `'down'`, `'up'`, `'click'`, `'wheel'`. Coordinates are CSS pixels in the viewport. `opts`: `button` (`'left'`, `'right'`, `'middle'`), `clickCount` (2 = double click), `deltaY` / `deltaX` for the wheel (default `deltaY` 100 = one notch of a real wheel, scrolling down; negative = up). `'click'` = move, down, up. The page receives real pointer, mouse, click, contextmenu and wheel events. |
| `await page.drag(x1, y1, x2, y2, { steps = 10, button = 'left', delay = 0 })` | press, `steps` interpolated moves with the button held (`delay` ms between them), release |
| `await page.key(type, code, opts)` | `type`: `'down'`, `'up'`, `'press'`. `code` is a DOM `KeyboardEvent.code` (`'KeyW'`, `'Digit1'`, `'Space'`, `'Escape'`, `'ArrowLeft'`, `'ShiftLeft'`, `'F2'`, `'NumpadAdd'` …) or a single character (`'+'`). `opts`: `shift`, `ctrl`, `alt`, `meta`, and `hold` (ms between down and up of a press). A modifier held with `'down'` applies to later key and mouse events until its `'up'`. A second `'down'` without an `'up'` is an auto-repeat. |
| `await page.type(text)` | inserts text into the focused element |
| `await page.touch(type, points)` | `type`: `'start'`, `'move'`, `'end'`, `'cancel'`; `points`: `[{ x, y, id }]`. `'start'` adds fingers, `'move'` moves the listed ones, `'end'` lifts the listed ones (all of them when the list is omitted). The page receives touch events and pointer events with `pointerType: 'touch'`. Works without touch emulation; see `setViewport` for feature detection. |
| `await page.setViewport({ width, height, dpr, mobile, touch })` | omitted fields keep their value; the page gets a `resize` event. `touch: true` changes `navigator.maxTouchPoints` (5) and the `(pointer: coarse)` / `(hover: none)` media queries at once, but `'ontouchstart' in window` only on the **next page load**: set it, then `goto`. Mouse input keeps working while touch is on. |
| `await page.fps(ms = 1000)` | counts `requestAnimationFrame` callbacks → `{ frames, ms, fps, meanMs, maxMs }` |
| `page.errors` | array of strings: uncaught exceptions and unhandled rejections (with stack), `console.error` / failed `console.assert` (with source location), resources that failed to load (a missing script or stylesheet), **WebGL errors** (`INVALID_OPERATION` …), a crashed page. |
| `page.logs` | array of strings: all other console output; levels other than `log` are prefixed (`[warning] …`); dialogs appear as `[dialog] …`. |
| `page.clearLogs()` | empties both arrays in place |
| `await page.close()` | closes this tab only |

Identical consecutive messages are stored once with a `[xN]` suffix, so a per-frame error does
not flood the arrays.

`decodePng(pathOrBuffer)` → `{ width, height, data /* RGBA bytes */, px(x, y) → [r, g, b, a], stats() }`
where `stats()` returns `{ mean: [r, g, b], min, max, lit /* fraction of pixels brighter than 16 */,
colors /* distinct colours, capped at 4096 */ }`. Use it to assert on screenshots ("not black",
"this pixel is ocean-blue") instead of only looking at them.

## 2. `tools/shot.js` as a CLI

```
node tools/shot.js <url-or-file> [--size 1280x720] [--dpr 1] [--wait 1500] [--wait-for "js"] [--eval "js"]
                   [--settle 0] [--out name.png] [--no-shot] [--allow-errors] [--ignore "regex"]
                   [--timeout 120000] [--headed] [--mobile] [--touch]
```

Order of operations: load the page → `--wait-for` (poll until truthy) → `--wait` ms (default
1500) → `--eval` → `--settle` ms → screenshot. It prints exactly one JSON object on stdout:

```json
{ "result": <value of --eval or null>, "errors": [...], "logs": [...], "shot": "<absolute path or null>" }
```

plus an `"error"` field if the run itself failed (file not found, `--eval` threw, timeout).
**Exit code 1** if the page logged errors (unless `--allow-errors`) or anything failed, else 0.

- Without `--out` the screenshot is `shots/<page-name>-<pid>.png` (unique per run, so parallel
  runs never overwrite each other — and so they pile up: pass `--out`, or `--no-shot`).
- `--ignore "regex"` moves matching page errors to `logs`. While the game is half-built the dev
  shell reports every missing script, so this keeps the exit code meaningful:
  `node tools/shot.js "dev/play.html?bare=1" --wait-for "window.DEV_READY" --ignore "ERR_FILE_NOT_FOUND"`
- Quoting `--eval`: put the JS in double quotes and use single quotes inside it
  (`--eval "document.querySelector('#ui').childElementCount"`). That works in PowerShell, cmd and
  bash. The other way round fails in Windows PowerShell, which strips inner double quotes. For
  anything longer than a line, write a scenario.

### Scenario mode

```
node tools/shot.js --run scenario.js [--size WxH] [--dpr 1] [--allow-errors] [--ignore "regex"] [--timeout ms] [--headed] [args…]
```

```js
// tools/smoke/example-scenario.js
module.exports = async (page, browser, args) => {
  await page.goto('tools/smoke/rig.html');
  await page.waitFor('window.READY');
  await page.mouse('click', 450, 350);                       // real input events, in CSS pixels
  await page.drag(200, 300, 600, 500, { steps: 8 });
  await page.key('press', 'KeyW');
  await page.mouse('wheel', 640, 360, { deltaY: -240 });
  await page.wait(50);
  const events = await page.eval('EV.map(e => e.t)');        // what the page saw
  const shot = await page.shot('example-scenario.png');
  return { clicks: events.filter(t => t === 'click').length, wheels: events.filter(t => t === 'wheel').length,
    keys: events.filter(t => t === 'keydown').length, shot, args };
};
```

The rig launches the browser, passes a blank page, prints the return value as JSON on stdout and
closes the browser whatever happens. Page errors are listed on stderr. Exit code 1 if the
scenario threw, its result is not JSON-serializable, or the page logged errors (unless
`--allow-errors`). Extra positional arguments arrive as `args`. Remember `--timeout` for
scenarios longer than two minutes.

## 3. What the browser is like

- **GPU.** WebGL2 runs on the real GPU, headless or not:
  `ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 (0x00002504) Direct3D11 vs_5_0 ps_5_0, D3D11)`,
  `MAX_TEXTURE_SIZE` 16384, `MAX_SAMPLES` 8, float and half-float colour buffers, linear float
  filtering. No special flag is needed for that; if a machine has no usable GPU the rig falls
  back to software rendering (SwiftShader), and `node tools/smoke/smoke.js` reports
  `"software": true`.
- **Frames.** `requestAnimationFrame` ticks at display rate: about 57 fps here (mean interval
  17.5 ms, with an occasional frame up to about 26 ms), in headless and headed mode alike, on
  every open page. The page is
  `visible` and focused; timers are not throttled.
- **Screenshots** show exactly what is composited, WebGL included, without
  `preserveDrawingBuffer`. One 1280×720 PNG takes about 0.1 s; an eval round trip about 0.3 ms;
  launching a browser about 0.5 s.
- **Fresh profile per launch.** `localStorage` works on `file://` pages and is shared by all
  pages of one launch, but every launch starts empty. To test save/load, do both in one session.
- **Offline.** Only `127.0.0.1` and `localhost` resolve; every other host fails with
  `ERR_NAME_NOT_RESOLVED` (which lands in `page.errors`). The game must not need the network, so
  this is deliberate. `launch({ network: true })` lifts it.
- **Audio** is rendered into a null device: nothing is audible, `AudioContext.currentTime`
  advances in real time, analysers see samples. The autoplay policy is the browser's real one:
  an `AudioContext` created before any input is `suspended` until the page resumes it after a
  click or key press (`page.mouse` / `page.key` count as real input). **`page.eval` itself counts
  as a user gesture**, so a context created inside an eval starts `running`.
- **Dialogs** (`alert`, `confirm`, `prompt`) are accepted automatically (`confirm` → `true`,
  `prompt` → `''`) and logged. **Downloads** started by the page go to `shots/downloads/`.
- **Keyboard shortcuts of the browser are live**: `F5` reloads the page unless the page calls
  `preventDefault()`, as for a real user.
- **Clean-up.** The browser is killed and its profile deleted on `close()`, on an uncaught error
  in your script, at the session timeout, on Ctrl+C, and — through a small watchdog process —
  even when your Node process is killed hard. If you ever suspect a leftover:
  `tasklist | findstr msedge` (the user's own Edge windows also show up there).

## 4. Limitations

- **Frame-rate numbers are capped by the display rate.** A frame that takes 2 ms and one that
  takes 15 ms both show ~57 fps. To measure cost, time the work itself: `performance.now()`
  around a frame that ends in `gl.finish()` or a 1-pixel `readPixels`, or use
  `EXT_disjoint_timer_query_webgl2` (available; it agreed with wall-clock timing to within 2%).
  `performance.now()` has 0.1 ms resolution.
- **GPU timings are real but noisy, and the GPU is shared.** Reference workload (`SMOKE.bench`:
  100,000 instanced lit triangles into a 1280×720 RGBA16F + depth target with 4× MSAA, blit
  resolve, 1-pixel readback): 1.1–2.1 ms per frame with the machine otherwise idle; 1 million
  triangles 4.7–11 ms; 4 million about 14 ms. With 12 rigs running at once the 100k case took
  1.7–9 ms. Treat a timing taken while other engineers are running rigs as an upper bound, and
  repeat it before drawing conclusions.
- A session lives at most `timeout` ms (default two minutes) — by design, so nothing is orphaned.
- `page.eval` grants user activation (see Audio). To test "audio stays silent until the first
  click", create the context from page code at load, not from an eval.
- The user agent contains `HeadlessChrome`. `devicePixelRatio`, viewport size and touch are
  emulated; the window is not a real phone. `screen.width/height` equal the viewport.
- Chromium reports only the first 256 WebGL errors of a context, then goes quiet.
- Windows only as tested. The code has Linux/macOS branches (set `SHOT_BROWSER` to a Chromium
  executable) but they have never been run.
- `decodePng` reads 8-bit non-interlaced PNGs (what the rig produces), not arbitrary PNG files.

## 5. Smoke test

```
node tools/smoke/smoke.js [--headed] [--size WxH]
```

Loads `tools/smoke/webgl.html`, prints a JSON capability report, saves `shots/smoke.png` (a lit
triangle at a pinned rotation, with the check list drawn over it), exits 0 if everything passed.
Takes about 4 s. The report:

- `webgl2`, `renderer` / `vendor` (unmasked, via `WEBGL_debug_renderer_info`), `version`, `glsl`,
  `software` (true if the renderer string names a software rasterizer)
- `limits`: `MAX_TEXTURE_SIZE`, `MAX_SAMPLES`, and 17 more
- `extensions`: `EXT_color_buffer_float`, `OES_texture_float_linear`,
  `EXT_color_buffer_half_float`, `WEBGL_debug_renderer_info` and others, each true/false;
  `missingExtensions` lists which of those four are absent; `supportedExtensions` is the full list
- `checks` (each `{ ok, … }` with the measured values):
  `msaaRgba16fResolve` (multisampled RGBA16F renderbuffer + DEPTH24, framebuffer complete, HDR
  values > 1 survive the blit-resolve into a texture, edge pixels are anti-aliased),
  `instancing` (`drawArraysInstanced` + `vertexAttribDivisor`, 8 instances read back),
  `uniformBlockStd140` (member offsets, draw, `bufferSubData` update),
  `depthTextureShadowSampler` (render into a `DEPTH_COMPONENT24` texture, sample it through
  `sampler2DShadow` with `COMPARE_REF_TO_TEXTURE`; also reports whether the comparison is
  bilinearly filtered), `litTriangle`
- `raf`: rAF rate measured by the page over one second; `rafFromRig`: the same through `page.fps`
- `bench`: the reference workload described under Limitations (`msPerFrame`)
- `screenshot`: proof that screenshots contain the canvas — a known colour `[255, 128, 0]` is
  drawn and read back from the PNG at the centre, a corner and as the mean; then the triangle is
  checked (centre lit, background dark, rotates when the angle changes, animates when spinning)
- `pageErrors`, `pageLogs`, `browser`, `viewport`, `elapsedMs`, and overall `ok`

`webgl.html` also opens by hand in any browser, and exposes `SMOKE.bench(instances, frames)`,
`SMOKE.freeze(angle|null)` and `SMOKE.solid([r, g, b]|null)` for experiments.

`node tools/smoke/rigtest.js [substring]` is the rig's own regression test (input events,
eval semantics, error capture, viewport, parallel launches, every clean-up path including a real
Ctrl+C, the server). Run it after changing `shot.js` or `server.js`. Last line:
`rigtest: N passed, M failed, … ms`.

## 6. Dev server

```
node tools/server.js [port]        # default 5174; PORT=5180 node tools/server.js also works (the argument wins)
```

- Serves the project root at `http://127.0.0.1:<port>/` (loopback only) with no-cache headers and
  correct MIME types; a directory serves its `index.html`, or a listing if it has none.
- `POST /__save?name=<file>` writes the request body to `shots/<file>`. Characters outside
  `[A-Za-z0-9._-]` in the name become `_`, so nothing can be written outside `shots/`. If the
  body is a `data:` URL (base64 or percent-encoded) it is decoded first, so a page can do
  `fetch('http://127.0.0.1:5174/__save?name=card.png', { method: 'POST', body: canvas.toDataURL() })`
  — from an `http://` page or from `file://` (CORS is open). The reply is
  `{ ok, file, path, bytes }`.
- A port already in use is reported clearly and the process exits with code 1. Stop the server
  with Ctrl+C.
- From Node: `const { start } = require('./tools/server.js'); const s = await start({ port: 0, quiet: true });`
  → `{ port, url, close() }` (`port: 0` picks a free port — use that in tests so parallel runs
  do not collide).

The game runs from `file://`, and so does the rig, so the server is optional: use it when you
want to open the game in your own browser with caching off, or to pull an image out of a page.
