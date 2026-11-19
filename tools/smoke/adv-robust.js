// DEMIURGE — tools/smoke/adv-robust.js : adversarial robustness test of tools/shot.js, written against docs/TOOLS.md only.
//   node tools/smoke/adv-robust.js [substring]        run all tests, or only those whose name contains the substring
// Covers: 4 rigs at once (CLI and module), error capture + CLI exit code, a page that never becomes ready (module, CLI,
// scenario, uncaught), paths with spaces, 390x844 mobile + touch, mouse drag / wheel, keyboard, page.eval value
// round trips, a page that blocks its main thread, a rig user that exits / throws / is killed without close(),
// and a final check that no msedge.exe and no temp profile is left behind.
// Last line: "adv-robust: N passed, M failed, … ms". Exit code 1 if anything failed.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, execFileSync } = require('child_process');
const { launch, decodePng } = require('../shot.js');

const ROOT = path.resolve(__dirname, '..', '..');
const SHOT = path.join(ROOT, 'tools', 'shot.js');
const filter = process.argv[2] || '';
const tests = [];
const test = (name, fn) => tests.push({ name, fn });
const sleep = ms => new Promise(r => setTimeout(r, ms));

function ok(cond, msg) { if (!cond) throw new Error('assertion failed: ' + msg); }
function eq(a, b, msg) { const x = JSON.stringify(a), y = JSON.stringify(b); if (x !== y) throw new Error((msg || 'eq') + ': got ' + x + ', expected ' + y); }

function edgePids() {
  if (process.platform !== 'win32') return [];
  const out = execFileSync('tasklist', ['/FI', 'IMAGENAME eq msedge.exe', '/FO', 'CSV', '/NH'], { windowsHide: true }).toString();
  return out.split(/\r?\n/).map(l => /^"msedge\.exe","(\d+)"/i.exec(l)).filter(Boolean).map(m => +m[1]);
}
function alive(pid) { try { process.kill(pid, 0); return true; } catch (e) { return e.code === 'EPERM'; } }
async function waitDead(pid, ms) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { if (!alive(pid)) return Date.now() - t0; await sleep(100); }
  return -1;
}
function profiles() { return fs.readdirSync(os.tmpdir()).filter(n => n.startsWith('demiurge-shot-')); }

/** Run node with args (no shell, so quoting is exact). → { code, stdout, stderr, ms, pid } */
function node(args, opts) {
  return new Promise(resolve => {
    const t0 = Date.now();
    const p = spawn(process.execPath, args, Object.assign({ cwd: ROOT, windowsHide: true }, opts || {}));
    let stdout = '', stderr = '';
    p.stdout.on('data', d => { stdout += d; });
    p.stderr.on('data', d => { stderr += d; });
    p.on('close', code => resolve({ code, stdout, stderr, ms: Date.now() - t0, pid: p.pid }));
  });
}
const cli = (...args) => node([SHOT, ...args]);

async function withPage(opts, fn) {
  const browser = await launch(opts || {});
  try { return await fn(await browser.page(), browser); } finally { await browser.close(); }
}

// ------------------------------------------------------------------------------------------------ concurrency

test('4 CLI rigs at once render the adv scene', async () => {
  const runs = await Promise.all([0, 1, 2, 3].map(i => cli('tools/smoke/adv-scene.html?hud=0', '--wait-for', 'window.ADV_READY', '--wait', '600',
    '--eval', '({ renderer: ADV.info.renderer, stats: ADV.stats(), bench: ADV.bench(60), err: ADV.error })', '--out', 'adv-par-cli-' + i + '.png')));
  const report = [];
  runs.forEach((r, i) => {
    ok(r.code === 0, 'rig ' + i + ' exit code ' + r.code + ': ' + r.stdout.slice(0, 400) + r.stderr.slice(0, 400));
    const j = JSON.parse(r.stdout);
    eq(j.errors, [], 'rig ' + i + ' page errors');
    ok(/ANGLE/.test(j.result.renderer), 'renderer string');
    ok(j.result.err === null, 'scene error');
    const st = decodePng(j.shot).stats();
    ok(st.colors > 1000 && st.max > 200, 'rig ' + i + ' screenshot is blank: ' + JSON.stringify(st));
    report.push({ ms: r.ms, benchMs: j.result.bench.msPerFrame, rafMs: j.result.stats.rafMeanMs, gpuMs: j.result.stats.gpuMeanMs });
  });
  return report;
});

