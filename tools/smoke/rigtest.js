// DEMIURGE — tools/smoke/rigtest.js : self-test of the headless rig (tools/shot.js) and the dev server (tools/server.js).
//   node tools/smoke/rigtest.js            run everything (about 45 s)
//   node tools/smoke/rigtest.js <substr>   only tests whose name contains <substr>
// Prints one line per test and a summary "rigtest: N passed, M failed". Exit code 1 if anything failed.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');
const { launch, decodePng, SHOTS, ROOT } = require('../shot.js');
const server = require('../server.js');

const SHOT_JS = path.resolve(__dirname, '..', 'shot.js');
const RIG = path.join(__dirname, 'rig.html');
const THROWS = path.join(__dirname, 'throws.html');
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- child modes (this file re-run as a victim process) ----------------------------------------
if (process.argv[2] === '--child') {
  (async () => {
    const mode = process.argv[3];
    const infoFile = process.argv[4];
    if (infoFile) process.on('exit', code => { try { fs.appendFileSync(infoFile + '.log', 'exit ' + code + '\n'); } catch (e) { /* ignore */ } });
    const browser = await launch({ timeout: 60000 });
    const page = await browser.page();
    await page.goto(RIG);
    const info = JSON.stringify({ node: process.pid, pid: browser.pid, profile: browser.profile });
    if (infoFile) fs.writeFileSync(infoFile, info);
    console.log(info);
    if (mode === 'throw') throw new Error('deliberate crash of the node script');
    if (mode === 'hang') setInterval(() => {}, 1000);
    if (mode === 'forget') return; // never closes the browser: only the session timeout can end this process
  })();
  return;
}

// ---- tiny harness -----------------------------------------------------------------------------
const filter = process.argv[2] || '';
const results = [];
const tests = [];
const test = (name, fn) => tests.push({ name, fn });
function ok(cond, msg) { if (!cond) throw new Error(msg || 'assertion failed'); }
function eq(a, b, msg) {
  if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error((msg ? msg + ': ' : '') + 'expected ' + JSON.stringify(b) + ', got ' + JSON.stringify(a));
}
async function rejects(promise, re, msg) {
  let err = null;
  try { await promise; } catch (e) { err = e; }
  ok(err, (msg || 'promise') + ': expected a rejection');
  if (re) ok(re.test(String(err.message)), (msg || 'promise') + ': rejection "' + err.message + '" does not match ' + re);
  return err;
}
function pidAlive(pid) { try { process.kill(pid, 0); return true; } catch (e) { return e.code === 'EPERM'; } }
function edgeProcs(profileName) {
  const ps = "Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | Where-Object { $_.CommandLine -like '*" + profileName + "*' } | ForEach-Object { $_.ProcessId }";
  const r = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps], { encoding: 'utf8', windowsHide: true });
  return String(r.stdout).split(/\s+/).filter(Boolean).map(Number);
}
async function until(fn, ms) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { if (fn()) return true; await sleep(100); }
  return !!fn();
}
function runNode(args, opts) {
  return spawnSync(process.execPath, args, Object.assign({ encoding: 'utf8', cwd: ROOT, windowsHide: true, timeout: 90000 }, opts || {}));
}

let shared = null;
async function sharedPage() {
  if (!shared || shared.closed) shared = await launch({ timeout: 180000 });
  const page = shared.pages.find(p => !p.closed) || await shared.page();
  await page.setViewport({ width: 1280, height: 720, dpr: 1, mobile: false, touch: false });
  page.clearLogs();
  return page;
}
const events = async (page) => page.eval('EV.splice(0)');

// ---- tests ------------------------------------------------------------------------------------

test('launch: private profile, real port, version', async () => {
  const b = await launch();
  try {
    ok(b.pid > 0 && b.port > 0, 'pid and port');
    ok(/demiurge-shot-/.test(b.profile) && fs.existsSync(b.profile), 'temp profile exists');
    ok(b.profile.startsWith(os.tmpdir()), 'profile is under os.tmpdir()');
    ok(/Edg|Chrome/.test(b.version), 'version: ' + b.version);
    const page = await b.page();
    eq(await page.eval('[innerWidth, innerHeight, devicePixelRatio]'), [1280, 720, 1]);
    eq(await page.eval('document.visibilityState'), 'visible');
    eq(await page.eval('document.hasFocus()'), true);
  } finally { await b.close(); }
  ok(b.closed, 'closed flag');
  ok(!fs.existsSync(b.profile), 'profile deleted by close()');
  ok(!pidAlive(b.pid), 'browser process gone after close()');
  eq(edgeProcs(path.basename(b.profile)), [], 'no msedge.exe left for this profile');
  await rejects(b.page(), /closed/, 'page() after close');
});

test('goto: path with spaces, file URL, relative path, query+hash, data URL, missing file', async () => {
  const page = await sharedPage();
  ok(/ /.test(RIG), 'the project path contains a space (precondition of this test)');
  const u1 = await page.goto(RIG);
  ok(/^file:\/\/\/.*%20.*rig\.html$/.test(u1), 'file URL: ' + u1);
  eq(await page.eval('document.title'), 'rig test page');
  eq(await page.eval('READY'), true, 'scripts ran before goto resolved');
  await page.goto(u1);
  eq(await page.eval('location.href'), u1);
  await page.goto('tools/smoke/rig.html');
  eq(await page.eval('document.title'), 'rig test page', 'relative to project root / cwd');
  await page.goto(RIG + '?seed=42&x=a%20b#debug');
  eq(await page.eval('[location.search, location.hash]'), ['?seed=42&x=a%20b', '#debug']);
  await page.goto('data:text/html,<title>inline</title><body>hello');
  eq(await page.eval('document.title'), 'inline');
  await page.goto('about:blank');
  eq(await page.eval('location.href'), 'about:blank');
  await rejects(page.goto(path.join(__dirname, 'no-such-page.html')), /not found/, 'missing file');
  await rejects(page.goto('http://127.0.0.1:9/'), /ERR_/, 'refused connection');
});

