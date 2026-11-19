// DEMIURGE — js/core/events.js : DM.Bus (FOUNDATION §3). Runs unchanged in Node and browsers.
//
//   DM.Bus.on(type, fn) → unsubscribe()    fn(payload, type); type '*' receives every event
//   DM.Bus.off(type, fn)                   removes every subscription of fn to that type
//   DM.Bus.emit(type, payload)             synchronous, in subscription order
//   DM.Bus.clear()                         remove all handlers
//
// Semantics:
//  - Order is true subscription order: handlers of the emitted type and '*' handlers are
//    interleaved by the moment they subscribed.
//  - A handler that throws is reported with console.error; the remaining handlers still run and
//    emit() itself never throws.
//  - Subscribing or unsubscribing during an emit is safe. A handler removed during an emit
//    (by itself, by another handler, or by clear()) is not called again in that emit; a handler
//    added during an emit is first called on the next emit.
//  - Handlers may emit further events (nested emits run to completion first).
//  - The same function may subscribe more than once; each subscription is called. The function
//    returned by on() removes only its own subscription and is safe to call repeatedly.
(function (DM) {
  'use strict';

  // type → array of { fn, seq, live }. The arrays are immutable: on/off replace them with a new
  // array, so an emit in progress keeps iterating over the list it started with.
  const lists = new Map();
  const EMPTY = [];
  let seq = 0;

  function drop(type, pred) {
    const list = lists.get(type);
    if (!list) return;
    let kept = null;
    for (let i = 0; i < list.length; i++) {
      const e = list[i];
      if (pred(e)) {
        e.live = false;
        if (!kept) kept = list.slice(0, i);
      } else if (kept) kept.push(e);
    }
    if (!kept) return;
    if (kept.length) lists.set(type, kept); else lists.delete(type);
  }

  function on(type, fn) {
    if (typeof fn !== 'function') throw new TypeError('DM.Bus.on("' + String(type) + '"): handler must be a function');
    const entry = { fn: fn, seq: ++seq, live: true };
    const list = lists.get(type);
    lists.set(type, list ? list.concat(entry) : [entry]);
    return function unsubscribe() {
      if (entry.live) drop(type, function (e) { return e === entry; });
    };
  }

  function off(type, fn) {
    drop(type, function (e) { return e.fn === fn; });
  }

  function emit(type, payload) {
    const a = lists.get(type) || EMPTY;
    const b = type === '*' ? EMPTY : (lists.get('*') || EMPTY);
    const na = a.length, nb = b.length;
    let i = 0, j = 0;
    while (i < na || j < nb) {
      const e = (j >= nb || (i < na && a[i].seq < b[j].seq)) ? a[i++] : b[j++];
      if (!e.live) continue;
      try {
        e.fn(payload, type);
      } catch (err) {
        try { console.error('DM.Bus: handler for "' + String(type) + '" threw:', err); } catch (e2) { /* nothing more we can do */ }
      }
    }
  }

  function clear() {
    lists.forEach(function (list) {
      for (let i = 0; i < list.length; i++) list[i].live = false;
    });
    lists.clear();
  }

  DM.Bus = { on: on, off: off, emit: emit, clear: clear };
})(globalThis.DM = globalThis.DM || {});
