// Tests for js/core/events.js (FOUNDATION §3): DM.Bus.
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')({ only: [] });      // foundation only: no sim modules needed
const Bus = DM.Bus;

// Run fn with console.error captured; returns the captured calls (each an array of arguments).
function captureErrors(fn) {
  const calls = [], orig = console.error;
  console.error = function () { calls.push(Array.prototype.slice.call(arguments)); };
  try { fn(); } finally { console.error = orig; }
  return calls;
}
// Every test starts from an empty bus and leaves it empty.
function busTest(name, fn) {
  T.test(name, () => {
    Bus.clear();
    try { fn(); } finally { Bus.clear(); }
  });
}

busTest('Bus: API shape', () => {
  for (const k of ['on', 'off', 'emit', 'clear']) T.eq(typeof Bus[k], 'function', k);
  const un = Bus.on('x', () => {});
  T.eq(typeof un, 'function', 'on returns an unsubscribe function');
  T.eq(Bus.emit('nobody-listens', { a: 1 }), undefined, 'emit with no handlers is a no-op');
  T.eq(Bus.emit('x'), undefined);
  Bus.off('never-subscribed', () => {});
  Bus.off('x', () => {});
  T.ok(true, 'off of an unknown type / handler does not throw');
});

busTest('Bus: handlers receive (payload, type), synchronously, in subscription order', () => {
  const log = [];
  Bus.on('quake', (p, t) => log.push(['a', p, t]));
  Bus.on('quake', (p, t) => log.push(['b', p, t]));
  Bus.on('quake', (p, t) => log.push(['c', p, t]));
  const payload = { cell: 42, power: 3 };
  Bus.emit('quake', payload);
  T.eq(log.length, 3, 'handlers ran before emit returned');
  T.eq(log.map(e => e[0]), ['a', 'b', 'c']);
  for (const e of log) { T.ok(e[1] === payload, 'same payload object'); T.eq(e[2], 'quake'); }
  log.length = 0;
  Bus.emit('quake');
  T.eq(log.map(e => e[1]), [undefined, undefined, undefined], 'payload is optional');
  log.length = 0;
  Bus.emit('quake', 0);
  Bus.emit('quake', null);
  Bus.emit('quake', 'text');
  T.eq(log.filter(e => e[0] === 'a').map(e => e[1]), [0, null, 'text'], 'any payload value');
});

busTest('Bus: types are independent', () => {
  const log = [];
  Bus.on('a', () => log.push('a'));
  Bus.on('b', () => log.push('b'));
  Bus.on('toString', () => log.push('toString'));
  Bus.on('__proto__', () => log.push('proto'));
  Bus.emit('a'); Bus.emit('b'); Bus.emit('c'); Bus.emit('a');
  T.eq(log, ['a', 'b', 'a']);
  Bus.emit('constructor'); Bus.emit('hasOwnProperty');
  T.eq(log.length, 3, 'object-prototype names are ordinary event types');
  Bus.emit('toString'); Bus.emit('__proto__');
  T.eq(log.slice(3), ['toString', 'proto']);
});

busTest('Bus: unsubscribe function and off()', () => {
  const log = [];
  const a = () => log.push('a'), b = () => log.push('b');
  const unA = Bus.on('e', a);
  Bus.on('e', b);
  Bus.emit('e');
  T.eq(log, ['a', 'b']);
  T.eq(unA(), undefined);
  Bus.emit('e');
  T.eq(log, ['a', 'b', 'b']);
  unA(); unA();
  Bus.emit('e');
  T.eq(log, ['a', 'b', 'b', 'b'], 'unsubscribing twice is harmless');
  Bus.off('e', b);
  Bus.emit('e');
  T.eq(log.length, 4, 'off removes the handler');
  Bus.off('e', b);
  // off only affects the named type
  Bus.on('e', a); Bus.on('f', a);
  Bus.off('f', a);
  Bus.emit('e'); Bus.emit('f');
  T.eq(log.length, 5);
  // a stale unsubscribe does not remove a later subscription of the same function
  const un1 = Bus.on('g', b);
  un1();
  Bus.on('g', b);
  un1();
  log.length = 0;
  Bus.emit('g');
  T.eq(log, ['b'], 'stale unsubscribe leaves the new subscription alone');
});