test('4 module rigs at once (adv-scene.js, full checks)', async () => {
  const runs = await Promise.all([0, 1, 2, 3].map(i => node([path.join(ROOT, 'tools', 'smoke', 'adv-scene.js'), '--tag', 'par' + i])));
  return runs.map((r, i) => {
    ok(r.code === 0, 'rig ' + i + ' exit code ' + r.code + ': ' + r.stdout.slice(0, 600) + r.stderr.slice(0, 400));
    const j = JSON.parse(r.stdout);
    ok(j.ok, 'rig ' + i + ' checks ' + JSON.stringify(j.checks));
    return { ms: r.ms, benchMs: j.bench.msPerFrame, rafMs: j.raf.rafMeanMs, gpuMs: j.raf.gpuMeanMs };
  });
});

// ------------------------------------------------------------------------------------------------ errors

test('page.errors holds the console.error and the uncaught exception', () => withPage({}, async page => {
  await page.goto('tools/smoke/adv-errors.html');
  await page.waitFor('window.ADV_ERR_READY');
  await page.wait(300);
  ok(page.errors.some(e => /adv-errors: console\.error message/.test(e)), 'console.error missing: ' + JSON.stringify(page.errors));
  ok(page.errors.some(e => /adv-errors: uncaught exception/.test(e)), 'uncaught exception missing: ' + JSON.stringify(page.errors));
  eq(page.errors.length, 2, 'number of errors');
  ok(page.errors.find(e => /uncaught/.test(e)).includes('adv-errors.html:'), 'uncaught exception has no file:line');
  ok(page.errors.find(e => /console\.error/.test(e)).includes('adv-errors.html:'), 'console.error has no file:line');
  ok(page.logs.some(l => l === 'adv-errors: plain log'), 'log missing: ' + JSON.stringify(page.logs));
  ok(page.logs.some(l => /^\[warning\] adv-errors: a warning/.test(l)), 'warning missing: ' + JSON.stringify(page.logs));
  return page.errors;
}));

test('CLI exits 1 on that page, 0 with --allow-errors, 0 with --ignore', async () => {
  const a = await cli('tools/smoke/adv-errors.html', '--wait', '400', '--no-shot');
  eq(a.code, 1, 'exit code');
  const ja = JSON.parse(a.stdout);
  ok(ja.errors.length === 2 && /console\.error message/.test(ja.errors.join()) && /uncaught exception/.test(ja.errors.join()), 'errors: ' + a.stdout);
  ok(ja.error === undefined, 'run itself must not have failed');
  const b = await cli('tools/smoke/adv-errors.html', '--wait', '400', '--no-shot', '--allow-errors');
  eq(b.code, 0, 'exit code with --allow-errors');
  eq(JSON.parse(b.stdout).errors.length, 2, 'errors still reported with --allow-errors');
  const c = await cli('tools/smoke/adv-errors.html', '--wait', '400', '--no-shot', '--ignore', 'adv-errors');
  eq(c.code, 0, 'exit code with --ignore');
  const jc = JSON.parse(c.stdout);
  eq(jc.errors, [], 'errors with --ignore');
  ok(jc.logs.filter(l => l.startsWith('[ignored error] ')).length === 2, 'ignored errors are listed in logs');
  const d = await cli('tools/smoke/adv-errors.html', '--wait', '400', '--no-shot', '--ignore', 'console\\.error message');
  eq(d.code, 1, 'exit code when only one of two errors is ignored');
});

// ------------------------------------------------------------------------------------------------ never ready

test('waitFor on a page that never becomes ready: TimeoutError, page still usable, browser killed by close()', async () => {
  const browser = await launch({});
  const pid = browser.pid;
  let elapsed;
  try {
    const page = await browser.page();
    await page.goto('tools/smoke/adv-never.html');
    const t0 = Date.now();
    let err = null;
    try { await page.waitFor('window.NEVER', 1500); } catch (e) { err = e; }
    elapsed = Date.now() - t0;
    ok(err && err.name === 'TimeoutError', 'expected TimeoutError, got ' + (err && err.name + ': ' + err.message));
    ok(/window\.NEVER/.test(err.message) && /1500 ms/.test(err.message), 'message: ' + err.message);
    ok(elapsed >= 1500 && elapsed < 2500, 'timed out after ' + elapsed + ' ms');
    ok((await page.eval('+document.getElementById("n").textContent')) > 10, 'page is still animating and reachable');
    let err2 = null;
    try { await page.waitFor('NOPE.deep.value', 300); } catch (e) { err2 = e; }
    ok(err2 && err2.name === 'TimeoutError' && /NOPE is not defined/.test(err2.message), 'throwing expression: ' + (err2 && err2.message));
    eq(page.errors, [], 'failed waitFor polls must not add page errors');
  } finally {
    await browser.close();
  }
  ok(!alive(pid), 'browser process ' + pid + ' survived close()');
  let err3 = null;
  try { await browser.page(); } catch (e) { err3 = e; }
  ok(err3 && /closed/.test(err3.message), 'call after close: ' + (err3 && err3.message));
  await browser.close();   // twice is fine
  return { elapsed };
});

