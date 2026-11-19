// DEMIURGE — js/render/renderer.js : DM.Renderer, the frame (SPEC-RENDER §2, §4, §11).
//
//   Renderer.init(canvas) → bool         GL.init, Post.init, init() of every loaded render module, FX.attach(); false without WebGL2
//   Renderer.setWorld(world)             build GPU data for a world (Planet.build, Objects.build); every dirty flag is set for the next frame
//   Renderer.resize()                    drawing buffer = CSS size × min(devicePixelRatio, cap), limited to the megapixel budget of the quality
//   Renderer.frame(view, dt)             view = DM.Camera.view() (null = far orbit); writes the Frame block, then does §4
//   Renderer.pick(clientX, clientY, out) → bool    ray through a canvas pixel → Planet.raycast; without Planet: the datum sphere + geo.cellAt.
//                                        out = { cell, pos: [x,y,z], dir: [x,y,z], t }
//   Renderer.project(worldPos, out) → bool         out = { x, y, depth }: canvas CSS pixels and the distance along the view axis (world
//                                        units). false when behind the camera or hidden by the planet (x, y are still written when in front)
//   Renderer.setBrush(dir | null, ang, rgba)       the brush ring (uBrush, uBrushCol); null or ang 0 hides it
//   Renderer.flash(worldPos, radius, rgb, seconds) transient light (uFlash) that decays; scaled by DM.Save.settings.flashes
//   Renderer.setQuality(q)               0 | 1 | 2: resizes the targets and the shadow map, sets dirty.objects
//   Renderer.showSnapshot(snap | null)   time-lapse frame: forwards to Planet / Objects, shows snap.seaLevel and snap.heat, night lights from
//                                        the share of urban cells, no FX while one is shown
//   Renderer.sun = { mode: 'orbit' | 'follow', dayLength: 150, angle: 0, dir: [x,y,z] }
//   Renderer.capture(mime = 'image/jpeg', quality = 0.9) → data URL (renders one frame first)
//   Renderer.freeze(t | null)            pin uTime.x / uTime.z and the sun angle to time t; modules are stepped with a fixed dt of 1/60
//   Renderer.dirty                       this frame's copy of world.dirty (render modules read this, never world.dirty)
//   Renderer.stats = { ms, draws, tris, instances, particles, cpu, gpu }
//        ms = cpu + gpu milliseconds of the last frame (gpu from EXT_disjoint_timer_query_webgl2 when the browser has it, else 0);
//        particles = DM.FX.count when FX exposes that number (FX may also write stats.particles itself during update / draw)
//   Renderer.slow                        true when the 2-second mean of stats.ms has exceeded 26 for 4 s (or, where the GPU cannot be
//                                        timed, the frame interval has averaged over 40 ms for 4 s)
//
// Extras (not in the spec; for render modules and dev pages):
//   Renderer.frozen                      null, or the time passed to freeze(): FX reseeds its Rng when this changes
//   Renderer.world, Renderer.snapshot    what is being shown
//   Renderer.seaLevel                    the displayed sea level (uPlanet.y), eased toward world.seaLevel at about 1 unit per second
//   Renderer.disabled                    { moduleName: 'reason' } for modules switched off after throwing (Renderer.revive(name) clears one)
//   Renderer.shadows                     set false to skip the shadow pass (unit 7 then holds a 1×1 depth texture cleared to 1.0)
//   Renderer.shadowSize                  side of the shadow map used by the last frame in texels (0 = the pass was skipped)
//   Renderer.stateErrors                 with DM.GL.debug: what a module left wrong on exit from a draw call ('Planet.drawWater: BLEND on')
//   Renderer.SUN_COLOR                   uSunCol.rgb; with the sky fill of <lighting> a white surface under the noon sun returns 2.5
//
// Modules are looked up as DM.Planet / DM.Objects / DM.Sky / DM.FX at call time and each call runs inside try/catch: a module that throws
// is disabled for the rest of the session with one console.error, and the frame goes on. DM.DevScene (dev/view.js only: the stand-in scene
// of the dev viewer; undefined in the game) is treated the same way, with the optional hooks init(), build(world), sync(world, dt, dirty),
// drawDepth(), draw() (after the terrain) and drawBlend() (after the atmosphere).
//
// GL.view (set before step 1 of every frame) = the Camera.view() fields (eye, target, up, right, focusDir, fov, aspect, near, far, zoom01,
// altitude) + viewProj (Float32Array(16)), and the extras viewMat, projMat, invViewProj, cssW, cssH. `aspect` is the canvas's own.
(function (DM) {
  'use strict';

  const Renderer = (DM.Renderer = DM.Renderer || {});
  const R0 = DM.R || 100;
  const BOUND = R0 + 22;                                    // nothing on the planet is farther from the centre (SPEC-RENDER §0)
  const TAU = Math.PI * 2;
  const MODULES = ['Planet', 'Objects', 'Sky', 'FX', 'DevScene'];
  const QUALITY = [
    { ratio: 1.0, mp: 1.6, shadow: 1024 },
    { ratio: 1.5, mp: 3.0, shadow: 2048 },
    { ratio: 2.0, mp: 5.0, shadow: 4096 },
  ];
  const NIGHT_LEVEL = [0.12, 0.2, 0.35, 0.5, 0.8, 1, 1];
  const DIRTY_KEYS = ['elev', 'color', 'water', 'objects', 'rivers', 'roads'];
  const SUN_DECLINATION = 0.2;                              // radians above the equator in 'orbit' mode
  const FROZEN_DT = 1 / 60;
  const SHADOW = 0, SCENE = 1;

  Renderer.SUN_COLOR = [2.31, 2.20, 1.98];
  Renderer.sun = Renderer.sun || { mode: 'orbit', dayLength: 150, angle: 0, dir: [Math.cos(SUN_DECLINATION), Math.sin(SUN_DECLINATION), 0] };
  Renderer.dirty = { elev: false, color: false, water: false, objects: false, rivers: false, roads: false };
  Renderer.stats = { ms: 0, draws: 0, tris: 0, instances: 0, particles: 0, cpu: 0, gpu: 0 };
  Renderer.slow = false;
  Renderer.frozen = null;
  Renderer.world = null;
  Renderer.snapshot = null;
  Renderer.seaLevel = 0;
  Renderer.disabled = {};
  Renderer.shadows = true;
  Renderer.shadowSize = 0;
  Renderer.stateErrors = [];

  let canvas = null, listening = null, ready = false, lost = false;
  let world = null, snapshot = null;
  let shadow = null, noShadow = null;
  let time = 0;
  let cssW = 0, cssH = 0, cssDpr = 0;
  let seaShown = 0, oceanFrac = 0, oceanAge = 1e9, snapNight = 0;
  let pendingAll = false, pendingObjects = false;
  let sunSet = false, postFailed = false;
  let pass = SCENE, shadowTex = null;
  const brush = [0, 1, 0, 0, 1, 1, 1, 1];                   // dir xyz, angle, rgba
  const flashes = [];                                        // { x, y, z, radius, r, g, b, age, life }

  // CPU-side matrices in double precision (Float32 copies go into the Frame block and GL.view)
  const viewM = new Float64Array(16), projM = new Float64Array(16), viewProjM = new Float64Array(16), invM = new Float64Array(16);
  const lightM = new Float64Array(16), shadowM = new Float64Array(16);
  const eye = [0, 0, 420], camX = [1, 0, 0], camY = [0, 1, 0], camF = [0, 0, -1];   // eye and the camera basis: right, up, forward
  let camFov = 35 * Math.PI / 180, camAspect = 16 / 9, camNear = 1, camFar = 600;
  const view = {
    eye: new Float32Array(3), target: new Float32Array(3), up: new Float32Array(3), right: new Float32Array(3), focusDir: new Float32Array(3),
    fov: camFov, aspect: camAspect, near: 1, far: 600, zoom01: 0, altitude: 320,
    viewProj: new Float32Array(16), viewMat: new Float32Array(16), projMat: new Float32Array(16), invViewProj: new Float32Array(16),
    cssW: 0, cssH: 0,
  };

  // GPU timing and the `slow` detector
  let timerExt = null, timerQuery = null, gpuMs = 0;
  const timerPending = [];
  let winT = 0, winMs = 0, winDt = 0, winN = 0, slowFor = 0;

  function now() { return (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now(); }
  function sstep(e0, e1, x) { const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); }

  // ------------------------------------------------------------------------------------------ module isolation
  function disable(name, fn, e) {
    const text = String(e && e.message ? e.message : e);
    Renderer.disabled[name] = fn + ': ' + text;
    console.error('DM.Renderer: module ' + name + ' disabled after an error in ' + name + '.' + fn + '()\n' + (e && e.stack ? e.stack : text));
  }
  function usable(name, fn) {
    const m = DM[name];
    return !!m && !Renderer.disabled[name] && typeof m[fn] === 'function';
  }
  /** Call DM[name][fn](a, b, c) if the module is loaded and healthy; a throw disables the module. Returns true if the call completed. */
  function call(name, fn, a, b, c) {
    const m = DM[name];
    if (!m || Renderer.disabled[name]) return false;
    const f = m[fn];
    if (typeof f !== 'function') return false;
    try { f.call(m, a, b, c); } catch (e) { disable(name, fn, e); return false; }
    return true;
  }
  /** Re-enable a module that was disabled (dev aid; the game never calls this). */
  Renderer.revive = function (name) { delete Renderer.disabled[name]; };

  // The canonical state of SPEC-RENDER §4 plus what belongs to the current pass. Asserted before every module draw call, so a module
  // that throws half-way (or forgets to clean up) cannot spoil what follows.
  function assertState() {
    const GL = DM.GL, gl = GL.gl;
    GL.resetState();
    if (pass === SHADOW) {
      shadow.bind();
      gl.colorMask(false, false, false, false);
      gl.enable(gl.POLYGON_OFFSET_FILL);
      gl.polygonOffset(1.5, 4.0);
      GL.bindTex(7, null);
    } else {
      DM.Post.scene.bind();
      gl.colorMask(true, true, true, true);
      gl.disable(gl.POLYGON_OFFSET_FILL);
      GL.bindTex(7, shadowTex);
    }
  }

  // dev aid (GL.debug): report what a draw call left behind
  function checkState(label) {
    const GL = DM.GL, gl = GL.gl, bad = [];
    if (!gl.isEnabled(gl.DEPTH_TEST)) bad.push('DEPTH_TEST off');
    if (gl.getParameter(gl.DEPTH_FUNC) !== gl.LEQUAL) bad.push('depthFunc not LEQUAL');
    if (!gl.getParameter(gl.DEPTH_WRITEMASK)) bad.push('depthMask false');
    if (gl.isEnabled(gl.BLEND)) bad.push('BLEND on');
    if (gl.isEnabled(gl.CULL_FACE)) bad.push('CULL_FACE on');
    if (gl.getParameter(gl.CULL_FACE_MODE) !== gl.BACK) bad.push('cullFace not BACK');
    if (gl.getParameter(gl.FRONT_FACE) !== gl.CCW) bad.push('frontFace not CCW');
    if (gl.isEnabled(gl.SCISSOR_TEST)) bad.push('SCISSOR_TEST on');
    if (gl.getParameter(gl.VERTEX_ARRAY_BINDING)) bad.push('a VAO is bound');
    const mask = gl.getParameter(gl.COLOR_WRITEMASK), want = pass === SCENE;
    if (mask[0] !== want || mask[1] !== want || mask[2] !== want || mask[3] !== want) bad.push('colorMask changed');
    if (gl.isEnabled(gl.POLYGON_OFFSET_FILL) !== (pass === SHADOW)) bad.push('POLYGON_OFFSET_FILL ' + (pass === SHADOW ? 'off' : 'on'));
    const fbo = pass === SHADOW ? shadow.fbo : DM.Post.scene.fbo;
    if (gl.getParameter(gl.FRAMEBUFFER_BINDING) !== fbo) bad.push('framebuffer changed');
    gl.activeTexture(gl.TEXTURE7);
    if (gl.getParameter(gl.TEXTURE_BINDING_2D) !== (pass === SHADOW ? null : shadowTex)) bad.push('texture unit 7 changed');
    gl.activeTexture(gl.TEXTURE0);
    GL.checkErrors(label);
    if (!bad.length) return;
    const msg = label + ': ' + bad.join(', ');
    if (Renderer.stateErrors.indexOf(msg) < 0 && Renderer.stateErrors.length < 64) {
      Renderer.stateErrors.push(msg);
      console.warn('DM.Renderer: non-canonical GL state on exit from ' + msg);
    }
  }

  /** One isolated module draw call with the canonical state asserted first. */
  function draw(name, fn, arg) {
    if (!usable(name, fn)) return;
    assertState();
    if (call(name, fn, arg) && DM.GL.debug) checkState(name + '.' + fn);
  }

  // ------------------------------------------------------------------------------------------ init, context loss
  function setup() {
    const GL = DM.GL;
    ready = false;
    if (!GL.init(canvas)) return false;
    const gl = GL.gl;
    lost = false;
    timerExt = gl.getExtension('EXT_disjoint_timer_query_webgl2');
    timerQuery = null; timerPending.length = 0; gpuMs = 0;
    shadow = null; shadowTex = null;
    noShadow = GL.depthTarget(1);                            // what unit 7 holds when shadows are off: everything is lit
    noShadow.bind();
    gl.depthMask(true);
    gl.clearDepth(1);
    gl.clear(gl.DEPTH_BUFFER_BIT);
    GL.toScreen();
    postFailed = false;
    try { DM.Post.init(); } catch (e) {
      console.error('DM.Renderer: Post.init failed\n' + (e && e.stack ? e.stack : e));
      return false;
    }
    for (let i = 0; i < MODULES.length; i++) call(MODULES[i], 'init');
    call('FX', 'attach');
    GL.resetState();
    cssW = cssH = 0;
    ready = true;
    Renderer.resize();
    return true;
  }

  function onLost(e) { e.preventDefault(); lost = true; }
  function onRestored() {
    const snap = snapshot;
    if (!setup()) return;
    if (world) {
      Renderer.setWorld(world);
      if (snap) Renderer.showSnapshot(snap);                 // a time-lapse frame was on screen: put it back
    }
  }

  Renderer.init = function (cv) {
    if (!cv || !DM.GL || !DM.Post) return false;
    canvas = cv;
    if (listening !== cv) {
      if (listening) {
        listening.removeEventListener('webglcontextlost', onLost);
        listening.removeEventListener('webglcontextrestored', onRestored);
      }
      cv.addEventListener('webglcontextlost', onLost, false);
      cv.addEventListener('webglcontextrestored', onRestored, false);
      listening = cv;
    }
    return setup();
  };

  // ------------------------------------------------------------------------------------------ size and quality
  function ensureShadow() {
    const GL = DM.GL, q = GL.quality | 0;
    let size = QUALITY[q].shadow;
    if (size > 2048 && GL.caps.maxTex < 8192) size = 2048;
    size = Math.min(size, GL.caps.maxTex || size);
    if (!shadow) shadow = GL.depthTarget(size);
    else if (shadow.w !== size) shadow.resize(size);
  }

  Renderer.resize = function () {
    const GL = DM.GL;
    if (!ready || lost || !canvas) return;
    const Q = QUALITY[GL.quality | 0];
    const cw = canvas.clientWidth || canvas.width || 1, ch = canvas.clientHeight || canvas.height || 1;
    const dpr = (typeof window !== 'undefined' && window.devicePixelRatio) || 1;
    cssW = cw; cssH = ch; cssDpr = dpr;
    const ratio = Math.min(dpr, Q.ratio);
    let w = cw * ratio, h = ch * ratio;
    const mp = w * h / 1e6;
    if (mp > Q.mp) { const s = Math.sqrt(Q.mp / mp); w *= s; h *= s; }
    const maxSide = GL.caps.maxTex || 4096;
    w = Math.max(1, Math.min(maxSide, Math.round(w))); h = Math.max(1, Math.min(maxSide, Math.round(h)));
    if (canvas.width !== w) canvas.width = w;
    if (canvas.height !== h) canvas.height = h;
    DM.Post.resize(w, h);
    ensureShadow();
  };

  Renderer.setQuality = function (q) {
    q = Math.min(2, Math.max(0, Math.round(+q || 0)));
    const GL = DM.GL;
    if (!GL) return;
    const changed = GL.quality !== q;
    GL.quality = q;
    if (ready && !lost) Renderer.resize();
    if (changed) {
      pendingObjects = true;
      winT = winMs = winDt = winN = 0; slowFor = 0; Renderer.slow = false;
    }
  };

  // ------------------------------------------------------------------------------------------ world
  function measureOcean() {
    oceanAge = 0;
    if (!world || !world.elev) { oceanFrac = 0; return; }
    const elev = world.elev, sea = snapshot && typeof snapshot.seaLevel === 'number' ? snapshot.seaLevel : world.seaLevel, n = elev.length;
    let k = 0;
    for (let c = 0; c < n; c++) if (elev[c] < sea) k++;
    oceanFrac = n ? k / n : 0;
  }

  Renderer.setWorld = function (w) {
    world = Renderer.world = w || null;
    snapshot = Renderer.snapshot = null;
    flashes.length = 0;
    if (!world) return;
    seaShown = Renderer.seaLevel = +world.seaLevel || 0;
    measureOcean();
    pendingAll = true;
    if (!ready || lost) return;
    DM.GL.resetState();
    call('Planet', 'build', world);
    call('Objects', 'build', world);
    call('DevScene', 'build', world);
    DM.GL.resetState();
  };

  Renderer.showSnapshot = function (snap) {
    snap = snap || null;
    if (snap === snapshot) return;
    snapshot = Renderer.snapshot = snap;
    if (snap) {
      const u = snap.urban;
      let k = 0;
      if (u && u.length) { for (let c = 0; c < u.length; c++) if (u[c] > 0) k++; k /= u.length; }
      snapNight = Math.min(1, 0.1 + 75 * k);
      if (typeof snap.seaLevel === 'number' && Math.abs(snap.seaLevel - seaShown) > 3) seaShown = snap.seaLevel;
    } else if (world) {
      seaShown = Renderer.seaLevel = +world.seaLevel || 0;   // the time-lapse is over: back to the live sea at once
    }
    measureOcean();
    if (!ready || lost) return;
    DM.GL.resetState();
    call('Planet', 'showSnapshot', snap);
    call('Objects', 'showSnapshot', snap);
    DM.GL.resetState();
  };

  // ------------------------------------------------------------------------------------------ small public setters
  Renderer.setBrush = function (dir, ang, rgba) {
    if (!dir || !(ang > 0)) { brush[3] = 0; return; }
    const l = Math.hypot(dir[0], dir[1], dir[2]) || 1;
    brush[0] = dir[0] / l; brush[1] = dir[1] / l; brush[2] = dir[2] / l; brush[3] = +ang;
    if (rgba) {
      brush[4] = +rgba[0] || 0; brush[5] = +rgba[1] || 0; brush[6] = +rgba[2] || 0;
      brush[7] = rgba[3] === undefined ? 1 : +rgba[3];
    } else { brush[4] = brush[5] = brush[6] = brush[7] = 1; }
  };

  Renderer.flash = function (worldPos, radius, rgb, seconds) {
    if (!worldPos || !(radius > 0)) return;
    const s = DM.Save && DM.Save.settings && typeof DM.Save.settings.flashes === 'number' ? DM.Save.settings.flashes : 1;
    if (!(s > 0)) return;
    const c = rgb || [1, 1, 1];
    if (flashes.length >= 6) flashes.shift();
    flashes.push({
      x: +worldPos[0] || 0, y: +worldPos[1] || 0, z: +worldPos[2] || 0, radius: +radius,
      r: (+c[0] || 0) * s, g: (+c[1] || 0) * s, b: (+c[2] || 0) * s, age: 0, life: Math.max(0.03, +seconds || 0.3),
    });
  };

  Renderer.freeze = function (t) {
    if (t === null || t === undefined || !Number.isFinite(+t)) { Renderer.frozen = null; return; }
    t = +t;
    Renderer.frozen = t;
    time = t;
    const s = Renderer.sun;
    s.angle = ((TAU * t / Math.max(1, +s.dayLength || 150)) % TAU + TAU) % TAU;
    sunSet = false;                                          // the next frame snaps the sun instead of easing toward it
  };

  // ------------------------------------------------------------------------------------------ camera
  function updateView(v) {
    const M4 = DM.M4;
    const cw = cssW || 1, ch = cssH || 1;
    camAspect = cw / ch;
    let tx, ty, tz, ux, uy, uz;
    if (v && v.eye && v.target) {
      eye[0] = v.eye[0]; eye[1] = v.eye[1]; eye[2] = v.eye[2];
      tx = v.target[0]; ty = v.target[1]; tz = v.target[2];
      if (v.up) { ux = v.up[0]; uy = v.up[1]; uz = v.up[2]; } else { ux = 0; uy = 1; uz = 0; }
      camFov = v.fov > 0.01 && v.fov < 3 ? +v.fov : 35 * Math.PI / 180;
    } else {                                                 // far orbit over the equator
      camFov = 35 * Math.PI / 180;
      const d0 = 1.08 * (R0 + 17) / Math.sin(Math.atan(Math.tan(camFov / 2) * Math.min(1, camAspect)));
      eye[0] = d0; eye[1] = 0; eye[2] = 0;
      tx = R0; ty = 0; tz = 0; ux = 0; uy = 1; uz = 0;
    }
    const d = Math.hypot(eye[0], eye[1], eye[2]);
    const altitude = v && Number.isFinite(v.altitude) ? +v.altitude : d - R0;
    camNear = v && v.near > 0 ? +v.near : Math.min(150, Math.max(0.25, 0.05 * altitude));
    camFar = v && v.far > camNear ? +v.far : Math.sqrt(Math.max(d * d - 93 * 93, 0)) + 75;
    M4.perspective(projM, camFov, camAspect, camNear, camFar);
    M4.lookAt(viewM, eye, [tx, ty, tz], [ux, uy, uz]);
    M4.mul(viewProjM, projM, viewM);
    if (!M4.invert(invM, viewProjM)) M4.identity(invM);
    camX[0] = viewM[0]; camX[1] = viewM[4]; camX[2] = viewM[8];
    camY[0] = viewM[1]; camY[1] = viewM[5]; camY[2] = viewM[9];
    camF[0] = -viewM[2]; camF[1] = -viewM[6]; camF[2] = -viewM[10];

    view.eye.set(eye);
    view.target[0] = tx; view.target[1] = ty; view.target[2] = tz;
    view.up.set(camY);
    view.right.set(camX);
    if (v && v.focusDir) view.focusDir.set(v.focusDir);
    else { const l = Math.hypot(tx, ty, tz) || 1; view.focusDir[0] = tx / l; view.focusDir[1] = ty / l; view.focusDir[2] = tz / l; }
    view.fov = camFov; view.aspect = camAspect; view.near = camNear; view.far = camFar;
    view.zoom01 = v && Number.isFinite(v.zoom01) ? Math.min(1, Math.max(0, +v.zoom01)) : 0;
    view.altitude = altitude;
    view.viewProj.set(viewProjM); view.viewMat.set(viewM); view.projMat.set(projM); view.invViewProj.set(invM);
    view.cssW = cw; view.cssH = ch;
    DM.GL.view = view;
  }

  // ------------------------------------------------------------------------------------------ sun and shadow fit
  function updateSun(dt) {
    const s = Renderer.sun, d = s.dir;
    let x, y, z;
    if (s.mode === 'follow') {                               // up and to the side of the camera: the view is always lit
      const f = view.focusDir;
      x = f[0] * 0.64 - camX[0] * 0.55 + camY[0] * 0.53;
      y = f[1] * 0.64 - camX[1] * 0.55 + camY[1] * 0.53;
      z = f[2] * 0.64 - camX[2] * 0.55 + camY[2] * 0.53;
    } else {
      if (Renderer.frozen === null) s.angle = (s.angle + dt * TAU / Math.max(1, +s.dayLength || 150)) % TAU;
      const cd = Math.cos(SUN_DECLINATION);
      x = Math.cos(s.angle) * cd; y = Math.sin(SUN_DECLINATION); z = Math.sin(s.angle) * cd;
    }
    let l = Math.hypot(x, y, z) || 1;
    x /= l; y /= l; z /= l;
    if (sunSet && Renderer.frozen === null) {                // ease, so a mode switch or a camera cut never snaps the light
      const k = 1 - Math.exp(-3 * dt);
      x = d[0] + (x - d[0]) * k; y = d[1] + (y - d[1]) * k; z = d[2] + (z - d[2]) * k;
      l = Math.hypot(x, y, z);
      if (l < 1e-4) { x = d[0]; y = d[1]; z = d[2]; l = Math.hypot(x, y, z) || 1; }
      x /= l; y /= l; z /= l;
    }
    d[0] = x; d[1] = y; d[2] = z;
    sunSet = true;
  }

  // Orthographic light fit of SPEC-RENDER §4: a box of half-width r around the camera target (the whole planet when r reaches its
  // bound), snapped to shadow texels; z always spans the whole planet. lightM: world → light clip; shadowM: world → texture space.
  function fitShadow(size) {
    const S = Renderer.sun.dir;
    const sx = S[0], sy = S[1], sz = S[2];
    // light-space axes: X = normalize(ref × S), Y = S × X, with ref = world +Y (+X when the sun is within 5° of a pole)
    let rx = 0, ry = 1, rz = 0;
    if (Math.abs(sy) > 0.9962) { rx = 1; ry = 0; }
    let xx = ry * sz - rz * sy, xy = rz * sx - rx * sz, xz = rx * sy - ry * sx;
    const xl = Math.hypot(xx, xy, xz) || 1;
    xx /= xl; xy /= xl; xz /= xl;
    const yx = sy * xz - sz * xy, yy = sz * xx - sx * xz, yz = sx * xy - sy * xx;
    const t = view.target;
    const dist = Math.hypot(eye[0] - t[0], eye[1] - t[1], eye[2] - t[2]);
    const r = Math.min(BOUND, Math.max(12, 1.6 * dist));
    let cx = 0, cy = 0;
    if (r < BOUND - 1e-6) {
      const texel = 2 * r / size;
      cx = Math.round((t[0] * xx + t[1] * xy + t[2] * xz) / texel) * texel;
      cy = Math.round((t[0] * yx + t[1] * yy + t[2] * yz) / texel) * texel;
    }
    const ir = 1 / r, iz = 1 / BOUND;
    // clip: x = (X·p − cx) / r, y = (Y·p − cy) / r, z = −S·p / BOUND (the side facing the sun is near)
    lightM[0] = xx * ir; lightM[4] = xy * ir; lightM[8] = xz * ir; lightM[12] = -cx * ir;
    lightM[1] = yx * ir; lightM[5] = yy * ir; lightM[9] = yz * ir; lightM[13] = -cy * ir;
    lightM[2] = -sx * iz; lightM[6] = -sy * iz; lightM[10] = -sz * iz; lightM[14] = 0;
    lightM[3] = 0; lightM[7] = 0; lightM[11] = 0; lightM[15] = 1;
    for (let c = 0; c < 4; c++) {
      const o = c * 4, add = c === 3 ? 0.5 : 0;
      shadowM[o] = lightM[o] * 0.5 + add; shadowM[o + 1] = lightM[o + 1] * 0.5 + add; shadowM[o + 2] = lightM[o + 2] * 0.5 + add;
      shadowM[o + 3] = c === 3 ? 1 : 0;
    }
  }
  function noShadowMatrix() {                                // every point maps to (0.5, 0.5, 0): sunShadow returns 1
    shadowM.fill(0);
    shadowM[12] = 0.5; shadowM[13] = 0.5; shadowM[14] = 0; shadowM[15] = 1;
  }

  // ------------------------------------------------------------------------------------------ the Frame block
  function stepFlashes(dt) {
    for (let i = flashes.length - 1; i >= 0; i--) {
      flashes[i].age += dt;
      if (flashes[i].age >= flashes[i].life) flashes.splice(i, 1);
    }
  }
  function writeFrame(dt) {
    const GL = DM.GL, F = GL.frame, hdr = !!(DM.Post.scene && DM.Post.scene.hdr);
    const d = Math.hypot(eye[0], eye[1], eye[2]);
    F.setMat('uViewProj', viewProjM);
    F.setMat('uView', viewM);
    F.setMat('uInvViewProj', invM);
    F.setMat('uShadowMat', shadowM);
    F.set('uCamPos', eye[0], eye[1], eye[2], d - R0);
    const s = Renderer.sun.dir, sc = Renderer.SUN_COLOR;
    F.set('uSunDir', s[0], s[1], s[2], hdr ? 1 : 0.4);
    F.set('uSunCol', sc[0], sc[1], sc[2], 1);
    const w = world, snap = snapshot;
    const year = snap && Number.isFinite(snap.year) ? snap.year : (w ? +w.year || 0 : 0);
    F.set('uTime', time, year, time * 0.01, dt);
    F.set('uScreen', DM.Post.scene ? DM.Post.scene.w : canvas.width, DM.Post.scene ? DM.Post.scene.h : canvas.height, view.zoom01, GL.quality | 0);
    F.set('uPlanet', R0, seaShown, GL.CLOUD_R, GL.ATMO_R);
    F.set('uBrush', brush[0], brush[1], brush[2], brush[3]);
    F.set('uBrushCol', brush[4], brush[5], brush[6], brush[7]);
    let best = null, bestK = 0;                              // one light slot: the strongest flash right now
    for (let i = 0; i < flashes.length; i++) {
      const f = flashes[i];
      let k = 1 - f.age / f.life;
      k = k * k * Math.max(f.r, f.g, f.b) * f.radius;
      if (k > bestK) { bestK = k; best = f; }
    }
    if (best) {
      const k = (1 - best.age / best.life) * (1 - best.age / best.life);
      F.set('uFlash', best.x, best.y, best.z, best.radius);
      F.set('uFlashCol', best.r * k, best.g * k, best.b * k, 0);
    } else { F.set('uFlash', 0, 0, 0, 0); F.set('uFlashCol', 0, 0, 0, 0); }
    const heat = snap && Number.isFinite(snap.heat) ? snap.heat : (w ? +w.heat || 0 : 0);
    const night = snap ? snapNight : NIGHT_LEVEL[Math.min(6, Math.max(0, w ? w.age | 0 : 0))];
    const overlay = DM.Planet && !Renderer.disabled.Planet ? +DM.Planet.overlay || 0 : 0;
    F.set('uWorld', heat, w ? Math.min(1, Math.max(0, +w.dust || 0)) : 0, night, overlay);
    F.set('uFx', sstep(14, 45, view.altitude), 0.5, oceanFrac, 0);
    for (let i = 0; i < 8; i++) F.setStorm(i, 0, 0, 0, 0);
  }

  // ------------------------------------------------------------------------------------------ timing
  function timerBegin() {
    const gl = DM.GL.gl;
    if (!timerExt) return;
    while (timerPending.length) {                            // collect finished queries, oldest first
      const q = timerPending[0];
      if (!gl.getQueryParameter(q, gl.QUERY_RESULT_AVAILABLE)) break;
      const ns = gl.getQueryParameter(q, gl.QUERY_RESULT);
      if (!gl.getParameter(timerExt.GPU_DISJOINT_EXT)) gpuMs = ns / 1e6;
      gl.deleteQuery(q);
      timerPending.shift();
    }
    if (timerPending.length < 4) {
      timerQuery = gl.createQuery();
      gl.beginQuery(timerExt.TIME_ELAPSED_EXT, timerQuery);
    }
  }
  function timerEnd() {
    if (!timerQuery) return;
    DM.GL.gl.endQuery(timerExt.TIME_ELAPSED_EXT);
    timerPending.push(timerQuery);
    timerQuery = null;
  }
  function trackSlow(ms, dt) {
    if (Renderer.frozen !== null || !(dt > 0) || dt > 0.25) return;       // paused tab, first frame, scripted time: not evidence
    winT += dt; winMs += ms; winDt += dt; winN++;
    if (winT < 2) return;
    const meanMs = winMs / winN, meanDt = 1000 * winDt / winN;
    const over = meanMs > 26 || (!timerExt && meanDt > 40);
    slowFor = over ? slowFor + winT : 0;
    Renderer.slow = slowFor >= 4;
    winT = winMs = winDt = winN = 0;
  }

  // ------------------------------------------------------------------------------------------ the frame
  function render(v, realDt, dt) {
    const GL = DM.GL, gl = GL.gl, Post = DM.Post, U = DM.U;
    const t0 = now();
    if (!canvas.width || !Post.scene || canvas.clientWidth !== cssW || canvas.clientHeight !== cssH ||
        ((typeof window !== 'undefined' && window.devicePixelRatio) || 1) !== cssDpr) Renderer.resize();
    if (!Post.scene) return;
    timerBegin();
    GL.stats.draws = GL.stats.tris = GL.stats.instances = 0;
    Renderer.stats.particles = 0;

    // --- everything the Frame block needs
    updateView(v);
    updateSun(dt);
    stepFlashes(dt);
    const snap = snapshot;
    const seaTarget = snap && typeof snap.seaLevel === 'number' ? snap.seaLevel : (world ? +world.seaLevel || 0 : 0);
    const gap = Math.abs(seaTarget - seaShown);
    seaShown = U.approach(seaShown, seaTarget, dt * (snap ? Math.max(4, gap * 6) : Math.max(1, gap * 0.6)));
    Renderer.seaLevel = seaShown;
    const shadowsOn = Renderer.shadows !== false && !!shadow;
    Renderer.shadowSize = shadowsOn ? shadow.w : 0;
    if (shadowsOn) fitShadow(shadow.w); else noShadowMatrix();
    writeFrame(dt);

    // --- 1. CPU work
    const dirty = Renderer.dirty;
    if (world && world.dirty) {
      const wd = world.dirty;
      for (let i = 0; i < DIRTY_KEYS.length; i++) {
        const k = DIRTY_KEYS[i];
        dirty[k] = !!wd[k] || pendingAll || (pendingObjects && k === 'objects');
        wd[k] = false;
      }
      pendingAll = pendingObjects = false;
      oceanAge += realDt;
      if ((dirty.elev || dirty.water) && oceanAge > 0.5) measureOcean();
      else if (oceanAge > 4) measureOcean();                 // the sea level also creeps without a flag (climate)
      GL.resetState();
      call('Planet', 'sync', world, dt, dirty);
      call('Objects', 'sync', world, dt, dirty);
      call('DevScene', 'sync', world, dt, dirty);
      call('Sky', 'update', world, dt);
      if (!snap) call('FX', 'update', world, dt);
    } else {
      for (let i = 0; i < DIRTY_KEYS.length; i++) dirty[DIRTY_KEYS[i]] = false;
    }

    // --- 2. shadow pass (uViewProj = the light's matrix)
    if (shadowsOn) {
      GL.frame.setMat('uViewProj', lightM);
      GL.frame.upload();
      pass = SHADOW;
      shadowTex = null;
      assertState();
      gl.clearDepth(1);
      gl.clear(gl.DEPTH_BUFFER_BIT);
      if (world) {
        draw('Planet', 'drawDepth');
        draw('DevScene', 'drawDepth');
        if ((GL.quality | 0) > 0) draw('Objects', 'drawDepth');
      }
      GL.resetState();
      gl.disable(gl.POLYGON_OFFSET_FILL);
      gl.colorMask(true, true, true, true);
    }

    // --- 3. scene pass
    GL.frame.setMat('uViewProj', viewProjM);
    GL.frame.upload();
    pass = SCENE;
    shadowTex = shadowsOn ? shadow.depthTex : noShadow.depthTex;
    GL.resetState();
    gl.disable(gl.POLYGON_OFFSET_FILL);
    Post.beginScene();
    GL.bindTex(7, shadowTex);
    if (world) {
      draw('Sky', 'drawBack');
      draw('Planet', 'drawTerrain');
      draw('DevScene', 'draw');
      draw('Objects', 'draw');
      draw('Planet', 'drawWater');
      if (!snap) draw('FX', 'draw', 0);
      draw('Sky', 'drawClouds');
      draw('Sky', 'drawAtmosphere');
      draw('DevScene', 'drawBlend');
      if (!snap) draw('FX', 'draw', 1);
    }

    // --- 4. post
    assertState();
    try { Post.run(); } catch (e) {
      if (!postFailed) console.error('DM.Renderer: Post.run failed\n' + (e && e.stack ? e.stack : e));
      postFailed = true;
    }
    GL.resetState();
    if (GL.debug) GL.checkErrors('Renderer.frame');
    timerEnd();

    const st = Renderer.stats;
    st.draws = GL.stats.draws; st.tris = GL.stats.tris; st.instances = GL.stats.instances;
    if (!st.particles && DM.FX && typeof DM.FX.count === 'number') st.particles = DM.FX.count;
    st.cpu = now() - t0; st.gpu = gpuMs; st.ms = st.cpu + st.gpu;
    trackSlow(st.ms, realDt);
  }

  let lastView = null;
  Renderer.frame = function (v, dt) {
    if (!ready || lost) return;
    if (DM.GL.gl.isContextLost()) { lost = true; return; }
    dt = Number.isFinite(dt) && dt > 0 ? Math.min(dt, 0.25) : 0;
    const frozen = Renderer.frozen !== null;
    if (frozen) time = Renderer.frozen; else time += dt;
    lastView = v || null;
    render(lastView, dt, frozen ? FROZEN_DT : dt);
  };

  Renderer.capture = function (mime, quality) {
    if (!ready || lost || !canvas) return '';
    render(lastView, 0, 0);
    return canvas.toDataURL(mime || 'image/jpeg', quality === undefined ? 0.9 : quality);
  };

  // ------------------------------------------------------------------------------------------ picking and projection
  Renderer.pick = function (clientX, clientY, out) {
    if (!canvas || !out) return false;
    const rect = canvas.getBoundingClientRect();
    if (!(rect.width > 0 && rect.height > 0)) return false;
    const nx = ((clientX - rect.left) / rect.width) * 2 - 1, ny = 1 - ((clientY - rect.top) / rect.height) * 2;
    const th = Math.tan(camFov / 2), kx = nx * th * camAspect, ky = ny * th;
    let dx = camF[0] + camX[0] * kx + camY[0] * ky, dy = camF[1] + camX[1] * kx + camY[1] * ky, dz = camF[2] + camX[2] * kx + camY[2] * ky;
    const l = Math.hypot(dx, dy, dz) || 1;
    dx /= l; dy /= l; dz /= l;
    if (usable('Planet', 'raycast')) {
      try { return !!DM.Planet.raycast([eye[0], eye[1], eye[2]], [dx, dy, dz], out); }
      catch (e) { disable('Planet', 'raycast', e); }
    }
    const b = eye[0] * dx + eye[1] * dy + eye[2] * dz;
    const disc = b * b - (eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]) + R0 * R0;
    if (disc < 0) return false;
    const t = -b - Math.sqrt(disc);
    if (!(t > 0)) return false;
    const px = eye[0] + dx * t, py = eye[1] + dy * t, pz = eye[2] + dz * t;
    if (!out.pos) out.pos = [0, 0, 0];
    if (!out.dir) out.dir = [0, 0, 0];
    out.pos[0] = px; out.pos[1] = py; out.pos[2] = pz;
    out.dir[0] = px / R0; out.dir[1] = py / R0; out.dir[2] = pz / R0;
    out.t = t;
    out.cell = world && world.geo ? world.geo.cellAt(px, py, pz, out.cell >= 0 && out.cell < world.n ? out.cell : -1) : -1;
    return true;
  };

  Renderer.project = function (p, out) {
    if (!canvas || !p || !out) return false;
    const m = viewProjM, x = p[0], y = p[1], z = p[2];
    const w = m[3] * x + m[7] * y + m[11] * z + m[15];
    out.depth = w;
    if (!(w > camNear * 0.5)) return false;
    const cw = cssW || canvas.clientWidth || 1, ch = cssH || canvas.clientHeight || 1;
    out.x = ((m[0] * x + m[4] * y + m[8] * z + m[12]) / w * 0.5 + 0.5) * cw;
    out.y = (0.5 - (m[1] * x + m[5] * y + m[9] * z + m[13]) / w * 0.5) * ch;
    // Hidden when the segment eye → p passes through the planet. Without asking the terrain, what is certainly solid is the sphere
    // up to the displayed sea surface (every cell is water at that radius or land above it), or up to p's own radius when p is lower.
    // A point on that sphere is hidden exactly when it is on the far side (so project → pick round-trips right up to the limb).
    const dx = x - eye[0], dy = y - eye[1], dz = z - eye[2];
    const len2 = dx * dx + dy * dy + dz * dz;
    const occ = Math.min(R0 + seaShown, Math.sqrt(x * x + y * y + z * z));
    if (occ > 0 && len2 > 0) {
      const b = eye[0] * dx + eye[1] * dy + eye[2] * dz;
      const c = eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2] - occ * occ;
      const disc = b * b - len2 * c;
      if (disc > 0) {
        const t = (-b - Math.sqrt(disc)) / len2;
        if (t > 0 && t < 1 - 1e-6) return false;
      }
    }
    return true;
  };
})(globalThis.DM = globalThis.DM || {});