busTest('Bus: the same function can subscribe more than once', () => {
  let n = 0;
  const h = () => n++;
  const un1 = Bus.on('e', h);
  Bus.on('e', h);
  Bus.emit('e');
  T.eq(n, 2, 'called once per subscription');
  un1();
  Bus.emit('e');
  T.eq(n, 3, 'the unsubscribe function removes only its own subscription');
  Bus.on('e', h); Bus.on('e', h);
  Bus.off('e', h);
  Bus.emit('e');
  T.eq(n, 3, 'off removes every subscription of that function');
});

busTest('Bus: "*" receives every event with (payload, type)', () => {
  const log = [];
  const un = Bus.on('*', (p, t) => log.push([t, p]));
  Bus.emit('birth', 1);
  Bus.emit('war', 2);
  Bus.emit('unheard-of', 3);
  T.eq(log, [['birth', 1], ['war', 2], ['unheard-of', 3]]);
  un();
  Bus.emit('birth', 4);
  T.eq(log.length, 3, 'wildcard unsubscribes like any handler');
  const h = () => log.push('x');
  Bus.on('*', h);
  Bus.off('*', h);
  Bus.emit('birth', 5);
  T.eq(log.length, 3, 'off("*", fn)');
  // emitting the literal type '*' reaches wildcard handlers exactly once
  let stars = 0;
  Bus.on('*', (p, t) => { stars++; T.eq(t, '*'); });
  Bus.emit('*', null);
  T.eq(stars, 1);
});

busTest('Bus: wildcard and typed handlers interleave in true subscription order', () => {
  const log = [];
  Bus.on('e', () => log.push('typed 1'));
  Bus.on('*', () => log.push('star 1'));
  Bus.on('e', () => log.push('typed 2'));
  Bus.on('other', () => log.push('other'));
  Bus.on('*', () => log.push('star 2'));
  Bus.on('e', () => log.push('typed 3'));
  Bus.emit('e');
  T.eq(log, ['typed 1', 'star 1', 'typed 2', 'star 2', 'typed 3']);
  log.length = 0;
  Bus.emit('other');
  T.eq(log, ['star 1', 'other', 'star 2']);
  log.length = 0;
  Bus.emit('none');
  T.eq(log, ['star 1', 'star 2']);
});

busTest('Bus: an exception in a handler is reported and does not stop the others or the emitter', () => {
  const log = [], boom = new Error('boom');
  Bus.on('e', () => log.push('before'));
  Bus.on('e', () => { log.push('thrower'); throw boom; });
  Bus.on('*', () => { throw new TypeError('wildcard boom'); });
  Bus.on('e', () => log.push('after'));
  Bus.on('*', (p, t) => log.push('star ' + t));
  let returned = false;
  const errors = captureErrors(() => { Bus.emit('e', 7); returned = true; });
  T.ok(returned, 'emit did not throw');
  T.eq(log, ['before', 'thrower', 'after', 'star e'], 'every other handler ran, in order');
  T.eq(errors.length, 2, 'one console.error per failing handler');
  T.ok(errors[0].indexOf(boom) >= 0, 'the error object is passed to console.error');
  T.ok(errors[0].some(a => typeof a === 'string' && a.indexOf('"e"') >= 0), 'the report names the event type');
  T.ok(errors[1].some(a => a instanceof TypeError));
  // the failing handler stays subscribed and the bus keeps working
  log.length = 0;
  const again = captureErrors(() => Bus.emit('e'));
  T.eq(log, ['before', 'thrower', 'after', 'star e']);
  T.eq(again.length, 2);
  // throwing non-Error values is handled too
  Bus.clear();
  Bus.on('odd', () => { throw 'a string'; });
  Bus.on('odd', () => { throw undefined; });
  Bus.on('odd', () => log.push('survived'));
  const odd = captureErrors(() => Bus.emit('odd'));
  T.eq(odd.length, 2);
  T.eq(log[log.length - 1], 'survived');
  // no console.error at all when nothing throws
  T.eq(captureErrors(() => { Bus.emit('odd-free'); Bus.clear(); Bus.on('fine', () => {}); Bus.emit('fine'); }).length, 0);
});