test('eval: expressions, statements, promises, return, by-value results', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  eq(await page.eval('1 + 1'), 2);
  eq(await page.eval('const a = 5; a * 2'), 10);
  eq(await page.eval('const a = 6; a * 2'), 12, 'const can be redeclared between evals');
  eq(await page.eval('let n = 0; for (let i = 0; i < 5; i++) n += i; n'), 10);
  eq(await page.eval('await new Promise(r => setTimeout(() => r(42), 30))'), 42, 'top-level await');
  eq(await page.eval('new Promise(r => setTimeout(() => r(43), 30))'), 43, 'promise result is awaited');
  eq(await page.eval('(async () => { await 0; return { deep: [1, { x: "y" }] }; })()'), { deep: [1, { x: 'y' }] });
  eq(await page.eval('return 7'), 7, 'return statement');
  eq(await page.eval('const q = await Promise.resolve(3); return q * 3;'), 9, 'await + return');
  eq(await page.eval('{ a: 1, b: [1, 2] }'), { a: 1, b: [1, 2] }, 'bare object literal');
  eq(await page.eval('if (true) { 5 }'), 5, 'block statement still works');
  eq(await page.eval('undefined'), undefined);
  eq(await page.eval('var zz = 3;'), undefined);
  eq(await page.eval('null'), null);
  ok(Number.isNaN(await page.eval('NaN')), 'NaN');
  eq(await page.eval('1 / 0'), Infinity);
  eq(await page.eval('"str"'), 'str');
  eq(await page.eval('new Float32Array([1, 2, 3])'), [1, 2, 3], 'typed array → array');
  eq(await page.eval('({ m: new Map([["k", 1]]), s: new Set([1, 2]), d: new Date(0), u: undefined, f() {}, big: 10n })'),
    { m: [['k', 1]], s: [1, 2], d: '1970-01-01T00:00:00.000Z', big: '10n' });
  eq(await page.eval('document.getElementById("box")'), '<div#box>', 'DOM node → description');
  eq(await page.eval('const o = { name: "loop" }; o.self = o; o'), { name: 'loop', self: '[Circular]' });
  eq((await page.eval('new Uint8Array(100000)')).length, 100000, 'large result');
  eq(await page.eval('window.fromEval = 5; fromEval'), 5);
  eq(await page.eval('fromEval + 1'), 6, 'state persists between evals');
});

test('eval: page-side exceptions reject with message and stack', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  let e = await rejects(page.eval('function boom() { throw new Error("boom!"); } boom()'), /Error: boom!/);
  eq(e.name, 'PageError');
  ok(/at boom/.test(e.message), 'page stack in message: ' + e.message);
  ok(/rigtest\.js/.test(e.stack), 'node stack points at the caller: ' + e.stack);
  e = await rejects(page.eval('nope.foo'), /ReferenceError: nope is not defined/);
  e = await rejects(page.eval('Promise.reject(new Error("async boom"))'), /Error: async boom/);
  e = await rejects(page.eval('await new Promise((_, rej) => setTimeout(() => rej(new TypeError("late")), 20))'), /TypeError: late/);
  e = await rejects(page.eval('throw "a plain string"'), /a plain string/);
  e = await rejects(page.eval('this is not javascript'), /SyntaxError/);
  e = await rejects(page.eval('return (() => { throw new RangeError("in return form"); })()'), /RangeError: in return form/);
  await page.wait(150);
  eq(page.errors, [], 'eval failures are reported to the caller, not to page.errors');
  eq(await page.eval('"still alive"'), 'still alive');
});

test('waitFor: resolves when truthy, rejects on timeout', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.eval('setTimeout(() => { window.late = { v: 9 }; }, 300); 0');
  const t0 = Date.now();
  eq(await page.waitFor('window.late && late.v'), 9, 'returns the truthy value');
  ok(Date.now() - t0 >= 250, 'actually waited');
  const t1 = Date.now();
  const e = await rejects(page.waitFor('window.neverSet', 400), /was not truthy within 400 ms/);
  eq(e.name, 'TimeoutError');
  ok(Date.now() - t1 >= 380 && Date.now() - t1 < 3000, 'timeout honoured: ' + (Date.now() - t1));
  const e2 = await rejects(page.waitFor('missingGlobal.ready', 200), /last evaluation threw: ReferenceError/);
  ok(e2, 'throwing expression is treated as not-yet and explained on timeout');
});

test('errors: a page that throws fills page.errors with locations', async () => {
  const page = await sharedPage();
  await page.goto(THROWS);
  await page.wait(400);
  const errs = page.errors.join('\n----\n');
  const has = (re, what) => ok(re.test(errs), 'page.errors should contain ' + what + '. Got:\n' + errs);
  has(/console\.error output 42/, 'console.error output');
  has(/Uncaught Error: deliberate uncaught error[\s\S]*at inner \(file:\/\/\/.*throws\.html:\d+:\d+\)[\s\S]*at outer/, 'the uncaught exception with its stack');
  has(/Uncaught \(in promise\) Error: deliberate unhandled rejection/, 'the unhandled rejection');
  has(/does-not-exist\.js/, 'the failed script load');
  has(/WebGL: INVALID_OPERATION/, 'the WebGL error');
  has(/ReferenceError: undefinedFunctionCall is not defined[\s\S]*throws\.html:\d+/, 'the ReferenceError with file and line');
  const logs = page.logs.join('\n');
  ok(/before the error \{a: 1, list: Array\(3\)\}/.test(logs), 'console.log with object preview. Got:\n' + logs);
  ok(/\[warning\] a warning/.test(logs), 'console.warn in logs');
  ok(page.errors.every(e => typeof e === 'string'), 'errors are strings');
  eq(shared.errors.length >= page.errors.length, true, 'browser.errors aggregates');
  page.clearLogs();
  eq([page.errors.length, page.logs.length], [0, 0], 'clearLogs');
  await page.goto(RIG);
  for (let i = 0; i < 5; i++) await page.eval('console.error("same message"); 0');
  await page.wait(100);
  eq(page.errors.length, 1, 'identical consecutive messages collapse');
  ok(/same message[\s\S]*\[x5\]/.test(page.errors[0]), 'with a repeat count: ' + page.errors[0]);
});

test('ignore: matching errors are filed under logs and do not fail the CLI', async () => {
  const b = await launch({ ignore: [/does-not-exist\.js/, 'unhandled rejection'] });
  try {
    const page = await b.page();
    await page.goto(THROWS);
    await page.wait(400);
    const errs = page.errors.join('\n');
    ok(!/does-not-exist\.js/.test(errs) && !/unhandled rejection/.test(errs), 'ignored messages are not in page.errors: ' + errs);
    ok(/deliberate uncaught error/.test(errs) && /WebGL: INVALID_OPERATION/.test(errs), 'other errors are still there');
    const ignored = page.logs.filter(l => /^\[ignored error\] /.test(l));
    ok(ignored.some(l => /does-not-exist\.js/.test(l)) && ignored.some(l => /unhandled rejection/.test(l)), 'ignored messages are in page.logs: ' + page.logs.join(' | '));
    b.ignore.push(/late addition/);
    await page.eval('console.error("a late addition"); console.error("still an error"); 0');
    await page.wait(100);
    ok(!page.errors.some(e => /late addition/.test(e)) && page.errors.some(e => /still an error/.test(e)), 'browser.ignore can be extended while running');
  } finally { await b.close(); }
  let r = runNode([SHOT_JS, 'tools/smoke/throws.html', '--no-shot', '--wait', '300', '--ignore', 'deliberate|does-not-exist|WebGL: INVALID|undefinedFunctionCall|console\\.error output']);
  eq(r.status, 0, 'every error ignored → exit 0. stdout: ' + r.stdout);
  let j = JSON.parse(r.stdout);
  eq(j.errors, [], 'errors empty');
  ok(j.logs.filter(l => /^\[ignored error\]/.test(l)).length >= 5, 'ignored errors listed in logs');
  r = runNode([SHOT_JS, 'tools/smoke/throws.html', '--no-shot', '--wait', '300', '--ignore', 'does-not-exist']);
  eq(r.status, 1, 'remaining errors still fail the run');
});

