// DEMIURGE — tools/api.js : the public API named in the specs, as data. Used by tests/api.test.js (Node) and
// tests/browser/api.scn.js (page) to catch a missing or misnamed function before integration does.
'use strict';

// namespaces that load in Node (core, sim, pure render/ui files, fixtures)
const node = {
  U: ['clamp', 'sat', 'lerp', 'smooth', 'sstep', 'remap', 'approach', 'damp', 'hash32', 'hash2', 'hashf', 'rng', 'noise3', 'fbm3', 'ridged3', 'b64enc', 'b64dec', 'fmtInt', 'fmtBig'],
  V3: ['create', 'set', 'copy', 'add', 'sub', 'scale', 'addScaled', 'dot', 'cross', 'len', 'dist', 'norm', 'lerp', 'slerp', 'angle', 'rotAxis', 'basis'],
  M4: ['create', 'identity', 'copy', 'mul', 'perspective', 'ortho', 'lookAt', 'invert', 'transformPoint', 'transformDir', 'translation', 'scaling', 'rotationAxis', 'fromBasis'],
  Bus: ['on', 'off', 'emit', 'clear'],
  Geo: ['build', 'get'],
  State: ['create', 'isSea', 'groundR', 'surfaceR', 'serialize', 'deserialize', 'sizeOf', 'newSettlement', 'newCulture', 'newUnit', 'newDisaster', 'freeColorSlot'],
  Names: ['language', 'word', 'place', 'person', 'culture', 'god', 'planet', 'cap'],
  Terrain: ['generate', 'sculpt', 'crater', 'cone', 'roughen', 'touched', 'derive'],
  Climate: ['update', 'stepIce', 'yearly', 'calibrate', 'datum', 'anomaly', 'emit', 'addDust'],
  Hydro: ['update', 'riverT'],
  Life: ['classify', 'capacity', 'step', 'burn', 'ignite', 'seed', 'bloom', 'fill', 'habitability', 'rebuild', 'cover'],
  Path: ['find', 'stepCost', 'reach'],
  Civ: ['found', 'claim', 'rebuild', 'destroy', 'damage', 'capture', 'setCulture', 'electCapital', 'infect', 'cure', 'at', 'near', 'live', 'canSettle', 'siteScore', 'bestSite', 'ageProgress', 'yearly', 'capacity'],
  Society: ['newCulture', 'yearly', 'witness', 'regard', 'raiseProphet', 'setEdict', 'aweIncome', 'rel', 'peek', 'atWar', 'declareWar', 'makePeace', 'onArrive', 'diplomacy'],
  Units: ['spawn', 'step', 'kill', 'near', 'dirOf'],
  Powers: ['unlocked', 'cost', 'can', 'use', 'start', 'update', 'tick', 'natural'],
  Chronicle: ['attach', 'add', 'yearStr', 'epithet', 'updateEpithets', 'snapshot', 'tale'],
  Sim: ['create', 'use', 'rederive', 'tick', 'advance', 'setPhase', 'refreshStats', 'checkEnding', 'score', 'genesis.progress', 'genesis.next', 'genesis.skip'],
  Models: ['get', 'has', 'names'],
  Towns: ['layout'],
  Camera: ['reset', 'setViewport', 'update', 'view', 'orbit', 'zoomBy', 'turn', 'flyTo', 'jump', 'shake', 'busy'],
  Fixtures: ['world', 'populate', 'ending'],
};

// namespaces that exist only in the page
const browser = {
  GL: ['init', 'chunk', 'program', 'mesh', 'instances', 'target', 'depthTarget', 'bindTex', 'toScreen', 'fsTri', 'checkErrors', 'frame.set', 'frame.setComp', 'frame.setMat', 'frame.setStorm', 'frame.upload'],
  Post: ['init', 'resize', 'beginScene', 'run'],
  Planet: ['init', 'build', 'sync', 'drawDepth', 'drawTerrain', 'drawWater', 'groundAt', 'surfaceAt', 'raycast', 'setOverlay', 'showSnapshot', 'cellColor'],
  Sky: ['init', 'update', 'drawBack', 'drawClouds', 'drawAtmosphere'],
  Objects: ['init', 'build', 'sync', 'drawDepth', 'draw', 'anchors', 'showSnapshot'],
  FX: ['init', 'attach', 'update', 'draw', 'spawn'],
  Renderer: ['init', 'setWorld', 'resize', 'frame', 'pick', 'project', 'setBrush', 'flash', 'setQuality', 'showSnapshot', 'capture', 'freeze'],
  Input: ['init', 'update'],
  Icons: ['svg'],
  HUD: ['init', 'update', 'toast'],
  Panels: ['init', 'update', 'openChronicle'],
  Screens: ['init', 'title', 'setup', 'pause', 'settings', 'help', 'ending', 'timelapse', 'card', 'close'],
  Save: ['loadSettings', 'saveSettings', 'write', 'read', 'meta', 'clear'],
  Audio: ['init', 'update', 'play', 'setVolumes', 'suspend', 'renderTest'],
  Music: ['init', 'update', 'stinger'],
  Game: ['boot', 'newWorld', 'continue', 'toTitle', 'save', 'setSpeed', 'togglePause', 'setPaused', 'armPower', 'setBrush', 'select', 'setOverlay', 'hideHud', 'usePower', 'nextPhase', 'setEdict', 'flyToCell', 'showEnding', 'showTimelapse', 'keepPlaying'],
  debug: ['new', 'ff', 'cam', 'camCell', 'use', 'find', 'settle', 'freeze', 'info'],
};

// check(DM, table) -> { missing: ['Ns.fn', …], absent: ['Ns', …] }   (absent = namespace not built yet)
function check(DM, table) {
  const missing = [], absent = [];
  for (const ns in table) {
    const obj = DM[ns];
    if (obj === undefined || obj === null) { absent.push(ns); continue; }
    for (const name of table[ns]) {
      let o = obj; const parts = name.split('.');
      for (let i = 0; i < parts.length && o !== undefined && o !== null; i++) o = o[parts[i]];
      if (typeof o !== 'function') missing.push(ns + '.' + name);
    }
  }
  return { missing, absent };
}

module.exports = { node, browser, check };