test('CLI --wait-for on a page that never becomes ready: exit 1, error in JSON, no browser left', async () => {
  const before = profiles();
  const r = await cli('tools/smoke/adv-never.html', '--wait-for', 'window.NEVER', '--timeout', '7000', '--no-shot');
  eq(r.code, 1, 'exit code');
  const j = JSON.parse(r.stdout);
  ok(/not truthy within|timeout/i.test(j.error || ''), 'error field: ' + r.stdout);
  ok(r.ms < 12000, 'took ' + r.ms + ' ms');
  eq(profiles().filter(p => p.startsWith('demiurge-shot-' + r.pid + '-')), [], 'profile left behind');
  return { ms: r.ms, error: j.error, profilesBefore: before.length };
});

test('scenario whose waitFor times out: exit 1, TimeoutError on stderr, browser gone', async () => {
  const r = await cli('--run', 'tools/smoke/adv-scn-never.js');
  eq(r.code, 1, 'exit code');
  ok(/scenario failed: TimeoutError: page\.waitFor/.test(r.stderr), 'stderr: ' + r.stderr);
  ok(/adv-scn-never\.js/.test(r.stderr), 'stack should point at the scenario file: ' + r.stderr);
  eq(r.stdout, '', 'stdout');
  eq(profiles().filter(p => p.startsWith('demiurge-shot-' + r.pid + '-')), [], 'profile left behind');
});

for (const mode of ['uncaught-wait', 'exit', 'throw']) {
  test('rig user that leaves without close() [' + mode + ']: browser is killed', async () => {
    const r = await node([path.join(ROOT, 'tools', 'smoke', 'adv-child.js'), mode]);
    const pid = JSON.parse(r.stdout.split('\n')[0]).pid;
    ok(pid > 0, 'no browser pid: ' + r.stdout + r.stderr);
    eq(r.code, mode === 'exit' ? 0 : 1, 'child exit code (stderr: ' + r.stderr.slice(0, 300) + ')');
    const dead = await waitDead(pid, 5000);
    ok(dead >= 0, 'browser ' + pid + ' still alive 5 s after its Node process ended');
    await sleep(300);
    eq(profiles().filter(p => p.startsWith('demiurge-shot-' + r.pid + '-')), [], 'profile left behind');
    return { browserDeadAfterMs: dead };
  });
}

test('rig user killed hard (taskkill /F): the watchdog removes browser and profile', async () => {
  const p = spawn(process.execPath, [path.join(ROOT, 'tools', 'smoke', 'adv-child.js'), 'idle'], { cwd: ROOT, windowsHide: true });
  let out = '';
  p.stdout.on('data', d => { out += d; });
  const t0 = Date.now();
  while (!/\n/.test(out) && Date.now() - t0 < 20000) await sleep(50);
  const pid = JSON.parse(out.split('\n')[0]).pid;
  ok(alive(pid), 'browser should be running');
  if (process.platform === 'win32') execFileSync('taskkill', ['/F', '/PID', String(p.pid)], { stdio: 'ignore', windowsHide: true });
  else p.kill('SIGKILL');
  const dead = await waitDead(pid, 20000);
  ok(dead >= 0, 'browser ' + pid + ' still alive 20 s after its Node process was killed');
  const t1 = Date.now();
  while (profiles().some(n => n.startsWith('demiurge-shot-' + p.pid + '-')) && Date.now() - t1 < 15000) await sleep(200);
  eq(profiles().filter(n => n.startsWith('demiurge-shot-' + p.pid + '-')), [], 'profile left behind');
  return { browserDeadAfterMs: dead };
});

