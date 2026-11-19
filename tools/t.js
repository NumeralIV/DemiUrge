// DEMIURGE — tools/t.js : tiny synchronous test helper. Each test FILE runs in its own Node process
// (tools/test.js spawns them), so stubs and Bus handlers cannot leak between files.
//
//   const T = require('../tools/t.js');
//   T.test('name', () => { T.ok(cond); T.eq(a, b); T.near(a, b, 1e-6); T.throws(() => f()); });
//   T.stub(DM.Civ, 'found', (w, cell) => fake);     // or T.stub(DM, 'Civ', { found() {...} }) for a whole missing module;
//                                                   // restored automatically when the current T.test ends
//   T.watchEvents();                                // call once per file: every Bus event emitted during a test is checked
//                                                   // against DM.EVENTS (unknown name, missing or extra payload key => test fails)
// You can also run a test file directly: node tests/foo.test.js
'use strict';
const T = { passed: 0, failed: 0, failures: [], current: '' };

class Fail extends Error {}
let stubs = [];          // [{obj, name, had, old}] for the current test
let fileStubs = [];      // stubs installed outside any test (live until the process exits)
let inTest = false;
let eventErrors = [];
let watching = false;

T.ok = function (cond, msg) { if (!cond) throw new Fail(msg || 'expected truthy value'); };
T.eq = function (a, b, msg) {
  const same = (typeof a === 'object' || typeof b === 'object') ? JSON.stringify(a) === JSON.stringify(b) : a === b;
  if (!same) throw new Fail((msg ? msg + ': ' : '') + 'expected ' + JSON.stringify(b) + ', got ' + JSON.stringify(a));
};
T.near = function (a, b, eps, msg) {
  eps = eps === undefined ? 1e-6 : eps;
  if (!(Math.abs(a - b) <= eps)) throw new Fail((msg ? msg + ': ' : '') + 'expected ' + b + ' ± ' + eps + ', got ' + a);
};
T.throws = function (fn, msg) {
  let threw = false;
  try { fn(); } catch (e) { threw = true; }
  if (!threw) throw new Fail(msg || 'expected function to throw');
};
T.stub = function (obj, name, value) {
  const rec = { obj, name, had: Object.prototype.hasOwnProperty.call(obj, name), old: obj[name] };
  (inTest ? stubs : fileStubs).push(rec);
  obj[name] = value;
  return value;
};
function restore(list) {
  for (let i = list.length - 1; i >= 0; i--) { const r = list[i]; if (r.had) r.obj[r.name] = r.old; else delete r.obj[r.name]; }
  list.length = 0;
}
T.watchEvents = function () {
  const DM = globalThis.DM;
  if (watching || !DM || !DM.Bus || !DM.EVENTS) return;
  watching = true;
  DM.Bus.on('*', (payload, type) => {
    const keys = DM.EVENTS[type];
    if (!keys) { eventErrors.push('unknown event "' + type + '"'); return; }
    const got = payload && typeof payload === 'object' ? Object.keys(payload) : [];
    for (const k of keys) if (got.indexOf(k) < 0) eventErrors.push('event "' + type + '" is missing payload key "' + k + '"');
    for (const k of got) if (keys.indexOf(k) < 0) eventErrors.push('event "' + type + '" has unexpected payload key "' + k + '"');
    if (payload) for (const k of got) { const v = payload[k]; if (typeof v === 'number' && !Number.isFinite(v)) eventErrors.push('event "' + type + '" payload "' + k + '" is ' + v); }
  });
};
T.test = function (name, fn) {
  T.current = name;
  inTest = true; eventErrors = [];
  const t0 = process.hrtime.bigint();
  let err = null;
  try { fn(); } catch (e) { err = e; }
  inTest = false;
  restore(stubs);
  if (!err && eventErrors.length) err = new Fail(Array.from(new Set(eventErrors)).slice(0, 6).join('; '));
  if (!err) {
    T.passed++;
    const ms = Number(process.hrtime.bigint() - t0) / 1e6;
    if (process.env.T_VERBOSE) console.log('  ok   ' + name + ' (' + ms.toFixed(1) + ' ms)');
  } else {
    T.failed++;
    const where = (err.stack || '').split('\n').slice(1, 4).join('\n');
    T.failures.push({ name, message: err.message, where });
    console.log('  FAIL ' + name + '\n       ' + err.message + (err instanceof Fail ? '' : '\n' + where));
  }
};

process.on('exit', () => {
  if (process.env.T_CHILD) process.stdout.write('\n##T ' + JSON.stringify({ passed: T.passed, failed: T.failed }) + '\n');
  else if (T.passed + T.failed > 0) console.log('\n' + T.passed + ' passed, ' + T.failed + ' failed');
  if (T.failed && !process.exitCode) process.exitCode = 1;
});
module.exports = T;