busTest('Bus: an exception inside a nested emit stays contained', () => {
  const log = [];
  Bus.on('outer', () => { log.push('outer 1'); Bus.emit('inner'); log.push('outer 1 resumed'); });
  Bus.on('inner', () => { throw new Error('inner boom'); });
  Bus.on('inner', () => log.push('inner 2'));
  Bus.on('outer', () => log.push('outer 2'));
  const errors = captureErrors(() => Bus.emit('outer'));
  T.eq(log, ['outer 1', 'inner 2', 'outer 1 resumed', 'outer 2']);
  T.eq(errors.length, 1);
});

busTest('Bus: a handler may unsubscribe itself during emit', () => {
  const log = [];
  let unB;
  Bus.on('e', () => log.push('a'));
  unB = Bus.on('e', () => { log.push('b'); unB(); });
  Bus.on('e', () => log.push('c'));
  const once = () => { log.push('once'); Bus.off('e', once); };
  Bus.on('e', once);
  Bus.on('e', () => log.push('d'));
  Bus.emit('e');
  T.eq(log, ['a', 'b', 'c', 'once', 'd'], 'nobody is skipped or called twice');
  log.length = 0;
  Bus.emit('e');
  T.eq(log, ['a', 'c', 'd'], 'self-removed handlers are gone afterwards');
});

busTest('Bus: a handler may unsubscribe others during emit', () => {
  const log = [];
  const a = () => log.push('a');
  const c = () => log.push('c');
  const e = () => log.push('e');
  Bus.on('x', a);
  Bus.on('x', () => { log.push('b removes a, c and e'); Bus.off('x', a); Bus.off('x', c); unE(); });
  Bus.on('x', c);
  Bus.on('x', () => log.push('d'));
  const unE = Bus.on('x', e);
  Bus.on('x', () => log.push('f'));
  Bus.emit('x');
  // a had already run; c and e were removed before their turn and are not called
  T.eq(log, ['a', 'b removes a, c and e', 'd', 'f']);
  log.length = 0;
  Bus.emit('x');
  T.eq(log, ['b removes a, c and e', 'd', 'f']);
});

busTest('Bus: handlers added during emit run from the next emit on', () => {
  const log = [];
  let added = false;
  Bus.on('e', () => {
    log.push('a');
    if (!added) { added = true; Bus.on('e', () => log.push('late')); Bus.on('*', () => log.push('late star')); }
  });
  Bus.on('e', () => log.push('b'));
  Bus.emit('e');
  T.eq(log, ['a', 'b'], 'not called in the emit that added them');
  log.length = 0;
  Bus.emit('e');
  T.eq(log, ['a', 'b', 'late', 'late star']);
});

busTest('Bus: a handler can unsubscribe and resubscribe itself during emit', () => {
  let n = 0;
  const h = () => { n++; Bus.off('e', h); Bus.on('e', h); };
  Bus.on('e', h);
  Bus.emit('e');
  T.eq(n, 1, 'no infinite loop, called once');
  Bus.emit('e');
  T.eq(n, 2, 'still subscribed exactly once');
});

busTest('Bus: clear() during emit stops the remaining handlers', () => {
  const log = [];
  Bus.on('e', () => { log.push('a'); Bus.clear(); });
  Bus.on('e', () => log.push('b'));
  Bus.on('*', () => log.push('star'));
  Bus.emit('e');
  T.eq(log, ['a']);
  Bus.emit('e');
  T.eq(log, ['a'], 'nothing is subscribed any more');
});

busTest('Bus: nested emits run depth-first and do not disturb the outer emit', () => {
  const log = [];
  Bus.on('a', (p) => { log.push('a1:' + p); Bus.emit('b', p + 1); log.push('a1 done'); });
  Bus.on('a', (p) => log.push('a2:' + p));
  Bus.on('b', (p) => { log.push('b1:' + p); if (p < 3) Bus.emit('a', p + 10); });
  Bus.on('b', (p) => log.push('b2:' + p));
  Bus.emit('a', 1);
  T.eq(log, ['a1:1', 'b1:2', 'a1:12', 'b1:13', 'b2:13', 'a1 done', 'a2:12', 'b2:2', 'a1 done', 'a2:1']);
  // re-entrant emit of the same type with an unsubscribe in the inner pass
  Bus.clear();
  log.length = 0;
  let depth = 0;
  const victim = () => log.push('victim@' + depth);
  Bus.on('r', () => { if (depth === 0) { depth = 1; Bus.emit('r'); depth = 0; } else Bus.off('r', victim); });
  Bus.on('r', victim);
  Bus.emit('r');
  T.eq(log, [], 'removed in the inner emit, so not called by the outer one either');
});