test('page that blocks its main thread: goto times out, eval ends at the session deadline, browser killed', async () => {
  const b1 = await launch({});
  const pid1 = b1.pid;
  let e1 = null, ms1;
  try {
    const page = await b1.page();
    const t0 = Date.now();
    try { await page.goto('tools/smoke/adv-hang.html?load=1', { timeout: 2000 }); } catch (e) { e1 = e; }
    ms1 = Date.now() - t0;
  } finally { await b1.close(); }
  ok(e1 && /no load event within 2000 ms/.test(e1.message), 'goto: ' + (e1 && e1.message));
  ok(!alive(pid1), 'browser with a hung page survived close()');

  const b2 = await launch({ timeout: 4000 });
  const pid2 = b2.pid;
  let e2 = null;
  const t0 = Date.now();
  try {
    const page = await b2.page();
    await page.goto('tools/smoke/adv-hang.html');
    await page.eval('ADV_HANG()');
  } catch (e) { e2 = e; } finally { await b2.close(); }
  const ms2 = Date.now() - t0;
  ok(e2 && /session timeout/.test(e2.message), 'eval: ' + (e2 && e2.message));
  ok(ms2 < 6000, 'session deadline took ' + ms2 + ' ms');
  ok(!alive(pid2), 'browser survived the session timeout');
  return { gotoMs: ms1, sessionMs: ms2 };
});

// ------------------------------------------------------------------------------------------------ paths

test('file paths containing spaces (relative, absolute, ?query, file:// URL, CLI, shot name)', () => withPage({}, async page => {
  const rel = 'tools/smoke/adv space dir/adv page one.html';
  const abs = path.join(ROOT, 'tools', 'smoke', 'adv space dir', 'adv page one.html');
  const u1 = await page.goto(rel);
  ok(/adv%20space%20dir\/adv%20page%20one\.html$/.test(u1), 'url: ' + u1);
  ok(await page.waitFor('window.ADV_INPUT_READY', 3000), 'relative path with spaces');
  await page.goto(abs);
  ok(await page.waitFor('window.ADV_INPUT_READY', 3000), 'absolute path with spaces');
  await page.goto(abs.replace(/\\/g, '/'));
  ok(await page.waitFor('window.ADV_INPUT_READY', 3000), 'absolute path with forward slashes');
  await page.goto(rel + '?a=1&b=two#frag');
  eq(await page.eval('[location.search, location.hash]'), ['?a=1&b=two', '#frag'], 'query and hash after a path with spaces');
  await page.goto(u1);
  ok(await page.waitFor('window.ADV_INPUT_READY', 3000), 'file:// URL');
  await page.goto('tools/smoke/adv space dir/adv 100% #1 [ok] & more.html');
  ok(await page.waitFor('window.ADV_INPUT_READY', 3000), 'file name with % # [ ] &');
  ok(/100%25%20%231/.test(await page.eval('location.href')), 'special characters are percent-encoded: ' + (await page.eval('location.href')));
  const shot = await page.shot('adv dir with space/adv shot one.png');
  ok(shot === path.join(ROOT, 'shots', 'adv dir with space', 'adv shot one.png') && fs.statSync(shot).size > 500, 'shot path with spaces: ' + shot);
  let miss = null;
  try { await page.goto('tools/smoke/adv space dir/no such page.html'); } catch (e) { miss = e; }
  ok(miss && /file not found/.test(miss.message), 'missing file: ' + (miss && miss.message));
  eq(page.errors, [], 'page errors');
  const r = await cli(rel, '--wait', '100', '--eval', 'document.title', '--out', 'adv dir with space/adv cli shot.png');
  eq(r.code, 0, 'CLI exit code: ' + r.stdout + r.stderr);
  const j = JSON.parse(r.stdout);
  eq(j.result, 'adv-input', 'CLI result');
  ok(fs.existsSync(j.shot) && / /.test(path.basename(j.shot)), 'CLI shot: ' + j.shot);
  const r2 = await node([SHOT, abs, '--wait', '100', '--no-shot', '--eval', 'location.pathname.length > 10'], { cwd: os.tmpdir() });
  eq(r2.code, 0, 'CLI from another working directory with an absolute path: ' + r2.stdout + r2.stderr);
  const r3 = await node([SHOT, rel, '--wait', '100', '--no-shot'], { cwd: os.tmpdir() });
  eq(r3.code, 0, 'CLI from another working directory with a project-relative path: ' + r3.stdout + r3.stderr);
}));