test('shot: file lands in shots/, pixels are right', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  const file = await page.shot('rigtest-shot.png');
  eq(file, path.join(SHOTS, 'rigtest-shot.png'), 'relative name → <project>/shots/');
  ok(path.isAbsolute(file) && fs.existsSync(file), 'file exists');
  const img = decodePng(file);
  eq([img.width, img.height], [1280, 720]);
  eq(img.px(450, 350), [255, 128, 0, 255], 'orange box');
  eq(img.px(100, 600), [32, 64, 96, 255], 'background');
  const st = img.stats();
  ok(st.lit > 0.99 && st.max === 255, 'stats: ' + JSON.stringify(st));
  const abs = path.join(os.tmpdir(), 'rigtest-abs-' + process.pid + '.png');
  eq(await page.shot(abs), abs, 'absolute path is used as is');
  ok(fs.existsSync(abs));
  fs.unlinkSync(abs);
  const clip = decodePng(await page.png({ clip: { x: 400, y: 300, width: 100, height: 100 } }));
  eq([clip.width, clip.height, clip.px(50, 50)], [100, 100, [255, 128, 0, 255]], 'clip');
  const jpg = await page.shot('rigtest-shot.jpg');
  const head = fs.readFileSync(jpg).subarray(0, 3);
  eq([head[0], head[1], head[2]], [0xff, 0xd8, 0xff], '.jpg name → JPEG data');
  fs.unlinkSync(jpg);
  fs.unlinkSync(file);
});

test('setViewport: size, dpr, mobile, touch', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.setViewport({ width: 800, height: 600, dpr: 2 });
  eq(await page.eval('[innerWidth, innerHeight, devicePixelRatio]'), [800, 600, 2]);
  const img = decodePng(await page.png());
  eq([img.width, img.height], [1600, 1200], 'screenshot is in device pixels');
  eq(img.px(900, 700), [255, 128, 0, 255], 'box at 2x');
  await page.setViewport({ width: 390, height: 844, dpr: 3, mobile: true, touch: true });
  // touch support is decided when a document is created: navigate after switching it on
  await page.goto('data:text/html,<meta name="viewport" content="width=device-width, initial-scale=1"><body style="margin:0">phone');
  eq(await page.eval('[innerWidth, innerHeight, devicePixelRatio, navigator.maxTouchPoints > 0, "ontouchstart" in window, matchMedia("(pointer: coarse)").matches]'), [390, 844, 3, true, true, true]);
  const phone = decodePng(await page.png());
  eq([phone.width, phone.height], [1170, 2532], 'phone screenshot in device pixels');
  await page.setViewport({ width: 1280, height: 720, dpr: 1, mobile: false, touch: false });
  await page.goto(RIG);
  eq(await page.eval('[innerWidth, innerHeight, devicePixelRatio, navigator.maxTouchPoints, "ontouchstart" in window]'), [1280, 720, 1, 0, false]);
});

test('mouse: move, click, double click, right click, wheel', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.mouse('move', 300, 200);
  await page.mouse('click', 310, 210);
  let ev = await events(page);
  const types = ev.map(e => e.t).join(' ');
  ok(/pointermove mousemove.*pointerdown mousedown pointerup mouseup click/.test(types), 'event order: ' + types);
  const click = ev.find(e => e.t === 'click');
  eq([click.x, click.y, click.b, click.d], [310, 210, 0, 1]);
  eq(ev.find(e => e.t === 'pointerdown').pt, 'mouse');
  eq(ev.find(e => e.t === 'mousedown').bs, 1, 'buttons bitmask on press');
  await page.mouse('click', 320, 220, { clickCount: 2 });
  ev = await events(page);
  ok(ev.some(e => e.t === 'dblclick' && e.x === 320 && e.y === 220), 'dblclick: ' + ev.map(e => e.t).join(' '));
  eq(ev.filter(e => e.t === 'click').map(e => e.d), [1, 2], 'click detail 1 then 2');
  await page.mouse('click', 330, 230, { button: 'right' });
  ev = await events(page);
  ok(ev.some(e => e.t === 'contextmenu' && e.b === 2), 'contextmenu');
  ok(ev.some(e => e.t === 'mousedown' && e.b === 2 && e.bs === 2), 'right button press');
  await page.mouse('click', 340, 240, { button: 'middle' });
  ev = await events(page);
  ok(ev.some(e => e.t === 'mousedown' && e.b === 1 && e.bs === 4), 'middle button press');
  await page.mouse('wheel', 640, 360, { deltaY: 240 });
  await page.mouse('wheel', 640, 360, { deltaY: -120 });
  await page.wait(100);
  ev = await events(page);
  eq(ev.filter(e => e.t === 'wheel').map(e => [e.x, e.y, e.dy]), [[640, 360, 240], [640, 360, -120]]);
  await page.key('down', 'ShiftLeft');
  await page.mouse('click', 350, 250);
  await page.key('up', 'ShiftLeft');
  ev = await events(page);
  eq(ev.find(e => e.t === 'click').shift, true, 'held Shift reaches mouse events');
});

test('drag: press, moves with the button held, release', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.drag(200, 300, 600, 500, { steps: 8 });
  const ev = await events(page);
  const down = ev.find(e => e.t === 'pointerdown'), up = ev.find(e => e.t === 'pointerup');
  eq([down.x, down.y], [200, 300]);
  eq([up.x, up.y], [600, 500]);
  const held = ev.filter(e => e.t === 'pointermove' && e.bs === 1);
  eq(held.length, 8, 'one move per step with buttons=1');
  eq([held[3].x, held[3].y], [400, 400], 'linear interpolation');
  await page.drag(100, 100, 150, 100, { button: 'right', steps: 2 });
  const ev2 = await events(page);
  ok(ev2.filter(e => e.t === 'mousemove' && e.bs === 2).length === 2, 'right-button drag');
});

