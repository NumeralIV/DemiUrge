// DEMIURGE — sim.js : DM.Sim, the orchestrator (SPEC-SIM §3.12). It owns time, the phase machine, the tick
// schedule, the genesis flow, the statistics and the endings; every rule of the world lives in another module.
//
//   Sim.TICK = 0.25 (years per tick)      Sim.REAL_PER_TICK = 0.125 (real seconds per tick at 1×)
//   Sim.PHASES                            ['molten', 'water', 'life', 'people', 'history', 'ended'] (phases only move forward)
//   Sim.create({ seed, name, L = 6, settings }) → world     State.create + Terrain.generate, phase 'molten', then Sim.use
//   Sim.use(world) → world                make a new or loaded world the active one: DM.world, Chronicle.attach, Sim.rederive
//   Sim.rederive(world)                   recompute every derived field and transient cache (after create / load / skip)
//   Sim.tick(world)                       advance one tick in any phase (never calls Powers.update)
//   Sim.advance(world, ticks = 1)         per tick: Powers.update(world, REAL_PER_TICK), then Sim.tick. The headless clock.
//   Sim.setPhase(world, phase) → bool     the ONLY writer of world.phase: entry actions, `phase` event, forced snapshot
//   Sim.genesis.progress(world) → { phase, value, canAdvance, hint, stats: { ocean, veg } }
//   Sim.genesis.next(world) → bool        water → life (sea ≥ −3.5), life → people (cover.veg ≥ 0.25)
//   Sim.genesis.skip(world, { sea = 0, cell = -1 }) → bool   straight to 'history' with the first tribe awake
//   Sim.refreshStats(world)               stats.pop / peakPop / believers
//   Sim.checkEnding(world) → ending|null  detects AND sets the ending (or emits people:lost); null once one is set
//   Sim.mood(devotion, love) → 'beloved' | 'feared' | 'forgotten' | 'parting'   the Ark thresholds
//   Sim.score(world) → { total, parts: { pop, ages, cultures, wonders, years, life, ending }, base, mult }
//                                         parts are points and add up to total (`ending` = what the multiplier added)
//
// Tick schedule (literal from the spec; the tick counter is incremented first, so the yearly group runs on whole years):
//   molten              heat −= 1/60; at 0 → setPhase('water')
//   water/life/people   Powers.tick → Life.burn → Life.step(tick % P, P) [P = 2 in 'life', else 8]
//                       → Climate.stepIce when tick % 20 === 10 → one heavy pass
//   history/ended       year += TICK → Powers.tick → Life.burn → Units.step(TICK) → Life.step(tick % 8, 8)
//                       → when (tick − vars.t0) % 4 === 0 the yearly group: Civ.yearly, Society.yearly, Climate.yearly,
//                         Powers.natural, Sim.refreshStats, Chronicle.snapshot, Sim.checkEnding
//                       → Climate.stepIce when tick % 20 === 10 → on non-yearly ticks one heavy pass
//   heavy pass          at most ONE per tick, the first that is due:
//                       (0) the Life.habitability that completes a Life.classify run on the previous tick
//                       (1) need.derive → Terrain.derive
//                       (2) (need.climate and ≥ 8 ticks since the last Climate.update) or ≥ 40 ticks since it → Climate.update
//                       (3) need.hydro and ≥ 8 ticks since the last Hydro.update → Hydro.update
//                       (4) need.life → Life.classify now, Life.habitability on the next tick (not started on the tick
//                           before a yearly tick, so the pair always lands on consecutive ticks)
// A step whose function does not exist yet is skipped, so a partial tree still ticks.
//
// Persisted keys owned here: vars.lifeBoost, vars.t0, vars.sim_empty (year the world was first seen empty),
// vars.sim_lost (people:lost already announced for this extinction). Transient: world._sched, world._ocean.
(function (DM) {
  'use strict';
  const Sim = (DM.Sim = DM.Sim || {});
  const TUNE = (DM.TUNE = DM.TUNE || {});

  TUNE.sim = {
    lifeBoost: 25,        // vars.lifeBoost during the 'life' phase (vegetation spreads fast while the player watches)
    coolTicks: 60,        // ticks the molten world takes to cool
    waterGate: -3.5,      // sea level needed to leave 'water'
    vegGate: 0.25,        // Life.cover().veg needed to leave 'life'
    lifeParts: 2,         // Life.step slices in the 'life' phase
    parts: 8,             // Life.step slices in every other phase
    iceEvery: 20,         // Climate.stepIce period in ticks ...
    icePhase: 10,         // ... on ticks where tick % iceEvery === icePhase
    climateMin: 8,        // least ticks between two Climate.update passes
    climateMax: 40,       // Climate.update runs at least this often even when nothing asked for it
    hydroMin: 8,          // least ticks between two Hydro.update passes
    silenceYears: 50,     // years with no settlement and no settler before the silence ending
    silencePop: 1000,     // peak population a world needs for its extinction to be an ending
    ashYears: 200,        // a nuclear strike this recent makes the silence 'ash'
    skipSeaMin: -6,       // clamp for genesis.skip's sea level
    skipSeaMax: 1.5,
    mood: { belovedDev: 0.35, belovedLove: 0.2, fearedLove: -0.2, forgottenDev: 0.2 },
    score: {
      pop: 100,           // × log10(1 + peak population)
      age: 150,           // per age reached
      culture: 60,        // per living culture
      wonder: 120,        // per standing wonder
      year: 0.1,          // per year survived ...
      yearCap: 5000,      // ... up to this many
      life: 400,          // × vegetation cover (0..1)
      mult: { beloved: 2, parting: 1.6, forgotten: 1.4, feared: 1.3, quiet: 0.6, ash: 0.4 },
    },
  };

  Sim.TICK = 0.25;
  Sim.REAL_PER_TICK = 0.125;
  Sim.PHASES = ['molten', 'water', 'life', 'people', 'history', 'ended'];
  const YEAR_TICKS = 4;            // 1 / Sim.TICK
  const HISTORY = 4;               // index of 'history' in Sim.PHASES

  // ------------------------------------------------------------------ collaborators (looked up at call time)
  function has(ns, name) { const m = DM[ns]; return !!m && typeof m[name] === 'function'; }
  // call DM[ns][name](a, b, c) if it exists; undefined otherwise
  function run(ns, name, a, b, c) {
    const m = DM[ns];
    if (!m || typeof m[name] !== 'function') return undefined;
    return m[name](a, b, c);
  }
  function sched(world) {
    let s = world._sched;
    if (!s) s = world._sched = { climate: -1e9, hydro: -1e9, hab: false };
    return s;
  }
  function coverVeg(world) {
    const c = run('Life', 'cover', world);
    return c && Number.isFinite(c.veg) ? c.veg : 0;
  }
  function anyoneAlive(world) {
    const ss = world.settlements, us = world.units;
    for (let i = 0; i < ss.length; i++) if (ss[i] && !ss[i].dead) return true;
    for (let i = 0; i < us.length; i++) if (us[i] && us[i].type === 'settler') return true;
    return false;
  }

  // ------------------------------------------------------------------ lifecycle
  Sim.create = function (opts) {
    opts = opts || {};
    const world = DM.State.create({ seed: opts.seed, name: opts.name, L: opts.L === undefined ? 6 : opts.L, geo: opts.geo || null });
    if (opts.settings) for (const k in opts.settings) if (opts.settings[k] !== undefined) world.settings[k] = opts.settings[k];
    run('Terrain', 'generate', world);
    return Sim.use(world);
  };

  Sim.use = function (world) {
    DM.world = world;
    run('Chronicle', 'attach');
    Sim.rederive(world);
    return world;
  };

  Sim.rederive = function (world) {
    const begun = Sim.PHASES.indexOf(world.phase) >= HISTORY;
    if (begun && !Number.isFinite(world.vars.seaBase)) run('Climate', 'calibrate', world);
    run('Terrain', 'derive', world);
    run('Climate', 'update', world);
    run('Hydro', 'update', world);
    run('Life', 'classify', world);
    run('Life', 'habitability', world);
    run('Life', 'rebuild', world);
    run('Civ', 'rebuild', world);
    const s = sched(world);
    s.climate = world.tick; s.hydro = world.tick; s.hab = false;
    Sim.refreshStats(world);
  };

  // ------------------------------------------------------------------ phases
  Sim.setPhase = function (world, phase) {
    const prev = world.phase, to = Sim.PHASES.indexOf(phase);
    if (to < 0 || to <= Sim.PHASES.indexOf(prev)) return false;       // unknown, unchanged or backwards
    world.phase = phase;
    const v = world.vars;
    if (phase === 'water') world.heat = 0;
    else if (phase === 'life') v.lifeBoost = TUNE.sim.lifeBoost;
    else if (phase === 'people') v.lifeBoost = 1;
    else if (phase === 'history') {
      v.lifeBoost = 1;
      run('Climate', 'calibrate', world);
      world.year = 0;
      v.t0 = world.tick;
      Sim.refreshStats(world);
    }
    DM.Bus.emit('phase', { phase, prev });
    run('Chronicle', 'snapshot', world, true);
    return true;
  };

  // ------------------------------------------------------------------ the clock
  function heavy(world, beforeYearly) {
    const T = TUNE.sim, s = sched(world), need = world.need, t = world.tick;
    if (s.hab) {
      s.hab = false;
      if (has('Life', 'habitability')) { DM.Life.habitability(world); return; }
    }
    if (need.derive && has('Terrain', 'derive')) { DM.Terrain.derive(world); return; }
    const dc = t - s.climate;
    if (((need.climate && dc >= T.climateMin) || dc >= T.climateMax) && has('Climate', 'update')) {
      s.climate = t; DM.Climate.update(world); return;
    }
    if (need.hydro && t - s.hydro >= T.hydroMin && has('Hydro', 'update')) { s.hydro = t; DM.Hydro.update(world); return; }
    if (need.life && !beforeYearly && has('Life', 'classify')) { DM.Life.classify(world); s.hab = true; }
  }

  Sim.tick = function (world) {
    DM.world = world;
    const T = TUNE.sim, tick = ++world.tick, phase = world.phase;
    if (phase === 'molten') {
      let h = world.heat - 1 / T.coolTicks;
      if (!(h > 1e-6)) h = 0;
      world.heat = h;
      if (h === 0) Sim.setPhase(world, 'water');
      return;
    }
    const hist = phase === 'history' || phase === 'ended';
    if (hist) world.year += Sim.TICK;
    run('Powers', 'tick', world);
    run('Life', 'burn', world);
    if (hist) run('Units', 'step', world, Sim.TICK);
    const P = phase === 'life' ? T.lifeParts : T.parts;
    run('Life', 'step', world, tick % P, P);
    let yearly = false, beforeYearly = false;
    if (hist) {
      const r = (((tick - (world.vars.t0 | 0)) % YEAR_TICKS) + YEAR_TICKS) % YEAR_TICKS;
      yearly = r === 0;
      beforeYearly = r === YEAR_TICKS - 1;
      if (yearly) {
        run('Civ', 'yearly', world);
        run('Society', 'yearly', world);
        run('Climate', 'yearly', world);
        run('Powers', 'natural', world);
        Sim.refreshStats(world);
        run('Chronicle', 'snapshot', world);
        Sim.checkEnding(world);
      }
    }
    if (tick % T.iceEvery === T.icePhase) run('Climate', 'stepIce', world);
    if (!yearly) heavy(world, beforeYearly);
  };

  Sim.advance = function (world, ticks) {
    DM.world = world;
    const n = ticks === undefined ? 1 : Math.max(0, Math.round(ticks));
    for (let i = 0; i < n; i++) {
      run('Powers', 'update', world, Sim.REAL_PER_TICK);
      Sim.tick(world);
    }
  };

  // ------------------------------------------------------------------ genesis
  function oceanShare(world) {
    let o = world._ocean;
    if (!o) o = world._ocean = { tick: -1, sea: 0, v: 0 };
    if (o.tick !== world.tick || o.sea !== world.seaLevel) {
      const elev = world.elev, sea = world.seaLevel, n = world.n;
      let k = 0;
      for (let c = 0; c < n; c++) if (elev[c] < sea) k++;
      o.tick = world.tick; o.sea = sea; o.v = n > 0 ? k / n : 0;
    }
    return o.v;
  }

  Sim.genesis = {};

  Sim.genesis.progress = function (world) {
    const T = TUNE.sim, U = DM.U, phase = world.phase, veg = coverVeg(world);
    let value = 1, canAdvance = false, hint = 'people';
    if (phase === 'molten') { value = U.sat(1 - world.heat); hint = 'cooling'; }
    else if (phase === 'water') {
      value = U.sat((world.seaLevel + 6.5) / 3);
      canAdvance = world.seaLevel >= T.waterGate;
      hint = canAdvance ? 'water-ok' : 'water-more';
    } else if (phase === 'life') {
      value = T.vegGate > 0 ? U.sat(veg / T.vegGate) : 1;
      canAdvance = veg >= T.vegGate;
      hint = canAdvance ? 'life-ok' : 'life-more';
    } else if (phase === 'people') value = 0;
    return { phase, value, canAdvance, hint, stats: { ocean: oceanShare(world), veg } };
  };

  Sim.genesis.next = function (world) {
    const T = TUNE.sim;
    if (world.phase === 'water' && world.seaLevel >= T.waterGate) return Sim.setPhase(world, 'life');
    if (world.phase === 'life' && coverVeg(world) >= T.vegGate) return Sim.setPhase(world, 'people');
    return false;
  };

  // wake the first tribe the way a player would (Powers.use 'awaken' in phase 'people'), falling back to the
  // lifecycle calls themselves when powers.js is not there yet
  function wake(world, cell) {
    if (world.phase !== 'people') Sim.setPhase(world, 'people');
    const valid = Number.isInteger(cell) && cell >= 0 && cell < world.n;
    if (has('Powers', 'use')) {
      let ok = valid && !!DM.Powers.use(world, 'awaken', cell, {});
      if (!ok) {
        const best = run('Civ', 'bestSite', world);
        if (Number.isInteger(best) && best >= 0 && best !== cell) ok = !!DM.Powers.use(world, 'awaken', best, {});
      }
    } else if (has('Civ', 'found') && has('Society', 'newCulture')) {
      let c = valid ? cell : -1;
      if (c < 0) { const best = run('Civ', 'bestSite', world); c = Number.isInteger(best) ? best : -1; }
      if (c >= 0) {
        const culture = DM.Society.newCulture(world, { parent: -1, cell: c, reason: 'awaken' });
        if (culture && DM.Civ.found(world, c, culture.id, 30, 'awaken')) world.god.awe = Math.max(world.god.awe, 60);
      }
    }
    if (world.phase !== 'history') Sim.setPhase(world, 'history');     // nobody woke (no site): history begins empty
  }

  Sim.genesis.skip = function (world, opts) {
    opts = opts || {};
    if (Sim.PHASES.indexOf(world.phase) >= HISTORY) return false;
    DM.world = world;
    const T = TUNE.sim;
    let sea = opts.sea === undefined ? 0 : +opts.sea;
    if (!Number.isFinite(sea)) sea = 0;
    world.heat = 0;
    world.seaLevel = DM.U.clamp(sea, T.skipSeaMin, T.skipSeaMax);
    for (const k in world.need) world.need[k] = true;
    for (const k in world.dirty) world.dirty[k] = true;
    Sim.rederive(world);
    run('Climate', 'calibrate', world);        // ice caps and the sea datum before anything grows (the sea is final)
    run('Life', 'fill', world, 1);
    run('Life', 'classify', world);
    run('Life', 'habitability', world);
    wake(world, opts.cell === undefined ? -1 : opts.cell);
    Sim.refreshStats(world);
    return world.phase === 'history';
  };

  // ------------------------------------------------------------------ statistics, endings, score
  Sim.refreshStats = function (world) {
    const ss = world.settlements, st = world.stats;
    let pop = 0, bel = 0;
    for (let i = 0; i < ss.length; i++) {
      const s = ss[i];
      if (!s || s.dead || !(s.pop > 0)) continue;
      pop += s.pop;
      if (s.devotion > 0) bel += s.pop * s.devotion;
    }
    st.pop = Number.isFinite(pop) ? pop : 0;
    st.believers = Number.isFinite(bel) ? bel : 0;
    if (!(st.peakPop >= st.pop)) st.peakPop = st.pop;
  };

  Sim.mood = function (devotion, love) {
    const m = TUNE.sim.mood;
    if (devotion >= m.belovedDev && love >= m.belovedLove) return 'beloved';
    if (love <= m.fearedLove) return 'feared';
    if (devotion < m.forgottenDev) return 'forgotten';
    return 'parting';
  };

  function setEnding(world, ending) {
    world.ending = ending;
    Sim.setPhase(world, 'ended');
    DM.Bus.emit('ending', world.ending);
    return world.ending;
  }

  Sim.checkEnding = function (world) {
    if (world.ending || world.phase !== 'history') return null;
    const T = TUNE.sim, v = world.vars, ark = v.ark;
    if (ark && typeof ark === 'object') {
      const culture = Number.isInteger(ark.culture) ? ark.culture : -1;
      const settlement = Number.isInteger(ark.settlement) ? ark.settlement : -1;
      const r = run('Society', 'regard', world, culture);
      let mood;
      if (r && Number.isFinite(r.devotion) && Number.isFinite(r.love)) mood = Sim.mood(r.devotion, r.love);
      else if (r && typeof r.mood === 'string' && T.score.mult[r.mood] !== undefined) mood = r.mood;
      else mood = Sim.mood(0, 0);
      return setEnding(world, { type: 'ark', mood, culture, settlement, year: world.year });
    }
    if (anyoneAlive(world)) {
      if (v.sim_empty !== undefined) delete v.sim_empty;
      if (v.sim_lost !== undefined) delete v.sim_lost;
      return null;
    }
    if (!Number.isFinite(v.sim_empty)) v.sim_empty = world.year;
    if (world.stats.peakPop >= T.silencePop) {
      if (world.year - v.sim_empty >= T.silenceYears) {
        const ash = Number.isFinite(v.lastNuke) && world.year - v.lastNuke <= T.ashYears;
        return setEnding(world, { type: 'silence', mood: ash ? 'ash' : 'quiet', culture: -1, settlement: -1, year: world.year });
      }
    } else if (!v.sim_lost) {
      v.sim_lost = true;
      DM.Bus.emit('people:lost', {});
    }
    return null;
  };

  Sim.score = function (world) {
    const S = TUNE.sim.score, st = world.stats, e = world.ending;
    const peak = st.peakPop > 0 ? st.peakPop : 0;
    let cultures = 0, wonders = 0;
    for (const c of world.cultures) if (c && !c.dead) cultures++;
    for (const s of world.settlements) if (s && !s.dead && s.b && s.b.wonder) wonders++;
    const years = Math.max(0, Math.min(e && Number.isFinite(e.year) ? e.year : world.year, S.yearCap));
    const parts = {
      pop: Math.round(S.pop * Math.log10(1 + peak)),
      ages: Math.round(S.age * (world.age > 0 ? world.age : 0)),
      cultures: Math.round(S.culture * cultures),
      wonders: Math.round(S.wonder * wonders),
      years: Math.round(S.year * years),
      life: Math.round(S.life * DM.U.sat(coverVeg(world))),
      ending: 0,
    };
    const base = parts.pop + parts.ages + parts.cultures + parts.wonders + parts.years + parts.life;
    let mult = 1;
    if (e && S.mult[e.mood] !== undefined) mult = S.mult[e.mood];
    const total = Math.round(base * mult);
    parts.ending = total - base;
    return { total, parts, base, mult };
  };
})(globalThis.DM = globalThis.DM || {});