// ------------------------------------------------------------------------------------------------ mobile + touch

test('390x844 mobile viewport with touch emulation: touchstart fires from page.touch', () => withPage({ width: 390, height: 844, dpr: 3, mobile: true, touch: true }, async page => {
  await page.goto('tools/smoke/adv-input.html');
  await page.waitFor('window.ADV_INPUT_READY');
  const env = await page.eval('({ w: innerWidth, h: innerHeight, dpr: devicePixelRatio, ontouchstart: "ontouchstart" in window, maxTouchPoints: navigator.maxTouchPoints, ' +
    'coarse: matchMedia("(pointer: coarse)").matches, hoverNone: matchMedia("(hover: none)").matches, sw: screen.width, sh: screen.height, cw: document.documentElement.clientWidth })');
  eq(env, { w: 390, h: 844, dpr: 3, ontouchstart: true, maxTouchPoints: 5, coarse: true, hoverNone: true, sw: 390, sh: 844, cw: 390 }, 'environment');
  await page.touch('start', [{ x: 100, y: 200, id: 0 }]);
  await page.touch('move', [{ x: 140, y: 260, id: 0 }]);
  await page.touch('start', [{ x: 300, y: 600, id: 1 }]);             // second finger
  await page.touch('move', [{ x: 150, y: 270, id: 0 }, { x: 280, y: 580, id: 1 }]);   // pinch
  await page.touch('end', [{ id: 0 }]);
  await page.touch('end');
  await page.wait(50);
  const ev = await page.eval('EV.filter(e => e.t.startsWith("touch"))');
  eq(ev.map(e => e.t), ['touchstart', 'touchmove', 'touchstart', 'touchmove', 'touchend', 'touchend'], 'touch event sequence');
  eq(ev[0].touches, [{ id: 0, x: 100, y: 200 }], 'first touchstart');
  ok(ev[0].trusted, 'touch events are trusted');
  eq(ev[2].touches.length, 2, 'two fingers down');
  eq(ev[3].touches.map(p => [p.x, p.y]), [[150, 270], [280, 580]], 'pinch move');
  eq(ev[4].touches.length, 1, 'one finger left after lifting id 0');
  eq(ev[5].touches.length, 0, 'no fingers left');
  const ptr = await page.eval('EV.filter(e => e.t === "pointerdown").map(e => e.pointerType)');
  eq(ptr, ['touch', 'touch'], 'pointer events from touch');
  const img = decodePng(await page.shot('adv-mobile.png'));
  eq([img.width, img.height], [1170, 2532], 'screenshot is width*dpr x height*dpr');
  eq(page.errors, [], 'page errors');
  return env;
}));

test('CLI --size 390x844 --mobile --touch --dpr 2', async () => {
  const r = await cli('tools/smoke/adv-input.html', '--size', '390x844', '--dpr', '2', '--mobile', '--touch', '--wait', '100',
    '--eval', "[innerWidth, innerHeight, devicePixelRatio, 'ontouchstart' in window]", '--out', 'adv-mobile-cli.png');
  eq(r.code, 0, 'exit code: ' + r.stdout + r.stderr);
  const j = JSON.parse(r.stdout);
  eq(j.result, [390, 844, 2, true], 'result');
  const img = decodePng(j.shot);
  eq([img.width, img.height], [780, 1688], 'screenshot size');
});

// ------------------------------------------------------------------------------------------------ mouse