test('key: down, up, press, modifiers, typing into an input', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.key('press', 'KeyW');
  let ev = await events(page);
  eq(ev.map(e => [e.t, e.code, e.key, e.kc]), [['keydown', 'KeyW', 'w', 87], ['keypress', 'KeyW', 'w', 119], ['keyup', 'KeyW', 'w', 87]]);
  await page.key('press', 'Escape');
  ev = await events(page);
  eq(ev.map(e => [e.t, e.code, e.key, e.kc]), [['keydown', 'Escape', 'Escape', 27], ['keyup', 'Escape', 'Escape', 27]]);
  await page.key('down', 'KeyA'); await page.key('down', 'KeyA'); await page.key('up', 'KeyA');
  ev = (await events(page)).filter(e => e.t === 'keydown');
  eq(ev.map(e => e.rep), [false, true], 'second down while held is a repeat');
  await page.key('down', 'ShiftLeft'); await page.key('press', 'KeyQ'); await page.key('up', 'ShiftLeft');
  ev = await events(page);
  const q = ev.find(e => e.t === 'keydown' && e.code === 'KeyQ');
  eq([q.key, q.shift], ['Q', true], 'Shift held → uppercase key');
  eq(ev.find(e => e.code === 'ShiftLeft').loc, 1, 'left modifier location');
  await page.key('press', 'KeyS', { ctrl: true });
  ev = await events(page);
  eq(ev.filter(e => e.t === 'keydown').map(e => [e.code, e.ctrl]), [['KeyS', true]]);
  ok(!ev.some(e => e.t === 'keypress'), 'no keypress with Ctrl');
  for (const [code, key, kc] of [['ArrowLeft', 'ArrowLeft', 37], ['Space', ' ', 32], ['Digit1', '1', 49], ['F2', 'F2', 113], ['Enter', 'Enter', 13], ['Minus', '-', 189], ['NumpadAdd', '+', 107]]) {
    await page.key('press', code);
    const d = (await events(page)).find(e => e.t === 'keydown');
    eq([d.code, d.key, d.kc], [code, key, kc], code);
  }
  await page.key('press', '+');
  const plus = (await events(page)).find(e => e.t === 'keydown');
  eq([plus.code, plus.key, plus.shift], ['Equal', '+', true], 'single character → code + shift');
  await rejects(page.key('press', 'NotAKey'), /unknown key/);
  // F6 moves keyboard focus into browser UI; the rig must hand it back to the page for the next key
  await page.key('press', 'F6');
  await page.wait(150);
  await events(page);
  await page.key('press', 'ArrowUp');
  eq((await events(page)).filter(e => e.t === 'keydown').map(e => e.code), ['ArrowUp'], 'keys reach the page again after F6');
  await page.mouse('click', 100, 30);
  eq(await page.eval('document.activeElement.id'), 'name', 'click focuses the input');
  for (const k of ['KeyH', 'KeyI']) await page.key('press', k);
  await page.key('press', 'Digit7', { shift: true });
  eq(await page.eval('document.getElementById("name").value'), 'hi&');
  await page.type(' there, Ünïcode');
  eq(await page.eval('document.getElementById("name").value'), 'hi& there, Ünïcode');
  await page.key('press', 'Backspace');
  eq(await page.eval('document.getElementById("name").value.length'), 17);
});

test('touch: one finger, second finger, move, lift one, lift all', async () => {
  const page = await sharedPage();
  await page.setViewport({ touch: true });
  await page.goto(RIG);
  await page.touch('start', [{ x: 100, y: 100, id: 0 }]);
  await page.touch('start', [{ x: 300, y: 300, id: 1 }]);
  await page.touch('move', [{ x: 80, y: 80, id: 0 }, { x: 340, y: 340, id: 1 }]);
  await page.touch('end', [{ id: 0 }]);
  await page.touch('move', [{ x: 360, y: 360, id: 1 }]);
  await page.touch('end');
  await page.wait(100);
  const ev = await events(page);
  const t = ev.filter(e => /^touch/.test(e.t));
  const brief = t.map(e => e.t + ':' + e.n + ':' + e.ch.map(c => c[0]).join(','));
  eq(brief.filter(s => /^touchstart/.test(s)), ['touchstart:1:0', 'touchstart:2:1'], 'starts: ' + brief.join(' '));
  ok(t.some(e => e.t === 'touchmove' && e.n === 2 && e.ch.some(c => c[1] === 340 && c[2] === 340)), 'two-finger move: ' + JSON.stringify(t));
  eq(brief.filter(s => /^touchend/.test(s)), ['touchend:1:0', 'touchend:0:1'], 'ends: ' + brief.join(' '));
  ok(t.some(e => e.t === 'touchmove' && e.n === 1 && e.ch[0][1] === 360), 'one-finger move after lifting the other: ' + brief.join(' '));
  const order = t.map(e => e.t);
  ok(order.indexOf('touchend') < order.lastIndexOf('touchmove'), 'finger 0 is lifted before finger 1 moves on: ' + order.join(' '));
  const p = ev.filter(e => /^pointer/.test(e.t));
  ok(p.some(e => e.t === 'pointerdown' && e.pt === 'touch'), 'pointer events with pointerType touch');
  eq(p.filter(e => e.t === 'pointerdown').length, 2);
  eq(p.filter(e => e.t === 'pointerup').length, 2);
  await rejects(page.touch('move', [{ x: 1, y: 1, id: 7 }]), /not down/);
  await page.setViewport({ touch: false });
});

test('rAF ticks at display rate on every page; timers are not throttled', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.wait(300);
  const f1 = await page.fps(1000);
  ok(f1.fps > 45 && f1.fps < 75, 'first page fps: ' + JSON.stringify(f1));
  const second = await shared.page();
  await second.goto(RIG);
  await second.wait(300);
  const [a, b] = await Promise.all([page.fps(1000), second.fps(1000)]);
  ok(a.fps > 45 && b.fps > 45, 'both pages animate at once: ' + JSON.stringify([a, b]));
  const ticks = await page.eval('new Promise(res => { let n = 0; const t0 = performance.now(); const id = setInterval(() => { n++; if (performance.now() - t0 >= 500) { clearInterval(id); res(n); } }, 5); })');
  ok(ticks > 60, 'setInterval(5 ms) fired ' + ticks + ' times in 500 ms');
  await second.close();
  await rejects(second.eval('1'), /closed/);
  eq(await page.eval('2'), 2, 'first page survives closing the second');
});

