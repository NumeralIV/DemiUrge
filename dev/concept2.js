// DEMIURGE — concept render (dev only), cinematic pass. Draws a planet from the real generator with a compact standalone
// WebGL2 renderer to show the intended look.  ?shot=hero|golden|storm|night  &seed=1  &city=2  &az=0
(function () {
  'use strict';
  const DM = window.DM, U = DM.U, V3 = DM.V3, M4 = DM.M4, R = DM.R, B = DM.BIOME;
  const q = new URLSearchParams(location.search);
  const SHOT = q.get('shot') || 'hero', SEED = +(q.get('seed') || 1), AZ = +(q.get('az') || 0), CITY = +(q.get('city') || 2);
  const CLOSE = SHOT === 'golden' || SHOT === 'storm';
  const canvas = document.getElementById('c');
  const W = canvas.width = canvas.clientWidth, H = canvas.height = canvas.clientHeight;
  const gl = canvas.getContext('webgl2', { antialias: false, alpha: false });
  gl.getExtension('EXT_color_buffer_float'); gl.getExtension('OES_texture_float_linear');
  const CLOUD_R = R + (CLOSE ? 16 : 11), ATMO_R = CLOUD_R + 7;

  // ------------------------------------------------------------------ world
  const w = DM.State.create({ seed: SEED, L: 6 }), geo = w.geo, n = w.n;
  DM.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  DM.Terrain.derive(w); DM.Climate.update(w); DM.Hydro.update(w); DM.Life.classify(w); DM.Life.fill(w, 1);
  for (let i = 0; i < 60; i++) DM.Climate.stepIce(w);
  DM.Climate.update(w); DM.Life.classify(w); DM.Life.habitability(w);

  // stand-in civilization (the sim will grow real ones)
  const cities = [], urban = new Float32Array(n), farm = new Float32Array(n), ext = new Float32Array(n * 4);
  const order = []; for (let c = 0; c < n; c++) if (w.hab[c] > 0.45 && (w.coast[c] <= 2 || w.fresh[c] <= 1)) order.push(c);
  order.sort((a, b) => (w.hab[b] + U.hashf(b, 7) * 0.25) - (w.hab[a] + U.hashf(a, 7) * 0.25));
  const tmpN = [];
  for (const c of order) {
    if (cities.length >= 70) break;
    let ok = true; for (const o of cities) if (geo.angle(c, o.cell) < 0.11) { ok = false; break; }
    if (!ok) continue;
    const size = cities.length < 8 ? 3 : cities.length < 24 ? 2 : 1;
    cities.push({ cell: c, size });
    const k = geo.ring(c, size + 3, tmpN);
    for (let i = 0; i < k; i++) {
      const d = tmpN[i]; if (w.elev[d] < 0) continue;
      const a = geo.angle(c, d) / geo.step;
      if (a <= size - 0.5) urban[d] = Math.max(urban[d], 1 - a / (size + 0.5));
      else if (w.hab[d] > 0.2 && U.hashf(d, 3) < 0.85) farm[d] = 1;
    }
  }
  const scored = cities.slice(0, 40).map(ct => { const cs = [], k = geo.within(ct.cell, 0.26, cs); let land = 0, peak = -9, pc = ct.cell, forest = 0;
    for (let i = 0; i < k; i++) { const c = cs[i]; if (w.elev[c] >= 0) { land++; if (w.biome[c] === B.FOREST || w.biome[c] === B.RAINFOREST || w.biome[c] === B.TAIGA) forest++; } if (w.elev[c] > peak) { peak = w.elev[c]; pc = c; } }
    const lf = land / k; return { ct, pc, s: (1 - Math.abs(lf - 0.68) * 2.2) + Math.min(peak, 6) * 0.22 + forest / k * 1.5 - Math.abs(geo.lat[ct.cell]) * 0.9 + ct.size * 0.15 }; }).sort((a, b) => b.s - a.s);
  const pick = scored[Math.min(CITY, scored.length - 1)], main = pick.ct;

  // ------------------------------------------------------------------ camera, sun, weather per shot
  const eye = V3.create(), tgt = V3.create(), up = V3.create(0, 1, 0), sun = V3.create(), f = V3.create(), e = V3.create(), nn = V3.create(), dir = V3.create(), side = V3.create();
  const storms = new Float32Array(12), cyc = new Float32Array(12), volc = new Float32Array(4), flash = new Float32Array(4);
  let goldSky = 0, cloud = 1, zoom = 0, tilt = 0, focusY = 0.5, treeAng = 0, expo = 0.62, bolt = 0, style = 'town', volcCell = -1;
  const fov = 35 * Math.PI / 180;
  const mixv = (o, parts) => { let x = 0, y = 0, z = 0; for (const [v, s] of parts) { x += v[0] * s; y += v[1] * s; z += v[2] * s; } V3.set(o, x, y, z); return V3.norm(o, o); };
  const setv = (arr, i, v, r) => { arr[4 * i] = v[0]; arr[4 * i + 1] = v[1]; arr[4 * i + 2] = v[2]; arr[4 * i + 3] = r; };
  geo.dir(main.cell, f); V3.basis(f, e, nn);
  { const pk = V3.create(); geo.dir(pick.pc, pk); const a0 = Math.atan2(V3.dot(pk, nn), V3.dot(pk, e)) + AZ; V3.scale(dir, e, Math.cos(a0)); V3.addScaled(dir, dir, nn, Math.sin(a0)); V3.cross(side, f, dir); }
  const dE = V3.dot(dir, e), dN = V3.dot(dir, nn);
  const tv = V3.create();
  if (!CLOSE) {
    const ed = mixv(V3.create(), SHOT === 'hero' ? [[f, 1], [e, 0.10], [nn, 0.12]] : [[f, 1], [e, -0.15]]);
    V3.scale(eye, ed, SHOT === 'hero' ? 395 : 330);
    if (SHOT === 'hero') mixv(sun, [[e, -1], [nn, 0.28], [f, 0.10]]); else mixv(sun, [[f, -1.9], [e, 1.2], [nn, 0.25]]);
    setv(cyc, 0, mixv(tv, [[ed, 1], [e, -0.70], [nn, 0.34]]), 0.17);
    setv(cyc, 1, mixv(tv, [[ed, 1], [e, -0.78], [nn, -0.36]]), 0.13);
    setv(storms, 0, mixv(tv, [[ed, 1], [nn, 0.34], [e, 0.42]]), 0.20); bolt = 1;
    if (SHOT === 'hero') { mixv(tv, [[ed, 1], [e, 0.08], [nn, -0.40]]); volcCell = geo.cellAt(tv[0], tv[1], tv[2]); }
  } else {
    const back = mixv(V3.create(), [[f, 1], [dir, SHOT === 'golden' ? -0.105 : -0.33]]);
    const gc = geo.cellAt(back[0], back[1], back[2]); V3.scale(eye, back, R + Math.max(w.elev[gc], 0) + (SHOT === 'golden' ? 4.4 : 12.5));
    mixv(tv, [[f, 1], [dir, SHOT === 'golden' ? 0.055 : 0.03]]); V3.scale(tgt, tv, R + Math.max(w.elev[main.cell], 0) + 0.6); V3.norm(up, eye);
    if (SHOT === 'golden') { mixv(sun, [[f, 0.25], [dir, 0.60], [side, -0.75]]); goldSky = 0.85; setv(storms, 0, mixv(tv, [[f, 1], [dir, 0.38], [side, 0.30]]), 0.17); tilt = 1.0; focusY = 0.47; treeAng = 0.5; expo = 0.74; style = 'village'; }
    else { mixv(sun, [[f, 0.30], [side, -0.92], [dir, 0.22]]); goldSky = 0.12; setv(storms, 0, mixv(tv, [[f, 1], [dir, 0.50], [side, 0.16]]), 0.30); tilt = 0.3; focusY = 0.55; treeAng = 0.8; expo = 0.85; bolt = 1; }
    zoom = 1;
  }
  if (volcCell >= 0) { DM.Terrain.cone(w, volcCell, 0.11, 8.0);
    const cs = [], k = geo.within(volcCell, 0.15, cs); for (let i = 0; i < k; i++) { const c = cs[i], a = geo.angle(volcCell, c); ext[4 * c + 2] = U.sat(1.4 - a / 0.11); ext[4 * c] = Math.max(ext[4 * c], U.sat(1 - a / 0.03) * 0.95); urban[c] = 0; farm[c] = 0; }
    const r2 = U.rng(99); for (let s = 0; s < 18; s++) { let c = geo.nbr[6 * volcCell + (s % geo.nbrN[volcCell])];
      for (let j = 0; j < 20 && c >= 0; j++) { ext[4 * c] = Math.max(ext[4 * c], 1 - j / 22); let best = -1, be = w.elev[c]; for (let t = 0; t < geo.nbrN[c]; t++) { const d = geo.nbr[6 * c + t], ee = w.elev[d] - r2.f() * 0.5; if (ee < be) { be = ee; best = d; } } if (best < 0 || w.elev[best] < -0.3) break; c = best; } }
    const vd = V3.create(); geo.dir(volcCell, vd); volc.set([vd[0], vd[1], vd[2], 0.0]); }

  // ------------------------------------------------------------------ colours and mesh
  const BC = []; BC[B.SEA] = [0.16, 0.15, 0.12]; BC[B.SHALLOWS] = [0.34, 0.30, 0.20]; BC[B.BEACH] = [0.60, 0.52, 0.34];
  BC[B.DESERT] = [0.56, 0.42, 0.24]; BC[B.SAVANNA] = [0.40, 0.36, 0.14]; BC[B.GRASSLAND] = [0.20, 0.35, 0.10];
  BC[B.FOREST] = [0.06, 0.19, 0.05]; BC[B.RAINFOREST] = [0.035, 0.15, 0.045]; BC[B.TAIGA] = [0.07, 0.16, 0.10];
  BC[B.TUNDRA] = [0.33, 0.32, 0.25]; BC[B.ICE] = [0.80, 0.84, 0.88]; BC[B.ROCK] = [0.27, 0.24, 0.22];
  BC[B.SNOWPEAK] = [0.84, 0.86, 0.90]; BC[B.WETLAND] = [0.11, 0.21, 0.12]; BC[B.BARREN] = [0.2, 0.17, 0.15]; BC[B.SEAICE] = [0.16, 0.15, 0.12];
  const riverT = DM.Hydro.riverT(w);
  const pos = new Float32Array(n * 3), col = new Float32Array(n * 3), dat = new Float32Array(n * 4), nrm = new Float32Array(n * 3);
  for (let c = 0; c < n; c++) {
    const r = R + w.elev[c];
    pos[3 * c] = geo.pos[3 * c] * r; pos[3 * c + 1] = geo.pos[3 * c + 1] * r; pos[3 * c + 2] = geo.pos[3 * c + 2] * r;
    let k = BC[w.biome[c]] || [1, 0, 1], cr = k[0], cg = k[1], cb = k[2];
    if (w.elev[c] >= 0) {
      if (w.ice[c] > 0.3) { const t = U.sat((w.ice[c] - 0.3) / 0.4); cr = U.lerp(cr, 0.82, t); cg = U.lerp(cg, 0.85, t); cb = U.lerp(cb, 0.9, t); }
      const farmable = farm[c] > 0 && w.ice[c] < 0.3 && w.biome[c] !== B.ROCK && w.biome[c] !== B.SNOWPEAK;
      if (farmable) { const h = U.hashf(c, 11), fc = h < 0.62 ? [0.62, 0.45, 0.10] : h < 0.86 ? [0.24, 0.40, 0.10] : [0.34, 0.24, 0.13];
        cr = U.lerp(cr, fc[0], 0.9); cg = U.lerp(cg, fc[1], 0.9); cb = U.lerp(cb, fc[2], 0.9); ext[4 * c + 1] = h < 0.62 ? 1 : 0.4; } else farm[c] = 0;
      if (urban[c] > 0) { const t = Math.min(1, urban[c] * 1.2); cr = U.lerp(cr, 0.36, t); cg = U.lerp(cg, 0.30, t); cb = U.lerp(cb, 0.22, t); }
      if (ext[4 * c + 2] > 0) { const t = Math.min(1, ext[4 * c + 2]); cr = U.lerp(cr, 0.06, t); cg = U.lerp(cg, 0.055, t); cb = U.lerp(cb, 0.055, t); }
    }
    const v = 0.9 + 0.2 * U.hashf(c, 1);
    col[3 * c] = cr * v; col[3 * c + 1] = cg * v; col[3 * c + 2] = cb * v;
    const river = (w.elev[c] >= 0 && ext[4 * c + 2] === 0 && (w.flow[c] > riverT || w.water[c] > 0.03)) ? Math.min(1, 0.45 + w.flow[c] / (riverT * 8)) : 0;
    dat[4 * c] = urban[c]; dat[4 * c + 1] = river; dat[4 * c + 2] = w.elev[c] < 0 ? w.ice[c] : 0; dat[4 * c + 3] = w.elev[c];
  }
  const tr = geo.tris;
  for (let i = 0; i < tr.length; i += 3) {
    const a = tr[i] * 3, b = tr[i + 1] * 3, c = tr[i + 2] * 3;
    const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2], vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    for (const k of [a, b, c]) { nrm[k] += nx; nrm[k + 1] += ny; nrm[k + 2] += nz; }
  }

  // ------------------------------------------------------------------ shaders
  const FRAME = `uniform mat4 uVP, uShadowM; uniform vec3 uCam, uSun; uniform float uCloud, uZoom, uBolt, uGold, uInside; uniform sampler2DShadow uShadow;
uniform vec4 uStorm[3]; uniform vec4 uCyc[3]; uniform vec4 uVolc; uniform vec4 uFlash;`;
  const NOISE = `
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y)*p.z); }
float vnoise(vec3 p){ vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  return mix(mix(mix(hash13(i), hash13(i+vec3(1,0,0)), f.x), mix(hash13(i+vec3(0,1,0)), hash13(i+vec3(1,1,0)), f.x), f.y),
             mix(mix(hash13(i+vec3(0,0,1)), hash13(i+vec3(1,0,1)), f.x), mix(hash13(i+vec3(0,1,1)), hash13(i+vec3(1,1,1)), f.x), f.y), f.z); }
float fbm(vec3 p){ float v = 0.0, a = 0.5; for (int i = 0; i < 5; i++){ v += a*vnoise(p); p = p*2.03 + vec3(11.3, 4.7, 7.1); a *= 0.5; } return v/0.96875; }
vec3 rotAx(vec3 v, vec3 k, float a){ float c = cos(a), s = sin(a); return v*c + cross(k, v)*s + k*dot(k, v)*(1.0 - c); }
float stormAt(vec3 d){ float s = 0.0; for (int i = 0; i < 3; i++){ float r = uStorm[i].w; if (r > 0.0){ float a = acos(clamp(dot(d, uStorm[i].xyz), -1.0, 1.0)); s = max(s, exp(-(a*a)/(r*r))); } } return s; }
float cloudCover(vec3 d){ float st = stormAt(d), cy = 0.0;
  for (int i = 0; i < 3; i++){ float r = uCyc[i].w; if (r > 0.0){ float a = acos(clamp(dot(d, uCyc[i].xyz), -1.0, 1.0)), t = a/r;
    d = rotAx(d, uCyc[i].xyz, 3.4*exp(-t*0.9)); cy += 0.30*exp(-(t - 0.75)*(t - 0.75)*2.2) - 0.35*exp(-t*t*110.0); } }
  vec3 p = d*2.6; p += 0.55*vec3(vnoise(p*1.3+3.0), vnoise(p*1.3+17.0), vnoise(p*1.3+41.0));
  float band = 0.07*cos(d.y*9.0) - 0.05;
  float nz = fbm(p + vec3(5.0)) + 0.16*(fbm(p*4.3 + 9.0) - 0.5) + uZoom*0.11*(fbm(p*17.0) - 0.5) + (1.0 - uZoom)*0.10*(fbm(p*9.0 + 2.0) - 0.5);
  return smoothstep(0.585, 0.70, nz + band + st*0.34 + cy); }`;
  const LIGHT = NOISE + `
float dayAt(vec3 P){ return smoothstep(-0.12, 0.22, dot(normalize(P), uSun)); }
vec3 sunTint(vec3 up){ return mix(vec3(1.0, 0.40, 0.14), vec3(1.0, 0.95, 0.88), smoothstep(-0.02, 0.42, dot(up, uSun))); }
vec3 skyTint(vec3 up){ return mix(vec3(1.0, 0.50, 0.22), vec3(0.30, 0.55, 1.0), smoothstep(0.0, 0.42, dot(up, uSun))); }
float sunShadow(vec3 P, vec3 N){ vec4 s = uShadowM*vec4(P + N*0.12, 1.0); vec3 c = s.xyz/s.w;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
  float t = 1.0/4096.0, a = 0.0; for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) a += texture(uShadow, vec3(c.xy + vec2(x, y)*t*1.3, c.z - 0.0006));
  return a/9.0; }
vec3 shade(vec3 alb, vec3 N, vec3 P, float spec, float emis){
  vec3 up = normalize(P); float day = dayAt(P); float ndl = max(dot(N, uSun), 0.0);
  float sh = sunShadow(P, N) * (1.0 - 0.72*cloudCover(normalize(up + uSun*0.13)));
  vec3 warm = sunTint(up);
  vec3 c = alb*(warm*2.9*ndl*sh + skyTint(up)*0.36*day*(0.55 + 0.45*dot(N, up)) + vec3(0.035, 0.05, 0.09)*(0.6 + 0.4*dot(N, up)));
  vec3 V = normalize(uCam - P), Hh = normalize(V + uSun);
  c += warm*spec*pow(max(dot(N, Hh), 0.0), 380.0)*sh*day*5.0;
  vec3 fd = P - uFlash.xyz; c += alb*vec3(0.65, 0.78, 1.0)*uFlash.w/(1.0 + dot(fd, fd)*0.012);
  return c + alb*emis; }
vec3 atmosphere(vec3 c, vec3 P){ vec3 up = normalize(P), V = normalize(uCam - P);
  float rim = pow(1.0 - max(dot(up, V), 0.0), 3.0), day = dayAt(P), dist = length(uCam - P);
  float haze = clamp(rim*0.75*(1.0 - uZoom) + (1.0 - exp(-dist*0.006))*0.70*uZoom, 0.0, 0.9);
  vec3 sky = skyTint(up)*(0.06 + 1.35*day) + sunTint(up)*pow(max(dot(-V, uSun), 0.0), 6.0)*0.45*uZoom*day;
  return mix(c, sky, haze*(0.25 + 0.75*day)); }`;
  function prog(vs, fs) {
    const p = gl.createProgram();
    for (const [t, s] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
      const sh = gl.createShader(t); gl.shaderSource(sh, '#version 300 es\nprecision highp float; precision highp sampler2DShadow;\n' + s); gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh) + '\n' + s.split('\n').map((l, i) => (i + 3) + ': ' + l).join('\n'));
      gl.attachShader(p, sh);
    }
    ['aPos', 'aNrm', 'aCol', 'aDat', 'iA', 'iB'].forEach((nm, i) => gl.bindAttribLocation(p, i, nm));
    gl.linkProgram(p); if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {}; const k = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < k; i++) { const nm = gl.getActiveUniform(p, i).name; u[nm] = gl.getUniformLocation(p, nm); }
    return { p, u };
  }
  function vao(attrs, index, inst) {
    const v = gl.createVertexArray(); gl.bindVertexArray(v);
    attrs.forEach((a, i) => { if (!a) return; const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, a.d, gl.STATIC_DRAW); gl.enableVertexAttribArray(i); gl.vertexAttribPointer(i, a.s, gl.FLOAT, false, 0, 0); });
    if (inst) { const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, inst, gl.STATIC_DRAW);
      for (let i = 0; i < 2; i++) { gl.enableVertexAttribArray(4 + i); gl.vertexAttribPointer(4 + i, 4, gl.FLOAT, false, 32, i * 16); gl.vertexAttribDivisor(4 + i, 1); } }
    if (index) { const b = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, b); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, index, gl.STATIC_DRAW); }
    gl.bindVertexArray(null); return v;
  }
  function target(w_, h_, msaa, depth) {
    const t = { w: w_, h: h_, fbo: gl.createFramebuffer() }; gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
    if (msaa) {
      const cb = gl.createRenderbuffer(); gl.bindRenderbuffer(gl.RENDERBUFFER, cb); gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.RGBA16F, w_, h_);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, cb);
      const db = gl.createRenderbuffer(); gl.bindRenderbuffer(gl.RENDERBUFFER, db); gl.renderbufferStorageMultisample(gl.RENDERBUFFER, 4, gl.DEPTH_COMPONENT24, w_, h_);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, db);
    } else {
      t.tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t.tex);
      if (depth) { gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, w_, h_, 0, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
      } else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w_, h_, 0, gl.RGBA, gl.HALF_FLOAT, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, depth ? gl.DEPTH_ATTACHMENT : gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
      if (depth) { gl.drawBuffers([gl.NONE]); gl.readBuffer(gl.NONE); }
    }
    return t;
  }

  const terrVS = FRAME + `layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol; layout(location=3) in vec4 aDat; layout(location=4) in vec4 aExt;
out vec3 vW, vN, vC; out vec4 vD, vX; void main(){ vW = aPos; vN = normalize(aNrm); vC = aCol; vD = aDat; vX = aExt; gl_Position = uVP*vec4(aPos, 1.0); }`;
  const pTerr = prog(terrVS, FRAME + LIGHT + `in vec3 vW, vN, vC; in vec4 vD, vX; out vec4 o;
void main(){
  vec3 fn = normalize(cross(dFdx(vW), dFdy(vW))); if (dot(fn, vW) < 0.0) fn = -fn;
  float fw = 0.85*smoothstep(2.0, 8.0, 1.9/max(length(fwidth(vW)), 1e-5));
  vec3 N = normalize(mix(vN, fn, fw)); vec3 up = normalize(vW);
  float det = smoothstep(0.25, 0.7, uZoom);
  float n1 = vnoise(vW*0.9), n2 = vnoise(vW*4.5), n3 = vnoise(vW*17.0);
  vec3 alb = vC*(1.0 + det*(0.30*(n1 - 0.5) + 0.24*(n2 - 0.5) + 0.16*(n3 - 0.5)));
  float ang = floor(vnoise(vW*0.42)*5.0)*1.1; vec3 tdir = normalize(cross(up, vec3(cos(ang), sin(ang*1.7), sin(ang))));
  float stripe = 0.5 + 0.5*sin(dot(vW, tdir)*24.0); alb *= mix(1.0, 0.70 + 0.55*stripe, clamp(vX.y, 0.0, 1.0)*det);
  float steep = 1.0 - dot(fn, up); alb = mix(alb, vec3(0.25, 0.22, 0.20)*(0.8 + 0.4*n2), smoothstep(0.10, 0.22, steep)*0.7*step(0.02, vD.w)*(1.0 - clamp(vX.z, 0.0, 1.0)));
  alb = mix(alb, vec3(0.05, 0.22, 0.34), smoothstep(0.5, 0.8, vD.y + (n2 - 0.5)*0.3)*0.9);
  float night = 1.0 - dayAt(vW);
  float ls = mix(1.5, 2.6, uZoom); vec3 cell = floor(vW*ls); float h = hash13(cell); vec3 f = fract(vW*ls) - 0.5;
  float lamp = step(1.0 - sqrt(vD.x)*0.95, h)*smoothstep(0.40, 0.08, length(f));
  vec3 c = shade(alb, N, vW, 0.0, 0.0) + vec3(1.0, 0.70, 0.36)*night*(lamp*mix(60.0, 14.0, uZoom)*(0.5 + 0.5*hash13(cell + 3.0)) + vD.x*vD.x*mix(0.5, 0.2, uZoom));
  float lava = vX.x*smoothstep(0.25, 0.75, 0.35 + vX.x*0.6 + (vnoise(vW*1.1) - 0.5)*0.9);
  c += vec3(1.0, 0.27, 0.03)*lava*16.0;
  o = vec4(atmosphere(c, vW), 1.0); }`);
  const pTerrD = prog(`uniform mat4 uVP; layout(location=0) in vec3 aPos; void main(){ gl_Position = uVP*vec4(aPos, 1.0); }`, `void main(){}`);
  const pSea = prog(FRAME + `layout(location=0) in vec3 aPos; layout(location=3) in vec4 aDat; out vec3 vW; out vec4 vD;
void main(){ vW = normalize(aPos)*${R.toFixed(1)}; vD = aDat; gl_Position = uVP*vec4(vW, 1.0); }`, FRAME + LIGHT + `in vec3 vW; in vec4 vD; out vec4 o;
void main(){ float depth = -vD.w; if (depth < -0.25) discard;
  vec3 up = normalize(vW); float wv = vnoise(vW*3.0)*0.5 + vnoise(vW*9.0)*0.25;
  float ws = mix(0.035, 0.11, uZoom);
  vec3 N = normalize(up + ws*vec3(vnoise(vW*5.0) - 0.5, vnoise(vW*5.0 + 9.0) - 0.5, vnoise(vW*5.0 + 21.0) - 0.5) + uZoom*0.05*vec3(vnoise(vW*23.0) - 0.5, vnoise(vW*23.0 + 5.0) - 0.5, vnoise(vW*23.0 + 13.0) - 0.5));
  float t = 1.0 - exp(-max(depth, 0.0)*1.1);
  vec3 alb = mix(vec3(0.05, 0.42, 0.44), vec3(0.008, 0.055, 0.19), t);
  float foam = smoothstep(0.16, 0.0, depth + (wv - 0.35)*0.16);
  alb = mix(alb, vec3(0.8), foam*0.85); alb = mix(alb, vec3(0.80, 0.85, 0.90), smoothstep(0.35, 0.6, vD.z));
  vec3 V = normalize(uCam - vW); float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0);
  vec3 R2 = reflect(-V, N); float glint = pow(max(dot(R2, uSun), 0.0), mix(380.0, 60.0, uZoom));
  vec3 c = shade(alb, N, vW, 0.0, 0.0) + sunTint(up)*glint*mix(5.0, 3.5, uZoom)*dayAt(vW) + skyTint(up)*fres*0.55*dayAt(vW);
  o = vec4(atmosphere(c, vW), clamp(0.5 + depth*1.3, 0.5, 0.97) + foam*0.4); }`);
  const instVS = FRAME + `layout(location=0) in vec3 aPos; layout(location=2) in vec3 aCol; layout(location=3) in vec4 aDat;
layout(location=4) in vec4 iA; layout(location=5) in vec4 iB; out vec3 vW, vC; out vec2 vE;
void main(){ vec3 up = normalize(iA.xyz), e = normalize(cross(vec3(0.0, 1.0, 0.0), up)), nn = cross(up, e);
  float cs = cos(iB.w), sn = sin(iB.w); vec3 l = aPos*iA.w; l.xz = vec2(l.x*cs - l.z*sn, l.x*sn + l.z*cs);
  vW = iA.xyz + e*l.x + up*l.y + nn*l.z; vC = mix(aCol, aCol*iB.rgb*2.2, aDat.z); vE = aDat.xw; gl_Position = uVP*vec4(vW, 1.0); }`;
  const pInst = prog(instVS, FRAME + LIGHT + `in vec3 vW, vC; in vec2 vE; out vec4 o;
void main(){ vec3 N = normalize(cross(dFdx(vW), dFdy(vW))); vec3 V = normalize(uCam - vW); if (dot(N, V) < 0.0) N = -N;
  float night = 1.0 - dayAt(vW);
  vec3 c = shade(vC, N, vW, 0.0, 0.0) + vec3(1.0, 0.75, 0.4)*vE.x*10.0*night + vC*vE.y*16.0;
  o = vec4(atmosphere(c, vW), 1.0); }`);
  const pInstD = prog(instVS, `in vec3 vW, vC; in vec2 vE; void main(){}`);
  const shellVS = FRAME + `layout(location=0) in vec3 aPos; uniform float uR; out vec3 vW; void main(){ vW = normalize(aPos)*uR; gl_Position = uVP*vec4(vW, 1.0); }`;
  const pCloud = prog(shellVS, FRAME + LIGHT + `in vec3 vW; out vec4 o;
void main(){ vec3 d = normalize(vW); float cv = cloudCover(d); if (cv < 0.01) discard;
  float st = stormAt(d), day = dayAt(vW), lit = max(dot(d, uSun) + 0.25, 0.0);
  vec3 warm = sunTint(d), rdv = normalize(vW - uCam);
  float body = 0.55 + 0.45*fbm(d*11.0);
  vec3 c = warm*(0.83*2.4*lit*body) + skyTint(d)*0.30*day + vec3(0.02, 0.03, 0.05);
  c *= mix(1.0, 0.28 + 0.45*(1.0 - cv), uInside); float sk = st*mix(smoothstep(0.2, 0.9, cv), 1.0, uInside); c *= mix(1.0, 0.16, sk); c = mix(c, vec3(0.045, 0.055, 0.08)*(0.5 + day), sk*0.55);
  c += warm*6.0*pow(max(dot(rdv, uSun), 0.0), 12.0)*cv*(1.0 - cv)*4.0*day;
  float fl = pow(vnoise(d*17.0 + 3.0), 11.0)*st*(1.0 - 0.9*day)*(1.0 - uInside); c += vec3(0.50, 0.66, 1.0)*fl*uBolt*70.0;
  vec3 fd = vW - uFlash.xyz; c += vec3(0.6, 0.72, 1.0)*uFlash.w*0.6/(1.0 + dot(fd, fd)*0.02);
  float va = acos(clamp(dot(d, uVolc.xyz), -1.0, 1.0)); c += vec3(1.0, 0.30, 0.05)*exp(-(va*va)/(uVolc.w*uVolc.w + 1e-6))*1.3*step(0.001, uVolc.w);
  o = vec4(c, cv*0.94*uCloud); }`);
  const pAtmo = prog(shellVS, FRAME + `in vec3 vW; out vec4 o; uniform float uR;
void main(){ vec3 rd = normalize(vW - uCam); float tca = max(dot(-uCam, rd), 0.0); vec3 p = uCam + rd*tca; float d = length(p);
  float inside = step(length(uCam), uR - 0.5); vec3 c;
  if (inside > 0.5) { vec3 upc = normalize(uCam); float se = dot(upc, uSun), zen = max(dot(rd, upc), 0.0), gold = uGold;
    vec3 hor = mix(vec3(0.55, 0.72, 0.98), vec3(1.0, 0.55, 0.20), gold), zc = mix(vec3(0.10, 0.28, 0.75), vec3(0.13, 0.17, 0.42), gold);
    c = mix(hor, zc, pow(zen, 0.42))*smoothstep(-0.2, 0.15, se)*0.62;
    c += vec3(1.0, 0.62, 0.25)*(pow(max(dot(rd, uSun), 0.0), 7.0)*0.45 + pow(max(dot(rd, uSun), 0.0), 40.0)*1.1);
  } else { float h = clamp((d - ${R.toFixed(1)})/(uR - ${R.toFixed(1)}), 0.0, 1.0); float a = exp(-h*3.6)*(1.0 - h);
    float s = dot(normalize(p), uSun); float lit = smoothstep(-0.28, 0.3, s);
    c = mix(vec3(1.0, 0.42, 0.14), vec3(0.28, 0.55, 1.0), smoothstep(-0.12, 0.28, s))*a*lit*1.9 + vec3(0.02, 0.04, 0.09)*a*0.6; }
  o = vec4(c, 1.0); }`);
  const pEmis = prog(`uniform mat4 uVP; layout(location=0) in vec3 aPos; layout(location=2) in vec3 aCol; layout(location=3) in vec4 aDat; out vec3 vC, vW; out vec4 vD;
void main(){ vC = aCol; vW = aPos; vD = aDat; gl_Position = uVP*vec4(aPos, 1.0); }`, `in vec3 vC, vW; in vec4 vD; out vec4 o; uniform vec3 uSun;
void main(){ float night = 1.0 - smoothstep(-0.12, 0.22, dot(normalize(vW), uSun)); float soft = 1.0 - vD.y*abs(vD.z); o = vec4(vC*mix(1.0, night, vD.x)*soft*soft, 1.0); }`);
  const FSVS = `out vec2 vUv; void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); vUv = p; gl_Position = vec4(p*2.0 - 1.0, 0.0, 1.0); }`;
  const pStar = prog(FSVS, `in vec2 vUv; out vec4 o; uniform mat4 uInv; uniform vec3 uCam, uSun; uniform float uStars;
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y)*p.z); }
float vn(vec3 p){ vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  return mix(mix(mix(hash13(i), hash13(i+vec3(1,0,0)), f.x), mix(hash13(i+vec3(0,1,0)), hash13(i+vec3(1,1,0)), f.x), f.y),
             mix(mix(hash13(i+vec3(0,0,1)), hash13(i+vec3(1,0,1)), f.x), mix(hash13(i+vec3(0,1,1)), hash13(i+vec3(1,1,1)), f.x), f.y), f.z); }
void main(){ vec4 q = uInv*vec4(vUv*2.0 - 1.0, 1.0, 1.0); vec3 rd = normalize(q.xyz/q.w - uCam);
  float band = exp(-abs(dot(rd, normalize(vec3(0.35, 0.75, -0.55))))*4.5);
  float dust = vn(rd*5.0)*0.6 + vn(rd*13.0)*0.4;
  vec3 g = rd*260.0, cell = floor(g); float h = hash13(cell); vec3 f = fract(g) - 0.5;
  float star = step(0.9968 - band*0.006, h)*smoothstep(0.42, 0.0, length(f))*(0.4 + 3.0*hash13(cell + 7.0));
  vec3 c = mix(vec3(0.8, 0.88, 1.0), vec3(1.0, 0.85, 0.7), hash13(cell + 2.0))*star + vec3(0.006, 0.008, 0.016);
  c += band*band*(vec3(0.050, 0.040, 0.075)*dust*dust*2.2 + vec3(0.03, 0.022, 0.018)*smoothstep(0.5, 0.9, dust));
  c *= uStars;
  float sd = max(dot(rd, uSun), 0.0); c += vec3(1.0, 0.86, 0.66)*(pow(sd, 3000.0)*70.0 + pow(sd, 90.0)*1.2 + pow(sd, 12.0)*0.12);
  o = vec4(c, 1.0); }`);
  const pBlur = prog(FSVS, `in vec2 vUv; out vec4 o; uniform sampler2D uT; uniform vec2 uDir; uniform float uThr;
void main(){ vec3 c = vec3(0.0); float ws = 0.0;
  for (int i = -4; i <= 4; i++){ float wt = exp(-float(i*i)/8.0); vec3 s = min(texture(uT, vUv + uDir*float(i)).rgb, vec3(64.0));
    if (uThr > 0.0){ float b = max(s.r, max(s.g, s.b)); s *= clamp((b - uThr)/max(b, 1e-4), 0.0, 1.0); } c += s*wt; ws += wt; }
  o = vec4(c/ws, 1.0); }`);
  const pFinal = prog(FSVS, `in vec2 vUv; out vec4 o; uniform sampler2D uT, uBlurT, uBloomT, uRayT; uniform float uTilt, uFocus, uExp, uRays, uGold; uniform vec2 uSunUV;
vec3 aces(vec3 x){ return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0); }
void main(){ vec3 c = texture(uT, vUv).rgb; float b = uTilt*smoothstep(0.10, 0.48, abs(vUv.y - uFocus));
  c = mix(c, texture(uBlurT, vUv).rgb, b); c += texture(uBloomT, vUv).rgb*0.9;
  if (uRays > 0.0) { vec2 dlt = (uSunUV - vUv)/44.0; vec2 p = vUv; vec3 acc = vec3(0.0); float dec = 1.0;
    for (int i = 0; i < 44; i++){ p += dlt; acc += texture(uRayT, clamp(p, 0.0, 1.0)).rgb*dec; dec *= 0.965; }
    c += acc/44.0*uRays*vec3(1.0, 0.82, 0.55); }
  c = aces(c*uExp); c = pow(c, vec3(1.0/2.2)); c = mix(c, c*c*(3.0 - 2.0*c), 0.22);
  float l = dot(c, vec3(0.299, 0.587, 0.114)); c = mix(vec3(l), c, 1.14); c *= mix(vec3(1.0), vec3(1.07, 1.0, 0.88), uGold);
  vec2 d = vUv - 0.5; c *= 1.0 - 0.6*dot(d, d); o = vec4(c, 1.0); }`);

  // ------------------------------------------------------------------ models and instances (close shots)
  function mesh(build) { const P = [], C = [], D = [];
    const tri = (a, b, c, col3, em, tm, al) => { for (const v of [a, b, c]) { P.push(v[0], v[1], v[2]); C.push(col3[0], col3[1], col3[2]); D.push(em || 0, 0, tm || 0, al || 0); } };
    const frustum = (y0, y1, r0, r1, k, col3, tm, al) => { for (let i = 0; i < k; i++) { const a0 = i / k * 6.2832, a1 = (i + 1) / k * 6.2832, c0 = Math.cos(a0), s0 = Math.sin(a0), c1 = Math.cos(a1), s1 = Math.sin(a1);
      tri([c0 * r0, y0, s0 * r0], [c1 * r0, y0, s1 * r0], [c1 * r1, y1, s1 * r1], col3, 0, tm, al); if (r1 > 0) tri([c0 * r0, y0, s0 * r0], [c1 * r1, y1, s1 * r1], [c0 * r1, y1, s0 * r1], col3, 0, tm, al); } };
    const cone = (y0, y1, r, k, col3, tm, al) => frustum(y0, y1, r, 0, k, col3, tm, al);
    const box = (x0, y0, z0, x1, y1, z1, col3, em) => { const v = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
      for (const q4 of [[0, 1, 2, 3], [5, 4, 7, 6], [4, 0, 3, 7], [1, 5, 6, 2], [3, 2, 6, 7]]) { tri(v[q4[0]], v[q4[1]], v[q4[2]], col3, em); tri(v[q4[0]], v[q4[2]], v[q4[3]], col3, em); } };
    build({ tri, cone, box, frustum });
    return { n: P.length / 3, attrs: [{ d: new Float32Array(P), s: 3 }, null, { d: new Float32Array(C), s: 3 }, { d: new Float32Array(D), s: 4 }] }; }
  const M = {
    conifer: mesh(m => { m.cone(-0.15, 0.28, 0.045, 5, [0.16, 0.10, 0.06]); m.cone(0.16, 0.62, 0.27, 6, [0.04, 0.15, 0.07], 1); m.cone(0.40, 0.98, 0.19, 6, [0.05, 0.19, 0.08], 1); }),
    broad: mesh(m => { m.cone(-0.15, 0.36, 0.05, 5, [0.17, 0.11, 0.06]); m.frustum(0.24, 0.50, 0.20, 0.36, 7, [0.05, 0.19, 0.05], 1); m.frustum(0.50, 0.92, 0.36, 0.0, 7, [0.09, 0.28, 0.06], 1); }),
    palm: mesh(m => { m.cone(-0.15, 0.72, 0.035, 5, [0.24, 0.17, 0.09]); for (let i = 0; i < 6; i++) { const a = i / 6 * 6.2832, c = Math.cos(a), s = Math.sin(a), a2 = a + 0.5; m.tri([0, 0.72, 0], [c * 0.36, 0.60, s * 0.36], [Math.cos(a2) * 0.2, 0.78, Math.sin(a2) * 0.2], [0.10, 0.30, 0.07], 0, 1); } }),
    house: mesh(m => { m.box(-0.15, -0.15, -0.11, 0.15, 0.16, 0.11, [0.50, 0.44, 0.36]); m.box(-0.155, 0.04, -0.04, -0.15, 0.11, 0.04, [1, 0.8, 0.5], 1); m.box(0.15, 0.04, -0.04, 0.155, 0.11, 0.04, [1, 0.8, 0.5], 1);
      const a = [-0.18, 0.16, -0.14], b = [0.18, 0.16, -0.14], c = [0.18, 0.16, 0.14], d = [-0.18, 0.16, 0.14], r0 = [-0.18, 0.32, 0], r1 = [0.18, 0.32, 0], rc = [0.36, 0.14, 0.09];
      m.tri(a, b, r1, rc, 0, 1); m.tri(a, r1, r0, rc, 0, 1); m.tri(c, d, r0, rc, 0, 1); m.tri(c, r0, r1, rc, 0, 1); m.tri(d, a, r0, rc, 0, 1); m.tri(b, c, r1, rc, 0, 1); }),
    tower: mesh(m => { m.box(-0.13, -0.15, -0.13, 0.13, 0.55, 0.13, [0.52, 0.47, 0.40]); m.box(-0.135, 0.3, -0.05, -0.13, 0.42, 0.05, [1, 0.8, 0.5], 1); m.cone(0.55, 0.85, 0.2, 4, [0.36, 0.14, 0.09], 1); }),
    hut: mesh(m => { m.frustum(-0.15, 0.15, 0.17, 0.16, 7, [0.40, 0.28, 0.16]); m.cone(0.11, 0.44, 0.29, 8, [0.56, 0.42, 0.16]); m.box(-0.03, -0.02, 0.15, 0.03, 0.10, 0.175, [1, 0.7, 0.35], 1); }),
    temple: mesh(m => { const st = [0.40, 0.34, 0.27]; for (let i = 0; i < 6; i++) { const s = 0.50 - 0.072 * i, y = -0.15 + 0.17 * i; m.box(-s, y, -s, s, y + 0.19, s, [st[0] * (1 - i * 0.03), st[1] * (1 - i * 0.03), st[2] * (1 - i * 0.03)]); }
      m.cone(0.87, 1.30, 0.12, 6, [0.62, 0.48, 0.20]); m.box(-0.07, 0.04, 0.5, 0.07, 0.19, 0.515, [1, 0.72, 0.35], 1); m.box(-0.62, -0.15, 0.36, -0.42, 0.05, 0.56, st); m.box(0.42, -0.15, 0.36, 0.62, 0.05, 0.56, st); }),
    fire: mesh(m => { m.box(-0.09, -0.05, -0.02, 0.09, 0.02, 0.02, [0.12, 0.08, 0.05]); m.box(-0.02, -0.05, -0.09, 0.02, 0.02, 0.09, [0.12, 0.08, 0.05]); m.cone(0.0, 0.24, 0.07, 5, [1.0, 0.42, 0.06], 0, 1); m.cone(0.0, 0.14, 0.09, 5, [1.0, 0.25, 0.03], 0, 1); }),
    boat: mesh(m => { const hc = [0.22, 0.13, 0.07]; m.box(-0.22, -0.03, -0.06, 0.22, 0.06, 0.06, hc); m.tri([0.22, -0.03, -0.06], [0.36, 0.08, 0], [0.22, 0.06, -0.06], hc); m.tri([0.22, -0.03, 0.06], [0.22, 0.06, 0.06], [0.36, 0.08, 0], hc); m.tri([0.22, 0.06, -0.06], [0.36, 0.08, 0], [0.22, 0.06, 0.06], hc);
      m.box(-0.012, 0.06, -0.012, 0.012, 0.46, 0.012, [0.3, 0.2, 0.1]); m.tri([0.0, 0.42, 0.0], [0.0, 0.12, 0.0], [-0.2, 0.14, 0.02], [0.75, 0.68, 0.55]); }),
    sheep: mesh(m => { m.box(-0.07, 0.03, -0.04, 0.07, 0.12, 0.04, [0.74, 0.72, 0.66]); m.box(0.07, 0.07, -0.025, 0.11, 0.13, 0.025, [0.14, 0.12, 0.11]); m.box(-0.06, -0.15, -0.035, -0.04, 0.03, -0.015, [0.14, 0.12, 0.11]); m.box(0.04, -0.15, 0.015, 0.06, 0.03, 0.035, [0.14, 0.12, 0.11]); }),
  };
  const inst = {}; for (const k in M) inst[k] = [];
  if (treeAng > 0) {
    const cells = [], cnt = geo.within(main.cell, treeAng, cells), p = V3.create(), ce = V3.create(), cn = V3.create(), q2 = V3.create();
    const ground = (dv, c) => { let ws = 1e-9, es = 0; const add = k => { const dx = geo.pos[3 * k] - dv[0], dy = geo.pos[3 * k + 1] - dv[1], dz = geo.pos[3 * k + 2] - dv[2]; const wt = 1 / (Math.pow(dx * dx + dy * dy + dz * dz, 1.5) + 1e-9); ws += wt; es += wt * w.elev[k]; };
      add(c); for (let k = 0; k < geo.nbrN[c]; k++) add(geo.nbr[6 * c + k]); return R + es / ws; };
    const put = (list, dv, c, scale, tint, yaw, rad) => { const gr = rad || ground(dv, c); list.push(dv[0] * gr, dv[1] * gr, dv[2] * gr, scale, tint[0], tint[1], tint[2], yaw); };
    const place = (list, c, jit, scale, tint, salt, rad) => { geo.dir(c, p); V3.basis(p, ce, cn);
      const a = U.hashf(c, salt) * 6.2832, r = Math.sqrt(U.hashf(c, salt + 1)) * jit * geo.step;
      V3.addScaled(q2, p, ce, Math.cos(a) * r); V3.addScaled(q2, q2, cn, Math.sin(a) * r); V3.norm(q2, q2); put(list, q2, c, scale, tint, U.hashf(c, salt + 2) * 6.2832, rad); };
    const local = (dx, dy, o) => { V3.set(o, f[0] + (e[0] * dx + nn[0] * dy) / R, f[1] + (e[1] * dx + nn[1] * dy) / R, f[2] + (e[2] * dx + nn[2] * dy) / R); return V3.norm(o, o); };
    const roofs = [[0.9, 0.45, 0.2], [0.75, 0.3, 0.25], [0.85, 0.6, 0.3]], one = [0.45, 0.45, 0.45], village = style === 'village';
    if (village) {                                   // temple, plaza fire and a ring of huts at the heart of the main settlement
      put(inst.temple, local(0, 0, q2), main.cell, 1.25, one, 0);
      put(inst.fire, local(-dE * 1.05, -dN * 1.05, q2), main.cell, 1.5, one, 0);
      for (let i = 0; i < 46; i++) { const a = i * 2.39996, r = 1.25 + 0.34 * Math.sqrt(i) + U.hashf(i, 5) * 0.2; if (Math.cos(a) * dE + Math.sin(a) * dN < -0.8 && r < 2.0) continue;
        local(Math.cos(a) * r, Math.sin(a) * r, q2); const c = geo.cellAt(q2[0], q2[1], q2[2], main.cell); if (w.elev[c] < 0.03) continue; put(inst.hut, q2, c, 0.95 + 0.5 * U.hashf(i, 6), one, U.hashf(i, 7) * 6.28); }
    }
    for (let i = 0; i < cnt; i++) {
      const c = cells[i], b = w.biome[c], a = geo.angle(main.cell, c);
      if (w.elev[c] < 0) { if (w.elev[c] > -1.6 && a < 0.10 && U.hashf(c, 77) < 0.16) place(inst.boat, c, 0.6, 1.25, one, 78, R + 0.02); continue; }
      if (w.elev[c] < 0.03 || w.water[c] > 0.03) continue;
      if (village && a < 0.028) continue;
      if (urban[c] > 0.05) { const k = Math.round(village ? 3 + urban[c] * 9 : 5 + urban[c] * 18);
        for (let j = 0; j < k; j++) place(village ? inst.hut : (U.hashf(c, 90 + j) < 0.07 + urban[c] * 0.06 ? inst.tower : inst.house), c, 0.62, village ? 0.9 + 0.4 * U.hashf(c, 200 + j) : 0.8 + 0.5 * U.hashf(c, 200 + j), village ? one : roofs[(U.hashf(c, 300 + j) * 3) | 0], 400 + j * 3); continue; }
      if (farm[c] > 0) { if (U.hashf(c, 50) < 0.3) place(village ? inst.hut : inst.house, c, 0.4, 0.85, village ? one : roofs[0], 60); if (U.hashf(c, 51) < 0.45) place(inst.broad, c, 0.6, 0.8, one, 70);
        if (a < 0.12 && ext[4 * c + 1] < 0.9 && U.hashf(c, 52) < 0.7) for (let j = 0; j < 5; j++) place(inst.sheep, c, 0.55, 1.2, one, 500 + j * 3); continue; }
      if (w.slope[c] > 0.9) continue; const v = w.veg[c], k = v > 0.78 ? 6 : v > 0.5 ? 4 : v > 0.22 ? 2 : 0;
      const list = (b === B.TAIGA || (b === B.FOREST && w.temp[c] < 9) || b === B.TUNDRA) ? inst.conifer : (b === B.SAVANNA || b === B.BEACH || (b === B.RAINFOREST && U.hashf(c, 9) < 0.35)) ? inst.palm : inst.broad;
      for (let j = 0; j < k; j++) { const g = 0.36 + 0.22 * U.hashf(c, 40 + j); place(list, c, 0.78, (0.85 + 0.75 * U.hashf(c, 20 + j)) * (b === B.RAINFOREST ? 1.25 : 1), [g, g * (0.95 + 0.25 * U.hashf(c, 41 + j)), g * 0.9], 30 + j * 3); }
    }
  }
  const IV = {}; for (const k in inst) { const a = new Float32Array(inst[k]); IV[k] = { v: vao(M[k].attrs, null, a), n: M[k].n, c: a.length / 8 }; }

  // ------------------------------------------------------------------ emissive geometry: lightning, rain, roads, aurora, meteor
  const EP = [], EC = [], ED = [], LP = [], LC = [], LD = [];
  const fwd = V3.create(), rgt = V3.create(), upv = V3.create(); V3.sub(fwd, tgt, eye); V3.norm(fwd, fwd); V3.cross(rgt, fwd, up); V3.norm(rgt, rgt); V3.cross(upv, rgt, fwd);
  const etri = (a, b, c, ca, cb, cc, night, soft) => { const vs = [[a, ca, soft ? soft[0] : 0], [b, cb, soft ? soft[1] : 0], [c, cc, soft ? soft[2] : 0]];
    for (const [v, cl, s] of vs) { EP.push(v[0], v[1], v[2]); EC.push(cl[0], cl[1], cl[2]); ED.push(night || 0, soft ? 1 : 0, s, 0); } };
  function ribbon(pts, wFn, cFn, night) { const sd = V3.create(), tc = V3.create(), sg = V3.create(); let pl = null, pr = null, pc = null;
    for (let i = 0; i < pts.length; i++) { const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)]; V3.sub(sg, b, a); V3.sub(tc, eye, pts[i]); V3.cross(sd, sg, tc); V3.norm(sd, sd);
      const t = i / (pts.length - 1), wd = wFn(t), l = [pts[i][0] - sd[0] * wd, pts[i][1] - sd[1] * wd, pts[i][2] - sd[2] * wd], r = [pts[i][0] + sd[0] * wd, pts[i][1] + sd[1] * wd, pts[i][2] + sd[2] * wd], c = cFn(t);
      if (pl) { etri(pl, pr, r, pc, pc, c, night, [-1, 1, 1]); etri(pl, r, l, pc, c, c, night, [-1, 1, -1]); } pl = l; pr = r; pc = c; } }
  const er = U.rng(SEED * 31 + 7);
  if (bolt && CLOSE) {                               // lightning under the storm, and its flash
    const sd = [storms[0], storms[1], storms[2]];
    for (let bi = 0; bi < 3; bi++) {
      const top = mixv(V3.create(), [[sd, 1], [e, (er.f() - 0.5) * 0.22], [nn, (er.f() - 0.5) * 0.22]]), bot = mixv(V3.create(), [[top, 1], [e, (er.f() - 0.5) * 0.05], [nn, (er.f() - 0.5) * 0.05]]);
      const gc = geo.cellAt(bot[0], bot[1], bot[2]); const a = [top[0] * (CLOUD_R - 0.6), top[1] * (CLOUD_R - 0.6), top[2] * (CLOUD_R - 0.6)], gr = R + Math.max(w.elev[gc], 0), b = [bot[0] * gr, bot[1] * gr, bot[2] * gr];
      let pts = [a, b]; for (let lvl = 0, amp = 2.2; lvl < 5; lvl++, amp *= 0.55) { const np = [pts[0]]; for (let i = 1; i < pts.length; i++) { const m = [(pts[i - 1][0] + pts[i][0]) / 2 + (er.f() - 0.5) * amp, (pts[i - 1][1] + pts[i][1]) / 2 + (er.f() - 0.5) * amp, (pts[i - 1][2] + pts[i][2]) / 2 + (er.f() - 0.5) * amp]; np.push(m, pts[i]); } pts = np; }
      const I = bi === 0 ? 70 : 34; ribbon(pts, () => bi === 0 ? 0.16 : 0.09, () => [0.72 * I, 0.82 * I, 1.0 * I], 0);
      for (let br = 0; br < 4; br++) { const s0 = 4 + ((er.f() * (pts.length - 12)) | 0), bp = [pts[s0]]; let cur = pts[s0].slice(); const dv = [(er.f() - 0.5) * 1.4, (er.f() - 0.5) * 1.4, (er.f() - 0.5) * 1.4];
        for (let j = 0; j < 7; j++) { cur = [cur[0] + dv[0] * 0.5 + (er.f() - 0.5) * 0.5 - top[0] * 0.5, cur[1] + dv[1] * 0.5 + (er.f() - 0.5) * 0.5 - top[1] * 0.5, cur[2] + dv[2] * 0.5 + (er.f() - 0.5) * 0.5 - top[2] * 0.5]; bp.push(cur); }
        ribbon(bp, t => 0.06 * (1 - t), () => [14, 17, 22], 0); }
      if (bi === 0) flash.set([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, 2.6]);
    }
  }
  if (CLOSE) {                                       // rain under the storm
    const sd = [storms[0], storms[1], storms[2]], sr = storms[3], k = SHOT === 'storm' ? 9000 : 5000, dv = V3.create();
    for (let i = 0; i < k; i++) { const a = er.f() * 6.2832, r = Math.sqrt(er.f()) * sr * 0.95; mixv(dv, [[sd, 1], [e, Math.cos(a) * r], [nn, Math.sin(a) * r]]);
      const h = R + 0.3 + er.f() * (CLOUD_R - R - 1), len = 0.9 + er.f() * 0.8, it = 0.10 + er.f() * 0.14;
      LP.push(dv[0] * h + e[0] * 0.12, dv[1] * h + e[1] * 0.12, dv[2] * h + e[2] * 0.12, dv[0] * (h - len), dv[1] * (h - len), dv[2] * (h - len));
      for (let j = 0; j < 2; j++) { LC.push(0.55 * it, 0.62 * it, 0.78 * it); LD.push(0, 0, 0, 0); } }
  }
  if (!CLOSE) {                                      // lit roads between neighbouring cities (night only)
    const a = V3.create(), b = V3.create(), m = V3.create();
    for (let i = 0; i < cities.length; i++) { const near = []; for (let j = 0; j < cities.length; j++) if (j !== i) { const d = geo.angle(cities[i].cell, cities[j].cell); if (d < 0.42) near.push([d, j]); }
      near.sort((x, y) => x[0] - y[0]);
      for (const nj of near.slice(0, 3)) { const j = nj[1];
        geo.dir(cities[i].cell, a); geo.dir(cities[j].cell, b); const pts = []; let ok = true;
        for (let s = 0; s <= 14; s++) { V3.slerp(m, a, b, s / 14); const c = geo.cellAt(m[0], m[1], m[2]); if (w.elev[c] < 0) { ok = false; break; } const rr = R + w.elev[c] + 0.25; pts.push([m[0] * rr, m[1] * rr, m[2] * rr]); }
        if (!ok) continue; const I = 2.2 + cities[i].size * 1.2;
        for (let s = 1; s < pts.length; s++) { LP.push(pts[s - 1][0], pts[s - 1][1], pts[s - 1][2], pts[s][0], pts[s][1], pts[s][2]); for (let t = 0; t < 2; t++) { LC.push(1.0 * I, 0.66 * I, 0.30 * I); LD.push(1, 0, 0, 0); } } } }
  }
  if (!CLOSE) {                                      // aurora curtains round the north pole, and a meteor
    const seg = 220, lat = 1.18;
    for (let ring = 0; ring < 2; ring++) for (let i = 0; i < seg; i++) { const a0 = i / seg * 6.2832, a1 = (i + 1) / seg * 6.2832, la = lat - ring * 0.07;
      const wob = t => la + 0.035 * Math.sin(t * 7 + ring * 2) + 0.02 * Math.sin(t * 19);
      const mk = (t, rr) => { const l2 = wob(t); return [Math.cos(l2) * Math.cos(t) * rr, Math.sin(l2) * rr, Math.cos(l2) * Math.sin(t) * rr]; };
      const s0 = 0.35 + 0.65 * Math.pow(U.noise3(Math.cos(a0) * 6, Math.sin(a0) * 6, ring * 3) * 0.5 + 0.5, 1.5), s1 = 0.35 + 0.65 * Math.pow(U.noise3(Math.cos(a1) * 6, Math.sin(a1) * 6, ring * 3) * 0.5 + 0.5, 1.5);
      const lo = R + 10, hi = R + 27, g = [0.10, 1.0, 0.45], pu = [0.42, 0.14, 0.85], I = 2.0 * (0.35 + 0.65 * U.hashf(i >> 1, ring + 5));
      const A = mk(a0, lo), Bq = mk(a1, lo), Cq = mk(a1, hi), Dq = mk(a0, hi);
      const cg0 = [g[0] * I * s0, g[1] * I * s0, g[2] * I * s0], cg1 = [g[0] * I * s1, g[1] * I * s1, g[2] * I * s1], cp0 = [pu[0] * s0 * 0.9, pu[1] * s0 * 0.9, pu[2] * s0 * 0.9], cp1 = [pu[0] * s1 * 0.9, pu[1] * s1 * 0.9, pu[2] * s1 * 0.9];
      etri(A, Bq, Cq, cg0, cg1, cp1, 1); etri(A, Cq, Dq, cg0, cp1, cp0, 1); }
    const head = [eye[0] + fwd[0] * 330 + rgt[0] * 150 + upv[0] * 78, eye[1] + fwd[1] * 330 + rgt[1] * 150 + upv[1] * 78, eye[2] + fwd[2] * 330 + rgt[2] * 150 + upv[2] * 78];
    const pts = []; for (let i = 0; i <= 16; i++) { const t = i / 16; pts.push([head[0] + (rgt[0] * 62 + upv[0] * 34) * t, head[1] + (rgt[1] * 62 + upv[1] * 34) * t, head[2] + (rgt[2] * 62 + upv[2] * 34) * t]); }
    ribbon(pts, t => 2.6 * (1 - t) + 0.1, t => { const k2 = Math.pow(1 - t, 2.2); return [14 * k2 + 0.6 * (1 - t), 5.5 * k2 + 0.15 * (1 - t), 1.0 * k2]; }, 0);
    for (const sI of [[3.4, 30], [9, 3]]) { const sz = sI[0], I = sI[1]; const q0 = c => [head[0] + rgt[0] * c[0] * sz + upv[0] * c[1] * sz, head[1] + rgt[1] * c[0] * sz + upv[1] * c[1] * sz, head[2] + rgt[2] * c[0] * sz + upv[2] * c[1] * sz];
      const cc = [1.0 * I, 0.75 * I, 0.4 * I], z = [0, 0, 0]; for (let i = 0; i < 12; i++) { const a0 = i / 12 * 6.2832, a1 = (i + 1) / 12 * 6.2832; etri(head, q0([Math.cos(a0), Math.sin(a0)]), q0([Math.cos(a1), Math.sin(a1)]), cc, z, z, 0); } }
  }
  const emisV = vao([{ d: new Float32Array(EP), s: 3 }, null, { d: new Float32Array(EC), s: 3 }, { d: new Float32Array(ED), s: 4 }]), emisN = EP.length / 3;
  const lineV = vao([{ d: new Float32Array(LP), s: 3 }, null, { d: new Float32Array(LC), s: 3 }, { d: new Float32Array(LD), s: 4 }]), lineN = LP.length / 3;

  // ------------------------------------------------------------------ buffers and matrices
  const terr = vao([{ d: pos, s: 3 }, { d: nrm, s: 3 }, { d: col, s: 3 }, { d: dat, s: 4 }, { d: ext, s: 4 }], geo.tris);
  const proj = M4.create(), view = M4.create(), vp = M4.create(), inv = M4.create(), lv = M4.create(), lp = M4.create(), lvp = M4.create(), bias = M4.create(), sm = M4.create();
  const dist = V3.dist(eye, tgt), elen = V3.len(eye), alt = elen - R;
  M4.perspective(proj, fov, W / H, Math.max(0.25, alt * 0.05), Math.sqrt(Math.max(elen * elen - 93 * 93, 1)) + 160);
  M4.lookAt(view, eye, tgt, up); M4.mul(vp, proj, view); M4.invert(inv, vp);
  const rr = U.clamp(1.6 * dist, 12, R + 22), big = dist > 76, c0 = big ? [0, 0, 0] : tgt, le = V3.create(); V3.addScaled(le, c0, sun, 260);
  M4.lookAt(lv, le, c0, Math.abs(sun[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0]); const r2 = big ? R + 22 : rr;
  M4.ortho(lp, -r2, r2, -r2, r2, 260 - (R + 22), 260 + (R + 22)); M4.mul(lvp, lp, lv);
  bias.set([0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, 0.5, 0.5, 0.5, 1]); M4.mul(sm, bias, lvp);
  const sp = [eye[0] + sun[0] * 1e4, eye[1] + sun[1] * 1e4, eye[2] + sun[2] * 1e4], cw = vp[3] * sp[0] + vp[7] * sp[1] + vp[11] * sp[2] + vp[15];
  const sunUV = [(vp[0] * sp[0] + vp[4] * sp[1] + vp[8] * sp[2] + vp[12]) / cw * 0.5 + 0.5, (vp[1] * sp[0] + vp[5] * sp[1] + vp[9] * sp[2] + vp[13]) / cw * 0.5 + 0.5];
  const rays = (CLOSE && cw > 0) ? (SHOT === 'golden' ? 0.7 : 0.45) : 0, gold = CLOSE ? U.sstep(0.5, 0.05, V3.dot(eye, sun) / elen) : 0;

  const SH = 4096, shadow = target(SH, SH, false, true), scene = target(W, H, true), res = target(W, H), half = target(W >> 1, H >> 1), qa = target(W >> 2, H >> 2), qb = target(W >> 2, H >> 2), ba = target(W >> 3, H >> 3), bb = target(W >> 3, H >> 3), ra = target(W >> 2, H >> 2), rb = target(W >> 2, H >> 2);
  const use = (P, mat) => { gl.useProgram(P.p); gl.uniformMatrix4fv(P.u.uVP, false, mat); if (P.u.uShadowM) gl.uniformMatrix4fv(P.u.uShadowM, false, sm);
    if (P.u.uCam) gl.uniform3fv(P.u.uCam, eye); if (P.u.uSun) gl.uniform3fv(P.u.uSun, sun); if (P.u.uCloud) gl.uniform1f(P.u.uCloud, cloud); if (P.u.uZoom) gl.uniform1f(P.u.uZoom, zoom); if (P.u.uBolt) gl.uniform1f(P.u.uBolt, bolt); if (P.u.uGold) gl.uniform1f(P.u.uGold, goldSky); if (P.u.uInside) gl.uniform1f(P.u.uInside, elen < CLOUD_R ? 1 : 0);
    if (P.u['uStorm[0]']) gl.uniform4fv(P.u['uStorm[0]'], storms); if (P.u['uCyc[0]']) gl.uniform4fv(P.u['uCyc[0]'], cyc); if (P.u.uVolc) gl.uniform4fv(P.u.uVolc, volc); if (P.u.uFlash) gl.uniform4fv(P.u.uFlash, flash);
    if (P.u.uShadow) { gl.activeTexture(gl.TEXTURE7); gl.bindTexture(gl.TEXTURE_2D, shadow.tex); gl.uniform1i(P.u.uShadow, 7); } };
  const drawInst = (P, mat) => { use(P, mat); for (const k in IV) { const o = IV[k]; if (!o.c) continue; gl.bindVertexArray(o.v); gl.drawArraysInstanced(gl.TRIANGLES, 0, o.n, o.c); } };
  const bind = t => { gl.bindFramebuffer(gl.FRAMEBUFFER, t ? t.fbo : null); gl.viewport(0, 0, t ? t.w : W, t ? t.h : H); };
  const fs = () => { gl.bindVertexArray(null); gl.drawArrays(gl.TRIANGLES, 0, 3); };
  const blur = (src, dst, dx, dy, thr) => { bind(dst); gl.useProgram(pBlur.p); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, src.tex); gl.uniform1i(pBlur.u.uT, 0); gl.uniform2f(pBlur.u.uDir, dx, dy); gl.uniform1f(pBlur.u.uThr, thr || 0); fs(); };
  const sphere = () => { gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0); };

  function render() {
    bind(shadow); gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.clear(gl.DEPTH_BUFFER_BIT); gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1.5, 4.0);
    gl.activeTexture(gl.TEXTURE7); gl.bindTexture(gl.TEXTURE_2D, null);
    gl.useProgram(pTerrD.p); gl.uniformMatrix4fv(pTerrD.u.uVP, false, lvp); sphere();
    gl.useProgram(pInstD.p); gl.uniformMatrix4fv(pInstD.u.uVP, false, lvp); for (const k in IV) { const o = IV[k]; if (!o.c) continue; gl.bindVertexArray(o.v); gl.drawArraysInstanced(gl.TRIANGLES, 0, o.n, o.c); }
    gl.disable(gl.POLYGON_OFFSET_FILL);
    bind(scene); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.disable(gl.DEPTH_TEST); gl.useProgram(pStar.p); gl.uniformMatrix4fv(pStar.u.uInv, false, inv); gl.uniform3fv(pStar.u.uCam, eye); gl.uniform3fv(pStar.u.uSun, sun);
    gl.uniform1f(pStar.u.uStars, alt < ATMO_R - R ? 1 - U.sstep(-0.15, 0.1, V3.dot(eye, sun) / elen) : 1); fs();
    gl.enable(gl.DEPTH_TEST);
    use(pTerr, vp); sphere();
    drawInst(pInst, vp);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    use(pSea, vp); sphere();
    gl.depthMask(false); gl.enable(gl.CULL_FACE);
    gl.cullFace(gl.FRONT); gl.blendFunc(gl.ONE, gl.ONE); use(pAtmo, vp); gl.uniform1f(pAtmo.u.uR, ATMO_R); sphere();
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.cullFace(elen > CLOUD_R ? gl.BACK : gl.FRONT); use(pCloud, vp); gl.uniform1f(pCloud.u.uR, CLOUD_R); sphere();
    gl.disable(gl.CULL_FACE); gl.cullFace(gl.BACK); gl.blendFunc(gl.ONE, gl.ONE);
    gl.useProgram(pEmis.p); gl.uniformMatrix4fv(pEmis.u.uVP, false, vp); gl.uniform3fv(pEmis.u.uSun, sun);
    if (emisN) { gl.bindVertexArray(emisV); gl.drawArrays(gl.TRIANGLES, 0, emisN); }
    if (lineN) { gl.bindVertexArray(lineV); gl.drawArrays(gl.LINES, 0, lineN); }
    gl.disable(gl.BLEND); gl.depthMask(true); gl.disable(gl.DEPTH_TEST);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, scene.fbo); gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, res.fbo); gl.blitFramebuffer(0, 0, W, H, 0, 0, W, H, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    blur(res, half, 0, 0); blur(half, qa, 2 / W, 0); blur(qa, qb, 0, 4 / H); blur(qb, qa, 6 / W, 0); blur(qa, qb, 0, 6 / H);
    blur(half, ba, 4 / W, 0, 3.0); blur(ba, bb, 0, 8 / H); blur(bb, ba, 12 / W, 0); blur(ba, bb, 0, 12 / H);
    blur(half, ra, 3 / W, 0, 2.2); blur(ra, rb, 0, 5 / H);
    bind(null); gl.useProgram(pFinal.p);
    [[res, 'uT'], [qb, 'uBlurT'], [bb, 'uBloomT'], [rb, 'uRayT']].forEach((tn, i) => { gl.activeTexture(gl.TEXTURE0 + i); gl.bindTexture(gl.TEXTURE_2D, tn[0].tex); gl.uniform1i(pFinal.u[tn[1]], i); });
    gl.uniform1f(pFinal.u.uTilt, tilt); gl.uniform1f(pFinal.u.uFocus, focusY); gl.uniform1f(pFinal.u.uExp, expo); gl.uniform1f(pFinal.u.uRays, rays); gl.uniform1f(pFinal.u.uGold, gold); gl.uniform2f(pFinal.u.uSunUV, sunUV[0], sunUV[1]); fs();
  }
  render(); requestAnimationFrame(() => { render(); requestAnimationFrame(() => { render();
    let ni = 0; for (const k in inst) ni += inst[k].length / 8;
    window.INFO = { shot: SHOT, instances: ni, emisTris: emisN / 3, lines: lineN / 2, sunUV, err: gl.getError() }; window.READY = true; }); });
})();