test('mouse drag and wheel arrive at a canvas listener', () => withPage({}, async page => {
  await page.goto('tools/smoke/adv-input.html');
  await page.waitFor('window.ADV_INPUT_READY');
  await page.drag(100, 120, 500, 420, { steps: 8 });
  await page.mouse('wheel', 640, 360, { deltaY: -240 });
  await page.wait(60);
  const ev = await page.eval('EV.splice(0)');
  const m = ev.filter(e => e.t.startsWith('mouse'));
  eq(m[0], { t: 'mousemove', x: 100, y: 120, button: 0, buttons: 0, shift: false, ctrl: false, pointerType: null, trusted: true }, 'approach move');
  eq([m[1].t, m[1].x, m[1].y, m[1].button, m[1].buttons], ['mousedown', 100, 120, 0, 1], 'mousedown');
  const moves = m.slice(2, -1);
  eq(moves.length, 8, 'number of drag moves');
  ok(moves.every(e => e.t === 'mousemove' && e.buttons === 1 && e.trusted), 'every drag move has the left button held: ' + JSON.stringify(moves));
  moves.forEach((e, i) => ok(Math.abs(e.x - (100 + 50 * (i + 1))) <= 1 && Math.abs(e.y - (120 + 37.5 * (i + 1))) <= 1, 'drag move ' + i + ' at ' + e.x + ',' + e.y));
  const up = m[m.length - 1];
  eq([up.t, up.x, up.y, up.button, up.buttons], ['mouseup', 500, 420, 0, 0], 'mouseup');
  eq(ev.filter(e => e.t === 'pointermove' && e.buttons === 1).length, 8, 'pointermove events during the drag');
  const w = ev.filter(e => e.t === 'wheel');
  eq(w.map(e => [e.x, e.y, e.deltaY, e.trusted]), [[640, 360, -240, true]], 'wheel');
  const img = decodePng(await page.shot('adv-drag.png'));
  eq(img.px(300, 270).slice(0, 3), [255, 221, 68], 'the canvas painted the drag trail (screenshot pixel at the 4th move)');
  eq(page.errors, [], 'page errors');
  return { clicksAfterDrag: ev.filter(e => e.t === 'click').length };
}));

test('mouse details: wheel deltas, right button drag, double click, modifiers, pointer events', () => withPage({}, async page => {
  await page.goto('tools/smoke/adv-input.html');
  await page.waitFor('window.ADV_INPUT_READY');
  await page.mouse('wheel', 640, 360, { deltaY: -240 });
  await page.mouse('wheel', 300, 200, { deltaY: 100, deltaX: 30 });
  await page.mouse('wheel', 10, 10);
  await page.wait(60);
  let ev = await page.eval('EV.splice(0)');
  const w = ev.filter(e => e.t === 'wheel');
  eq(w.map(e => [e.x, e.y, e.deltaX, e.deltaY, e.deltaMode, e.trusted]), [[640, 360, 0, -240, 0, true], [300, 200, 30, 100, 0, true], [10, 10, 0, 100, 0, true]], 'wheel events');

  await page.drag(200, 200, 260, 240, { steps: 3, button: 'right' });
  await page.wait(60);
  ev = await page.eval('EV.splice(0)');
  eq(ev.filter(e => e.t === 'mousedown').map(e => [e.button, e.buttons]), [[2, 2]], 'right mousedown');
  ok(ev.filter(e => e.t === 'mousemove' && e.buttons === 2).length === 3, 'right-drag moves');
  eq(ev.filter(e => e.t === 'contextmenu').length, 1, 'contextmenu');
  eq(ev.filter(e => e.t === 'pointerdown').map(e => e.pointerType), ['mouse'], 'pointerdown from mouse');

  await page.mouse('click', 50, 60, { clickCount: 2 });
  await page.key('down', 'ShiftLeft');
  await page.mouse('click', 70, 80);
  await page.key('up', 'ShiftLeft');
  await page.mouse('click', 90, 100, { button: 'middle' });
  await page.wait(60);
  ev = await page.eval('EV.splice(0)');
  eq(ev.filter(e => e.t === 'dblclick').map(e => [e.x, e.y]), [[50, 60]], 'double click');
  eq(ev.filter(e => e.t === 'click').map(e => [e.x, e.y, e.shift]), [[50, 60, false], [50, 60, false], [70, 80, true]], 'clicks (a middle click is auxclick, not click)');
  eq(ev.filter(e => e.t === 'mousedown').map(e => e.button), [0, 0, 0, 1], 'buttons pressed');
  let bad = null;
  try { await page.mouse('poke', 1, 1); } catch (e) { bad = e; }
  ok(bad && /unknown type/.test(bad.message), 'unknown mouse type must throw');
  eq(page.errors, [], 'page errors');
}));

// ------------------------------------------------------------------------------------------------ keyboard