test('audio: real autoplay policy, WebAudio clock runs (silently) after input', async () => {
  const page = await sharedPage();
  // a context created while the page loads, before any input, must be suspended exactly as in a user's browser
  await page.goto('data:text/html,<body style="margin:0;height:100vh"><script>window.ac = new AudioContext(); window.atLoad = ac.state;' +
    'addEventListener("pointerdown", () => ac.resume());</script>');
  await page.wait(200);
  eq(await page.eval('[atLoad, ac.state]'), ['suspended', 'suspended'], 'suspended until the user interacts');
  await page.mouse('click', 300, 300);
  await page.waitFor('ac.state === "running"', 3000);
  const t0 = await page.eval('ac.currentTime');
  await page.wait(500);
  const dt = (await page.eval('ac.currentTime')) - t0;
  ok(dt > 0.35 && dt < 0.8, 'audio clock advanced ' + dt.toFixed(3) + ' s in 500 ms');
  eq(await page.eval('const o = ac.createOscillator(), an = ac.createAnalyser(); o.connect(an); an.connect(ac.destination); o.start(); ' +
    'await new Promise(r => setTimeout(r, 200)); const b = new Float32Array(an.fftSize); an.getFloatTimeDomainData(b); b.some(v => Math.abs(v) > 0.5)'), true, 'the graph renders samples');
  eq(await page.eval('new AudioContext().state'), 'running', 'page.eval counts as a user gesture');
  eq(page.errors, []);
});

test('dialogs are auto-accepted and logged', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  eq(await page.eval('alert("hello dialog"); const c = confirm("sure?"); [c, 5]'), [true, 5]);
  ok(page.logs.some(l => /\[dialog\] alert: hello dialog/.test(l)), 'logged: ' + page.logs.join(' | '));
});

test('downloads land in shots/downloads', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  const name = 'rigtest-download-' + process.pid + '.txt';
  const file = path.join(SHOTS, 'downloads', name);
  await page.eval('const a = document.createElement("a"); a.href = URL.createObjectURL(new Blob(["planet card bytes"])); a.download = ' + JSON.stringify(name) + '; document.body.appendChild(a); a.click(); 0');
  ok(await until(() => shared.downloads.some(d => d.name === name && d.state === 'completed'), 5000), 'download completed: ' + JSON.stringify(shared.downloads));
  eq(shared.downloads.find(d => d.name === name).file, file);
  eq(fs.readFileSync(file, 'utf8'), 'planet card bytes');
  fs.unlinkSync(file);
});

test('large screenshot (3840x2160 of noise) survives the WebSocket', async () => {
  const page = await sharedPage();
  await page.setViewport({ width: 1920, height: 1080, dpr: 2 });
  await page.goto('data:text/html,<body style="margin:0"><canvas id="c" style="width:100vw;height:100vh;display:block"></canvas>');
  await page.eval('const c = document.getElementById("c"); c.width = 3840; c.height = 2160; const g = c.getContext("2d"); const im = g.createImageData(3840, 2160); ' +
    'let s = 1; for (let i = 0; i < im.data.length; i++) { s = (s * 1664525 + 1013904223) >>> 0; im.data[i] = i % 4 === 3 ? 255 : s >>> 24; } g.putImageData(im, 0, 0); 0');
  await page.wait(100);
  const buf = await page.png();
  ok(buf.length > 8e6, 'PNG is ' + (buf.length / 1e6).toFixed(1) + ' MB');
  const img = decodePng(buf);
  eq([img.width, img.height], [3840, 2160]);
  ok(img.stats().colors > 4000, 'noise decoded');
  await page.setViewport({ width: 1280, height: 720, dpr: 1 });
});

test('server: static files, no-cache, MIME, traversal, /__save (data URL, raw, from a page)', async () => {
  const s = await server.start({ port: 0, quiet: true });
  const saved = [];
  try {
    ok(s.port > 0 && /^http:\/\/127\.0\.0\.1:\d+\/$/.test(s.url), s.url);
    let r = await fetch(s.url + 'docs/FOUNDATION.md');
    eq(r.status, 200);
    ok(/no-store/.test(r.headers.get('cache-control')) && /no-cache/.test(r.headers.get('cache-control')), 'cache-control: ' + r.headers.get('cache-control'));
    eq(r.headers.get('content-type'), 'text/markdown; charset=utf-8');
    eq(await r.text(), fs.readFileSync(path.join(ROOT, 'docs', 'FOUNDATION.md'), 'utf8'), 'body matches the file');
    r = await fetch(s.url + 'tools/smoke/rig.html');
    eq([r.status, r.headers.get('content-type')], [200, 'text/html; charset=utf-8']);
    r = await fetch(s.url + 'tools/shot.js');
    eq([r.status, r.headers.get('content-type')], [200, 'text/javascript; charset=utf-8']);
    r = await fetch(s.url + 'tools/shot.js', { method: 'HEAD' });
    eq([r.status, +r.headers.get('content-length'), (await r.text()).length], [200, fs.statSync(SHOT_JS).size, 0], 'HEAD');
    eq((await fetch(s.url + 'nope/missing.js')).status, 404);
    for (const evil of ['..%2f..%2fnoctiluca.html', '%2e%2e/%2e%2e/secret', '..%5c..%5cWindows%5cwin.ini', 'tools/..%2f..%2f..%2fx']) {
      const st = (await fetch(s.url + evil)).status;
      ok(st === 403 || st === 404, 'traversal "' + evil + '" → ' + st);
    }
    r = await fetch(s.url + 'tools/smoke');
    ok(r.redirected && r.url.endsWith('/tools/smoke/'), 'directory redirect');
    ok(/rig\.html/.test(await r.text()), 'directory listing');

    // /__save with a data: URL body (base64)
    const bytes = Buffer.from([0, 1, 2, 250, 251, 252, 253, 254, 255, 10, 13]);
    const n1 = 'rigtest-save-' + process.pid + '.bin';
    r = await fetch(s.url + '__save?name=' + n1, { method: 'POST', body: 'data:application/octet-stream;base64,' + bytes.toString('base64') });
    let j = await r.json();
    saved.push(path.join(SHOTS, n1));
    eq([r.status, j.ok, j.file, j.bytes], [200, true, 'shots/' + n1, bytes.length]);
    ok(fs.readFileSync(path.join(SHOTS, n1)).equals(bytes), 'decoded bytes landed in shots/');
    // percent-encoded data: URL
    const n2 = 'rigtest-save-' + process.pid + '.txt';
    r = await fetch(s.url + '__save?name=' + n2, { method: 'POST', body: 'data:text/plain,hello%20world%21' });
    saved.push(path.join(SHOTS, n2));
    eq(fs.readFileSync(path.join(SHOTS, n2), 'utf8'), 'hello world!');
    // raw body
    r = await fetch(s.url + '__save?name=' + n2, { method: 'POST', body: bytes });
    ok(fs.readFileSync(path.join(SHOTS, n2)).equals(bytes), 'raw body written as is');
    // name sanitizing: nothing escapes shots/
    r = await fetch(s.url + '__save?name=' + encodeURIComponent('../../evil dir/..\\rigtest evil*' + process.pid + '.txt'), { method: 'POST', body: 'x' });
    j = await r.json();
    saved.push(j.path);
    ok(/^shots\/[A-Za-z0-9._-]+$/.test(j.file), 'sanitized name: ' + j.file);
    eq(path.dirname(j.path), SHOTS, 'still inside shots/');
    ok(!fs.existsSync(path.join(ROOT, '..', 'evil dir')), 'nothing written outside');
    eq((await fetch(s.url + '__save?name=x.bin')).status, 405, 'GET /__save is refused');

    // from a real page: http goto, canvas → data URL → POST
    const page = await sharedPage();
    await page.goto(s.url + 'tools/smoke/rig.html?via=http');
    eq(await page.eval('[location.protocol, document.title]'), ['http:', 'rig test page']);
    const n3 = 'rigtest-canvas-' + process.pid + '.png';
    const reply = await page.eval('const c = document.createElement("canvas"); c.width = 64; c.height = 32; const g = c.getContext("2d"); g.fillStyle = "rgb(10, 200, 30)"; g.fillRect(0, 0, 64, 32); ' +
      'await (await fetch("/__save?name=' + n3 + '", { method: "POST", body: c.toDataURL("image/png") })).json()');
    saved.push(path.join(SHOTS, n3));
    eq(reply.ok, true);
    const img = decodePng(path.join(SHOTS, n3));
    eq([img.width, img.height, img.px(5, 5)], [64, 32, [10, 200, 30, 255]], 'PNG posted by the page');
    // and from a file:// page (cross-origin POST, as the game itself would do)
    await page.goto(RIG);
    const n4 = 'rigtest-cors-' + process.pid + '.txt';
    eq((await page.eval('await (await fetch("' + s.url + '__save?name=' + n4 + '", { method: "POST", body: "from file origin" })).json()')).bytes, 16);
    saved.push(path.join(SHOTS, n4));
    eq(page.errors, [], 'no CORS errors');

    await rejects(server.start({ port: s.port, quiet: true }), /already in use/, 'second server on the same port');
  } finally {
    await s.close();
    for (const f of saved) { try { fs.unlinkSync(f); } catch (e) { /* not created */ } }
  }
  await rejects(fetch(s.url + 'docs/FOUNDATION.md'), null, 'server stopped');
});

