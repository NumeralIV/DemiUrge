// DEMIURGE — concept render (dev only). Draws a planet from the real generator with a compact standalone WebGL2 renderer
// to show the intended look: faceted terrain, depth-coloured sea with foam, cloud shell with ground shadows, atmosphere rim,
// sun shadow map, instanced trees and houses, night-side city lights, bloom, tilt-shift, ACES.
(function () {
  'use strict';
  const DM = window.DM, U = DM.U, V3 = DM.V3, M4 = DM.M4, R = DM.R, B = DM.BIOME;
  const q = new URLSearchParams(location.search);
  const SHOT = q.get('shot') || 'orbit', SEED = +(q.get('seed') || 1), AZ = +(q.get('az') || 0), CITY = +(q.get('city') || 0);
  const canvas = document.getElementById('c');
  const W = canvas.width = canvas.clientWidth, H = canvas.height = canvas.clientHeight;
  const gl = canvas.getContext('webgl2', { antialias: false, alpha: false });
  gl.getExtension('EXT_color_buffer_float'); gl.getExtension('OES_texture_float_linear');

  // ------------------------------------------------------------------ world
  const w = DM.State.create({ seed: SEED, L: 6 }), geo = w.geo, n = w.n;
  DM.Terrain.generate(w); w.seaLevel = 0; w.heat = 0; w.phase = 'history';
  DM.Terrain.derive(w); DM.Climate.update(w); DM.Hydro.update(w); DM.Life.classify(w); DM.Life.fill(w, 1);
  for (let i = 0; i < 60; i++) DM.Climate.stepIce(w);
  DM.Climate.update(w); DM.Life.classify(w); DM.Life.habitability(w);

  // a stand-in civilization: towns on the best ground near coasts and rivers (the sim will grow real ones)
  const rng = U.rng(SEED * 977 + 5), cities = [], urban = new Float32Array(n), farm = new Float32Array(n);
  const order = []; for (let c = 0; c < n; c++) if (w.hab[c] > 0.45 && (w.coast[c] <= 2 || w.fresh[c] <= 1)) order.push(c);
  order.sort((a, b) => (w.hab[b] + U.hashf(b, 7) * 0.25) - (w.hab[a] + U.hashf(a, 7) * 0.25));
  const tmpN = [];
  for (const c of order) {
    if (cities.length >= 70) break;
    let ok = true; for (const o of cities) if (geo.angle(c, o.cell) < 0.11) { ok = false; break; }
    if (!ok) continue;
    const size = cities.length < 8 ? 3 : cities.length < 24 ? 2 : 1;
    cities.push({ cell: c, size });
    const k = geo.ring(c, size + 2, tmpN);
    for (let i = 0; i < k; i++) {
      const d = tmpN[i]; if (w.elev[d] < 0) continue;
      const a = geo.angle(c, d) / geo.step;
      if (a <= size - 0.5) urban[d] = Math.max(urban[d], 1 - a / (size + 0.5));
      else if (w.hab[d] > 0.2 && U.hashf(d, 3) < 0.8) farm[d] = Math.max(farm[d], 1);
    }
  }

  // ------------------------------------------------------------------ colours
  const BC = []; BC[B.SEA] = [0.16, 0.15, 0.12]; BC[B.SHALLOWS] = [0.34, 0.30, 0.20]; BC[B.BEACH] = [0.60, 0.52, 0.34];
  BC[B.DESERT] = [0.56, 0.42, 0.24]; BC[B.SAVANNA] = [0.40, 0.36, 0.14]; BC[B.GRASSLAND] = [0.20, 0.35, 0.10];
  BC[B.FOREST] = [0.075, 0.21, 0.06]; BC[B.RAINFOREST] = [0.04, 0.16, 0.05]; BC[B.TAIGA] = [0.08, 0.17, 0.11];
  BC[B.TUNDRA] = [0.33, 0.32, 0.25]; BC[B.ICE] = [0.80, 0.84, 0.88]; BC[B.ROCK] = [0.27, 0.24, 0.22];
  BC[B.SNOWPEAK] = [0.84, 0.86, 0.90]; BC[B.WETLAND] = [0.11, 0.21, 0.12]; BC[B.BARREN] = [0.2, 0.17, 0.15]; BC[B.SEAICE] = [0.16, 0.15, 0.12];
  const riverT = DM.Hydro.riverT(w);
  const pos = new Float32Array(n * 3), col = new Float32Array(n * 3), dat = new Float32Array(n * 4), nrm = new Float32Array(n * 3);
  for (let c = 0; c < n; c++) {
    const r = R + w.elev[c] + (w.elev[c] >= 0 ? w.water[c] * 0 : 0);
    pos[3 * c] = geo.pos[3 * c] * r; pos[3 * c + 1] = geo.pos[3 * c + 1] * r; pos[3 * c + 2] = geo.pos[3 * c + 2] * r;
    let k = BC[w.biome[c]] || [1, 0, 1], cr = k[0], cg = k[1], cb = k[2];
    if (w.elev[c] >= 0) {
      if (w.ice[c] > 0.3) { const t = U.sat((w.ice[c] - 0.3) / 0.4); cr = U.lerp(cr, 0.82, t); cg = U.lerp(cg, 0.85, t); cb = U.lerp(cb, 0.9, t); }
      if (farm[c] > 0 && w.ice[c] < 0.3 && w.biome[c] !== B.ROCK && w.biome[c] !== B.SNOWPEAK) {
        const h = U.hashf(c, 11), f = h < 0.45 ? [0.50, 0.42, 0.13] : h < 0.8 ? [0.26, 0.40, 0.10] : [0.36, 0.26, 0.14];
        cr = U.lerp(cr, f[0], 0.85); cg = U.lerp(cg, f[1], 0.85); cb = U.lerp(cb, f[2], 0.85);
      }
      if (urban[c] > 0) { const t = Math.min(1, urban[c] * 1.2); cr = U.lerp(cr, 0.34, t); cg = U.lerp(cg, 0.31, t); cb = U.lerp(cb, 0.28, t); }
    }
    const v = 0.9 + 0.2 * U.hashf(c, 1);
    col[3 * c] = cr * v; col[3 * c + 1] = cg * v; col[3 * c + 2] = cb * v;
    const river = (w.elev[c] >= 0 && (w.flow[c] > riverT || w.water[c] > 0.03)) ? Math.min(1, 0.45 + w.flow[c] / (riverT * 8)) : 0;
    dat[4 * c] = urban[c]; dat[4 * c + 1] = river; dat[4 * c + 2] = w.elev[c] < 0 ? w.ice[c] : 0; dat[4 * c + 3] = w.elev[c];
  }
  const tr = geo.tris;
  for (let i = 0; i < tr.length; i += 3) {
    const a = tr[i] * 3, b = tr[i + 1] * 3, c = tr[i + 2] * 3;
    const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2], vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    for (const k of [a, b, c]) { nrm[k] += nx; nrm[k + 1] += ny; nrm[k + 2] += nz; }
  }

  // ------------------------------------------------------------------ gl helpers
  const NOISE = `
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y)*p.z); }
float vnoise(vec3 p){ vec3 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);
  return mix(mix(mix(hash13(i), hash13(i+vec3(1,0,0)), f.x), mix(hash13(i+vec3(0,1,0)), hash13(i+vec3(1,1,0)), f.x), f.y),
             mix(mix(hash13(i+vec3(0,0,1)), hash13(i+vec3(1,0,1)), f.x), mix(hash13(i+vec3(0,1,1)), hash13(i+vec3(1,1,1)), f.x), f.y), f.z); }
float fbm(vec3 p){ float v = 0.0, a = 0.5; for (int i = 0; i < 5; i++){ v += a*vnoise(p); p = p*2.03 + vec3(11.3, 4.7, 7.1); a *= 0.5; } return v/0.96875; }
float cloudCover(vec3 d){ vec3 p = d*2.6; p += 0.55*vec3(vnoise(p*1.3+3.0), vnoise(p*1.3+17.0), vnoise(p*1.3+41.0));
  float band = 0.07*cos(d.y*9.0) - 0.05; float n = fbm(p + vec3(5.0)) + 0.16*(fbm(p*4.3 + 9.0) - 0.5); return smoothstep(0.585, 0.70, n + band); }`;
  const FRAME = `uniform mat4 uVP, uShadowM; uniform vec3 uCam, uSun; uniform float uCloud, uZoom; uniform sampler2DShadow uShadow;`;
  const LIGHT = NOISE + `
float dayAt(vec3 P){ return smoothstep(-0.12, 0.22, dot(normalize(P), uSun)); }
float sunShadow(vec3 P, vec3 N){ vec4 s = uShadowM*vec4(P + N*0.12, 1.0); vec3 c = s.xyz/s.w;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
  float t = 1.0/4096.0, a = 0.0; for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) a += texture(uShadow, vec3(c.xy + vec2(x, y)*t*1.3, c.z - 0.0006));
  return a/9.0; }
vec3 shade(vec3 alb, vec3 N, vec3 P, float spec, float emis){
  vec3 up = normalize(P); float day = dayAt(P); float ndl = max(dot(N, uSun), 0.0);
  float sh = sunShadow(P, N) * (1.0 - 0.5*cloudCover(normalize(up + uSun*0.11)));
  vec3 warm = mix(vec3(1.0, 0.45, 0.2), vec3(1.0, 0.96, 0.9), smoothstep(-0.05, 0.35, dot(up, uSun)));
  vec3 c = alb*(warm*2.6*ndl*sh + vec3(0.30, 0.42, 0.62)*0.42*day*(0.55 + 0.45*dot(N, up)) + vec3(0.035, 0.05, 0.09)*(0.6 + 0.4*dot(N, up)));
  vec3 V = normalize(uCam - P), Hh = normalize(V + uSun);
  c += warm*spec*pow(max(dot(N, Hh), 0.0), 420.0)*sh*day*5.0;
  return c + alb*emis; }
vec3 atmosphere(vec3 c, vec3 P){ vec3 up = normalize(P), V = normalize(uCam - P);
  float rim = pow(1.0 - max(dot(up, V), 0.0), 3.0), day = dayAt(P), dist = length(uCam - P);
  float haze = clamp(rim*0.75*(1.0 - uZoom) + (1.0 - exp(-dist*0.011))*0.85*uZoom, 0.0, 0.9);
  vec3 sky = mix(vec3(0.9, 0.38, 0.16), vec3(0.30, 0.55, 1.0), smoothstep(-0.05, 0.3, dot(up, uSun)))*(0.06 + 1.5*day);
  return mix(c, sky, haze*(0.25 + 0.75*day)); }`;
  function prog(vs, fs) {
    const p = gl.createProgram();
    for (const [t, s] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
      const sh = gl.createShader(t); gl.shaderSource(sh, '#version 300 es\nprecision highp float; precision highp sampler2DShadow;\n' + s); gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh) + '\n' + s.split('\n').map((l, i) => (i + 2) + ': ' + l).join('\n'));
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

  // ------------------------------------------------------------------ programs
  const terrVS = FRAME + `layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol; layout(location=3) in vec4 aDat;
out vec3 vW, vN, vC; out vec4 vD; void main(){ vW = aPos; vN = normalize(aNrm); vC = aCol; vD = aDat; gl_Position = uVP*vec4(aPos, 1.0); }`;
  const pTerr = prog(terrVS, FRAME + LIGHT + `in vec3 vW, vN, vC; in vec4 vD; out vec4 o;
void main(){
  vec3 fn = normalize(cross(dFdx(vW), dFdy(vW))); if (dot(fn, vW) < 0.0) fn = -fn;
  float fw = 0.85*smoothstep(2.0, 8.0, 1.9/max(length(fwidth(vW)), 1e-5));
  vec3 N = normalize(mix(vN, fn, fw)); vec3 up = normalize(vW);
  float det = smoothstep(0.25, 0.7, uZoom);
  float n1 = vnoise(vW*0.9), n2 = vnoise(vW*4.5), n3 = vnoise(vW*17.0);
  vec3 alb = vC*(1.0 + det*(0.30*(n1 - 0.5) + 0.22*(n2 - 0.5) + 0.14*(n3 - 0.5)));
  float steep = 1.0 - dot(fn, up); alb = mix(alb, vec3(0.25, 0.22, 0.20)*(0.8 + 0.4*n2), smoothstep(0.10, 0.22, steep)*0.7*step(0.02, vD.w));
  alb = mix(alb, vec3(0.05, 0.22, 0.34), smoothstep(0.5, 0.8, vD.y + (n2 - 0.5)*0.3)*0.9);            // rivers and lakes
  float night = 1.0 - dayAt(vW);
  float ls = mix(1.5, 2.6, uZoom); vec3 cell = floor(vW*ls); float h = hash13(cell); vec3 f = fract(vW*ls) - 0.5;
  float lamp = step(1.0 - sqrt(vD.x)*0.95, h)*smoothstep(0.40, 0.08, length(f));
  vec3 c = shade(alb, N, vW, 0.0, 0.0) + vec3(1.0, 0.70, 0.36)*night*(lamp*mix(60.0, 14.0, uZoom)*(0.5 + 0.5*hash13(cell + 3.0)) + vD.x*vD.x*mix(0.5, 0.2, uZoom));
  o = vec4(atmosphere(c, vW), 1.0); }`);
  const pTerrD = prog(`uniform mat4 uVP; layout(location=0) in vec3 aPos; void main(){ gl_Position = uVP*vec4(aPos, 1.0); }`, `void main(){}`);
  const pSea = prog(FRAME + `layout(location=0) in vec3 aPos; layout(location=3) in vec4 aDat; out vec3 vW; out vec4 vD;
void main(){ vW = normalize(aPos)*${R.toFixed(1)}; vD = aDat; gl_Position = uVP*vec4(vW, 1.0); }`, FRAME + LIGHT + `in vec3 vW; in vec4 vD; out vec4 o;
void main(){ float depth = -vD.w; if (depth < -0.25) discard;
  vec3 up = normalize(vW); float wv = vnoise(vW*3.0)*0.5 + vnoise(vW*9.0)*0.25;
  vec3 N = normalize(up + 0.035*vec3(vnoise(vW*5.0) - 0.5, vnoise(vW*5.0 + 9.0) - 0.5, vnoise(vW*5.0 + 21.0) - 0.5));
  float t = 1.0 - exp(-max(depth, 0.0)*1.1);
  vec3 alb = mix(vec3(0.05, 0.42, 0.44), vec3(0.008, 0.055, 0.19), t);
  float foam = smoothstep(0.16, 0.0, depth + (wv - 0.35)*0.16);
  alb = mix(alb, vec3(0.8), foam*0.85); alb = mix(alb, vec3(0.80, 0.85, 0.90), smoothstep(0.35, 0.6, vD.z));
  vec3 V = normalize(uCam - vW); float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0);
  vec3 c = shade(alb, N, vW, 0.9, 0.0) + vec3(0.25, 0.45, 0.8)*fres*0.5*dayAt(vW);
  o = vec4(atmosphere(c, vW), clamp(0.5 + depth*1.3, 0.5, 0.97) + foam*0.4); }`);
  const instVS = FRAME + `layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol; layout(location=3) in vec4 aDat;
layout(location=4) in vec4 iA; layout(location=5) in vec4 iB; out vec3 vW, vC; out float vE;
void main(){ vec3 up = normalize(iA.xyz), e = normalize(cross(vec3(0.0, 1.0, 0.0), up)), nn = cross(up, e);
  float cs = cos(iB.w), sn = sin(iB.w); vec3 l = aPos*iA.w; l.xz = vec2(l.x*cs - l.z*sn, l.x*sn + l.z*cs);
  vW = iA.xyz + e*l.x + up*l.y + nn*l.z; vC = mix(aCol, aCol*iB.rgb*2.2, aDat.z); vE = aDat.x; gl_Position = uVP*vec4(vW, 1.0); }`;
  const pInst = prog(instVS, FRAME + LIGHT + `in vec3 vW, vC; in float vE; out vec4 o;
void main(){ vec3 N = normalize(cross(dFdx(vW), dFdy(vW))); vec3 V = normalize(uCam - vW); if (dot(N, V) < 0.0) N = -N;
  float night = 1.0 - dayAt(vW);
  vec3 c = shade(vC, N, vW, 0.0, 0.0) + vec3(1.0, 0.75, 0.4)*vE*10.0*night;
  o = vec4(atmosphere(c, vW), 1.0); }`);
  const pInstD = prog(instVS, `in vec3 vW, vC; in float vE; void main(){}`);
  const shellVS = FRAME + `layout(location=0) in vec3 aPos; uniform float uR; out vec3 vW; void main(){ vW = normalize(aPos)*uR; gl_Position = uVP*vec4(vW, 1.0); }`;
  const pCloud = prog(shellVS, FRAME + LIGHT + `in vec3 vW; out vec4 o;
void main(){ vec3 d = normalize(vW); float cv = cloudCover(d); if (cv < 0.01) discard;
  float day = dayAt(vW), lit = max(dot(d, uSun) + 0.25, 0.0);
  vec3 warm = mix(vec3(1.0, 0.5, 0.35), vec3(1.0), smoothstep(-0.05, 0.4, dot(d, uSun)));
  float body = 0.55 + 0.45*fbm(d*11.0);
  vec3 c = warm*(0.83*2.3*lit*body) + vec3(0.25, 0.35, 0.55)*0.35*day + vec3(0.02, 0.03, 0.05);
  o = vec4(c, cv*0.92*uCloud); }`);
  const pAtmo = prog(shellVS, FRAME + `in vec3 vW; out vec4 o;
void main(){ vec3 rd = normalize(vW - uCam); float tca = max(dot(-uCam, rd), 0.0); vec3 p = uCam + rd*tca; float d = length(p);
  float h = clamp((d - ${R.toFixed(1)})/17.0, 0.0, 1.0); float a = exp(-h*3.6)*(1.0 - h);
  float s = dot(normalize(p), uSun); float lit = smoothstep(-0.28, 0.3, s);
  vec3 c = mix(vec3(1.0, 0.42, 0.14), vec3(0.28, 0.55, 1.0), smoothstep(-0.12, 0.28, s));
  o = vec4(c*a*lit*1.9 + vec3(0.02, 0.04, 0.09)*a*0.6, 1.0); }`);
  const FSVS = `out vec2 vUv; void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); vUv = p; gl_Position = vec4(p*2.0 - 1.0, 0.0, 1.0); }`;
  const pStar = prog(FSVS, `in vec2 vUv; out vec4 o; uniform mat4 uInv; uniform vec3 uCam, uSun; uniform float uStars;
float hash13(vec3 p){ p = fract(p*0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y)*p.z); }
void main(){ vec4 q = uInv*vec4(vUv*2.0 - 1.0, 1.0, 1.0); vec3 rd = normalize(q.xyz/q.w - uCam);
  vec3 g = rd*260.0, cell = floor(g); float h = hash13(cell); vec3 f = fract(g) - 0.5;
  float star = step(0.9965, h)*smoothstep(0.42, 0.0, length(f))*(0.4 + 3.0*hash13(cell + 7.0));
  vec3 c = (vec3(0.85, 0.9, 1.0)*star + vec3(0.006, 0.008, 0.016))*uStars;
  float band = exp(-abs(dot(rd, normalize(vec3(0.3, 0.8, -0.5))))*5.0); c += vec3(0.010, 0.012, 0.022)*band;
  float sd = max(dot(rd, uSun), 0.0); c += vec3(1.0, 0.92, 0.8)*(pow(sd, 3000.0)*60.0 + pow(sd, 60.0)*0.5 + pow(sd, 8.0)*0.06);
  o = vec4(c, 1.0); }`);
  const pBlur = prog(FSVS, `in vec2 vUv; out vec4 o; uniform sampler2D uT; uniform vec2 uDir; uniform float uThr;
void main(){ vec3 c = vec3(0.0); float ws = 0.0;
  for (int i = -4; i <= 4; i++){ float wt = exp(-float(i*i)/8.0); vec3 s = min(texture(uT, vUv + uDir*float(i)).rgb, vec3(64.0));
    if (uThr > 0.0){ float b = max(s.r, max(s.g, s.b)); s *= clamp((b - uThr)/max(b, 1e-4), 0.0, 1.0); } c += s*wt; ws += wt; }
  o = vec4(c/ws, 1.0); }`);
  const pFinal = prog(FSVS, `in vec2 vUv; out vec4 o; uniform sampler2D uT, uBlurT, uBloomT; uniform float uTilt, uFocus;
vec3 aces(vec3 x){ return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0); }
void main(){ vec3 c = texture(uT, vUv).rgb; float b = uTilt*smoothstep(0.10, 0.48, abs(vUv.y - uFocus));
  c = mix(c, texture(uBlurT, vUv).rgb, b); c += texture(uBloomT, vUv).rgb*0.9;
  c = aces(c*0.62); c = pow(c, vec3(1.0/2.2)); float l = dot(c, vec3(0.299, 0.587, 0.114)); c = mix(vec3(l), c, 1.12);
  vec2 d = vUv - 0.5; c *= 1.0 - 0.55*dot(d, d); o = vec4(c, 1.0); }`);

  // ------------------------------------------------------------------ camera per shot
  const eye = V3.create(), tgt = V3.create(), up = V3.create(0, 1, 0), sun = V3.create(), f = V3.create(), e = V3.create(), nn = V3.create();
  let cloud = 1, zoom = 0, tilt = 0, focusY = 0.5, fov = 35 * Math.PI / 180, treeAng = 0, focusCell = -1;
  const ll = (la, lo, o) => V3.set(o, Math.cos(la) * Math.cos(lo), Math.sin(la), Math.cos(la) * Math.sin(lo));
  // face the largest landmass's best city
  const scored = cities.slice(0, 40).map(ct => { const cs = [], k = geo.within(ct.cell, 0.26, cs); let land = 0, peak = -9, pc = ct.cell, forest = 0;
    for (let i = 0; i < k; i++) { const c = cs[i]; if (w.elev[c] >= 0) { land++; if (w.biome[c] === B.FOREST || w.biome[c] === B.RAINFOREST || w.biome[c] === B.TAIGA) forest++; } if (w.elev[c] > peak) { peak = w.elev[c]; pc = c; } }
    const lf = land / k; return { ct, pc, s: (1 - Math.abs(lf - 0.68) * 2.2) + Math.min(peak, 6) * 0.22 + forest / k * 1.5 - Math.abs(geo.lat[ct.cell]) * 0.9 + ct.size * 0.15 }; }).sort((a, b) => b.s - a.s);
  const pick = scored[Math.min(CITY, scored.length - 1)], main = pick.ct; geo.dir(main.cell, f);
  if (SHOT === 'orbit') {
    V3.basis(f, e, nn); V3.addScaled(eye, f, e, 0.25); V3.addScaled(eye, eye, nn, 0.18); V3.norm(eye, eye); V3.scale(eye, eye, 430);
    V3.addScaled(sun, f, e, -1.25); V3.addScaled(sun, sun, nn, 0.55); V3.norm(sun, sun);
  } else if (SHOT === 'night') {
    V3.basis(f, e, nn); V3.addScaled(eye, f, e, -0.15); V3.norm(eye, eye); V3.scale(eye, eye, 330);
    V3.addScaled(sun, f, e, 1.9); V3.scale(sun, sun, -1); V3.addScaled(sun, sun, e, 1.2); V3.addScaled(sun, sun, nn, 0.25); V3.norm(sun, sun);
    V3.set(tgt, e[0] * 22, e[1] * 22, e[2] * 22);
  } else {
    V3.basis(f, e, nn); const dir = V3.create(), pk = V3.create(); geo.dir(pick.pc, pk);
    const a0 = Math.atan2(V3.dot(pk, nn), V3.dot(pk, e)) + AZ; V3.scale(dir, e, Math.cos(a0)); V3.addScaled(dir, dir, nn, Math.sin(a0));
    const back = V3.create(); V3.addScaled(back, f, dir, -0.17); V3.norm(back, back);
    const gc = geo.cellAt(back[0], back[1], back[2]); V3.scale(eye, back, R + Math.max(w.elev[gc], 0) + 7.5);
    const fw2 = V3.create(); V3.addScaled(fw2, f, dir, 0.03); V3.norm(fw2, fw2); V3.scale(tgt, fw2, R + Math.max(w.elev[main.cell], 0) + 0.6);
    V3.norm(up, eye);
    const side = V3.create(); V3.cross(side, f, dir); V3.addScaled(sun, f, side, 1.25); V3.addScaled(sun, sun, dir, -0.35); V3.scale(sun, sun, 1); V3.addScaled(sun, sun, f, 0.15); V3.norm(sun, sun);
    cloud = 0; zoom = 1; tilt = 0.9; focusY = 0.42; treeAng = 0.42; focusCell = main.cell;
  }

  // ------------------------------------------------------------------ instances (close shot)
  function mesh(build) { const P = [], C = [], D = [];
    const tri = (a, b, c, col3, em, tm) => { for (const v of [a, b, c]) { P.push(v[0], v[1], v[2]); C.push(col3[0], col3[1], col3[2]); D.push(em || 0, 0, tm || 0, 0); } };
    const cone = (y0, y1, r, k, col3, tm) => { for (let i = 0; i < k; i++) { const a0 = i / k * 6.2832, a1 = (i + 1) / k * 6.2832; tri([Math.cos(a0) * r, y0, Math.sin(a0) * r], [Math.cos(a1) * r, y0, Math.sin(a1) * r], [0, y1, 0], col3, 0, tm); } };
    const box = (x0, y0, z0, x1, y1, z1, col3, em) => { const v = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
      for (const q4 of [[0, 1, 2, 3], [5, 4, 7, 6], [4, 0, 3, 7], [1, 5, 6, 2], [3, 2, 6, 7]]) { tri(v[q4[0]], v[q4[1]], v[q4[2]], col3, em); tri(v[q4[0]], v[q4[2]], v[q4[3]], col3, em); } };
    build({ tri, cone, box });
    return { n: P.length / 3, attrs: [{ d: new Float32Array(P), s: 3 }, null, { d: new Float32Array(C), s: 3 }, { d: new Float32Array(D), s: 4 }] }; }
  const M = {
    conifer: mesh(m => { m.cone(-0.15, 0.28, 0.045, 5, [0.16, 0.10, 0.06]); m.cone(0.16, 0.62, 0.26, 6, [0.05, 0.17, 0.08]); m.cone(0.40, 0.95, 0.19, 6, [0.06, 0.20, 0.09]); }),
    broad: mesh(m => { m.cone(-0.15, 0.36, 0.05, 5, [0.17, 0.11, 0.06]); m.cone(0.62, 0.22, 0.30, 7, [0.07, 0.24, 0.06]); m.cone(0.30, 0.86, 0.30, 7, [0.10, 0.30, 0.07]); }),
    palm: mesh(m => { m.cone(-0.15, 0.7, 0.035, 5, [0.22, 0.16, 0.09]); m.cone(0.80, 0.55, 0.30, 6, [0.08, 0.27, 0.07]); m.cone(0.62, 0.84, 0.16, 6, [0.10, 0.30, 0.08]); }),
    house: mesh(m => { m.box(-0.15, -0.15, -0.11, 0.15, 0.16, 0.11, [0.50, 0.44, 0.36]); m.box(-0.155, 0.04, -0.04, -0.15, 0.11, 0.04, [1, 0.8, 0.5], 1); m.box(0.15, 0.04, -0.04, 0.155, 0.11, 0.04, [1, 0.8, 0.5], 1);
      const a = [-0.18, 0.16, -0.14], b = [0.18, 0.16, -0.14], c = [0.18, 0.16, 0.14], d = [-0.18, 0.16, 0.14], r0 = [-0.18, 0.32, 0], r1 = [0.18, 0.32, 0], rc = [0.36, 0.14, 0.09];
      m.tri(a, b, r1, rc, 0, 1); m.tri(a, r1, r0, rc, 0, 1); m.tri(c, d, r0, rc, 0, 1); m.tri(c, r0, r1, rc, 0, 1); m.tri(d, a, r0, rc, 0, 1); m.tri(b, c, r1, rc, 0, 1); }),
    tower: mesh(m => { m.box(-0.13, -0.15, -0.13, 0.13, 0.55, 0.13, [0.52, 0.47, 0.40]); m.box(-0.135, 0.3, -0.05, -0.13, 0.42, 0.05, [1, 0.8, 0.5], 1); m.cone(0.55, 0.85, 0.2, 4, [0.36, 0.14, 0.09], 1); }),
  };
  const inst = { conifer: [], broad: [], palm: [], house: [], tower: [] };
  if (treeAng > 0) {
    const cells = [], cnt = geo.within(focusCell, treeAng, cells), p = V3.create(), ce = V3.create(), cn = V3.create(), q2 = V3.create();
    const ground = (dirv, c) => { let ws = 1e-9, es = 0; const add = k => { const dx = geo.pos[3 * k] - dirv[0], dy = geo.pos[3 * k + 1] - dirv[1], dz = geo.pos[3 * k + 2] - dirv[2]; const wt = 1 / (Math.pow(dx * dx + dy * dy + dz * dz, 1.5) + 1e-9); ws += wt; es += wt * w.elev[k]; };
      add(c); for (let k = 0; k < geo.nbrN[c]; k++) add(geo.nbr[6 * c + k]); return R + es / ws; };
    const place = (list, c, jit, scale, tint, salt) => { geo.dir(c, p); V3.basis(p, ce, cn);
      const a = U.hashf(c, salt) * 6.2832, r = Math.sqrt(U.hashf(c, salt + 1)) * jit * geo.step;
      V3.addScaled(q2, p, ce, Math.cos(a) * r); V3.addScaled(q2, q2, cn, Math.sin(a) * r); V3.norm(q2, q2);
      const gr = ground(q2, c); list.push(q2[0] * gr, q2[1] * gr, q2[2] * gr, scale, tint[0], tint[1], tint[2], U.hashf(c, salt + 2) * 6.2832); };
    const roofs = [[0.9, 0.45, 0.2], [0.75, 0.3, 0.25], [0.85, 0.6, 0.3]];
    for (let i = 0; i < cnt; i++) {
      const c = cells[i]; if (w.elev[c] < 0.03 || w.water[c] > 0.03) continue;
      const b = w.biome[c];
      if (urban[c] > 0.05) { const k = Math.round(4 + urban[c] * 16);
        for (let j = 0; j < k; j++) place(U.hashf(c, 90 + j) < 0.07 + urban[c] * 0.06 ? inst.tower : inst.house, c, 0.62, 0.8 + 0.5 * U.hashf(c, 200 + j), roofs[(U.hashf(c, 300 + j) * 3) | 0], 400 + j * 3); continue; }
      if (farm[c] > 0) { if (U.hashf(c, 50) < 0.35) place(inst.house, c, 0.4, 0.85, roofs[0], 60); if (U.hashf(c, 51) < 0.5) place(inst.broad, c, 0.55, 0.7, [1, 1, 1], 70); continue; }
      const v = w.veg[c], k = v > 0.78 ? 4 : v > 0.5 ? 3 : v > 0.22 ? 1 : 0;
      const list = (b === B.TAIGA || (b === B.FOREST && w.temp[c] < 9) || b === B.TUNDRA) ? inst.conifer : (b === B.RAINFOREST || b === B.SAVANNA || b === B.BEACH) ? inst.palm : inst.broad;
      for (let j = 0; j < k; j++) place(list, c, 0.6, (0.75 + 0.6 * U.hashf(c, 20 + j)) * (b === B.RAINFOREST ? 1.2 : 1), [1, 1, 1], 30 + j * 3);
    }
  }
  const IV = {}; for (const k in inst) { const a = new Float32Array(inst[k]); IV[k] = { v: vao(M[k].attrs, null, a), n: M[k].n, c: a.length / 8 }; }

  // ------------------------------------------------------------------ buffers and matrices
  const terr = vao([{ d: pos, s: 3 }, { d: nrm, s: 3 }, { d: col, s: 3 }, { d: dat, s: 4 }], geo.tris);
  const proj = M4.create(), view = M4.create(), vp = M4.create(), inv = M4.create(), lv = M4.create(), lp = M4.create(), lvp = M4.create(), bias = M4.create(), sm = M4.create();
  const dist = V3.dist(eye, tgt), elen = V3.len(eye), alt = elen - R;
  M4.perspective(proj, fov, W / H, Math.max(0.25, alt * 0.05), Math.sqrt(Math.max(elen * elen - 93 * 93, 1)) + 140);
  M4.lookAt(view, eye, tgt, up); M4.mul(vp, proj, view); M4.invert(inv, vp);
  const rr = U.clamp(1.6 * dist, 12, R + 22), big = dist > 76, c0 = big ? [0, 0, 0] : tgt, le = V3.create(); V3.addScaled(le, c0, sun, 260);
  M4.lookAt(lv, le, c0, Math.abs(sun[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0]); const r2 = big ? R + 22 : rr;
  M4.ortho(lp, -r2, r2, -r2, r2, 260 - (R + 22), 260 + (R + 22)); M4.mul(lvp, lp, lv);
  bias.set([0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, 0.5, 0.5, 0.5, 1]); M4.mul(sm, bias, lvp);

  const SH = 4096, shadow = target(SH, SH, false, true), scene = target(W, H, true), res = target(W, H), half = target(W >> 1, H >> 1), qa = target(W >> 2, H >> 2), qb = target(W >> 2, H >> 2), ba = target(W >> 3, H >> 3), bb = target(W >> 3, H >> 3);
  const use = (P, mat) => { gl.useProgram(P.p); gl.uniformMatrix4fv(P.u.uVP, false, mat); if (P.u.uShadowM) gl.uniformMatrix4fv(P.u.uShadowM, false, sm);
    if (P.u.uCam) gl.uniform3fv(P.u.uCam, eye); if (P.u.uSun) gl.uniform3fv(P.u.uSun, sun); if (P.u.uCloud) gl.uniform1f(P.u.uCloud, cloud); if (P.u.uZoom) gl.uniform1f(P.u.uZoom, zoom);
    if (P.u.uShadow) { gl.activeTexture(gl.TEXTURE7); gl.bindTexture(gl.TEXTURE_2D, shadow.tex); gl.uniform1i(P.u.uShadow, 7); } };
  const drawInst = (P, mat) => { use(P, mat); for (const k in IV) { const o = IV[k]; if (!o.c) continue; gl.bindVertexArray(o.v); gl.drawArraysInstanced(gl.TRIANGLES, 0, o.n, o.c); } };
  const bind = t => { gl.bindFramebuffer(gl.FRAMEBUFFER, t ? t.fbo : null); gl.viewport(0, 0, t ? t.w : W, t ? t.h : H); };
  const fs = () => { gl.bindVertexArray(null); gl.drawArrays(gl.TRIANGLES, 0, 3); };
  const blur = (src, dst, dx, dy, thr) => { bind(dst); gl.useProgram(pBlur.p); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, src.tex); gl.uniform1i(pBlur.u.uT, 0); gl.uniform2f(pBlur.u.uDir, dx, dy); gl.uniform1f(pBlur.u.uThr, thr || 0); fs(); };

  function render() {
    // shadow
    bind(shadow); gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.clear(gl.DEPTH_BUFFER_BIT); gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1.5, 4.0);
    gl.activeTexture(gl.TEXTURE7); gl.bindTexture(gl.TEXTURE_2D, null);
    gl.useProgram(pTerrD.p); gl.uniformMatrix4fv(pTerrD.u.uVP, false, lvp); gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0);
    gl.useProgram(pInstD.p); gl.uniformMatrix4fv(pInstD.u.uVP, false, lvp); for (const k in IV) { const o = IV[k]; if (!o.c) continue; gl.bindVertexArray(o.v); gl.drawArraysInstanced(gl.TRIANGLES, 0, o.n, o.c); }
    gl.disable(gl.POLYGON_OFFSET_FILL);
    // scene
    bind(scene); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.disable(gl.DEPTH_TEST); gl.useProgram(pStar.p); gl.uniformMatrix4fv(pStar.u.uInv, false, inv); gl.uniform3fv(pStar.u.uCam, eye); gl.uniform3fv(pStar.u.uSun, sun); gl.uniform1f(pStar.u.uStars, alt < 17 ? 1 - U.sstep(-0.1, 0.2, V3.dot(eye, sun) / elen) : 1); fs();
    gl.enable(gl.DEPTH_TEST);
    use(pTerr, vp); gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0);
    drawInst(pInst, vp);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    use(pSea, vp); gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0);
    gl.depthMask(false); gl.enable(gl.CULL_FACE);
    if (cloud > 0) { gl.cullFace(gl.BACK); use(pCloud, vp); gl.uniform1f(pCloud.u.uR, R + 11); gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0); }
    gl.cullFace(gl.FRONT); gl.blendFunc(gl.ONE, gl.ONE); use(pAtmo, vp); gl.uniform1f(pAtmo.u.uR, R + 17); gl.bindVertexArray(terr); gl.drawElements(gl.TRIANGLES, geo.tris.length, gl.UNSIGNED_INT, 0);
    gl.disable(gl.CULL_FACE); gl.cullFace(gl.BACK); gl.disable(gl.BLEND); gl.depthMask(true); gl.disable(gl.DEPTH_TEST);
    // post
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, scene.fbo); gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, res.fbo); gl.blitFramebuffer(0, 0, W, H, 0, 0, W, H, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    blur(res, half, 0, 0); blur(half, qa, 2 / W, 0); blur(qa, qb, 0, 4 / H); blur(qb, qa, 6 / W, 0); blur(qa, qb, 0, 6 / H);        // soft copy for tilt-shift
    blur(half, ba, 4 / W, 0, 3.0); blur(ba, bb, 0, 8 / H); blur(bb, ba, 12 / W, 0); blur(ba, bb, 0, 12 / H);                            // bloom
    bind(null); gl.useProgram(pFinal.p);
    [[res, 'uT'], [qb, 'uBlurT'], [bb, 'uBloomT']].forEach(([t, nm], i) => { gl.activeTexture(gl.TEXTURE0 + i); gl.bindTexture(gl.TEXTURE_2D, t.tex); gl.uniform1i(pFinal.u[nm], i); });
    gl.uniform1f(pFinal.u.uTilt, tilt); gl.uniform1f(pFinal.u.uFocus, focusY); fs();
  }
  render(); requestAnimationFrame(() => { render(); requestAnimationFrame(() => { render(); window.INFO = { cities: cities.length, trees: inst.conifer.length / 8 + inst.broad.length / 8 + inst.palm.length / 8, houses: inst.house.length / 8, err: gl.getError() }; window.READY = true; }); });
})();