test('keyboard events', () => withPage({}, async page => {
  await page.goto('tools/smoke/adv-input.html');
  await page.waitFor('window.ADV_INPUT_READY');
  await page.key('press', 'KeyW');
  await page.key('press', 'KeyW', { shift: true });
  await page.key('press', 'Space');
  await page.key('press', 'Escape');
  await page.key('press', 'ArrowLeft');
  await page.key('press', 'Digit1');
  await page.key('press', 'F2');
  await page.key('press', '+');
  await page.key('press', 'NumpadAdd');
  await page.key('press', 'F5');                 // the fixture prevents the reload, as the game must
  await page.key('down', 'KeyD');
  await page.key('down', 'KeyD');                // auto-repeat
  await page.key('up', 'KeyD');
  await page.key('down', 'ControlLeft');
  await page.key('press', 'KeyS');
  await page.key('up', 'ControlLeft');
  await page.wait(60);
  const ev = await page.eval('EV.filter(e => e.t.startsWith("key"))');
  const downs = ev.filter(e => e.t === 'keydown').map(e => [e.code, e.key, e.shift, e.ctrl, e.repeat]);
  eq(downs, [['KeyW', 'w', false, false, false], ['KeyW', 'W', true, false, false], ['Space', ' ', false, false, false], ['Escape', 'Escape', false, false, false],
    ['ArrowLeft', 'ArrowLeft', false, false, false], ['Digit1', '1', false, false, false], ['F2', 'F2', false, false, false], ['Equal', '+', true, false, false],
    ['NumpadAdd', '+', false, false, false], ['F5', 'F5', false, false, false], ['KeyD', 'd', false, false, false], ['KeyD', 'd', false, false, true],
    ['ControlLeft', 'Control', false, true, false], ['KeyS', 's', false, true, false]], 'keydown events');
  eq(ev.filter(e => e.t === 'keyup').length, 13, 'keyup events');
  ok(ev.every(e => e.trusted), 'key events are trusted');
  eq(ev.find(e => e.code === 'KeyW').keyCode, 87, 'legacy keyCode');
  eq(ev.find(e => e.code === 'NumpadAdd').location, 3, 'numpad location');
  ok(await page.eval('window.ADV_INPUT_READY && EV.length > 0'), 'page was not reloaded by F5');
  let bad = null;
  try { await page.key('press', 'NoSuchKey'); } catch (e) { bad = e; }
  ok(bad && /unknown key/.test(bad.message), 'unknown key must throw');
  eq(page.errors, [], 'page errors');
}));

// ------------------------------------------------------------------------------------------------ eval