test('server CLI: default-style start with a port argument and PORT env', async () => {
  for (const mode of ['arg', 'env']) {
    const port = 5600 + Math.floor(Math.random() * 300);
    const child = spawn(process.execPath, [path.join(ROOT, 'tools', 'server.js')].concat(mode === 'arg' ? [String(port)] : []),
      { cwd: ROOT, env: Object.assign({}, process.env, mode === 'env' ? { PORT: String(port) } : { PORT: '1' }), stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    let out = '';
    child.stdout.on('data', d => { out += d; });
    child.stderr.on('data', d => { out += d; });
    try {
      ok(await until(() => /dev server: http/.test(out), 5000), mode + ': server announced itself: ' + out);
      ok(out.indexOf(':' + port + '/') > 0, mode + ': listens on ' + port + ': ' + out);
      const r = await fetch('http://127.0.0.1:' + port + '/docs/CONCEPT.md');
      eq(r.status, 200, mode);
      ok(/DEMIURGE/.test(await r.text()));
    } finally { child.kill(); }
  }
});

test('parallel: four rigs at the same time', async () => {
  const run = async (i) => {
    const b = await launch({ width: 640 + i * 10, height: 360 });
    try {
      const page = await b.page();
      await page.goto(RIG + '?rig=' + i);
      const v = await page.eval('await new Promise(r => setTimeout(r, 200)); [location.search, innerWidth]');
      const f = await page.fps(500);
      const img = decodePng(await page.png());
      return { i, v, fps: f.fps, w: img.width, pid: b.pid, port: b.port, profile: b.profile };
    } finally { await b.close(); }
  };
  const out = await Promise.all([0, 1, 2, 3].map(run));
  for (const o of out) {
    eq(o.v, ['?rig=' + o.i, 640 + o.i * 10], 'rig ' + o.i + ' saw its own page');
    eq(o.w, 640 + o.i * 10);
    ok(o.fps > 30, 'rig ' + o.i + ' fps ' + o.fps);
    ok(!fs.existsSync(o.profile), 'profile ' + o.i + ' removed');
    ok(!pidAlive(o.pid), 'browser ' + o.i + ' gone');
  }
  eq(new Set(out.map(o => o.port)).size, 4, 'distinct debugging ports');
  eq(new Set(out.map(o => o.profile)).size, 4, 'distinct profiles');
});

test('timeout: the session deadline kills the browser and rejects pending calls', async () => {
  const b = await launch({ timeout: 1500 });
  const page = await b.page();
  await page.goto(RIG);
  const t0 = Date.now();
  await rejects(page.eval('new Promise(() => {})'), /session timeout/, 'pending eval');
  const dt = Date.now() - t0;
  ok(dt > 800 && dt < 4000, 'rejected after ' + dt + ' ms');
  ok(b.closed, 'browser marked closed');
  ok(!pidAlive(b.pid) && !fs.existsSync(b.profile), 'process and profile gone');
  await rejects(page.eval('1'), /session timeout/, 'later calls');
  await rejects(page.goto(RIG), /session timeout/, 'later goto');
  await b.close();
});

test('cleanup: uncaught error in the script still kills the browser', async () => {
  const r = runNode([__filename, '--child', 'throw']);
  eq(r.status, 1, 'exit code of the crashing script. stderr: ' + r.stderr);
  ok(/deliberate crash/.test(r.stderr), 'the script error is printed');
  const info = JSON.parse(r.stdout.trim().split('\n')[0]);
  ok(await until(() => !pidAlive(info.pid) && !fs.existsSync(info.profile), 5000), 'browser ' + info.pid + ' and profile gone');
  eq(edgeProcs(path.basename(info.profile)), []);
});

test('cleanup: forgetting close() ends at the session timeout (CLI-less script)', async () => {
  const infoFile = path.join(os.tmpdir(), 'rigtest-forget-' + process.pid + '.json');
  const t0 = Date.now();
  const r = spawnSync(process.execPath, ['-e',
    'const { launch } = require(' + JSON.stringify(SHOT_JS) + '); launch({ timeout: 2000 }).then(async b => { await b.page(); require("fs").writeFileSync(' + JSON.stringify(infoFile) + ', JSON.stringify({ pid: b.pid, profile: b.profile })); });'],
  { encoding: 'utf8', windowsHide: true, timeout: 30000 });
  const dt = Date.now() - t0;
  eq(r.status, 0, 'process ends by itself. stderr: ' + r.stderr);
  ok(dt >= 1800 && dt < 10000, 'ended after ' + dt + ' ms');
  const info = JSON.parse(fs.readFileSync(infoFile, 'utf8'));
  fs.unlinkSync(infoFile);
  ok(!pidAlive(info.pid) && !fs.existsSync(info.profile), 'browser and profile gone');
});

test('cleanup: hard kill of the node process → watchdog kills the browser', async () => {
  const infoFile = path.join(os.tmpdir(), 'rigtest-hang-' + process.pid + '.json');
  try { fs.unlinkSync(infoFile); } catch (e) { /* none */ }
  const child = spawn(process.execPath, [__filename, '--child', 'hang', infoFile], { stdio: 'ignore', windowsHide: true });
  ok(await until(() => fs.existsSync(infoFile), 15000), 'child started a browser');
  await sleep(200);
  const info = JSON.parse(fs.readFileSync(infoFile, 'utf8'));
  fs.unlinkSync(infoFile);
  ok(pidAlive(info.pid) && fs.existsSync(info.profile), 'browser is running');
  ok(edgeProcs(path.basename(info.profile)).length >= 3, 'browser process tree is up');
  spawnSync('taskkill', ['/PID', String(child.pid), '/F'], { windowsHide: true }); // no /T: only node dies, exit handlers do not run
  ok(await until(() => !pidAlive(info.pid) && !fs.existsSync(info.profile), 12000), 'watchdog removed browser ' + info.pid + ' and ' + info.profile);
  eq(edgeProcs(path.basename(info.profile)), [], 'no msedge.exe left');
});

test('cleanup: Ctrl+C (real console SIGINT) kills the browser', async () => {
  // The victim runs in its own hidden console under a PowerShell that re-enables Ctrl+C (test harnesses often
  // start us with Ctrl+C ignored, and that setting is inherited). Then a real CTRL_C_EVENT is raised there.
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rigtest-sigint-'));
  const infoFile = path.join(dir, 'info.json');
  const inner = path.join(dir, 'inner.ps1');
  const outer = path.join(dir, 'outer.ps1');
  const Q = '`"';
  fs.writeFileSync(inner, [
    'param([string]$Node, [string]$Script, [string]$Out)',
    'Add-Type -Namespace Native -Name Con2 -MemberDefinition \'[DllImport("kernel32.dll")] public static extern bool SetConsoleCtrlHandler(IntPtr h, bool add);\'',
    '[void][Native.Con2]::SetConsoleCtrlHandler([IntPtr]::Zero, $false)',
    '& $Node $Script --child hang $Out',
  ].join('\r\n'));
  fs.writeFileSync(outer, [
    'param([string]$Inner, [string]$Node, [string]$Script, [string]$Out)',
    'Add-Type -Namespace Native -Name Con -MemberDefinition \'[DllImport("kernel32.dll")] public static extern bool AttachConsole(uint pid); [DllImport("kernel32.dll")] public static extern bool FreeConsole(); [DllImport("kernel32.dll")] public static extern bool SetConsoleCtrlHandler(IntPtr h, bool add); [DllImport("kernel32.dll")] public static extern bool GenerateConsoleCtrlEvent(uint ev, uint grp);\'',
    '$p = Start-Process -FilePath powershell.exe -ArgumentList "-NoProfile","-ExecutionPolicy","Bypass","-File","' + Q + '$Inner' + Q + '","-Node","' + Q + '$Node' + Q + '","-Script","' + Q + '$Script' + Q + '","-Out","' + Q + '$Out' + Q + '" -PassThru -WindowStyle Hidden',
    '$deadline = (Get-Date).AddSeconds(20)',
    'while (-not (Test-Path $Out) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }',
    'Start-Sleep -Milliseconds 400',
    '[void][Native.Con]::FreeConsole()',
    '$att = [Native.Con]::AttachConsole($p.Id)',
    '[void][Native.Con]::SetConsoleCtrlHandler([IntPtr]::Zero, $true)',
    '$gen = [Native.Con]::GenerateConsoleCtrlEvent(0, 0)',
    'Start-Sleep -Milliseconds 2500',
    '[System.IO.File]::WriteAllText("$Out.ps", "att=$att gen=$gen")',
  ].join('\r\n'));
  try {
    spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', outer, '-Inner', inner, '-Node', process.execPath, '-Script', __filename, '-Out', infoFile],
      { encoding: 'utf8', windowsHide: true, timeout: 60000 });
    ok(fs.existsSync(infoFile), 'victim started');
    const info = JSON.parse(fs.readFileSync(infoFile, 'utf8'));
    const psSaid = fs.existsSync(infoFile + '.ps') ? fs.readFileSync(infoFile + '.ps', 'utf8') : '(nothing)';
    eq(psSaid, 'att=True gen=True', 'CTRL_C_EVENT was generated in the victim console');
    ok(await until(() => !pidAlive(info.node), 6000), 'node exited after Ctrl+C');
    const log = fs.existsSync(infoFile + '.log') ? fs.readFileSync(infoFile + '.log', 'utf8').trim() : '(no exit event)';
    eq(log, 'exit 130', 'SIGINT handler ran and exited with code 130');
    ok(await until(() => !pidAlive(info.pid) && !fs.existsSync(info.profile), 6000), 'browser and profile gone after Ctrl+C');
    eq(edgeProcs(path.basename(info.profile)), []);
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test('offline by default: only loopback hosts resolve', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  // no packet leaves the machine: the browser's resolver refuses the name
  eq(await page.eval('fetch("https://demiurge-offline-check.invalid/").then(r => "reached", e => "blocked")'), 'blocked');
  await page.wait(100);
  ok(page.errors.some(e => /ERR_NAME_NOT_RESOLVED/.test(e)), 'reported as a page error: ' + JSON.stringify(page.errors));
});

test('browser shortcuts behave as in a real browser: F5 reloads unless the page prevents it', async () => {
  const page = await sharedPage();
  await page.goto(RIG);
  await page.eval('window.marker = 1; 0');
  await page.key('press', 'F5');
  await page.wait(400);
  eq(await page.eval('window.marker === undefined && READY'), true, 'unhandled F5 reloaded the page');
  await page.eval('window.marker = 1; window.addEventListener("keydown", e => { if (e.code === "F5") e.preventDefault(); }); 0');
  await page.key('press', 'F5');
  await page.wait(400);
  eq(await page.eval('window.marker'), 1, 'preventDefault() in the page stops the reload');
});

test('CLI: page mode prints { result, errors, logs, shot } and sets the exit code', async () => {
  const out = 'rigtest-cli-' + process.pid + '.png';
  let r = runNode([SHOT_JS, 'tools/smoke/rig.html', '--size', '800x600', '--dpr', '2', '--wait', '200', '--eval', '[document.title, innerWidth, devicePixelRatio]', '--out', out]);
  eq(r.status, 0, 'exit code. stderr: ' + r.stderr + ' stdout: ' + r.stdout);
  let j = JSON.parse(r.stdout);
  eq(Object.keys(j), ['result', 'errors', 'logs', 'shot']);
  eq(j.result, ['rig test page', 800, 2]);
  eq([j.errors, j.shot], [[], path.join(SHOTS, out)]);
  const img = decodePng(j.shot);
  eq([img.width, img.height], [1600, 1200]);
  fs.unlinkSync(j.shot);

  r = runNode([SHOT_JS, 'tools/smoke/throws.html', '--no-shot']);
  eq(r.status, 1, 'page errors → exit 1');
  j = JSON.parse(r.stdout);
  ok(j.errors.length >= 4 && j.errors.some(e => /deliberate uncaught error/.test(e)), 'errors listed: ' + JSON.stringify(j.errors));
  ok(j.logs.some(l => /before the error/.test(l)), 'logs listed');
  eq(j.shot, null, '--no-shot');

  r = runNode([SHOT_JS, 'tools/smoke/throws.html', '--no-shot', '--allow-errors']);
  eq(r.status, 0, '--allow-errors → exit 0');
  ok(JSON.parse(r.stdout).errors.length >= 4, 'errors still reported');

  r = runNode([SHOT_JS, 'tools/smoke/no-such-file.html']);
  eq(r.status, 1, 'missing file → exit 1');
  ok(/not found/.test(JSON.parse(r.stdout).error), 'error field: ' + r.stdout);

  r = runNode([SHOT_JS, 'tools/smoke/rig.html', '--no-shot', '--eval', 'nope()']);
  eq(r.status, 1, 'failing --eval → exit 1');
  ok(/ReferenceError: nope is not defined/.test(JSON.parse(r.stdout).error));

  r = runNode([SHOT_JS, 'tools/smoke/rig.html', '--no-shot', '--wait-for', 'window.READY', '--wait', '0', '--eval', 'READY']);
  eq([r.status, JSON.parse(r.stdout).result], [0, true], '--wait-for');

  r = runNode([SHOT_JS, 'tools/smoke/rig.html', '--no-shot', '--timeout', '1500', '--eval', 'new Promise(() => {})']);
  eq(r.status, 1, 'timeout → exit 1');
  ok(/session timeout/.test(JSON.parse(r.stdout).error), 'timeout reported: ' + r.stdout);

  r = runNode([SHOT_JS]);
  eq(r.status, 1, 'no arguments → usage + exit 1');
  ok(/usage:/.test(r.stdout));
});

test('CLI: --run scenario prints the return value', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'rigtest-scn-'));
  try {
    const good = path.join(dir, 'good scenario.js');
    fs.writeFileSync(good, 'module.exports = async (page, browser, args) => {\n' +
      '  await page.goto(' + JSON.stringify(RIG) + ');\n' +
      '  await page.waitFor("window.READY");\n' +
      '  const size = await page.eval("[innerWidth, innerHeight]");\n' +
      '  const second = await browser.page();\n' +
      '  return { size, args, title: await page.eval("document.title"), pages: browser.pages.length, secondUrl: await second.eval("location.href") };\n' +
      '};\n');
    let r = runNode([SHOT_JS, '--run', good, '--size', '900x500', 'extra1', 'extra2']);
    eq(r.status, 0, 'exit code. stderr: ' + r.stderr);
    eq(JSON.parse(r.stdout), { size: [900, 500], args: ['extra1', 'extra2'], title: 'rig test page', pages: 2, secondUrl: 'about:blank' });

    const bad = path.join(dir, 'bad.js');
    fs.writeFileSync(bad, 'module.exports = async (page) => { await page.goto(' + JSON.stringify(RIG) + '); throw new Error("scenario exploded"); };\n');
    r = runNode([SHOT_JS, '--run', bad]);
    eq(r.status, 1, 'throwing scenario → exit 1');
    ok(/scenario exploded/.test(r.stderr) && r.stdout === '', 'error on stderr, nothing on stdout');

    const errs = path.join(dir, 'errs.js');
    fs.writeFileSync(errs, 'module.exports = async (page) => { await page.goto(' + JSON.stringify(THROWS) + '); await page.wait(300); return page.errors.length; };\n');
    r = runNode([SHOT_JS, '--run', errs]);
    eq(r.status, 1, 'page errors → exit 1');
    ok(+r.stdout >= 4 && /page errors \(\d+\)/.test(r.stderr) && /deliberate uncaught error/.test(r.stderr), 'value on stdout, errors on stderr');
    r = runNode([SHOT_JS, '--run', errs, '--allow-errors']);
    eq(r.status, 0, '--allow-errors');
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test('module: launch failure is clean (bad executable)', async () => {
  await rejects(launch({ exe: path.join(os.tmpdir(), 'no-such-browser.exe') }), /not found/, 'missing exe');
  const before = fs.readdirSync(os.tmpdir()).filter(n => n.startsWith('demiurge-shot-' + process.pid + '-')).length;
  await rejects(launch({ args: ['--remote-debugging-port=1'] , timeout: 4000 }), null, 'browser that cannot open its port');
  eq(fs.readdirSync(os.tmpdir()).filter(n => n.startsWith('demiurge-shot-' + process.pid + '-')).length, before, 'failed launch leaves no profile behind');
});

// ---- runner -----------------------------------------------------------------------------------
(async () => {
  const t0 = Date.now();
  const selected = tests.filter(t => t.name.toLowerCase().includes(filter.toLowerCase()));
  for (const t of selected) {
    const start = Date.now();
    try {
      await t.fn();
      results.push(true);
      console.log('  ok   ' + t.name + '  (' + (Date.now() - start) + ' ms)');
    } catch (e) {
      results.push(false);
      console.log('  FAIL ' + t.name + '\n       ' + String(e && e.stack || e).split('\n').slice(0, 12).join('\n       '));
    }
  }
  if (shared) await shared.close();
  // nothing of ours may be left behind
  const mine = edgeProcs('demiurge-shot-' + process.pid + '-');
  const dirs = fs.readdirSync(os.tmpdir()).filter(n => n.startsWith('demiurge-shot-' + process.pid + '-'));
  if (mine.length || dirs.length) { results.push(false); console.log('  FAIL leftovers: msedge pids ' + JSON.stringify(mine) + ', profiles ' + JSON.stringify(dirs)); }
  else console.log('  ok   no leftover msedge.exe processes or profiles from this run');
  const failed = results.filter(r => !r).length;
  console.log('\nrigtest: ' + (results.length - failed) + ' passed, ' + failed + ' failed, ' + (Date.now() - t0) + ' ms');
  process.exit(failed ? 1 : 0);
})();