busTest('Bus: clear() removes everything; the bus is usable afterwards', () => {
  let n = 0;
  const un = Bus.on('a', () => n++);
  Bus.on('b', () => n++);
  Bus.on('*', () => n++);
  Bus.clear();
  Bus.emit('a'); Bus.emit('b'); Bus.emit('c');
  T.eq(n, 0);
  un();
  Bus.on('a', () => n++);
  un();
  Bus.emit('a');
  T.eq(n, 1, 'new subscriptions work, old unsubscribe functions are inert');
  Bus.clear(); Bus.clear();
  Bus.emit('a');
  T.eq(n, 1);
});

busTest('Bus: on() rejects non-functions', () => {
  for (const bad of [undefined, null, 'handler', 42, {}, []]) {
    let err = null;
    try { Bus.on('e', bad); } catch (e) { err = e; }
    T.ok(err instanceof TypeError, 'TypeError for ' + JSON.stringify(bad));
  }
  let reached = false;
  Bus.on('e', () => { reached = true; });
  Bus.emit('e');
  T.ok(reached, 'the bus is intact after rejected subscriptions');
});

busTest('Bus: stress — many handlers unsubscribing themselves and each other mid-emit', () => {
  const U = DM.U, r = U.rng(4242);
  const N = 600, calls = new Int32Array(N), alive = new Uint8Array(N).fill(1), uns = new Array(N);
  const expected = new Int32Array(N);
  let order = [];
  for (let i = 0; i < N; i++) {
    const mode = r.int(0, 3), target = r.int(0, N - 1);
    uns[i] = Bus.on(i % 7 === 0 ? '*' : 'tick', () => {
      calls[i]++;
      order.push(i);
      if (mode === 1) { uns[i](); alive[i] = 0; }                       // remove self
      else if (mode === 2) { uns[target](); alive[target] = 0; }        // remove someone else
    });
  }
  // Model: walk in subscription order; a handler runs iff it is still alive at its turn.
  function simulate(aliveNow) {
    const a = Uint8Array.from(aliveNow), ran = [];
    const q = U.rng(4242);
    const modes = [], targets = [];
    for (let i = 0; i < N; i++) { modes.push(q.int(0, 3)); targets.push(q.int(0, N - 1)); }
    for (let i = 0; i < N; i++) {
      if (!a[i]) continue;
      ran.push(i);
      if (modes[i] === 1) a[i] = 0; else if (modes[i] === 2) a[targets[i]] = 0;
    }
    return { ran, alive: a };
  }
  for (let round = 0; round < 4; round++) {
    const model = simulate(alive);
    order = [];
    Bus.emit('tick', round);
    T.eq(order, model.ran, 'round ' + round + ': exactly the live handlers ran, in subscription order');
    T.eq(Array.from(alive), Array.from(model.alive), 'round ' + round + ': same survivors');
  }
  T.ok(Math.max.apply(null, Array.from(calls)) <= 4, 'nobody was called twice in one emit');
  T.ok(order.length > 0 && order.length < N, 'some handlers survived, some were removed');
});

busTest('Bus: performance sanity — emit does not copy handler lists', () => {
  // 200 handlers, 20,000 emits. A regression to per-emit copying or quadratic behaviour would
  // take seconds; this takes a few milliseconds. (No timing assertion: just must complete.)
  let n = 0;
  for (let i = 0; i < 200; i++) Bus.on('tick', () => { n++; });
  for (let i = 0; i < 20000; i++) Bus.emit('tick', i);
  T.eq(n, 4000000);
  // subscribing / unsubscribing many handlers leaves no residue
  const uns = [];
  for (let i = 0; i < 2000; i++) uns.push(Bus.on('churn', () => { n++; }));
  for (const u of uns) u();
  const before = n;
  Bus.emit('churn');
  T.eq(n, before);
});