test('page.eval round trips: objects, arrays, numbers, undefined, promises, rejections', () => withPage({}, async page => {
  await page.goto('tools/smoke/adv-never.html');
  eq(await page.eval('({ a: 1, b: "two", c: [3, { d: null }], e: true })'), { a: 1, b: 'two', c: [3, { d: null }], e: true }, 'object');
  eq(await page.eval('{ a: 1, b: { c: 2 } }'), { a: 1, b: { c: 2 } }, 'bare object literal');
  eq(await page.eval('[1, "x", [2, 3], { k: [] }, null]'), [1, 'x', [2, 3], { k: [] }, null], 'array');
  eq(await page.eval('[]'), [], 'empty array');
  eq(await page.eval('({})'), {}, 'empty object');
  ok((await page.eval('42')) === 42, 'integer');
  ok((await page.eval('-0.125')) === -0.125, 'float');
  ok((await page.eval('1e21')) === 1e21, 'big number');
  ok(Number.isNaN(await page.eval('NaN')), 'NaN');
  ok((await page.eval('1 / 0')) === Infinity && (await page.eval('-1 / 0')) === -Infinity, 'Infinity');
  ok(Object.is(await page.eval('-0'), -0), 'negative zero');
  ok((await page.eval('undefined')) === undefined, 'undefined');
  ok((await page.eval('void 0')) === undefined, 'void 0');
  ok((await page.eval('window.noSuchProperty')) === undefined, 'missing property');
  ok((await page.eval('let advX = 5;')) === undefined, 'statement without a value');
  ok((await page.eval('(function () {})()')) === undefined, 'function returning nothing');
  ok((await page.eval('null')) === null, 'null');
  ok((await page.eval('"str"')) === 'str' && (await page.eval('""')) === '' && (await page.eval('false')) === false, 'string / empty string / false');
  eq(await page.eval('new Float32Array([1.5, 2, -3])'), [1.5, 2, -3], 'typed array');
  eq(await page.eval('new Map([["a", 1]])'), [['a', 1]], 'Map');
  eq(await page.eval('new Set([1, 2])'), [1, 2], 'Set');
  eq(await page.eval('({ n: NaN, u: undefined, f() {}, big: 10n })'), { n: null, big: '10n' }, 'NaN / undefined / function / bigint inside an object');
  ok(typeof (await page.eval('document.body')) === 'string', 'DOM node');
  eq(await page.eval('const o = { name: "loop" }; o.self = o; o'), { name: 'loop', self: '[Circular]' }, 'cycle');
  eq(await page.eval('Promise.resolve({ v: [1, 2] })'), { v: [1, 2] }, 'resolved promise');
  eq(await page.eval('new Promise(r => setTimeout(() => r(7), 120))'), 7, 'promise resolved later');
  eq(await page.eval('await new Promise(r => requestAnimationFrame(() => r("raf")))'), 'raf', 'top-level await');
  eq(await page.eval('const q = 6; return q * 7;'), 42, 'top-level return');
  eq(await page.eval('"x".repeat(3000000).length'), 3000000, 'long computation result');
  eq((await page.eval('"y".repeat(3000000)')).length, 3000000, '3 MB string by value');
  eq((await page.eval('Array.from({ length: 200000 }, (_, i) => i)')).length, 200000, '200k element array');
  eq(await page.eval('"snow ☃ and 🌍 — ünïcödé"'), 'snow ☃ and 🌍 — ünïcödé', 'unicode');

  const rejects = async (js, re, what) => {
    let err = null;
    try { await page.eval(js); } catch (e) { err = e; }
    ok(err, what + ': did not reject');
    ok(err.name === 'PageError', what + ': error name ' + err.name);
    ok(re.test(err.message), what + ': message ' + err.message);
    return err;
  };
  await rejects('Promise.reject(new Error("adv rejected"))', /adv rejected/, 'rejected promise (Error)');
  await rejects('Promise.reject("plain string reason")', /plain string reason/, 'rejected promise (string)');
  await rejects('Promise.reject({ code: 7 })', /"code":7/, 'rejected promise (object)');
  await rejects('new Promise((_, rej) => setTimeout(() => rej(new RangeError("late rejection")), 80))', /RangeError: late rejection/, 'promise rejected later');
  await rejects('(async () => { await 0; throw new TypeError("async throw"); })()', /TypeError: async throw/, 'async function that throws');
  await rejects('throw new Error("sync throw")', /sync throw/, 'synchronous throw');
  await rejects('noSuchFunction()', /noSuchFunction is not defined/, 'ReferenceError');
  await rejects('this is not javascript', /SyntaxError/, 'SyntaxError');
  await page.wait(150);
  eq(page.errors, [], 'failed evals must not be added to page.errors');
  ok((await page.eval('1 + 1')) === 2, 'page still works after the failures');
  const t0 = Date.now();
  for (let i = 0; i < 200; i++) await page.eval('1');
  return { evalRoundTripMs: +((Date.now() - t0) / 200).toFixed(3) };
}));

// ------------------------------------------------------------------------------------------------ run

(async () => {
  const before = edgePids();
  const beforeProfiles = profiles();
  const t0 = Date.now();
  let passed = 0, failed = 0;
  for (const t of tests) {
    if (filter && !t.name.includes(filter)) continue;
    const s = Date.now();
    try {
      const r = await t.fn();
      passed++;
      console.log('PASS  ' + t.name + '  (' + (Date.now() - s) + ' ms)' + (r === undefined || r === null ? '' : '\n      ' + JSON.stringify(r)));
    } catch (e) {
      failed++;
      console.log('FAIL  ' + t.name + '  (' + (Date.now() - s) + ' ms)\n      ' + String(e && e.stack || e).replace(/\n/g, '\n      '));
    }
  }
  // leftovers: every msedge.exe that was not there before, and every temp profile that was not there before
  await sleep(1500);
  let extra = edgePids().filter(p => !before.includes(p));
  for (let i = 0; i < 20 && extra.length; i++) { await sleep(500); extra = edgePids().filter(p => !before.includes(p)); }
  const extraProfiles = profiles().filter(p => !beforeProfiles.includes(p));
  const clean = extra.length === 0 && extraProfiles.length === 0;
  console.log((clean ? 'PASS' : 'FAIL') + '  no leftover msedge.exe or profile  (msedge before: ' + before.length + ', after: ' + edgePids().length +
    (clean ? '' : ', new pids: ' + extra.join(',') + ', new profiles: ' + extraProfiles.join(',')) + ')');
  if (clean) passed++; else failed++;
  console.log('adv-robust: ' + passed + ' passed, ' + failed + ' failed, ' + (Date.now() - t0) + ' ms');
  process.exitCode = failed ? 1 : 0;
})();
