// DEMIURGE — tools/load.js : load the DOM-free part of the game into Node.
//
//   const DM = require('../tools/load.js')();                       // everything that exists (core, sim, pure render/ui files)
//   const DM = require('../tools/load.js')({ only: ['civ', 'economy'] });
//        core + state.js + schema.js + just the named files. Use this in a module's own tests so that a
//        half-written neighbour (other engineers are editing files next to yours) cannot break your run.
//
// Files load in dependency order. Missing files are skipped. A file that throws while loading is reported
// on stderr and skipped (the rest still load) unless it is one you asked for by name, in which case the
// error is rethrown.
'use strict';
const fs = require('fs');
const path = require('path');
const ROOT = path.resolve(__dirname, '..');

const BASE = [
  'js/core/util.js',
  'js/core/vec.js',
  'js/core/events.js',
  'js/core/geo.js',
  'js/sim/state.js',
  'js/sim/schema.js',
];
const REST = [
  'js/sim/names.js',
  'js/sim/terrain.js',
  'js/sim/climate.js',
  'js/sim/hydro.js',
  'js/sim/life.js',
  'js/sim/path.js',
  'js/sim/civ.js',
  'js/sim/economy.js',
  'js/sim/society.js',
  'js/sim/diplomacy.js',
  'js/sim/units.js',
  'js/sim/powers.js',
  'js/sim/disasters.js',
  'js/sim/chronicle.js',
  'js/sim/sim.js',
  'js/sim/balance.js',
  'js/render/models.js',
  'js/render/towns.js',
  'js/ui/camera.js',
  'dev/fixtures.js',
];
const FILES = BASE.concat(REST);
const done = new Set();

function loadFile(f, strict) {
  if (done.has(f)) return;
  const p = path.join(ROOT, f);
  if (!fs.existsSync(p)) return;            // not written yet
  done.add(f);
  try { require(p); }
  catch (e) {
    if (strict) throw e;
    process.stderr.write('load.js: skipped ' + f + ' (' + (e && e.message) + ')\n');
  }
}

module.exports = function load(opts) {
  for (const f of BASE) loadFile(f, true);
  if (opts && opts.only) {
    const want = opts.only.map(n => String(n).replace(/\.js$/, ''));
    for (const f of REST) { const base = path.basename(f, '.js'); if (want.indexOf(base) >= 0) loadFile(f, true); }
    for (const n of want) if (!REST.some(f => path.basename(f, '.js') === n)) throw new Error('load.js: unknown module "' + n + '"');
  } else {
    for (const f of REST) loadFile(f, false);
  }
  return globalThis.DM;
};
module.exports.FILES = FILES;
module.exports.ROOT = ROOT;
