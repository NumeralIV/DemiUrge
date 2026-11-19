// DEMIURGE — game.js : the shell. State machine, main loop, sim pacing, saves, and the debug/automation API.
// Exposes DM.Game and DM.debug (see docs/SPEC-UI.md §0).
(function (DM) {
  'use strict';
  const U = DM.U, V3 = DM.V3, Bus = DM.Bus;
  const TICKS_PER_SEC = 8;          // at 1x: 8 ticks = 2 years per real second
  const SIM_BUDGET_MS = 6;          // max sim time per frame
  const AUTOSAVE_SEC = 120;
  const SPEEDS = [0.25, 1, 2, 4, 8];

  const G = {
    state: 'boot', paused: false, speed: 1, power: null, brushAng: 0.06, selected: -1, overlay: 0, hudHidden: false,
    world: null, time: 0, frameMs: 0, simMs: 0, fps: 60, errors: [],
  };
  let acc = 0, autosaveT = 0, last = 0, fade = 1, fadeTo = 0, fadeFn = null, canvas = null;
  let ptrY = 0.5, slowT = 0, slowFrom = 0, lowerT = 0, focusCell = -1;
  const timers = [];
  const titleDir = V3.create(0.6, 0.35, 0.72), Y = [0, 1, 0], tmp = V3.create();
  const now = () => performance.now();
  const has = (m, f) => !!(DM[m] && typeof DM[m][f] === 'function');
  const settings = () => (DM.Save && DM.Save.settings) || {};
  const genesis = w => !!w && w.phase !== 'history' && w.phase !== 'ended';

  function setState(s) {
    if (G.state === s) return;
    const prev = G.state; G.state = s;
    applySun();
    Bus.emit('ui:state', { state: s, prev });
  }
  function fail(where, e) {
    const msg = where + ': ' + (e && e.stack ? e.stack : e);
    if (G.errors.length < 30) G.errors.push(msg);
    console.error(msg);
  }
  function after(seconds, fn) { timers.push({ t: seconds, fn }); }
  function clearTimers() { timers.length = 0; }
  function toast(text, kind) { Bus.emit('ui:toast', { text, kind: kind || 'info' }); }

  // ------------------------------------------------------------------ settings
  function applySun() {
    if (!DM.Renderer || !DM.Renderer.sun) return;
    const follow = G.state === 'title' || G.state === 'timelapse' || genesis(G.world) || settings().sun === 'follow';
    DM.Renderer.sun.mode = follow ? 'follow' : 'orbit';
  }
  G.applySettings = function () {
    const s = settings();
    if (has('Renderer', 'setQuality') && s.quality >= 0) DM.Renderer.setQuality(s.quality);
    applySun();
    document.documentElement.style.setProperty('--ui-scale', String(s.uiScale || 1));
    if (has('Audio', 'setVolumes')) DM.Audio.setVolumes();
  };
  function autoQuality() {
    const s = settings();
    if (!(s.quality < 0)) return;
    let q = 2;
    try {
      const coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
      if (coarse && Math.min(screen.width, screen.height) < 820) q = 0;
      else if (DM.GL && DM.GL.caps && !DM.GL.caps.hdr) q = 1;
    } catch (e) { q = 1; }
    s.quality = q;
    if (has('Save', 'saveSettings')) DM.Save.saveSettings();
  }

  // ------------------------------------------------------------------ world lifecycle
  G.parseSeed = function (v) {
    if (v === undefined || v === null || v === '') return (Math.random() * 4294967296) >>> 0;
    if (typeof v === 'number') return v >>> 0;
    const s = String(v).trim();
    if (/^\d+$/.test(s)) return Number(s) >>> 0;
    let h = 2166136261;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
    return h >>> 0;
  };

  function adopt(world) {
    G.world = world;
    G.power = null; G.selected = -1; G.overlay = 0; G.paused = false; G.speed = 1; acc = 0; autosaveT = 0; slowT = 0; focusCell = -1;
    clearTimers();
    if (has('Planet', 'setOverlay')) DM.Planet.setOverlay(0);
    if (has('Renderer', 'showSnapshot')) DM.Renderer.showSnapshot(null);
    if (has('Renderer', 'setWorld')) DM.Renderer.setWorld(world);
    if (has('Renderer', 'setBrush')) DM.Renderer.setBrush(null, 0, null);
    if (canvas) canvas.classList.remove('armed');
    G.hudHidden = false;
  }
  function setPower(id) {            // no toggle
    if (id && !(DM.Powers && DM.Powers.byId && DM.Powers.byId[id])) id = null;
    if (id && G.world && !DM.Powers.unlocked(G.world, id)) id = null;
    id = id || null;
    if (G.power === id) return;
    G.power = id;
    if (!id && has('Renderer', 'setBrush')) DM.Renderer.setBrush(null, 0, null);
    if (canvas) canvas.classList.toggle('armed', !!id);
    Bus.emit('ui:power', { id });
  }
  function armForPhase(phase) {
    setPower(phase === 'water' ? 'comet' : phase === 'life' ? 'seed' : phase === 'people' ? 'awaken' : null);
    G.setOverlay(phase === 'people' ? 5 : 0);
    applySun();
  }
  function capitalCell(world) {
    let c = -1, best = -1;
    for (const s of world.settlements) if (s && !s.dead && (s.capital ? 1e12 : 0) + s.pop > best) { best = (s.capital ? 1e12 : 0) + s.pop; c = s.cell; }
    return c;
  }
  function startPlay(world, opts) {
    adopt(world);
    const c = (world.phase === 'history' || world.phase === 'ended') ? capitalCell(world) : -1;
    if (c >= 0) { world.geo.dir(c, tmp); DM.Camera.jump(tmp, 0.4, 0); }
    else DM.Camera.jump(titleDir, 0.1, 0);
    setState('play');
    if (DM.Screens) DM.Screens.close();
    armForPhase(world.phase);
    Bus.emit('ui:hud', { hidden: false });
    Bus.emit('ui:speed', { speed: G.speed, paused: G.paused });
    Bus.emit('ui:select', { id: -1 });
    if (opts && opts.fresh && has('Save', 'clear')) DM.Save.clear();
  }

  G.newWorld = function (opts) {
    opts = opts || {};
    G.transition(() => {
      const seed = G.parseSeed(opts.seed);
      const name = opts.name || (DM.Names ? DM.Names.planet(DM.U.rng(seed ^ 0x9e3779b9)) : 'Unnamed');
      const world = DM.Sim.create({ seed, name, L: opts.L || 6, settings: { calamity: opts.calamity === undefined ? 1 : opts.calamity } });
      if (opts.quick) DM.Sim.genesis.skip(world, {});
      startPlay(world, { fresh: true });
      if (opts.quick) { const c = capitalCell(world); if (c >= 0) { G.select(world.settlements.findIndex(s => s && !s.dead && s.cell === c)); G.flyToCell(c, 0.55); after(3.5, () => G.flyToCell(c, 0.3)); } }
    });
  };
  G.continue = function () {
    if (!DM.Save) return;
    DM.Save.read().then(world => {
      if (!world) { toast('The saved world could not be read.', 'bad'); return; }
      G.transition(() => { DM.Sim.use(world); startPlay(world, null); if (world.ending) G.hideHud(false); });
    });
  };
  G.save = function (quiet) {
    if (!DM.Save || !G.world || G.state === 'title' || G.state === 'boot') return Promise.resolve(false);
    if (G.world.phase === 'molten') return Promise.resolve(false);
    return DM.Save.write(G.world).then(ok => { if (!quiet) toast(ok ? 'World saved.' : 'Could not save.', ok ? 'info' : 'bad'); return ok; });
  };
  G.toTitle = function (immediate) {
    const go = () => {
      const world = DM.Sim.create({ seed: 20261117, name: 'Aurel', L: 6, settings: { calamity: 0 } });
      DM.Sim.genesis.skip(world, {});
      adopt(world);
      DM.Camera.jump(titleDir, 0.05, 0);
      setState('title');
      if (DM.Screens) DM.Screens.title();
    };
    if (immediate) go(); else { if (G.state === 'play') G.save(true); G.transition(go); }
  };
  // fade to black, run fn, fade back in
  G.transition = function (fn) { fadeFn = fn; fadeTo = 1; };

  // ------------------------------------------------------------------ controls
  G.setSpeed = function (n) {
    G.speed = SPEEDS.indexOf(n) >= 0 ? n : 1; G.paused = false; slowT = 0;
    Bus.emit('ui:speed', { speed: G.speed, paused: G.paused });
  };
  G.setPaused = function (b) { b = !!b; if (G.paused === b) return; G.paused = b; Bus.emit('ui:speed', { speed: G.speed, paused: G.paused }); if (b) G.save(true); };
  G.togglePause = function () { G.setPaused(!G.paused); };
  G.armPower = function (id) { setPower(id && id === G.power ? null : id); };       // arming the armed power disarms it
  G.setBrush = function (ang) { G.brushAng = U.clamp(ang, 0.03, 0.15); };
  G.select = function (id) { id = (id === undefined || id === null) ? -1 : id; if (G.selected === id) return; G.selected = id; Bus.emit('ui:select', { id }); };
  G.setOverlay = function (mode) {
    mode = (((mode | 0) % 6) + 6) % 6;
    if (G.overlay === mode) return;
    G.overlay = mode; if (has('Planet', 'setOverlay')) DM.Planet.setOverlay(mode); Bus.emit('ui:overlay', { mode });
  };
  G.hideHud = function (b) { b = !!b; if (G.hudHidden === b) return; G.hudHidden = b; Bus.emit('ui:hud', { hidden: b }); };
  G.usePower = function (cell, dt) {
    const w = G.world; if (!w || !G.power || G.state !== 'play' || cell < 0) return false;
    const id = G.power;
    const ok = DM.Powers.use(w, id, cell, { ang: G.brushAng, dt: dt || 0 });
    if (ok && id === 'awaken') setPower(null);
    return ok;
  };
  G.nextPhase = function () { return !!(G.world && DM.Sim.genesis.next(G.world)); };
  G.setEdict = function (cultureId, edict) { return !!(G.world && DM.Society.setEdict(G.world, cultureId, edict)); };
  G.flyToCell = function (cell, zoom) {
    if (!G.world || !(cell >= 0) || cell >= G.world.n) return;
    G.world.geo.dir(cell, tmp); DM.Camera.flyTo(tmp, zoom === undefined ? null : zoom, 1.6);
  };
  G.keepPlaying = function () {
    if (G.state !== 'ending' && G.state !== 'timelapse') return;
    if (has('Renderer', 'showSnapshot')) DM.Renderer.showSnapshot(null);
    setState('play'); G.hideHud(false);
    if (DM.Screens) DM.Screens.close();
  };
  G.showTimelapse = function () {
    if (!G.world || G.world.snapshots.length < 2) return false;
    setState('timelapse'); if (DM.Screens) DM.Screens.timelapse();
    return true;
  };
  G.showEnding = function () {
    if (!G.world || !G.world.ending) return;
    if (has('Renderer', 'showSnapshot')) DM.Renderer.showSnapshot(null);
    setState('ending'); if (DM.Screens) DM.Screens.ending(G.world.ending);
  };

  function onEnding(ending) {
    if (G.state !== 'play' || !G.world) return;
    G.save(true);
    setPower(null); G.select(-1); G.hideHud(true);
    G.speed = 1; G.paused = false; slowT = 0; Bus.emit('ui:speed', { speed: 1, paused: false });
    const w = G.world, s = ending && ending.settlement >= 0 ? w.settlements[ending.settlement] : null;
    if (ending && ending.type === 'ark' && s) {
      w.geo.dir(s.cell, tmp); DM.Camera.flyTo(tmp, 0.6, 2.5);
      after(8.5, () => { w.geo.dir(s.cell, tmp); DM.Camera.flyTo(tmp, 0.15, 3.5); });
      after(12.5, G.showEnding);
    } else {
      DM.Camera.flyTo(DM.Camera.state.focus, 0.0, 6);
      after(7, G.showEnding);
    }
  }

  // ------------------------------------------------------------------ sim pacing
  function runSim(dt) {
    const w = G.world;
    if (!w || G.paused || G.state !== 'play') return;
    acc += dt * TICKS_PER_SEC * (genesis(w) ? 1 : G.speed);
    if (acc < 1) return;
    const t0 = now(); let n = 0;
    while (acc >= 1) {
      DM.Sim.tick(w); acc -= 1; n++;
      if (now() - t0 > SIM_BUDGET_MS) { if (acc > 2) acc = 2; break; }   // cannot keep up: drop time rather than spiral
    }
    G.simMs += ((now() - t0) / n - G.simMs) * 0.05;
  }

  // ------------------------------------------------------------------ main loop
  function frame(ts) {
    requestAnimationFrame(frame);
    let dt = (ts - last) / 1000; last = ts;
    if (!(dt > 0) || dt > 0.25) dt = 1 / 60;
    G.step(dt);
  }
  G.step = function (dt) {
    const t0 = now();
    G.time += dt;
    try {
      // fades / transitions
      fade = U.approach(fade, fadeTo, dt * 2.2);
      if (fadeFn && fade >= 0.999) { const fn = fadeFn; fadeFn = null; try { fn(); } catch (e) { fail('transition', e); } fadeTo = 0; }
      // timers
      for (let i = timers.length - 1; i >= 0; i--) { const tm = timers[i]; tm.t -= dt; if (tm.t <= 0) { timers.splice(i, 1); try { tm.fn(); } catch (e) { fail('timer', e); } } }
      // auto-slow restore
      if (slowT > 0) { slowT -= dt; if (slowT <= 0 && G.speed === 1 && !G.paused && slowFrom > 1) { G.speed = slowFrom; Bus.emit('ui:speed', { speed: G.speed, paused: G.paused }); } }

      const w = G.world, P = DM.Post && DM.Post.set;
      if (P) {
        P.fade = fade;
        const armed = !!G.power && G.state === 'play';
        P.tilt = (settings().tilt === undefined ? 1 : settings().tilt) * (armed ? 0.35 : 1);
        P.focusY = U.damp(P.focusY === undefined ? 0.5 : P.focusY, armed ? ptrY : 0.5, 6, dt);
      }

      if (DM.Input) DM.Input.update(dt);
      if (G.state === 'title' || G.state === 'timelapse' || G.state === 'ending') {
        if (!(DM.Camera.busy && DM.Camera.busy())) {       // slow cinematic drift
          const s = DM.Camera.state;
          V3.rotAxis(tmp, s.focus, Y, dt * (G.state === 'title' ? 0.035 : 0.05));
          DM.Camera.jump(tmp, s.zoom, s.heading);
        }
      }
      runSim(dt);
      if (w && G.state === 'play' && has('Powers', 'update')) DM.Powers.update(w, dt);     // real-time pieces run even while paused

      DM.Camera.update(dt, has('Planet', 'groundAt') ? DM.Planet.groundAt : null);
      if (w) DM.Renderer.frame(DM.Camera.view(), dt);
      if (DM.HUD) DM.HUD.update(dt);
      if (DM.Panels) DM.Panels.update(dt);
      if (has('Audio', 'update')) {
        const cs = DM.Camera.state;
        if (w) focusCell = w.geo.cellAt(cs.focus[0], cs.focus[1], cs.focus[2], focusCell);
        DM.Audio.update(dt, { state: G.state, paused: G.paused, speed: G.speed, zoom01: cs.zoom, focusCell: w ? focusCell : -1, day: dayAtFocus(), world: w });
      }
      // autosave
      if (G.state === 'play' && !G.paused) { autosaveT += dt; if (autosaveT > AUTOSAVE_SEC) { autosaveT = 0; G.save(true); } }
      // automatic quality drop
      lowerT -= dt;
      if (DM.Renderer && DM.Renderer.slow && lowerT <= 0 && settings().quality > 0) {
        lowerT = 20; settings().quality -= 1;
        if (has('Save', 'saveSettings')) DM.Save.saveSettings();
        G.applySettings(); toast('Graphics lowered for smoothness', 'info');
      }
    } catch (e) { fail('frame', e); }
    G.frameMs += (now() - t0 - G.frameMs) * 0.05;
    G.fps += ((dt > 0 ? 1 / dt : 60) - G.fps) * 0.05;
  };
  function dayAtFocus() {
    const sun = DM.Renderer && DM.Renderer.sun && DM.Renderer.sun.dir; if (!sun) return 1;
    const f = DM.Camera.state.focus;
    return U.sstep(-0.15, 0.25, f[0] * sun[0] + f[1] * sun[1] + f[2] * sun[2]);
  }
  function onResize() {
    if (!canvas) return;
    if (has('Camera', 'setViewport')) DM.Camera.setViewport(canvas.clientWidth || window.innerWidth, canvas.clientHeight || window.innerHeight);
    if (has('Renderer', 'resize')) DM.Renderer.resize();
  }

  // ------------------------------------------------------------------ boot
  G.boot = function () { return bootCommon(true); };
  // dev: initialise everything but do not create the title world (tests call DM.debug.new / DM.debug.fixture)
  G.bootBare = function () { return bootCommon(false); };
  G._adoptForDev = function (world) { fadeFn = null; fade = 0; fadeTo = 0; startPlay(world, null); };
  function bootCommon(withTitle) {
    canvas = document.getElementById('view');
    if (has('Save', 'loadSettings')) DM.Save.loadSettings();
    let ok = false;
    try { ok = DM.Renderer.init(canvas); } catch (e) { fail('Renderer.init', e); }
    if (!ok) { document.getElementById('nogl').style.display = 'flex'; return false; }
    autoQuality();
    DM.Camera.reset();
    if (DM.Input) DM.Input.init(canvas);
    if (DM.HUD) DM.HUD.init();
    if (DM.Panels) DM.Panels.init();
    if (DM.Screens) DM.Screens.init();
    G.applySettings();
    onResize();

    Bus.on('ui:settings', G.applySettings);
    Bus.on('camera:shake', p => { if (p && DM.Camera.shake) DM.Camera.shake((p.amount || 0) * (settings().shake === undefined ? 1 : settings().shake)); });
    Bus.on('phase', p => { if (G.state === 'play' && p) armForPhase(p.phase); });
    Bus.on('people:lost', () => { if (G.state === 'play') { setPower('awaken'); G.setOverlay(5); } });
    Bus.on('settlement:founded', p => {
      if (G.state !== 'play' || !p || p.by !== 'awaken') return;
      G.select(p.id); G.setOverlay(0); G.flyToCell(p.cell, 0.55); after(3.5, () => G.flyToCell(p.cell, 0.3));
    });
    Bus.on('settlement:destroyed', p => { if (p && p.id === G.selected) G.select(-1); });
    Bus.on('ending', onEnding);
    Bus.on('chronicle:entry', p => {
      if (G.state !== 'play' || !p || !p.entry || p.entry.weight < 3 || !settings().autoSlow) return;
      if (G.speed >= 4 && !G.paused) { slowFrom = G.speed; G.speed = 1; slowT = 6; Bus.emit('ui:speed', { speed: 1, paused: false }); }
    });

    const wake = () => { if (has('Audio', 'init')) DM.Audio.init(); };
    window.addEventListener('pointerdown', wake, { passive: true });
    window.addEventListener('keydown', wake);
    window.addEventListener('pointermove', e => { const h = window.innerHeight || 1; ptrY = U.sat(e.clientY / h); }, { passive: true });
    window.addEventListener('resize', onResize);
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) { if (G.state === 'play') G.save(true); if (has('Audio', 'suspend')) DM.Audio.suspend(true); }
      else if (has('Audio', 'suspend')) DM.Audio.suspend(false);
    });
    window.addEventListener('beforeunload', () => { if (G.state === 'play') G.save(true); });

    if (withTitle) { G.toTitle(true); fade = 1; fadeTo = 0; }
    else { fade = 0; fadeTo = 0; }
    requestAnimationFrame(frame);
    return true;
  }

  // ------------------------------------------------------------------ debug / automation
  const D = { freeAwe: false };
  D.info = function () {
    const w = G.world;
    return {
      state: G.state, phase: w ? w.phase : null, year: w ? Math.floor(w.year) : 0, age: w ? w.age : 0,
      pop: w ? Math.round(w.stats.pop) : 0,
      settlements: w ? w.settlements.filter(s => s && !s.dead).length : 0,
      cultures: w ? w.cultures.filter(c => c && !c.dead).length : 0,
      units: w ? w.units.length : 0,
      awe: w ? Math.round(w.god.awe) : 0, aweMax: w ? Math.round(w.god.aweMax) : 0,
      seaLevel: w ? +w.seaLevel.toFixed(3) : 0, anomaly: w && has('Climate', 'anomaly') ? +DM.Climate.anomaly(w).toFixed(2) : 0,
      speed: G.speed, paused: G.paused, power: G.power, selected: G.selected, overlay: G.overlay, hudHidden: G.hudHidden,
      screen: DM.Screens ? DM.Screens.current || null : null,
      frameMs: +G.frameMs.toFixed(2), simMs: +G.simMs.toFixed(3), fps: Math.round(G.fps),
      errors: G.errors.slice(-5), ending: w ? w.ending : null,
    };
  };
  D.new = function (o) {
    o = o || {};
    const world = DM.Sim.create({ seed: G.parseSeed(o.seed === undefined ? 7 : o.seed), name: o.name || 'Testworld', L: o.L || 6, settings: { calamity: o.calamity === undefined ? 1 : o.calamity } });
    if (o.quick !== false) DM.Sim.genesis.skip(world, {});
    fadeFn = null; fade = 0; fadeTo = 0;
    startPlay(world, null);
    if (o.years) D.ff(o.years);
    return D.info();
  };
  D.ff = function (years) { DM.Sim.advance(G.world, Math.round(years / DM.Sim.TICK)); return D.info(); };
  D.tick = function (n) { DM.Sim.advance(G.world, n || 1); return D.info(); };
  D.cam = function (latDeg, lonDeg, zoom) {
    const la = latDeg * Math.PI / 180, lo = lonDeg * Math.PI / 180;
    V3.set(tmp, Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo));
    DM.Camera.jump(tmp, zoom === undefined ? DM.Camera.state.zoom : zoom, 0);
  };
  D.camCell = function (cell, zoom) { G.world.geo.dir(cell, tmp); DM.Camera.jump(tmp, zoom === undefined ? DM.Camera.state.zoom : zoom, 0); };
  D.use = function (id, cell, seconds) {
    const w = G.world; let ok = false;
    const def = DM.Powers.byId[id]; if (!def) return false;
    const pay = () => { if (D.freeAwe) w.god.awe = w.god.aweMax; };
    if (def.brush) {
      const n = Math.max(1, Math.round((seconds || 0.5) * 60));
      for (let i = 0; i < n; i++) { pay(); ok = DM.Powers.use(w, id, cell, { ang: G.brushAng, dt: 1 / 60 }) || ok; DM.Powers.update(w, 1 / 60); }
    } else { pay(); ok = DM.Powers.use(w, id, cell, { ang: G.brushAng, dt: 0 }); DM.Powers.update(w, 3); }
    return ok;
  };
  D.find = function (what) {
    const w = G.world, n = w.n; let best = -1, bv = -1e9;
    const live = w.settlements.filter(s => s && !s.dead);
    if (what === 'capital') { for (const s of live) if (s.capital && s.pop > bv) { bv = s.pop; best = s.cell; } if (best >= 0) return best; what = 'city'; }
    if (what === 'city') { for (const s of live) if (s.pop > bv) { bv = s.pop; best = s.cell; } return best; }
    const B = DM.BIOME;
    for (let c = 0; c < n; c += 3) {
      let v = -1e9;
      const sea = w.elev[c] < w.seaLevel;
      if (what === 'coast') v = (!sea && w.coast[c] === 1) ? w.veg[c] + U.hashf(c, 1) : -1e9;
      else if (what === 'forest') v = (!sea && (w.biome[c] === B.FOREST || w.biome[c] === B.RAINFOREST)) ? w.veg[c] + U.hashf(c, 2) * 0.2 : -1e9;
      else if (what === 'peak') v = w.elev[c];
      else if (what === 'sea') v = sea ? (w.seaLevel - w.elev[c]) : -1e9;
      else if (what === 'desert') v = (!sea && w.biome[c] === B.DESERT) ? U.hashf(c, 3) : -1e9;
      if (v > bv) { bv = v; best = c; }
    }
    return best;
  };
  D.settle = function (frames) {
    return new Promise(resolve => { let n = frames || 3; const f = () => { if (--n <= 0) resolve(D.info()); else requestAnimationFrame(f); }; requestAnimationFrame(f); });
  };
  D.freeze = function (t) { if (has('Renderer', 'freeze')) DM.Renderer.freeze(t === undefined ? null : t); };
  D.step = function (n, dt) { for (let i = 0; i < (n || 1); i++) G.step(dt || 1 / 60); return D.info(); };

  DM.Game = G;
  DM.debug = D;
})(globalThis.DM = globalThis.DM || {});
