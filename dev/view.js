// DEMIURGE — dev/view.js : the render test bed behind dev/view.html (SPEC-RENDER §12). URL parameters are listed in view.html.
//
//   window.DV = {
//     world                      the world being shown
//     ready                      false until the world is built and two frames have rendered with every dirty flag consumed
//     cam(latDeg, lonDeg, zoom, headingDeg = 0)   jump the camera (zoom omitted = unchanged)
//     tick(n = 1)                advance the sim with Sim.advance when DM.Sim is loaded → world.tick
//     settle(frames = 3) → Promise   resolves after that many more frames have rendered
//     find(what) → cell          'capital' | 'city' | 'coast' | 'forest' | 'peak' | 'sea' | 'desert' | 'boxes' (the test scene's cluster)
//     pick(x, y) → { hit, cell, pos, dir, t }     Renderer.pick at client coordinates
//     project(pos) → { ok, x, y, depth }          Renderer.project
//     stats() → Renderer.stats + quality, sizes, HDR, MSAA samples, bloom levels, tilt, shadow size, module states, …
//     freeze(t | null)           Renderer.freeze
//     frameMs(n = 30) → { mean, min, max, n }     n frames back to back, each timed around gl.finish()
//     step(dt = 1/60)            render one frame right now (outside requestAnimationFrame)
//     how, notes, errors, frames, params, modules, camera ('builtin' | 'DM.Camera')
//     check.frameOffsets() / allChunks() / badShader() / roundTrip(n) / canonical()     self-tests used by tests/browser/rendercore.scn.js
//   }
//
// The world: DM.Fixtures.world when fixture=1, else Sim.create + genesis.skip (+ years) when DM.Sim is loaded, else a noise planet
// built here from the foundation alone (also the fallback when either of the others throws).
// DM.DevScene (defined here, drawn by the renderer like any module) stands in for what is missing: for DM.Planet a grey test sphere of
// radius 100 with boxes (one white, one emissive) or the world's bare terrain; for DM.Sky an atmosphere halo and optional clouds.
(function (DM) {
  'use strict';

  const U = DM.U, V3 = DM.V3, R = DM.R;
  const DEG = Math.PI / 180;
  const qs = new URLSearchParams(location.search);
  const num = (k, d) => { const v = qs.get(k); const n = v === null || v === '' ? NaN : +v; return Number.isFinite(n) ? n : d; };
  const P = {
    seed: num('seed', 7) >>> 0, L: Math.min(7, Math.max(2, num('L', 6) | 0)), years: Math.max(0, num('years', 0)),
    fixture: qs.get('fixture') === '1', phase: qs.get('phase') || '', zoom: U.sat(num('zoom', 0)), lat: num('lat', 14), lon: num('lon', 24),
    heading: num('heading', 0), sun: qs.get('sun') || (qs.has('sunAngle') ? 'orbit' : 'follow'), sunAngle: num('sunAngle', NaN),
    overlay: num('overlay', 0) | 0, q: Math.min(2, Math.max(0, num('q', 2) | 0)), only: qs.has('only') ? qs.get('only') : null,
    t: num('t', NaN), size: /^(\d+)x(\d+)$/.exec(qs.get('size') || ''), nohdr: qs.get('nohdr') === '1', shadows: qs.get('shadows') !== '0',
    debug: qs.get('debug') === '1', hud: qs.has('hud') ? qs.get('hud') === '1' : !qs.has('t'), cam: qs.get('cam') || '',
    live: qs.get('live') === '1', stand: qs.get('stand') || '', clouds: U.sat(num('clouds', 0)), halo: qs.get('halo') !== '0',
    brush: num('brush', 0), world: qs.get('world') || '',
  };

  const DV = window.DV = {
    world: null, ready: false, paused: false, how: '', notes: [], errors: [], frames: 0, params: P, modules: {}, camera: 'builtin', check: {},
  };
  window.addEventListener('error', e => { DV.errors.push(String(e.message) + ' @ ' + String(e.filename || '').split('/').pop() + ':' + e.lineno); });
  function note(s) { DV.notes.push(s); console.warn('view.js: ' + s); }

  const canvas = document.getElementById('view');
  const hudEl = document.getElementById('dv-hud');
  const RENDER_MODULES = ['Planet', 'Sky', 'Objects', 'FX'];

  // ------------------------------------------------------------------------------------------ worlds
  // A plausible planet from the foundation alone (the same closed forms as tests/helpers/world.js).
  function noiseWorld(seed, L) {
    const w = DM.State.create({ seed, name: 'Noise ' + seed, L });
    const g = w.geo, n = w.n, B = DM.BIOME, off = (seed % 97) * 3.17 + 0.5;
    const raw = new Float32Array(n);
    for (let c = 0; c < n; c++) {
      const x = g.pos[3 * c], y = g.pos[3 * c + 1], z = g.pos[3 * c + 2];
      raw[c] = U.fbm3(x * 1.5 + off, y * 1.5 - off, z * 1.5 + off * 0.5, 5) + 0.3 * U.ridged3(x * 3.1 + off, y * 3.1, z * 3.1 - off, 3) - 0.15;
    }
    const sorted = Float32Array.from(raw).sort();
    const shore = sorted[Math.min(n - 1, Math.floor(n * 0.6))];
    for (let c = 0; c < n; c++) {
      const d = raw[c] - shore;
      w.elev[c] = d >= 0 ? Math.min(8, 0.02 + d * 9) : Math.max(-6.5, d * 12);
    }
    w.seaLevel = 0; w.heat = 0; w.phase = 'history'; w.vars.seaBase = 0;
    for (let c = 0; c < n; c++) {
      let s = 0;
      for (let k = 0; k < g.nbrN[c]; k++) { const d = Math.abs(w.elev[g.nbr[6 * c + k]] - w.elev[c]); if (d > s) s = d; }
      w.slope[c] = s;
    }
    const q = new Int32Array(n); let head = 0, tail = 0;
    for (let c = 0; c < n; c++) { if (w.elev[c] < 0) { w.coast[c] = 0; q[tail++] = c; } else w.coast[c] = 255; }
    while (head < tail) {
      const c = q[head++], d = w.coast[c];
      if (d >= 254) continue;
      for (let k = 0; k < g.nbrN[c]; k++) { const b = g.nbr[6 * c + k]; if (w.coast[b] > d + 1) { w.coast[b] = d + 1; q[tail++] = b; } }
    }
    const cap = [0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0];
    for (let c = 0; c < n; c++) {
      const x = g.pos[3 * c], y = g.pos[3 * c + 1], z = g.pos[3 * c + 2];
      const sea = w.elev[c] < 0, h = Math.max(0, w.elev[c]);
      let t = 29 - 53 * Math.pow(Math.abs(y), 1.5) - 5.5 * h;
      const seaIce = sea && t < -6;
      if (sea && t < -2) t = -2;
      w.temp[c] = t;
      const m = U.sat(0.55 + 0.45 * U.fbm3(x * 2.3 + 11, y * 2.3 + 5, z * 2.3 - 7, 3) - 0.012 * Math.min(60, w.coast[c]));
      w.moist[c] = sea ? 1 : m;
      w.fresh[c] = sea ? 255 : (m > 0.62 ? 1 : 4);
      let b;
      if (sea) b = seaIce ? B.SEAICE : (-w.elev[c] > 0.6 ? B.SEA : B.SHALLOWS);
      else if (t < -9) b = B.ICE;
      else if (h > 4.2 && t < -2) b = B.SNOWPEAK;
      else if (w.slope[c] > 1.1 || h > 4.6) b = B.ROCK;
      else if (h < 0.12 && w.coast[c] <= 1) b = B.BEACH;
      else if (t < -6) b = B.TUNDRA;
      else if (t < 3) b = m > 0.3 ? B.TAIGA : B.TUNDRA;
      else if (m < 0.14) b = B.DESERT;
      else if (m < 0.3) b = t > 18 ? B.SAVANNA : B.GRASSLAND;
      else if (m < 0.6) b = B.FOREST;
      else b = t > 20 ? B.RAINFOREST : B.FOREST;
      w.biome[c] = b;
      w.ice[c] = (b === B.ICE || b === B.SEAICE || b === B.SNOWPEAK) ? 1 : 0;
      w.veg[c] = cap[b] * 0.9;
      w.fauna[c] = sea ? 0.4 : cap[b] * 0.6;
      w.hab[c] = sea || b === B.ICE || b === B.ROCK || b === B.SNOWPEAK ? 0 : U.sat((1 - Math.abs(t - 17) / 22) * (0.25 + 0.75 * U.sat(m * 1.6)) * (1 - U.sat(w.slope[c] / 1.5)));
    }
    for (const k in w.need) w.need[k] = false;
    return w;
  }

  // Strip a finished world back to an earlier genesis phase, so the molten / water / life looks can be seen without playing them.
  function applyPhase(w, phase) {
    const n = w.n, B = DM.BIOME;
    const clear = ['veg', 'ice', 'water', 'farm', 'urban', 'road', 'fire', 'scorch', 'lava', 'fauna', 'wet', 'rad', 'flow'];
    if (phase === 'molten' || phase === 'water') {
      for (const f of clear) if (w[f]) w[f].fill(0);
      w.owner.fill(-1);
      w.heat = phase === 'molten' ? 1 : 0;
      w.seaLevel = phase === 'molten' ? -9 : num('sea', -3);
      for (let c = 0; c < n; c++) w.biome[c] = w.elev[c] < w.seaLevel ? (w.seaLevel - w.elev[c] > 0.6 ? B.SEA : B.SHALLOWS) : B.BARREN;
      w.rivers = [];
    } else if (phase === 'life') {
      for (const f of ['farm', 'urban', 'road', 'fire', 'scorch', 'lava', 'rad']) if (w[f]) w[f].fill(0);
      w.owner.fill(-1);
      w.heat = 0;
      const off = (w.seed % 89) * 0.7;                       // vegetation has spread from a few places only
      for (let c = 0; c < n; c++) {
        const g = w.geo.pos, k = U.sstep(-0.1, 0.25, U.fbm3(g[3 * c] * 1.3 + off, g[3 * c + 1] * 1.3, g[3 * c + 2] * 1.3 - off, 3));
        w.veg[c] *= k;
        if (k < 0.3 && w.elev[c] >= w.seaLevel && w.biome[c] !== B.ICE && w.biome[c] !== B.SNOWPEAK) w.biome[c] = B.BARREN;
      }
    } else return;
    w.phase = phase; w.year = 0; w.age = 0;
    w.settlements = []; w.cultures = []; w.units = []; w.disasters = []; w._fires = null;
    for (const k in w.dirty) w.dirty[k] = true;
  }

  function buildWorld() {
    let w = null;
    if (P.world === 'noise') w = null;
    else if (P.fixture) {
      if (DM.Fixtures && DM.Fixtures.world) {
        try { w = DM.Fixtures.world({ seed: P.seed, L: P.L }); DV.how = 'fixture'; } catch (e) { note('Fixtures.world threw (' + e.message + '): using the noise planet'); w = null; }
      } else note('DM.Fixtures is not built yet: using the noise planet');
    } else if (DM.Sim && DM.Sim.create) {
      try {
        w = DM.Sim.create({ seed: P.seed, name: 'Viewer ' + P.seed, L: P.L });
        if (!P.phase || P.phase === 'history') {
          DM.Sim.genesis.skip(w, {});
          if (P.years > 0) DM.Sim.advance(w, Math.round(P.years / (DM.Sim.TICK || 0.25)));
        } else if (DM.Sim.genesis && DM.Sim.genesis.skip) DM.Sim.genesis.skip(w, {});      // full world first, stripped back below
        DV.how = 'sim';
      } catch (e) { note('the sim could not build a world (' + e.message + '): using the noise planet'); w = null; }
    }
    if (!w) { w = noiseWorld(P.seed, P.L); DV.how = 'noise'; }
    if (P.phase && P.phase !== 'history') applyPhase(w, P.phase);
    for (const k in w.dirty) w.dirty[k] = true;
    return w;
  }

  // ------------------------------------------------------------------------------------------ camera
  const cam = { focus: [1, 0, 0], fwd: [0, 1, 0], zoom: 0, fov: 35 * DEG };
  const cv = {
    eye: new Float32Array(3), target: new Float32Array(3), up: new Float32Array(3), right: new Float32Array(3), focusDir: new Float32Array(3),
    fov: cam.fov, aspect: 16 / 9, near: 1, far: 600, zoom01: 0, altitude: 320,
  };
  const tA = [0, 0, 0], tB = [0, 0, 0];
  let camHint = -1;

  function groundR(dir) {
    if (DM.Planet && typeof DM.Planet.surfaceAt === 'function' && !(DM.Renderer.disabled || {}).Planet) {
      try { const r = DM.Planet.surfaceAt(dir, -1); if (Number.isFinite(r) && r > 50) return r; } catch (e) { /* fall through */ }
    }
    return Dev.groundAt(dir);
  }
  function setFocus(dir, headingRad) {
    V3.norm(cam.focus, dir);
    V3.basis(cam.focus, tA, tB);                             // east, north
    const h = headingRad || 0;
    for (let i = 0; i < 3; i++) cam.fwd[i] = tB[i] * Math.cos(h) + tA[i] * Math.sin(h);
  }
  function orthoFwd() {                                      // keep fwd tangent to the sphere at the focus (parallel transport)
    const d = V3.dot(cam.fwd, cam.focus);
    V3.addScaled(cam.fwd, cam.fwd, cam.focus, -d);
    if (V3.len(cam.fwd) < 1e-5) { V3.basis(cam.focus, tA, tB); V3.copy(cam.fwd, tB); }
    V3.norm(cam.fwd, cam.fwd);
  }
  function builtinView() {
    const w = canvas.clientWidth || 1, h = canvas.clientHeight || 1, aspect = w / h, fov = cam.fov;
    const d0 = 1.08 * (R + 17) / Math.sin(Math.atan(Math.tan(fov / 2) * Math.min(1, aspect)));
    const n = cam.focus, f = cam.fwd;
    orthoFwd();
    const gr = groundR(n);
    const h0 = d0 - R, alt = h0 * Math.pow(6 / h0, cam.zoom);
    const tilt = 58 * DEG * U.smooth((cam.zoom - 0.35) / 0.65);
    const ct = Math.cos(tilt), st = Math.sin(tilt);
    const ux = n[0] * ct - f[0] * st, uy = n[1] * ct - f[1] * st, uz = n[2] * ct - f[2] * st;       // from the target toward the eye
    let re = gr + alt, D = 0;
    for (let it = 0; it < 2; it++) {                         // the eye sits `alt` above the ground beneath it
      D = -gr * ct + Math.sqrt(Math.max(gr * gr * ct * ct - gr * gr + re * re, 0));
      cv.eye[0] = n[0] * gr + ux * D; cv.eye[1] = n[1] * gr + uy * D; cv.eye[2] = n[2] * gr + uz * D;
      re = Math.max(groundR(V3.norm(tA, cv.eye)) + Math.max(alt, 1.5), gr + 1.5);
    }
    V3.scale(cv.target, n, gr);
    for (let i = 0; i < 3; i++) cv.up[i] = f[i] * ct + n[i] * st;
    V3.cross(cv.right, [-ux, -uy, -uz], cv.up);
    V3.norm(cv.right, cv.right);
    V3.copy(cv.focusDir, n);
    const d = V3.len(cv.eye);
    cv.fov = fov; cv.aspect = aspect; cv.zoom01 = cam.zoom;
    cv.altitude = d - groundR(V3.norm(tA, cv.eye));
    cv.near = U.clamp(0.05 * cv.altitude, 0.25, 150);
    cv.far = Math.sqrt(Math.max(d * d - 93 * 93, 0)) + 75;
    return cv;
  }
  function useGameCamera() { return DV.camera === 'DM.Camera' && DM.Camera; }
  function camFail(e) { note('DM.Camera threw (' + (e && e.message) + '): using the built-in camera'); DV.camera = 'builtin'; }
  function currentView(dt) {
    if (useGameCamera()) {
      try {
        DM.Camera.update(dt, groundR);
        const v = DM.Camera.view();
        if (v && v.eye && Number.isFinite(v.eye[0])) {
          if (DM.Camera.state) { V3.copy(cam.focus, DM.Camera.state.focus); cam.zoom = DM.Camera.state.zoom; }
          return v;
        }
        camFail(new Error('view() returned nothing usable'));
      } catch (e) { camFail(e); }
    }
    return builtinView();
  }
  function orbitBy(dx, dy) {
    if (useGameCamera()) { try { DM.Camera.orbit(dx, dy); return; } catch (e) { camFail(e); } }
    const v = builtinView();
    const dist = Math.hypot(v.eye[0] - v.target[0], v.eye[1] - v.target[1], v.eye[2] - v.target[2]);
    const k = 2 * dist * Math.tan(cam.fov / 2) / (canvas.clientHeight || 1) / V3.len(v.target);
    V3.cross(tA, cam.fwd, cam.focus);                        // screen right on the tangent plane
    for (let i = 0; i < 3; i++) cam.focus[i] += (-dx * tA[i] + dy * cam.fwd[i]) * k;
    V3.norm(cam.focus, cam.focus);
    orthoFwd();
  }
  function zoomBy(a) {
    if (useGameCamera()) { try { DM.Camera.zoomBy(a); return; } catch (e) { camFail(e); } }
    cam.zoom = U.sat(cam.zoom + a);
  }
  function turnBy(a) {
    if (useGameCamera()) { try { DM.Camera.turn(a); return; } catch (e) { camFail(e); } }
    V3.rotAxis(cam.fwd, cam.fwd, cam.focus, a);
  }
  DV.cam = function (latDeg, lonDeg, zoom, headingDeg) {
    const la = latDeg * DEG, lo = lonDeg * DEG;
    const dir = [Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo)];
    if (zoom !== undefined && zoom !== null) cam.zoom = U.sat(+zoom);
    setFocus(dir, (headingDeg || 0) * DEG);
    if (useGameCamera()) { try { DM.Camera.jump(dir, cam.zoom, (headingDeg || 0) * DEG); } catch (e) { camFail(e); } }
  };
  DV.camCell = function (cell, zoom) {
    const d = DV.world.geo.dir(cell, [0, 0, 0]);
    DV.cam(Math.asin(U.clamp(d[1], -1, 1)) / DEG, Math.atan2(d[2], d[0]) / DEG, zoom);
  };

  // ------------------------------------------------------------------------------------------ DM.DevScene: the stand-in scene
  const BOX_SITE = [14, 24];                                 // latitude, longitude of the test scene's cluster of boxes
  const NIGHT_SITE = [-8, 204];                              // a patch of tiny emissive boxes on the far side ("city lights")
  const BIOME_COLOR = [
    [0.16, 0.15, 0.12], [0.30, 0.27, 0.18], [0.45, 0.40, 0.26], [0.42, 0.33, 0.18], [0.26, 0.24, 0.09], [0.13, 0.20, 0.06], [0.05, 0.12, 0.04],
    [0.03, 0.10, 0.03], [0.06, 0.11, 0.07], [0.22, 0.22, 0.17], [0.80, 0.82, 0.85], [0.22, 0.20, 0.19], [0.85, 0.85, 0.85], [0.08, 0.12, 0.07],
    [0.20, 0.17, 0.15], [0.75, 0.80, 0.85],
  ];

  const VS = [
    'in vec3 aPos; in vec3 aNormal; in vec4 aColor; in vec4 aData;',
    'uniform vec4 uStand;             // x > 0.5: put the vertex on the sea sphere (radius uPlanet.x + uPlanet.y)',
    'out vec3 vWorld; out vec3 vNormal; out vec4 vColor; out vec4 vData;',
    'void main() {',
    '  vec3 p = aPos, n = aNormal;',
    '  if (uStand.x > 0.5) { n = normalize(aPos); p = n * (uPlanet.x + uPlanet.y); }',
    '  vWorld = p; vNormal = n; vColor = aColor; vData = aData;',
    '  gl_Position = uViewProj * vec4(p, 1.0);',
    '}',
  ].join('\n');
  const VS_DEPTH = [
    'in vec3 aPos;',
    'void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }',
  ].join('\n');
  const FS = [
    '#include <lighting>',
    'uniform vec4 uTint;              // rgb multiplies the vertex colour, a = specular',
    'in vec3 vWorld; in vec3 vNormal; in vec4 vColor; in vec4 vData;',
    'out vec4 oColor;',
    'void main() {',
    '  vec3 n = normalize(vNormal);',
    '  vec3 dir = normalize(vWorld);',
    '  vec3 albedo = vColor.rgb * uTint.rgb;',
    '  if (vData.z > 0.5) {                                   // the test sphere: a faint 10-degree checker and, close up, a 1-unit grid',
    '    float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.z, dir.x);',
    '    vec2 g = vec2(lon, lat) * (18.0 / 3.14159265);',
    '    albedo *= 1.0 + 0.07 * (mod(floor(g.x) + floor(g.y), 2.0) - 0.5);',
    '    vec2 f = vec2(lon * cos(lat), lat) * uPlanet.x;',
    '    vec2 w = fwidth(f);',
    '    vec2 l = smoothstep(vec2(0.0), w * 1.5 + 0.02, abs(fract(f + 0.5) - 0.5));',
    '    albedo *= mix(1.0, 0.72 + 0.28 * min(l.x, l.y), smoothstep(0.35, 0.6, uScreen.z) * (1.0 - smoothstep(0.15, 0.5, max(w.x, w.y))));',
    '  }',
    '  vec3 c = shade(albedo, n, vWorld, max(vData.y, uTint.a), vData.x);',
    '  if (uBrush.w > 0.0) {                                  // the brush ring (the real one is drawn by Planet)',
    '    float ang = acos(clamp(dot(dir, uBrush.xyz), -1.0, 1.0));',
    '    float wd = max(fwidth(ang) * 1.5, uBrush.w * 0.035);',
    '    float ring = 1.0 - smoothstep(wd * 0.5, wd, abs(ang - uBrush.w));',
    '    if (uBrushCol.a < 0.5) {',
    '      vec3 e = normalize(cross(uBrush.xyz, abs(uBrush.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));',
    '      float az = atan(dot(dir, cross(uBrush.xyz, e)), dot(dir, e));',
    '      ring *= step(0.5, fract(az * (12.0 / 6.2831853)));',
    '    }',
    '    c = mix(c, uBrushCol.rgb * 1.2 * uSunDir.w, ring * 0.85);',
    '  }',
    '  oColor = vec4(atmosphere(c, vWorld), 1.0);',
    '}',
  ].join('\n');
  const VS_SHELL = [
    'in vec3 aPos;',
    'uniform vec4 uStand;             // x = shell radius',
    'out vec3 vWorld;',
    'void main() { vWorld = aPos * uStand.x; gl_Position = uViewProj * vec4(vWorld, 1.0); }',
  ].join('\n');
  const FS_HALO = [
    '#include <lighting>',
    'in vec3 vWorld;',
    'out vec4 oColor;',
    'void main() { oColor = vec4(inscatter(uCamPos.xyz, normalize(vWorld - uCamPos.xyz), 1.0e5), 0.0); }',
  ].join('\n');
  const FS_CLOUD = [
    '#include <lighting>',
    'in vec3 vWorld;',
    'out vec4 oColor;',
    'void main() {',
    '  vec3 dir = normalize(vWorld);',
    '  float c = cloudCover(dir);',
    '  if (c * uFx.x < 0.004) discard;',
    '  vec3 toSun = uSunDir.xyz - dir * dot(uSunDir.xyz, dir);            // crude self-shadowing: denser toward the sun = darker',
    '  float c2 = cloudCover(normalize(dir + toSun * 0.012));',
    '  float lit = clamp(0.80 - 0.9 * (c2 - c) - 0.22 * c, 0.42, 1.0) + 0.2 * (1.0 - c);',
    '  oColor = vec4(atmosphere(lightAt(vWorld) * 0.85 * min(lit, 1.0), vWorld), c * uFx.x);',
    '}',
  ].join('\n');

  // The instanced part of the test scene: little houses drawn with GL.instances, using the per-instance layout of SPEC-RENDER §9
  // (aI0 = base position xyz + scale, aI1 = yaw / sway phase / emissive / fade, aI2 = tint rgb + amount, aI3 = burn / snow).
  // Model space: +Y up, origin at the base centre; aData = (emissive mask, sway weight, tint mask, 0).
  const INST_POS = [
    'vec3 dm_instPos(out vec3 ax, out vec3 up, out vec3 az) {',
    '  up = normalize(aI0.xyz);',
    '  vec3 e = cross(vec3(0.0, 1.0, 0.0), up);',
    '  float el = length(e);',
    '  e = el > 1.0e-4 ? e / el : vec3(1.0, 0.0, 0.0);',
    '  vec3 n = cross(up, e);',
    '  float cy = cos(aI1.x), sy = sin(aI1.x);',
    '  ax = e * cy + n * sy;',
    '  az = cross(ax, up);',
    '  return aI0.xyz + (ax * aPos.x + up * aPos.y + az * aPos.z) * (aI0.w * aI1.w);',
    '}',
  ].join('\n');
  const VS_INST = [
    'in vec3 aPos; in vec3 aNormal; in vec4 aColor; in vec4 aData;',
    'in vec4 aI0; in vec4 aI1; in vec4 aI2; in vec4 aI3;',
    'out vec3 vWorld; out vec3 vNormal; out vec4 vColor; out vec4 vData;',
    INST_POS,
    'void main() {',
    '  vec3 ax, up, az;',
    '  vWorld = dm_instPos(ax, up, az);',
    '  vNormal = ax * aNormal.x + up * aNormal.y + az * aNormal.z;',
    '  vColor = vec4(mix(aColor.rgb, aI2.rgb, aData.z * aI2.a), 1.0);',
    '  vData = vec4(aData.x * aI1.z, 0.0, 0.0, 0.0);',
    '  gl_Position = uViewProj * vec4(vWorld, 1.0);',
    '}',
  ].join('\n');
  const VS_INST_DEPTH = [
    'in vec3 aPos; in vec4 aI0; in vec4 aI1;',
    INST_POS,
    'void main() { vec3 ax, up, az; gl_Position = uViewProj * vec4(dm_instPos(ax, up, az), 1.0); }',
  ].join('\n');

  // a house: walls, a gabled roof that takes the instance tint, one window that takes the instance emissive
  function houseModel() {
    const pos = [], nrm = [], col = [], dat = [], idx = [], C = [0, 0.4, 0];
    function face(pts, color, data) {
      const a = pts[0], b = pts[1], c = pts[2], base = pos.length / 3;
      let nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
      let ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
      let nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
      const l = Math.hypot(nx, ny, nz) || 1;
      nx /= l; ny /= l; nz /= l;
      let mx = 0, my = 0, mz = 0;
      for (const p of pts) { mx += p[0]; my += p[1]; mz += p[2]; }
      const flip = (mx / pts.length - C[0]) * nx + (my / pts.length - C[1]) * ny + (mz / pts.length - C[2]) * nz < 0;
      if (flip) { nx = -nx; ny = -ny; nz = -nz; }
      for (const p of pts) { pos.push(p[0], p[1], p[2]); nrm.push(nx, ny, nz); col.push(color[0], color[1], color[2], 1); dat.push(data[0], data[1], data[2], 0); }
      for (let k = 1; k + 1 < pts.length; k++) { if (flip) idx.push(base, base + k + 1, base + k); else idx.push(base, base + k, base + k + 1); }
    }
    const W = [0.62, 0.58, 0.50], RF = [0.30, 0.22, 0.18], x = 0.5, z = 0.4, y0 = -0.15, y1 = 0.6, yr = 1.0;
    face([[x, y0, -z], [x, y0, z], [x, y1, z], [x, y1, -z]], W, [0, 0, 0]);
    face([[-x, y0, z], [-x, y0, -z], [-x, y1, -z], [-x, y1, z]], W, [0, 0, 0]);
    face([[x, y0, z], [-x, y0, z], [-x, y1, z], [x, y1, z]], W, [0, 0, 0]);
    face([[-x, y0, -z], [x, y0, -z], [x, y1, -z], [-x, y1, -z]], W, [0, 0, 0]);
    face([[x, y1, z], [-x, y1, z], [-x, yr, 0], [x, yr, 0]], RF, [0, 0, 1]);          // roof slopes: tint mask 1
    face([[-x, y1, -z], [x, y1, -z], [x, yr, 0], [-x, yr, 0]], RF, [0, 0, 1]);
    face([[x, y1, -z], [x, y1, z], [x, yr, 0]], W, [0, 0, 0]);                         // gables
    face([[-x, y1, z], [-x, y1, -z], [-x, yr, 0]], W, [0, 0, 0]);
    face([[0.28, 0.18, z + 0.01], [-0.28, 0.18, z + 0.01], [-0.28, 0.46, z + 0.01], [0.28, 0.46, z + 0.01]], [0.85, 0.62, 0.25], [1, 0, 0]);   // window: emissive mask 1
    return { aPos: new Float32Array(pos), aNormal: new Float32Array(nrm), aColor: new Float32Array(col), aData: new Float32Array(dat), index: new Uint16Array(idx) };
  }
  const VILLAGE = { east: 18, north: 0, rows: 5, cols: 5, pitch: 1.5 };          // where the instanced houses stand, relative to BOX_SITE
  function fillVillage(inst) {
    const la = BOX_SITE[0] * DEG, lo = BOX_SITE[1] * DEG;
    const n0 = [Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo)], e0 = [0, 0, 0], nn0 = [0, 0, 0];
    V3.basis(n0, e0, nn0);
    const colors = DM.CULTURE_COLORS || [[0.80, 0.35, 0.02], [0.10, 0.45, 0.80], [0, 0.35, 0.18], [0.87, 0.77, 0.06], [0.67, 0.11, 0], [0.60, 0.19, 0.38]];
    const d = inst.data;
    let k = 0;
    for (let r = 0; r < VILLAGE.rows; r++) for (let c = 0; c < VILLAGE.cols; c++, k++) {
      const east = VILLAGE.east + (c - (VILLAGE.cols - 1) / 2) * VILLAGE.pitch + (U.hashf(k, 21) - 0.5) * 0.5;
      const north = VILLAGE.north + (r - (VILLAGE.rows - 1) / 2) * VILLAGE.pitch + (U.hashf(k, 22) - 0.5) * 0.5;
      const p = [0, 0, 0];
      for (let i = 0; i < 3; i++) p[i] = n0[i] * R + e0[i] * east + nn0[i] * north;
      V3.scale(p, V3.norm(p, p), R);
      const tint = colors[k % colors.length], o = 16 * k;
      d[o] = p[0]; d[o + 1] = p[1]; d[o + 2] = p[2]; d[o + 3] = 0.8 + 0.5 * U.hashf(k, 23);              // aI0: base position, scale
      d[o + 4] = U.hashf(k, 24) * U.TAU; d[o + 5] = 0; d[o + 6] = k % 4 === 0 ? 12 : 0; d[o + 7] = 1;      // aI1: yaw, sway phase, emissive, fade
      d[o + 8] = tint[0] * 0.85; d[o + 9] = tint[1] * 0.85; d[o + 10] = tint[2] * 0.85; d[o + 11] = 1;       // aI2: tint, amount
      d[o + 12] = 0; d[o + 13] = 0; d[o + 14] = 0; d[o + 15] = 0;                                           // aI3: burn, snow
    }
    inst.count = k;
  }

  function pushBox(out, site, east, north, w, h, d, yaw, color, emissive, spec) {
    // a box standing on the sphere: its base centre `east`, `north` world units from the site, sunk 0.15 into the ground
    const la = site[0] * DEG, lo = site[1] * DEG;
    const n0 = [Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo)], e0 = [0, 0, 0], nn0 = [0, 0, 0];
    V3.basis(n0, e0, nn0);
    const up = [0, 0, 0];
    for (let i = 0; i < 3; i++) up[i] = n0[i] * R + e0[i] * east + nn0[i] * north;
    V3.norm(up, up);
    const e = [0, 0, 0], nn = [0, 0, 0];
    V3.basis(up, e, nn);
    const ax = [0, 0, 0], az = [0, 0, 0];
    for (let i = 0; i < 3; i++) { ax[i] = e[i] * Math.cos(yaw) + nn[i] * Math.sin(yaw); az[i] = -e[i] * Math.sin(yaw) + nn[i] * Math.cos(yaw); }
    const hw = w / 2, hd = d / 2, y0 = R - 0.15, y1 = R + h;
    const corner = (sx, y, sz) => [up[0] * y + ax[0] * sx * hw + az[0] * sz * hd, up[1] * y + ax[1] * sx * hw + az[1] * sz * hd, up[2] * y + ax[2] * sx * hw + az[2] * sz * hd];
    const faces = [                                          // four corners (counter-clockwise seen from outside) and the normal
      [corner(1, y0, -1), corner(1, y0, 1), corner(1, y1, 1), corner(1, y1, -1), ax],
      [corner(-1, y0, 1), corner(-1, y0, -1), corner(-1, y1, -1), corner(-1, y1, 1), [-ax[0], -ax[1], -ax[2]]],
      [corner(1, y0, 1), corner(-1, y0, 1), corner(-1, y1, 1), corner(1, y1, 1), az],
      [corner(-1, y0, -1), corner(1, y0, -1), corner(1, y1, -1), corner(-1, y1, -1), [-az[0], -az[1], -az[2]]],
      [corner(-1, y1, 1), corner(-1, y1, -1), corner(1, y1, -1), corner(1, y1, 1), up],
    ];
    for (const f of faces) {
      const base = out.pos.length / 3;
      for (let k = 0; k < 4; k++) {
        out.pos.push(f[k][0], f[k][1], f[k][2]);
        out.nrm.push(f[4][0], f[4][1], f[4][2]);
        out.col.push(color[0], color[1], color[2], 1);
        out.dat.push(emissive || 0, spec || 0, 0, 0);
      }
      // wind each quad so its front face looks along the normal
      const a = f[0], b = f[1], c = f[2];
      const cx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
      const cy = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
      const cz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
      if (cx * f[4][0] + cy * f[4][1] + cz * f[4][2] >= 0) out.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
      else out.idx.push(base, base + 2, base + 1, base, base + 3, base + 2);
    }
    return { centre: [up[0] * (R + h / 2), up[1] * (R + h / 2), up[2] * (R + h / 2)], base: [up[0] * R, up[1] * R, up[2] * R], top: [up[0] * (R + h), up[1] * (R + h), up[2] * (R + h)], height: h };
  }

  function testBoxes() {
    const o = { pos: [], nrm: [], col: [], dat: [], idx: [], marks: {} };
    const W = [0.85, 0.85, 0.85], M = o.marks;
    // the cluster the acceptance shots look at
    M.white = pushBox(o, BOX_SITE, -7, 1, 3.2, 3.2, 3.2, 0.3, W, 0, 0);                         // sun-lit white: must not bloom
    M.emissive = pushBox(o, BOX_SITE, 7, 2.5, 2.2, 2.2, 2.2, -0.4, [0.85, 0.36, 0.09], 8, 0);      // emissive: must bloom
    M.pillar = pushBox(o, BOX_SITE, 0, 7, 1.6, 9, 1.6, 0.6, [0.55, 0.52, 0.48], 0, 0);           // tall pillar: long shadow
    pushBox(o, BOX_SITE, 0.5, -9, 9, 0.6, 3, 0.15, [0.45, 0.30, 0.14], 0, 0);         // low slab
    pushBox(o, BOX_SITE, -10, -7, 2.2, 2.2, 2.2, 0.8, [0.60, 0.08, 0.06], 0, 0);
    pushBox(o, BOX_SITE, 10.5, -6.5, 2.2, 2.6, 2.2, 0.2, [0.08, 0.42, 0.10], 0, 0);
    M.glossy = pushBox(o, BOX_SITE, 12, 9, 2.4, 1.8, 2.4, 1.1, [0.07, 0.16, 0.60], 0, 0.6);      // glossy blue: specular glint
    pushBox(o, BOX_SITE, -13, 10, 2, 4, 2, 0.5, [0.80, 0.62, 0.10], 0, 0);
    for (let i = 0; i < 9; i++) {                                                     // a street of house-sized boxes (shadow detail up close)
      const x = -3.2 + (i % 3) * 0.9 + U.hashf(i, 5) * 0.2, z = -3.6 + ((i / 3) | 0) * 0.9 + U.hashf(i, 6) * 0.2;
      pushBox(o, BOX_SITE, x, z, 0.4, 0.3 + 0.25 * U.hashf(i, 7), 0.4, U.hashf(i, 8) * 1.5, [0.55 + 0.3 * U.hashf(i, 9), 0.42, 0.30], 0, 0);
    }
    pushBox(o, BOX_SITE, 2.2, -1.2, 0.5, 1.4, 0.5, 0.2, [0.7, 0.7, 0.66], 0, 0);     // a tower
    M.lamp = pushBox(o, BOX_SITE, 3.4, -3.4, 0.3, 0.3, 0.3, 0.9, [0.85, 0.5, 0.15], 14, 0);   // a lit window-sized emitter
    // pillars round the globe so every view has shadow casters, some of them at the terminator
    for (let la = -60; la <= 60; la += 30) {
      const steps = la === 0 ? 24 : Math.abs(la) === 30 ? 18 : 10;
      for (let k = 0; k < steps; k++) {
        const lo = k * 360 / steps + la * 0.37 + 9;
        const h = 4 + 7 * U.hashf(k, la + 100), s = 2 + 2 * U.hashf(k, la + 200);
        const tone = 0.25 + 0.6 * U.hashf(k, la + 300);
        pushBox(o, [la, lo], 0, 0, s, h, s, U.hashf(k, la + 400) * 1.5, [tone, tone * (0.8 + 0.2 * U.hashf(k, la + 500)), tone * 0.8], 0, 0);
      }
    }
    for (let i = 0; i < 40; i++) {                                                    // night-side lights
      const r = 5 * Math.sqrt(U.hashf(i, 11)), a = U.hashf(i, 12) * U.TAU;
      pushBox(o, NIGHT_SITE, Math.cos(a) * r, Math.sin(a) * r, 0.3, 0.25 + 0.4 * U.hashf(i, 13), 0.3, a, [0.85, 0.55, 0.22], i % 3 ? 14 : 0, 0);
    }
    return o;
  }

  const Dev = DM.DevScene = {
    mode: 'none',                 // 'test' | 'terrain' | 'none': what stands in for DM.Planet
    sky: false,                   // true: DM.Sky is missing, draw the halo (and clouds when `clouds` > 0)
    halo: true, clouds: 0,
    _prog: null, _depth: null, _halo: null, _cloud: null, _ball: null, _boxes: null, _land: null, _unit: null, _shell: null, _world: null,
    _instProg: null, _instDepth: null, _house: null, _inst: null,

    init() {
      const GL = DM.GL;
      Dev._prog = GL.program('dev.stand', VS, FS);
      Dev._depth = GL.program('dev.standDepth', VS_DEPTH, 'void main() {}');
      Dev._halo = GL.program('dev.halo', VS_SHELL, FS_HALO);
      Dev._cloud = GL.program('dev.cloud', VS_SHELL, FS_CLOUD);
      Dev._instProg = GL.program('dev.inst', VS_INST, FS);
      Dev._instDepth = GL.program('dev.instDepth', VS_INST_DEPTH, 'void main() {}');
      Dev._ball = Dev._boxes = Dev._land = Dev._unit = Dev._shell = Dev._house = Dev._inst = null;
      const g = DM.Geo.get(4);
      Dev._shell = GL.mesh({ aPos: g.pos, index: g.tris });
    },

    build(world) {
      const GL = DM.GL, g = world.geo, n = world.n;
      Dev._world = world;
      for (const k of ['_ball', '_boxes', '_land', '_unit', '_inst', '_house']) if (Dev[k]) { Dev[k].free(); Dev[k] = null; }
      if (Dev.mode === 'test') {
        const pos = new Float32Array(3 * n), col = new Float32Array(4 * n), dat = new Float32Array(4 * n);
        for (let i = 0; i < 3 * n; i++) pos[i] = g.pos[i] * R;
        for (let c = 0; c < n; c++) { col[4 * c] = col[4 * c + 1] = col[4 * c + 2] = 0.2; col[4 * c + 3] = 1; dat[4 * c + 2] = 1; }
        Dev._ball = GL.mesh({ aPos: pos, aNormal: g.pos, aColor: col, aData: dat, index: g.tris });
        const b = testBoxes();
        DV.marks = b.marks;
        Dev._boxes = GL.mesh({ aPos: new Float32Array(b.pos), aNormal: new Float32Array(b.nrm), aColor: new Float32Array(b.col),
          aData: new Float32Array(b.dat), index: new Uint32Array(b.idx) });
        Dev._house = GL.mesh(houseModel());
        Dev._inst = GL.instances(Dev._house, VILLAGE.rows * VILLAGE.cols);
        fillVillage(Dev._inst);
        Dev._inst.upload();
      } else if (Dev.mode === 'terrain') {
        Dev._land = GL.mesh({ aPos: new Float32Array(3 * n), aNormal: new Float32Array(3 * n), aColor: new Float32Array(4 * n),
          aData: new Float32Array(4 * n), index: g.tris, dynamic: ['aPos', 'aNormal', 'aColor', 'aData'] });
        Dev._unit = GL.mesh({ aPos: g.pos, index: g.tris });
        Dev._terrain(world, true, true);
      }
    },

    _terrain(world, shape, colour) {
      const g = world.geo, n = world.n, B = DM.BIOME;
      if (shape) {
        const pos = new Float32Array(3 * n), nrm = new Float32Array(3 * n), tris = g.tris;
        for (let c = 0; c < n; c++) { const r = R + world.elev[c]; pos[3 * c] = g.pos[3 * c] * r; pos[3 * c + 1] = g.pos[3 * c + 1] * r; pos[3 * c + 2] = g.pos[3 * c + 2] * r; }
        for (let t = 0; t < tris.length; t += 3) {
          const a = 3 * tris[t], b = 3 * tris[t + 1], c = 3 * tris[t + 2];
          const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2];
          const vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
          const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
          nrm[a] += nx; nrm[a + 1] += ny; nrm[a + 2] += nz; nrm[b] += nx; nrm[b + 1] += ny; nrm[b + 2] += nz; nrm[c] += nx; nrm[c + 1] += ny; nrm[c + 2] += nz;
        }
        for (let c = 0; c < n; c++) {
          const l = Math.hypot(nrm[3 * c], nrm[3 * c + 1], nrm[3 * c + 2]) || 1;
          nrm[3 * c] /= l; nrm[3 * c + 1] /= l; nrm[3 * c + 2] /= l;
        }
        Dev._land.update('aPos', pos); Dev._land.update('aNormal', nrm);
      }
      if (colour) {
        const col = new Float32Array(4 * n), dat = new Float32Array(4 * n), heat = world.heat || 0;
        for (let c = 0; c < n; c++) {
          const k = BIOME_COLOR[world.biome[c]] || BIOME_COLOR[B.BARREN], v = 0.9 + 0.2 * U.hashf(c, 77);
          let r = k[0] * v, gg = k[1] * v, b = k[2] * v;
          const ice = world.elev[c] >= world.seaLevel ? world.ice[c] : 0;
          r += (0.82 - r) * ice; gg += (0.84 - gg) * ice; b += (0.86 - b) * ice;
          const hot = Math.max(heat, Math.min(1, world.lava[c]));
          if (hot > 0) { const m = hot * (0.35 + 0.65 * U.hashf(c, 78)); r += (0.85 - r) * m; gg += (0.30 - gg) * m; b += (0.06 - b) * m; dat[4 * c] = 7 * m; }
          col[4 * c] = r; col[4 * c + 1] = gg; col[4 * c + 2] = b; col[4 * c + 3] = 1;
        }
        Dev._land.update('aColor', col); Dev._land.update('aData', dat);
      }
    },

    sync(world, dt, dirty) {
      if (Dev.sky) DM.GL.frame.setComp('uFx', 1, Dev.clouds);          // no clouds (and no cloud shadows) unless asked for
      if (Dev.mode === 'terrain' && Dev._land && (dirty.elev || dirty.color)) Dev._terrain(world, dirty.elev, true);
    },

    groundAt(dir) {
      const w = Dev._world;
      if (Dev.mode !== 'terrain' || !w) return R;
      camHint = w.geo.cellAt(dir[0], dir[1], dir[2], camHint);
      return R + Math.max(w.elev[camHint], DM.Renderer.seaLevel);
    },

    drawDepth() {
      if (Dev.mode === 'none') return;
      Dev._depth.use();
      if (Dev._ball) Dev._ball.draw();
      if (Dev._boxes) Dev._boxes.draw();
      if (Dev._land) Dev._land.draw();
      if (Dev._inst) { Dev._instDepth.use(); Dev._inst.draw(); }
    },

    draw() {
      if (Dev.mode === 'none') return;
      const gl = DM.GL.gl, p = Dev._prog.use();
      gl.uniform4f(p.u.uStand, 0, 0, 0, 0);
      gl.uniform4f(p.u.uTint, 1, 1, 1, 0);
      if (Dev._ball) Dev._ball.draw();
      if (Dev._boxes) Dev._boxes.draw();
      if (Dev._land) {
        Dev._land.draw();
        gl.uniform4f(p.u.uStand, 1, 0, 0, 0);                         // the sea: a plain glossy sphere at the displayed sea level
        gl.uniform4f(p.u.uTint, 0.03, 0.10, 0.19, 0.16);
        Dev._unit.draw();
      }
      if (Dev._inst) {
        const q = Dev._instProg.use();
        gl.uniform4f(q.u.uTint, 1, 1, 1, 0);
        Dev._inst.draw();
      }
    },

    drawBlend() {
      if (!Dev.sky) return;
      const GL = DM.GL, gl = GL.gl;
      gl.depthMask(false);
      gl.enable(gl.CULL_FACE);
      if (Dev.clouds > 0) {
        const outside = GL.frame.get('uCamPos')[3] + R > GL.CLOUD_R;
        gl.cullFace(outside ? gl.BACK : gl.FRONT);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        gl.enable(gl.BLEND);
        const p = Dev._cloud.use();
        gl.uniform4f(p.u.uStand, GL.CLOUD_R, 0, 0, 0);
        Dev._shell.draw();
      }
      if (Dev.halo) {
        gl.cullFace(gl.FRONT);
        gl.blendFunc(gl.ONE, gl.ONE);
        gl.enable(gl.BLEND);
        const p = Dev._halo.use();
        gl.uniform4f(p.u.uStand, GL.ATMO_R, 0, 0, 0);
        Dev._shell.draw();
      }
      gl.disable(gl.BLEND);
      gl.disable(gl.CULL_FACE);
      gl.cullFace(gl.BACK);
      gl.depthMask(true);
    },
  };

  // ------------------------------------------------------------------------------------------ the page API
  let settleWaiters = [], cleanFrames = 0, simAcc = 0, lastTs = 0;

  DV.step = function (dt) {
    dt = dt === undefined ? 1 / 60 : dt;
    const w = DV.world;
    if (P.live && w && DM.Sim && DM.Sim.advance && DM.Renderer.frozen === null) {
      simAcc += dt;
      let guard = 0;
      while (simAcc >= 0.125 && guard++ < 4) { simAcc -= 0.125; try { DM.Sim.advance(w, 1); } catch (e) { note('Sim.advance threw: ' + e.message); P.live = false; } }
    }
    if (P.brush) DM.Renderer.setBrush(cam.focus, Math.abs(P.brush), [1.0, 0.85, 0.45, P.brush > 0 ? 1 : 0.35]);
    DM.Renderer.frame(currentView(dt), dt);
    DV.frames++;
    const d = DM.Renderer.dirty;
    cleanFrames = (d.elev || d.color || d.water || d.objects || d.rivers || d.roads) ? 0 : cleanFrames + 1;
    if (!DV.ready && w && cleanFrames >= 2) DV.ready = true;
    if (settleWaiters.length) {
      const keep = [];
      for (const s of settleWaiters) { if (--s.n <= 0) s.resolve(DV.frames); else keep.push(s); }
      settleWaiters = keep;
    }
  };
  DV.settle = function (frames) {
    return new Promise(resolve => { settleWaiters.push({ n: Math.max(1, frames === undefined ? 3 : frames | 0), resolve }); });
  };
  DV.tick = function (n) {
    const w = DV.world;
    if (w && DM.Sim && DM.Sim.advance) DM.Sim.advance(w, Math.max(1, n | 0 || 1));
    return w ? w.tick : 0;
  };
  DV.freeze = function (t) { DM.Renderer.freeze(t === undefined ? null : t); };
  DV.pick = function (x, y) {
    const out = { cell: -1, pos: [0, 0, 0], dir: [0, 0, 0], t: 0 };
    const hit = DM.Renderer.pick(x, y, out);
    return { hit, cell: out.cell, pos: Array.from(out.pos), dir: Array.from(out.dir), t: out.t };
  };
  DV.project = function (pos) {
    const out = { x: 0, y: 0, depth: 0 };
    const ok = DM.Renderer.project(pos, out);
    return { ok, x: out.x, y: out.y, depth: out.depth };
  };
  /** World position at a latitude and longitude in degrees, at radius r (default: the datum sphere). */
  DV.pos = function (latDeg, lonDeg, r) {
    const la = latDeg * DEG, lo = lonDeg * DEG, k = r === undefined ? R : r;
    return [Math.cos(la) * Math.cos(lo) * k, Math.sin(la) * k, Math.cos(la) * Math.sin(lo) * k];
  };
  /** World position `east` and `north` world units from the test scene's cluster of boxes, `h` above the sphere. */
  DV.at = function (east, north, h) {
    const n0 = V3.norm([0, 0, 0], DV.pos(BOX_SITE[0], BOX_SITE[1])), e0 = [0, 0, 0], nn0 = [0, 0, 0], p = [0, 0, 0];
    V3.basis(n0, e0, nn0);
    for (let i = 0; i < 3; i++) p[i] = n0[i] * R + e0[i] * east + nn0[i] * north;
    return V3.scale(p, V3.norm(p, p), R + (h || 0));
  };
  DV.find = function (what) {
    const w = DV.world, n = w.n, B = DM.BIOME;
    if (what === 'boxes') { const la = BOX_SITE[0] * DEG, lo = BOX_SITE[1] * DEG; return w.geo.cellAt(Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo)); }
    let best = -1, bv = -1e9;
    const live = w.settlements.filter(s => s && !s.dead);
    if (what === 'capital') { for (const s of live) if (s.capital && s.pop > bv) { bv = s.pop; best = s.cell; } if (best >= 0) return best; what = 'city'; }
    if (what === 'city') { for (const s of live) if (s.pop > bv) { bv = s.pop; best = s.cell; } return best; }
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
  function moduleStates() {
    const out = {};
    for (const m of RENDER_MODULES) out[m] = DM.Renderer.disabled[m] ? 'disabled' : DM[m] ? 'ok' : (DV.modules[m] === 'excluded' ? 'excluded' : 'missing');
    return out;
  }
  DV.stats = function () {
    const GL = DM.GL, Rn = DM.Renderer, info = DM.Post.info, s = Rn.stats;
    return {
      ms: s.ms, cpu: s.cpu, gpu: s.gpu, draws: s.draws, tris: s.tris, instances: s.instances, particles: s.particles,
      quality: GL.quality, buffer: [canvas.width, canvas.height], css: [canvas.clientWidth, canvas.clientHeight],
      hdr: GL.caps.hdr, samples: info.samples, bloomLevels: info.bloomLevels, tilt: info.tilt, fxaa: info.fxaa,
      shadows: Rn.shadows !== false, shadowSize: Rn.shadowSize, modules: moduleStates(), disabled: Object.assign({}, Rn.disabled), stateErrors: Rn.stateErrors.slice(),
      frames: DV.frames, how: DV.how, camera: DV.camera, stand: Dev.mode, slow: Rn.slow, frozen: Rn.frozen,
      zoom: cam.zoom, sun: Array.from(Rn.sun.dir), sunMode: Rn.sun.mode, seaLevel: Rn.seaLevel, notes: DV.notes.slice(), errors: DV.errors.slice(),
    };
  };
  DV.frameMs = function (n) {
    n = Math.max(1, n | 0 || 30);
    const gl = DM.GL.gl, px = new Uint8Array(4);
    let sum = 0, min = 1e9, max = 0;
    const v = currentView(0);
    for (let i = 0; i < n + 3; i++) {
      const t0 = performance.now();
      DM.Renderer.frame(v, 1 / 60);
      gl.finish();
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);       // forces completion even where finish() is a no-op
      const ms = performance.now() - t0;
      if (i < 3) continue;                                            // warm-up
      sum += ms; if (ms < min) min = ms; if (ms > max) max = ms;
    }
    return { mean: +(sum / n).toFixed(3), min: +min.toFixed(3), max: +max.toFixed(3), n };
  };

  // ------------------------------------------------------------------------------------------ self-tests (used by the browser scenario)
  DV.check.frameOffsets = function () {
    const GL = DM.GL, gl = GL.gl;
    const p = GL.program('dev.checkFrame', GL.FS_VS, 'in vec2 vUv; out vec4 o; void main() { o = vec4(vUv, uTime.x, uStorms[7].w); }');
    p.use();
    const names = GL.frame.names;
    const idx = gl.getUniformIndices(p.id, names);
    const off = gl.getActiveUniforms(p.id, idx, gl.UNIFORM_OFFSET);
    const block = gl.getUniformBlockIndex(p.id, 'Frame');
    const table = {};
    let ok = true;
    for (let i = 0; i < names.length; i++) {
      const cpu = GL.frame.offsets[names[i]] * 4;
      table[names[i]] = [cpu, off[i]];
      if (cpu !== off[i] || idx[i] === gl.INVALID_INDEX) ok = false;
    }
    const size = gl.getActiveUniformBlockParameter(p.id, block, gl.UNIFORM_BLOCK_DATA_SIZE);
    const binding = gl.getActiveUniformBlockParameter(p.id, block, gl.UNIFORM_BLOCK_BINDING);
    return { ok: ok && size === GL.frame.size && binding === 0, table, glSize: size, cpuSize: GL.frame.size, binding };
  };
  DV.check.allChunks = function () {
    const GL = DM.GL, gl = GL.gl;
    const body = [
      '#include <noise>', '#include <clouds>', '#include <lighting>',
      'vec3 dm_all(vec3 p) {',
      '  vec3 n = normalize(p + vec3(0.0, 0.0, 1.0e-3));',
      '  float a = hash13(p) + hash33(p).y + vnoise(p) + fbm3(p) + fbm5(p) + cloudCover(n) + dayAt(p) + sunShadow(p, n) + cloudShade(p);',
      '  return lightAt(p) + shade(vec3(0.5), n, p, 0.5, 1.0) + inscatter(uCamPos.xyz, n, 50.0) + atmosphere(vec3(a), p) + moonDir();',
      '}',
    ].join('\n');
    const vs = body + '\nin vec3 aPos; out vec3 vC; void main() { vC = dm_all(aPos); gl_Position = uViewProj * vec4(aPos, 1.0); }';
    const fs = body + '\nin vec3 vC; out vec4 o; void main() { o = vec4(vC + dm_all(vC), 1.0); }';
    try {
      const p = GL.program('dev.checkChunks', vs, fs);
      p.use();
      const unit = p.u.uShadowMap ? gl.getUniform(p.id, p.u.uShadowMap) : -1;
      const attr = gl.getAttribLocation(p.id, 'aPos');
      gl.useProgram(null);
      return { ok: unit === 7 && attr === 0, shadowUnit: unit, aPos: attr, uniforms: Object.keys(p.u) };
    } catch (e) { return { ok: false, error: String(e.message).slice(0, 3000) }; }
  };
  DV.check.badShader = function () {
    try {
      DM.GL.program('dev.checkBad', DM.GL.FS_VS, 'in vec2 vUv;\nout vec4 o;\nvoid main() {\n  o = vec4(thisIsNotDefined, 1.0);\n}').use();
      return { threw: false };
    } catch (e) {
      const m = String(e.message);
      return { threw: true, hasName: m.indexOf('dev.checkBad') >= 0, hasNumbered: /^\s*\d+: .*thisIsNotDefined/m.test(m), hasLog: /ERROR|error/.test(m), length: m.length, head: m.slice(0, 400) };
    }
  };
  // Renderer.project then Renderer.pick for points of the datum sphere that are in view: the worst distance between a point and its pick
  DV.check.roundTrip = function (count) {
    const rng = U.rng(4242), v = currentView(0), out = { cell: -1, pos: [0, 0, 0], dir: [0, 0, 0], t: 0 }, pr = { x: 0, y: 0, depth: 0 };
    DM.Renderer.frame(v, 0);
    const rect = canvas.getBoundingClientRect();
    let worst = 0, tried = 0, hits = 0, offscreen = 0, hidden = 0;
    const dist = Math.hypot(v.eye[0] - v.target[0], v.eye[1] - v.target[1], v.eye[2] - v.target[2]);
    const spread = Math.min(1.4, 1.1 * dist * Math.tan(v.fov / 2) * Math.max(1, v.aspect) / R);       // about what the screen shows
    for (let i = 0; i < (count || 200) * 20 && tried < (count || 200); i++) {
      const a = rng.range(0, U.TAU), r = spread * Math.sqrt(rng.f());
      V3.basis(v.focusDir, tA, tB);
      const p = [0, 0, 0];
      for (let k = 0; k < 3; k++) p[k] = v.focusDir[k] * Math.cos(r) + (tA[k] * Math.cos(a) + tB[k] * Math.sin(a)) * Math.sin(r);
      V3.scale(p, V3.norm(p, p), R);
      if (!DM.Renderer.project(p, pr)) { hidden++; continue; }
      if (pr.x < 1 || pr.y < 1 || pr.x > rect.width - 1 || pr.y > rect.height - 1) { offscreen++; continue; }
      tried++;
      if (!DM.Renderer.pick(pr.x + rect.left, pr.y + rect.top, out)) continue;
      hits++;
      const d = Math.hypot(out.pos[0] - p[0], out.pos[1] - p[1], out.pos[2] - p[2]);
      if (d > worst) worst = d;
      if (DV.world && out.cell !== DV.world.geo.cellAt(out.pos[0], out.pos[1], out.pos[2])) worst = 1e9;
    }
    return { tried, hits, worst, hidden, offscreen };
  };
  // where the tall pillar's shadow falls on the sphere, and a sun-lit point on its other side (both as client pixels)
  DV.check.shadowProbe = function (dist) {
    const m = DV.marks && DV.marks.pillar, sun = DM.Renderer.sun.dir;
    if (!m) return null;
    const n = V3.norm([0, 0, 0], m.base), t = [0, 0, 0];
    V3.addScaled(t, sun, n, -V3.dot(sun, n));
    V3.norm(t, t);
    const d = dist || 3.5, a = [0, 0, 0], b = [0, 0, 0];
    V3.scale(a, V3.norm(a, V3.addScaled(a, m.base, t, -d)), R);
    V3.scale(b, V3.norm(b, V3.addScaled(b, m.base, t, d)), R);
    return { shadow: DV.project(a), lit: DV.project(b), shadowPos: a, litPos: b, sunElevation: Math.asin(U.clamp(V3.dot(sun, n), -1, 1)) / DEG };
  };
  // n points on a brush ring of angular radius `ang` round unit direction `dir`, as client pixels, at the azimuths the ring
  // shader counts from (so a dashed ring can be checked dash by dash): [{ x, y, ok, az }]
  DV.check.ringProbe = function (dir, ang, n) {
    const b = V3.norm([0, 0, 0], dir), e = [0, 0, 0], f = [0, 0, 0], out = [];
    V3.norm(e, V3.cross(e, b, Math.abs(b[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0]));
    V3.cross(f, b, e);
    for (let i = 0; i < n; i++) {
      const az = (i + 0.5) / n * U.TAU, p = [0, 0, 0];
      for (let k = 0; k < 3; k++) p[k] = (b[k] * Math.cos(ang) + (e[k] * Math.cos(az) + f[k] * Math.sin(az)) * Math.sin(ang)) * R;
      const q = DV.project(p);
      out.push({ x: q.x, y: q.y, ok: q.ok, az });
    }
    return out;
  };
  // what texture unit 7 holds while the stand-in scene draws: nothing in the shadow pass, the shadow map (a depth texture with
  // hardware comparison) in the scene pass
  DV.check.unit7 = function () {
    const gl = DM.GL.gl, seen = {};
    const probe = () => {
      gl.activeTexture(gl.TEXTURE7);
      const tex = gl.getParameter(gl.TEXTURE_BINDING_2D);
      const r = { bound: !!tex, compare: tex ? gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE) === gl.COMPARE_REF_TO_TEXTURE : false,
        fbo: gl.getParameter(gl.FRAMEBUFFER_BINDING) !== null, colorMask: gl.getParameter(gl.COLOR_WRITEMASK)[0], offset: gl.isEnabled(gl.POLYGON_OFFSET_FILL) };
      gl.activeTexture(gl.TEXTURE0);
      return r;
    };
    const d0 = Dev.drawDepth, d1 = Dev.draw;
    Dev.drawDepth = function () { seen.shadowPass = probe(); return d0.call(Dev); };
    Dev.draw = function () { seen.scenePass = probe(); return d1.call(Dev); };
    try { DM.Renderer.frame(currentView(0), 0); } finally { Dev.drawDepth = d0; Dev.draw = d1; }
    seen.glError = gl.getError();
    return seen;
  };
  // the GL state the renderer leaves behind after a frame (should be the canonical state of SPEC-RENDER §4)
  DV.check.canonical = function () {
    const gl = DM.GL.gl;
    DM.Renderer.frame(currentView(0), 0);
    return {
      depthTest: gl.isEnabled(gl.DEPTH_TEST), depthFunc: gl.getParameter(gl.DEPTH_FUNC) === gl.LEQUAL, depthMask: gl.getParameter(gl.DEPTH_WRITEMASK),
      blend: gl.isEnabled(gl.BLEND), cull: gl.isEnabled(gl.CULL_FACE), cullBack: gl.getParameter(gl.CULL_FACE_MODE) === gl.BACK,
      ccw: gl.getParameter(gl.FRONT_FACE) === gl.CCW, scissor: gl.isEnabled(gl.SCISSOR_TEST), vao: gl.getParameter(gl.VERTEX_ARRAY_BINDING) === null,
      polygonOffset: gl.isEnabled(gl.POLYGON_OFFSET_FILL), colorMask: Array.from(gl.getParameter(gl.COLOR_WRITEMASK)), glError: gl.getError(),
    };
  };

  // What the scene target is made of, and the linear scene values (after the MSAA resolve, before bloom and tone-mapping) under
  // the given client pixels: [[x, y], …] → [[r, g, b], …]. Renders one frame first.
  DV.check.hdr = function (points) {
    const GL = DM.GL, gl = GL.gl, Post = DM.Post;
    DM.Renderer.frame(currentView(0), 0);
    const scene = Post.scene, rect = canvas.getBoundingClientRect();
    scene.bind();
    const type = gl.getFramebufferAttachmentParameter(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
    const out = {
      float: type === gl.FLOAT, redBits: gl.getFramebufferAttachmentParameter(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.FRAMEBUFFER_ATTACHMENT_RED_SIZE),
      depthBits: gl.getFramebufferAttachmentParameter(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE),
      samples: gl.getParameter(gl.SAMPLES), w: scene.w, h: scene.h, values: [],
    };
    const tmp = GL.target(scene.w, scene.h, { hdr: true, deep: true });
    scene.blitTo(tmp);
    tmp.bind();
    const f = new Float32Array(4), b = new Uint8Array(4), scale = GL.frame.get('uSunDir')[3] || 1;
    for (const p of points || []) {
      const x = Math.min(scene.w - 1, Math.max(0, Math.floor((p[0] - rect.left) / rect.width * scene.w)));
      const y = Math.min(scene.h - 1, Math.max(0, Math.floor((1 - (p[1] - rect.top) / rect.height) * scene.h)));
      if (out.float) { gl.readPixels(x, y, 1, 1, gl.RGBA, gl.FLOAT, f); out.values.push([f[0], f[1], f[2]]); }
      else { gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, b); out.values.push([b[0] / 255 / scale, b[1] / 255 / scale, b[2] / 255 / scale]); }
    }
    out.glError = gl.getError();
    GL.toScreen();
    tmp.free();
    GL.resetState();
    return out;
  };

  // The GL helpers on their own (meshes, instances, targets, programs, the Frame buffer), verified by reading pixels back from a
  // small offscreen target. Returns { ok, failed: [names], results: { name: 'ok' | 'FAIL: …' } }.
  DV.check.helpers = function () {
    const GL = DM.GL, gl = GL.gl, results = {}, failed = [];
    const ok = (name, cond, detail) => { results[name] = cond ? 'ok' : 'FAIL' + (detail !== undefined ? ': ' + JSON.stringify(detail) : ''); if (!cond) failed.push(name); };
    const near = (a, b, eps) => a.length === b.length && a.every((v, i) => Math.abs(v - b[i]) <= (eps || 0.02));
    while (gl.getError() !== gl.NO_ERROR) { /* start clean */ }
    const T = GL.target(16, 16, { hdr: true });
    const f4 = new Float32Array(4), b4 = new Uint8Array(4);
    const read = (x, y) => {
      T.bind();
      if (T.hdr) { gl.readPixels(x, y, 1, 1, gl.RGBA, gl.FLOAT, f4); return [f4[0], f4[1], f4[2]]; }
      gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, b4); return [b4[0] / 255, b4[1] / 255, b4[2] / 255];
    };
    const clear = () => { T.bind(); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); };
    gl.disable(gl.DEPTH_TEST);
    const ALL = 'in vec3 aPos; in vec3 aNormal; in vec4 aColor; in vec4 aData; in vec4 aI0; in vec4 aI1; in vec4 aI2; in vec4 aI3;\n';
    // every attribute contributes, so a missing one that did not read as its documented constant shows up in the colour
    const pFlat = GL.program('dev.t.flat', ALL + 'out vec3 vC; void main() { vC = aColor.rgb * aNormal.y * aColor.a + aNormal.xzx + aData.xyz + aData.www + aI0.xyz + aI1.xyz + aI2.xyz + aI3.xyz; gl_Position = vec4(aPos.xy, 0.0, 1.0); }',
      'in vec3 vC; out vec4 o; void main() { o = vec4(vC, 1.0); }');
    const pInst = GL.program('dev.t.inst', ALL + 'out vec3 vC; void main() { vC = aI2.rgb * aColor.rgb + aI1.xyz + aI3.xyz; gl_Position = vec4(aPos.xy * aI0.w + aI0.xy, 0.0, 1.0); }',
      'in vec3 vC; out vec4 o; void main() { o = vec4(vC, 1.0); }');
    const quad = (x0, x1) => [x0, -1, 0, x1, -1, 0, x1, 1, 0, x0, -1, 0, x1, 1, 0, x0, 1, 0];
    const WHITE = [1, 1, 1], BLACK = [0, 0, 0];

    // --- GL.mesh
    const m = GL.mesh({ aPos: new Float32Array(quad(-1, 0)), dynamic: ['aPos'] });
    clear(); pFlat.use(); m.draw();
    ok('mesh: missing attributes read as their constants', m.count === 6 && near(read(4, 8), WHITE) && near(read(12, 8), BLACK), [m.count, read(4, 8), read(12, 8)]);
    m.update('aPos', new Float32Array(quad(-1, 0).concat(quad(0, 1))));
    clear(); pFlat.use(); m.draw();
    ok('mesh.update: a larger array reallocates and updates count', m.count === 12 && near(read(4, 8), WHITE) && near(read(12, 8), WHITE), [m.count, read(12, 8)]);
    clear(); pFlat.use(); m.draw(gl.TRIANGLES, 6, 6);
    ok('mesh.draw(mode, count, first)', near(read(4, 8), BLACK) && near(read(12, 8), WHITE), [read(4, 8), read(12, 8)]);
    m.update('aColor', new Float32Array(48).map((v, i) => (i % 4 === 0 || i % 4 === 3) ? 1 : 0));
    clear(); pFlat.use(); m.draw();
    ok('mesh.update: an attribute added later', m.count === 12 && near(read(4, 8), [1, 0, 0]), read(4, 8));
    m.update('aPos', new Float32Array(quad(-1, 0)));
    ok('mesh.update: a smaller array shrinks count', m.count === 6, m.count);
    m.free();
    const mi = GL.mesh({ aPos: new Float32Array([-1, -1, 0, 0, -1, 0, 0, 1, 0, -1, 1, 0, 0, -1, 0, 1, -1, 0, 1, 1, 0, 0, 1, 0]), index: new Uint16Array([0, 1, 2, 0, 2, 3]) });
    clear(); pFlat.use(); mi.draw();
    ok('mesh: indexed (Uint16)', mi.count === 6 && near(read(4, 8), WHITE) && near(read(12, 8), BLACK), [mi.count, read(4, 8), read(12, 8)]);
    mi.setIndex(new Uint32Array([0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]));
    clear(); pFlat.use(); mi.draw();
    const both = near(read(4, 8), WHITE) && near(read(12, 8), WHITE);
    clear(); pFlat.use(); mi.draw(gl.TRIANGLES, 6, 6);
    ok('mesh.setIndex: a larger Uint32 array, and an indexed sub-range', mi.count === 12 && both && near(read(4, 8), BLACK) && near(read(12, 8), WHITE), [mi.count, both, read(4, 8), read(12, 8)]);
    mi.free();

    // --- GL.instances
    const q = GL.mesh({ aPos: new Float32Array(quad(-1, 1)) });
    const inst = GL.instances(q, 4);
    const put = (i, x, y, s, r, g, b) => { const d = inst.data, o = 16 * i; d.fill(0, o, o + 16); d[o] = x; d[o + 1] = y; d[o + 3] = s; d[o + 8] = r; d[o + 9] = g; d[o + 10] = b; };
    put(0, -0.5, -0.5, 0.4, 1, 0, 0); put(1, 0.5, -0.5, 0.4, 0, 1, 0); put(2, -0.5, 0.5, 0.4, 0, 0, 1); put(3, 0.5, 0.5, 0.4, 1, 1, 1);
    inst.count = 4; inst.upload();
    clear(); pInst.use(); inst.draw();
    ok('instances: four instances, per-instance aI0 / aI2', near(read(4, 4), [1, 0, 0]) && near(read(12, 4), [0, 1, 0]) && near(read(4, 12), [0, 0, 1]) && near(read(12, 12), WHITE) && near(read(8, 8), BLACK),
      [read(4, 4), read(12, 4), read(4, 12), read(12, 12), read(8, 8)]);
    put(2, -0.5, 0.5, 0.4, 1, 1, 0); inst.upload(2, 1);
    clear(); pInst.use(); inst.draw();
    ok('instances.upload(first, count)', near(read(4, 12), [1, 1, 0]) && near(read(4, 4), [1, 0, 0]), [read(4, 12), read(4, 4)]);
    const before = inst.data;
    inst.ensure(40);
    put(4, 0, 0, 0.1, 0.5, 0.25, 2); inst.count = 5; inst.upload();
    clear(); pInst.use(); inst.draw();
    ok('instances.ensure: grows, keeps the data, reallocates on the GPU', inst.capacity >= 40 && inst.data !== before && inst.data.length === 16 * inst.capacity && near(read(8, 8), T.hdr ? [0.5, 0.25, 2] : [0.5, 0.25, 1]) && near(read(12, 4), [0, 1, 0]),
      [inst.capacity, read(8, 8), read(12, 4)]);
    q.update('aColor', new Float32Array(24).map((v, i) => (i % 4 === 1 || i % 4 === 3) ? 1 : 0));       // the mesh changes after the instances exist
    q.setIndex(new Uint16Array([0, 1, 2, 3, 4, 5]));
    clear(); pInst.use(); inst.draw();
    ok('instances: follow a mesh that gained an attribute and an index', near(read(12, 12), [0, 1, 0]) && near(read(4, 4), BLACK), [read(12, 12), read(4, 4)]);
    clear(); pInst.use(); inst.draw(1);
    ok('instances.draw(count)', near(read(4, 4), BLACK) && near(read(12, 4), BLACK), [read(4, 4), read(12, 4)]);
    const m2 = GL.mesh({ aPos: new Float32Array(quad(-1, 1)) });
    clear(); pFlat.use(); m2.draw();
    ok('mesh: constants are set again after an instanced draw', near(read(8, 8), WHITE), read(8, 8));
    ok('helpers leave no VAO bound', gl.getParameter(gl.VERTEX_ARRAY_BINDING) === null);
    inst.free(); q.free(); m2.free();

    // --- GL.target
    const pHot = GL.program('dev.t.hot', GL.FS_VS, 'in vec2 vUv; out vec4 o; void main() { o = vec4(5.0, 0.25, 70.0, 1.0); }');
    clear(); pHot.use(); GL.fsTri();
    ok('target: ' + (T.hdr ? 'RGBA16F keeps values above 1' : 'RGBA8 without float targets'), near(read(3, 3), T.hdr ? [5, 0.25, 70] : [1, 0.25, 1], 0.05), read(3, 3));
    const M = GL.target(16, 16, { hdr: true, samples: 4 });
    M.bind(); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    const tri = GL.mesh({ aPos: new Float32Array([-1, -1, 0, 1, -1, 0, -1, 0.83, 0]) });
    pFlat.use(); tri.draw();
    M.blitTo(T);
    let soft = 0, full = 0;
    for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) { const v = read(x, y)[0]; if (v > 0.05 && v < 0.95) soft++; else if (v >= 0.95) full++; }
    ok('target: MSAA renderbuffer resolves with blitTo', M.samples === Math.min(4, GL.caps.maxSamples) && M.tex === null && full > 60 && (M.samples > 0 ? soft >= 8 : true), { samples: M.samples, soft, full });
    M.resize(32, 8);
    ok('target.resize', M.w === 32 && M.h === 8, [M.w, M.h]);
    M.free(); tri.free();
    const D = GL.target(8, 8, { depthTex: true });
    let threw = false;
    try { GL.target(8, 8, { depthTex: true, samples: 4 }); } catch (e) { threw = true; }
    ok('target: depthTex, and depthTex with samples throws', !!D.depthTex && !!D.tex && threw, { depthTex: !!D.depthTex, threw });
    D.free();
    const S = GL.depthTarget(64);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, S.depthTex);
    const cmp = gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE) === gl.COMPARE_REF_TO_TEXTURE && gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC) === gl.LEQUAL &&
      gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER) === gl.LINEAR && gl.getTexParameter(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S) === gl.CLAMP_TO_EDGE;
    gl.bindTexture(gl.TEXTURE_2D, null);
    ok('depthTarget: depth-only, hardware compare LEQUAL, LINEAR, CLAMP_TO_EDGE', cmp && S.tex === null && S.w === 64);
    S.free();
    const some = gl.createTexture();
    GL.bindTex(5, some);
    gl.activeTexture(gl.TEXTURE5);
    const on5 = gl.getParameter(gl.TEXTURE_BINDING_2D) === some;
    gl.activeTexture(gl.TEXTURE0);
    GL.bindTex(5, null);
    ok('bindTex binds the unit and leaves unit 0 active', on5 && gl.getParameter(gl.ACTIVE_TEXTURE) === gl.TEXTURE0);
    gl.deleteTexture(some);

    // --- programs and chunks
    let twice = false, unknown = '';
    try { GL.program('dev.t.twice', GL.FS_VS, '#include <noise>\n#include <lighting>\n#include <noise>\n#include <clouds>\nin vec2 vUv; out vec4 o; void main() { o = vec4(fbm3(vec3(vUv, 0.0))); }').use(); twice = true; } catch (e) { twice = false; unknown = String(e.message).slice(0, 300); }
    ok('program: a chunk included several times is expanded once', twice, unknown);
    try { GL.program('dev.t.unknown', GL.FS_VS, '#include <nosuchchunk>\nvoid main() {}'); unknown = ''; } catch (e) { unknown = String(e.message); }
    ok('program: an unknown chunk throws with the program name', unknown.indexOf('dev.t.unknown') >= 0 && unknown.indexOf('nosuchchunk') >= 0, unknown);
    ok('program: same name and source returns the cached program', GL.program('dev.t.hot', GL.FS_VS, 'in vec2 vUv; out vec4 o; void main() { o = vec4(5.0, 0.25, 70.0, 1.0); }') === pHot);
    const pArr = GL.program('dev.t.arr', GL.FS_VS, 'uniform vec4 uList[3]; uniform float uOne; in vec2 vUv; out vec4 o; void main() { o = uList[2] * uOne; }');
    pArr.use();
    gl.uniform4fv(pArr.u.uList, new Float32Array([0, 0, 0, 0, 0, 0, 0, 0, 0.25, 0.5, 0.75, 1]));
    gl.uniform1f(pArr.u.uOne, 1);
    clear(); pArr.use(); GL.fsTri();
    ok('program: prog.u has plain and array uniforms, undefined for unknown names', near(read(5, 5), [0.25, 0.5, 0.75]) && pArr.u.uNope === undefined && pArr.u.uViewProj === undefined, read(5, 5));

    // --- the Frame block, CPU mirror → GPU
    const F = GL.frame, keep = Float32Array.from(F.data);
    F.set('uFx', 0.125, 0.25, 0.375, 0.5); F.setComp('uFx', 2, 0.75);
    F.setStorm(5, 0.5, 0.625, 0.875, 0.0625);
    F.setMat('uShadowMat', [1, 2, 3, 4, 5, 6, 7, 8, 0.25, 0.5, 0.75, 1, 13, 14, 15, 16]);
    F.upload();
    const pF = GL.program('dev.t.frame', GL.FS_VS, 'in vec2 vUv; out vec4 o; void main() { o = vUv.x < 0.34 ? vec4(uFx.xyz, 1.0) : vUv.x < 0.67 ? vec4(uStorms[5].xyz, 1.0) : vec4(uShadowMat[2].xyz, 1.0); }');
    clear(); pF.use(); GL.fsTri();
    ok('frame: set / setComp / setStorm / setMat / upload reach the shader', near(read(2, 8), [0.125, 0.25, 0.75]) && near(read(8, 8), [0.5, 0.625, 0.875]) && near(read(14, 8), [0.25, 0.5, 0.75]) && near(Array.from(F.get('uStorms').subarray(20, 24)), [0.5, 0.625, 0.875, 0.0625], 1e-6),
      [read(2, 8), read(8, 8), read(14, 8)]);
    F.data.set(keep); F.upload();

    const err = gl.getError();
    ok('no GL error', err === gl.NO_ERROR, '0x' + err.toString(16));
    T.free();
    GL.toScreen();
    GL.resetState();
    return { ok: failed.length === 0, failed, results };
  };

  // A module that throws is switched off and the frame goes on. (The renderer reports it with one console.error.)
  DV.check.isolation = function () {
    const Rn = DM.Renderer, GL = DM.GL, gl = GL.gl, had = DM.FX, calls = { update: 0, draw0: 0, draw1: 0 };
    DM.FX = {
      update() { calls.update++; },
      draw(layer) { if (layer === 0) { calls.draw0++; gl.enable(gl.BLEND); gl.depthMask(false); throw new Error('deliberate failure (isolation test)'); } calls.draw1++; },
    };
    const v = currentView(0), px = new Uint8Array(4);
    let out;
    try {
      Rn.frame(v, 1 / 60);
      const first = Object.assign({}, calls), disabled = Rn.disabled.FX || null;
      Rn.frame(v, 1 / 60); Rn.frame(v, 1 / 60);
      GL.toScreen();
      gl.readPixels(gl.drawingBufferWidth >> 1, gl.drawingBufferHeight >> 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      out = { disabled, first, afterThreeFrames: Object.assign({}, calls), centre: Array.from(px), draws: Rn.stats.draws,
        blendLeftOn: gl.isEnabled(gl.BLEND), depthMask: gl.getParameter(gl.DEPTH_WRITEMASK), glError: gl.getError() };
    } finally {
      if (had) DM.FX = had; else delete DM.FX;
      Rn.revive('FX');
    }
    return out;
  };

  // Renderer.showSnapshot with recording stand-ins for Planet, Objects and FX
  DV.check.snapshot = function () {
    const Rn = DM.Renderer, GL = DM.GL, w = DV.world, log = [], fx = { update: 0, draw: 0 };
    const saved = { Planet: DM.Planet, Objects: DM.Objects, FX: DM.FX };
    DM.Planet = { showSnapshot(s) { log.push('Planet:' + (s ? 'snap' : 'null')); } };
    DM.Objects = { showSnapshot(s) { log.push('Objects:' + (s ? 'snap' : 'null')); } };
    DM.FX = { update() { fx.update++; }, draw() { fx.draw++; } };
    const v = currentView(0), out = {};
    const frames = n => { for (let i = 0; i < n; i++) Rn.frame(v, 1 / 60); };
    try {
      frames(2);
      out.fxBefore = Object.assign({}, fx);
      out.nightBefore = GL.frame.get('uWorld')[2];
      const urban = new Uint8Array(w.n);
      for (let c = 0; c < w.n; c += 100) urban[c] = 200;                         // 1 % of the cells are built up
      const snap = { year: 1234, heat: 0.5, seaLevel: w.seaLevel + 2, pop: 1000, biome: w.biome, owner: new Int8Array(w.n).fill(-1), urban };
      Rn.showSnapshot(snap);
      Rn.showSnapshot(snap);                                                     // the same frame again: no second rebuild
      fx.update = fx.draw = 0;
      frames(90);
      out.during = { log: log.slice(), fx: Object.assign({}, fx), seaShown: Rn.seaLevel, seaWanted: snap.seaLevel, year: GL.frame.get('uTime')[1], heat: GL.frame.get('uWorld')[0],
        night: GL.frame.get('uWorld')[2], isSnapshot: Rn.snapshot === snap };
      log.length = 0;
      Rn.showSnapshot(null);
      fx.update = fx.draw = 0;
      frames(90);
      out.after = { log: log.slice(), fx: Object.assign({}, fx), seaShown: Rn.seaLevel, seaWanted: w.seaLevel, year: GL.frame.get('uTime')[1], heat: GL.frame.get('uWorld')[0],
        night: GL.frame.get('uWorld')[2], isSnapshot: Rn.snapshot !== null };
    } finally {
      for (const k in saved) { if (saved[k]) DM[k] = saved[k]; else delete DM[k]; }
    }
    return out;
  };

  // Lose the WebGL context and get it back: the renderer stops drawing, then rebuilds everything and carries on.
  DV.check.contextLoss = async function () {
    const gl = DM.GL.gl, ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) return { supported: false };
    const once = name => new Promise(resolve => canvas.addEventListener(name, resolve, { once: true }));
    const out = { supported: true };
    const lost = once('webglcontextlost');
    ext.loseContext();
    await lost;
    await DV.settle(3);
    out.whileLost = { isContextLost: gl.isContextLost(), frames: DV.frames };
    const back = once('webglcontextrestored');
    ext.restoreContext();
    await back;
    await DV.settle(4);
    const px = new Uint8Array(4), g = DM.GL.gl;
    DM.Renderer.frame(currentView(0), 0);
    DM.GL.toScreen();
    g.readPixels(g.drawingBufferWidth >> 1, g.drawingBufferHeight >> 1, 1, 1, g.RGBA, g.UNSIGNED_BYTE, px);
    out.after = { isContextLost: g.isContextLost(), draws: DM.Renderer.stats.draws, tris: DM.Renderer.stats.tris, centre: Array.from(px),
      disabled: Object.assign({}, DM.Renderer.disabled), glError: g.getError(), scene: !!DM.Post.scene, hdr: DM.GL.caps.hdr };
    return out;
  };

  // ------------------------------------------------------------------------------------------ overlay text, input
  let hudT = 0;
  function hud(dt) {
    hudT -= dt;
    if (hudEl.classList.contains('hidden') || hudT > 0) return;
    hudT = 0.25;
    const s = DV.stats(), w = DV.world, m = s.modules;
    const f = cam.focus;
    const lines = [
      'DEMIURGE render viewer    ' + (w ? w.name + ' · seed ' + w.seed + ' · ' + DV.how + ' world · ' + w.phase + ' · year ' + Math.floor(w.year) : 'no world'),
      'modules  ' + RENDER_MODULES.map(k => k + ' ' + m[k]).join('   ') + '   stand-in ' + Dev.mode + (Dev.sky ? ' + sky' : ''),
      'quality ' + s.quality + ' · ' + s.buffer[0] + '×' + s.buffer[1] + ' px · ' + (s.hdr ? 'HDR' : 'LDR') + ' · MSAA ' + s.samples + (s.fxaa ? ' · FXAA' : '') +
        ' · bloom ' + s.bloomLevels + ' · tilt ' + s.tilt.toFixed(2) + ' · shadow map ' + (s.shadowSize || 'off'),
      'frame   cpu ' + s.cpu.toFixed(2) + ' ms · gpu ' + s.gpu.toFixed(2) + ' ms · ' + s.draws + ' draws · ' + U.fmtBig(s.tris) + ' tris · ' + U.fmtBig(s.instances) +
        ' inst · ' + s.particles + ' particles' + (s.slow ? ' · SLOW' : ''),
      'camera  ' + DV.camera + ' · lat ' + (Math.asin(U.clamp(f[1], -1, 1)) / DEG).toFixed(1) + ' lon ' + (Math.atan2(f[2], f[0]) / DEG).toFixed(1) + ' zoom ' + cam.zoom.toFixed(2) +
        ' · sun ' + s.sunMode + (s.frozen !== null ? ' · frozen at t=' + s.frozen : ''),
    ];
    for (const k in s.disabled) lines.push('DISABLED ' + k + ': ' + s.disabled[k].slice(0, 140));
    for (const e of s.stateErrors.slice(0, 4)) lines.push('GL STATE ' + e);
    for (const e of DV.notes.slice(-3)) lines.push('note: ' + e.slice(0, 150));
    for (const e of DV.errors.slice(-3)) lines.push('ERROR ' + e.slice(0, 150));
    hudEl.textContent = lines.join('\n');
  }

  function bindInput() {
    let drag = null;
    canvas.addEventListener('contextmenu', e => e.preventDefault());
    canvas.addEventListener('pointerdown', e => { drag = { x: e.clientX, y: e.clientY, b: e.button }; try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* synthetic */ } });
    canvas.addEventListener('pointermove', e => {
      if (!drag) return;
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      drag.x = e.clientX; drag.y = e.clientY;
      if (drag.b === 2) turnBy(dx * 0.006); else orbitBy(dx, dy);
    });
    const end = () => { drag = null; };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
    canvas.addEventListener('wheel', e => { e.preventDefault(); zoomBy(-Math.sign(e.deltaY) * 0.05); }, { passive: false });
    window.addEventListener('keydown', e => {
      const Rn = DM.Renderer;
      if (e.code === 'KeyH') hudEl.classList.toggle('hidden');
      else if (e.code === 'KeyQ') Rn.setQuality((DM.GL.quality + 1) % 3);
      else if (e.code === 'KeyS') Rn.sun.mode = Rn.sun.mode === 'orbit' ? 'follow' : 'orbit';
      else if (e.code === 'BracketLeft') Rn.sun.angle -= 0.1;
      else if (e.code === 'BracketRight') Rn.sun.angle += 0.1;
      else if (e.code === 'KeyF') Rn.freeze(Rn.frozen === null ? 5 : null);
      else if (e.code === 'KeyO' && DM.Planet && DM.Planet.setOverlay) DM.Planet.setOverlay(((DM.Planet.overlay | 0) + 1) % 6);
      else if (e.code === 'ArrowLeft') orbitBy(40, 0);
      else if (e.code === 'ArrowRight') orbitBy(-40, 0);
      else if (e.code === 'ArrowUp') orbitBy(0, 40);
      else if (e.code === 'ArrowDown') orbitBy(0, -40);
      else if (e.code === 'Equal' || e.code === 'NumpadAdd') zoomBy(0.05);
      else if (e.code === 'Minus' || e.code === 'NumpadSubtract') zoomBy(-0.05);
    });
  }

  // ------------------------------------------------------------------------------------------ boot
  function fail(text) {
    const el = document.getElementById('dv-msg');
    el.textContent = text; el.style.display = 'flex';
    DV.errors.push(text);
  }

  function boot() {
    if (!DM.GL || !DM.Post || !DM.Renderer) return fail('The render core (gl.js, post.js, renderer.js) is not loaded.');
    // only=: hide the render modules that were not asked for
    const only = P.only === null ? null : P.only.toLowerCase().split(',').map(s => s.trim()).filter(Boolean);
    DV.hidden = {};
    for (const m of RENDER_MODULES) {
      if (only && only.indexOf(m.toLowerCase()) < 0 && DM[m]) { DV.hidden[m] = DM[m]; delete DM[m]; DV.modules[m] = 'excluded'; }
      else DV.modules[m] = DM[m] ? 'ok' : 'missing';
    }
    const active = RENDER_MODULES.filter(m => DM[m]);
    Dev.mode = DM.Planet ? 'none' : (P.stand === 'test' || P.stand === 'terrain' || P.stand === 'none' ? P.stand : (active.length ? 'terrain' : 'test'));
    Dev.sky = !DM.Sky; Dev.halo = P.halo; Dev.clouds = P.clouds;

    if (P.size) { canvas.style.width = P.size[1] + 'px'; canvas.style.height = P.size[2] + 'px'; }
    if (P.hud) hudEl.classList.remove('hidden');
    DM.GL.force.noHdr = P.nohdr;
    DM.GL.debug = P.debug;
    DM.Renderer.shadows = P.shadows;
    DM.Renderer.setQuality(P.q);
    if (!DM.Renderer.init(canvas)) return fail('WebGL2 is not available.');

    const w = DV.world = DM.world = buildWorld();
    DM.Renderer.setWorld(w);
    if (DM.Planet && DM.Planet.setOverlay && P.overlay) { try { DM.Planet.setOverlay(P.overlay); } catch (e) { note('Planet.setOverlay threw: ' + e.message); } }

    DV.camera = DM.Camera && DM.Camera.view && P.cam !== 'builtin' ? 'DM.Camera' : 'builtin';
    if (useGameCamera()) { try { DM.Camera.reset(); DM.Camera.setViewport(canvas.clientWidth, canvas.clientHeight); } catch (e) { camFail(e); } }
    cam.zoom = P.zoom;
    DV.cam(P.lat, P.lon, P.zoom, P.heading);

    DM.Renderer.sun.mode = P.sun === 'orbit' ? 'orbit' : 'follow';
    if (Number.isFinite(P.t)) DM.Renderer.freeze(P.t);
    if (Number.isFinite(P.sunAngle)) DM.Renderer.sun.angle = P.sunAngle * DEG;

    bindInput();
    window.addEventListener('resize', () => {
      if (useGameCamera()) { try { DM.Camera.setViewport(canvas.clientWidth, canvas.clientHeight); } catch (e) { camFail(e); } }
      DM.Renderer.resize();
    });
    const loop = ts => {
      requestAnimationFrame(loop);
      const dt = lastTs ? Math.min(0.1, Math.max(0, (ts - lastTs) / 1000)) : 1 / 60;
      lastTs = ts;
      if (DV.paused) return;
      DV.step(dt);
      hud(dt);
    };
    requestAnimationFrame(loop);
  }

  try { boot(); } catch (e) { fail('view.js: ' + (e && e.stack ? e.stack : e)); console.error(e); }
})(globalThis.DM = globalThis.DM || {});
