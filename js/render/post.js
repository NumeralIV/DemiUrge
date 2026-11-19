// DEMIURGE — js/render/post.js : DM.Post, the scene target and everything between it and the canvas (SPEC-RENDER §5).
//
//   Post.init()                 create the programs (callable again after a context restore)
//   Post.resize(w, h)           (re)allocate the targets for a w × h drawing buffer at the current GL.quality
//   Post.scene                  the scene target: MSAA 4× HDR + depth at quality ≥ 1, single-sample HDR + depth at quality 0
//                               (RGBA8 when GL.caps.hdr is false)
//   Post.beginScene()           bind and clear the scene target (black, depth 1)
//   Post.run()                  resolve → bloom → tilt-shift → tonemap / grade / vignette / grain / flash / fade (+ FXAA at quality 0) → canvas
//   Post.set = { exposure: 0.6, bloom: 0.9, tilt: 1, focusY: 0.5, vignette: 0.35, grain: 0.02, fade: 0, saturation: 1.05,
//                flash: [r, g, b, a] }
//   Post.info                   what the last run did: { samples, hdr, bloomLevels, tilt (strength used), fxaa, w, h }
//
// Bloom: soft-knee threshold 3.0 scene units (knee 1.0), input clamped to 64, 3 levels at quality 0 and 5 above, summed with a
// tent filter on the way up and divided by the level count. Nothing diffuse reaches the knee (sun-lit white is about 2.15).
// Tilt-shift: a separable blur whose radius grows away from the horizontal band at screen height focusY (0 = top);
// strength = set.tilt × smoothstep(0.45, 1, zoom01); off at quality 0. Tonemap: ACES fit, then gamma 2.2.
// `flash` is a full-screen tint applied after the tonemap; its alpha is scaled by the `flashes` setting (DM.Save.settings).
// Without float targets (GL.caps.hdr false) the scene arrives scaled by 0.4: exposure is divided by 0.4 and bloom is skipped.
(function (DM) {
  'use strict';

  const Post = (DM.Post = DM.Post || {});
  const BLOOM_THRESHOLD = 3.0, BLOOM_KNEE = 1.0, BLOOM_CLAMP = 64.0, LDR_SCALE = 0.4;

  Post.scene = null;
  Post.set = Post.set || {
    exposure: 0.6, bloom: 0.9, tilt: 1, focusY: 0.5, vignette: 0.35, grain: 0.02, fade: 0, saturation: 1.05, flash: [1, 1, 1, 0],
  };
  Post.info = { samples: 0, hdr: false, bloomLevels: 0, tilt: 0, fxaa: false, w: 0, h: 0 };

  let prog = null;                   // { pre, down, up, tilt, final, finalFxaa }
  let resolve = null, tiltA = null, chain = [];
  let builtQ = -1, builtW = 0, builtH = 0, builtHdr = null;

  // ------------------------------------------------------------------------------------------ shaders
  const FS_DOWN_FN = [
    'uniform sampler2D uTex;',
    'uniform vec2 uPx;                // size of one source texel',
    'in vec2 vUv;',
    'out vec4 oColor;',
    'vec3 dm_tap(vec2 o) { return min(textureLod(uTex, vUv + o * uPx, 0.0).rgb, vec3(' + BLOOM_CLAMP.toFixed(1) + ')); }',
    'vec3 dm_down13() {',
    '  vec3 a = dm_tap(vec2(-2.0, 2.0)), b = dm_tap(vec2(0.0, 2.0)), c = dm_tap(vec2(2.0, 2.0));',
    '  vec3 d = dm_tap(vec2(-2.0, 0.0)), e = dm_tap(vec2(0.0, 0.0)), f = dm_tap(vec2(2.0, 0.0));',
    '  vec3 g = dm_tap(vec2(-2.0, -2.0)), h = dm_tap(vec2(0.0, -2.0)), i = dm_tap(vec2(2.0, -2.0));',
    '  vec3 j = dm_tap(vec2(-1.0, 1.0)), k = dm_tap(vec2(1.0, 1.0)), l = dm_tap(vec2(-1.0, -1.0)), m = dm_tap(vec2(1.0, -1.0));',
    '  return e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;',
    '}',
  ].join('\n');

  const FS_PRE = FS_DOWN_FN + '\n' + [
    'void main() {',
    '  vec3 c = dm_down13();',
    '  float br = max(c.r, max(c.g, c.b));',
    '  float soft = clamp(br - ' + (BLOOM_THRESHOLD - BLOOM_KNEE).toFixed(2) + ', 0.0, ' + (2 * BLOOM_KNEE).toFixed(2) + ');',
    '  soft = soft * soft / ' + (4 * BLOOM_KNEE).toFixed(2) + ';',
    '  float k = max(soft, br - ' + BLOOM_THRESHOLD.toFixed(2) + ') / max(br, 1e-4);',
    '  oColor = vec4(c * k, 1.0);',
    '}',
  ].join('\n');

  const FS_DOWN = FS_DOWN_FN + '\nvoid main() { oColor = vec4(dm_down13(), 1.0); }';

  const FS_UP = [
    'uniform sampler2D uTex;',
    'uniform vec2 uPx;',
    'in vec2 vUv;',
    'out vec4 oColor;',
    'void main() {',
    '  vec3 s = textureLod(uTex, vUv, 0.0).rgb * 0.25;',
    '  s += (textureLod(uTex, vUv + vec2(uPx.x, 0.0), 0.0).rgb + textureLod(uTex, vUv - vec2(uPx.x, 0.0), 0.0).rgb',
    '      + textureLod(uTex, vUv + vec2(0.0, uPx.y), 0.0).rgb + textureLod(uTex, vUv - vec2(0.0, uPx.y), 0.0).rgb) * 0.125;',
    '  s += (textureLod(uTex, vUv + uPx, 0.0).rgb + textureLod(uTex, vUv - uPx, 0.0).rgb',
    '      + textureLod(uTex, vUv + vec2(uPx.x, -uPx.y), 0.0).rgb + textureLod(uTex, vUv + vec2(-uPx.x, uPx.y), 0.0).rgb) * 0.0625;',
    '  oColor = vec4(s, 1.0);',
    '}',
  ].join('\n');

  // the blur radius in pixels at screen height uv.y; shared by the horizontal pass and the vertical taps of the final pass
  const TILT_FN = [
    'uniform vec4 uTilt;              // x strength 0..1, y focus height in uv (0 = bottom), z max radius in px, w unused',
    'float dm_tiltRadius(float y) {',
    '  float d = abs(y - uTilt.y);',
    '  return uTilt.x * uTilt.z * smoothstep(0.09, 0.56, d);',
    '}',
  ].join('\n');

  const FS_TILT = [
    'uniform sampler2D uTex;',
    'uniform vec2 uPx;',
    TILT_FN,
    'in vec2 vUv;',
    'out vec4 oColor;',
    'void main() {',
    '  float r = dm_tiltRadius(vUv.y);',
    '  vec3 c = textureLod(uTex, vUv, 0.0).rgb;',
    '  if (r > 0.3) {',
    '    float wsum = 1.0;',
    '    for (int i = 1; i <= 5; i++) {',
    '      float fi = float(i);',
    '      float w = exp(-0.08 * fi * fi);',
    '      vec2 o = vec2(uPx.x * r * fi * 0.2, 0.0);',
    '      c += (textureLod(uTex, vUv + o, 0.0).rgb + textureLod(uTex, vUv - o, 0.0).rgb) * w;',
    '      wsum += 2.0 * w;',
    '    }',
    '    c /= wsum;',
    '  }',
    '  oColor = vec4(c, 1.0);',
    '}',
  ].join('\n');

  const FS_FINAL = [
    '#include <noise>',
    'uniform sampler2D uTex;          // the scene (resolved; horizontally tilt-blurred when uTilt.x > 0)',
    'uniform sampler2D uBloom;',
    'uniform vec2 uPx;                // one scene texel',
    'uniform vec4 uP0;                // exposure, bloom gain, vignette, grain',
    'uniform vec4 uP1;                // fade, saturation, unused, unused',
    'uniform vec4 uFlashTint;         // rgb, a',
    TILT_FN,
    'in vec2 vUv;',
    'out vec4 oColor;',
    'vec3 dm_aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }',
    'vec3 dm_scene(vec2 uv) {',
    '  vec3 c = textureLod(uTex, uv, 0.0).rgb;',
    '  float r = dm_tiltRadius(uv.y);',
    '  if (r > 0.3) {',
    '    float wsum = 1.0;',
    '    for (int i = 1; i <= 5; i++) {',
    '      float fi = float(i);',
    '      float w = exp(-0.08 * fi * fi);',
    '      vec2 o = vec2(0.0, uPx.y * r * fi * 0.2);',
    '      c += (textureLod(uTex, uv + o, 0.0).rgb + textureLod(uTex, uv - o, 0.0).rgb) * w;',
    '      wsum += 2.0 * w;',
    '    }',
    '    c /= wsum;',
    '  }',
    '  return c;',
    '}',
    '// scene + bloom -> exposure -> ACES -> gamma 2.2 (display-referred, what FXAA wants to look at)',
    'vec3 dm_display(vec2 uv, vec3 bloom) { return pow(dm_aces((dm_scene(uv) + bloom) * uP0.x), vec3(0.4545455)); }',
    'float dm_luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }',
    'void main() {',
    '  vec3 bloom = textureLod(uBloom, vUv, 0.0).rgb * uP0.y;',
    '  vec3 c = dm_display(vUv, bloom);',
    '#ifdef DM_FXAA',
    '  vec3 nw = dm_display(vUv + vec2(-1.0, 1.0) * uPx, bloom), ne = dm_display(vUv + vec2(1.0, 1.0) * uPx, bloom);',
    '  vec3 sw = dm_display(vUv + vec2(-1.0, -1.0) * uPx, bloom), se = dm_display(vUv + vec2(1.0, -1.0) * uPx, bloom);',
    '  float lNW = dm_luma(nw), lNE = dm_luma(ne), lSW = dm_luma(sw), lSE = dm_luma(se), lM = dm_luma(c);',
    '  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE))), lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));',
    '  if (lMax - lMin > max(0.03, lMax * 0.10)) {',
    '    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));',
    '    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);',
    '    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * uPx;',
    '    vec3 a = 0.5 * (dm_display(vUv + dir * (1.0 / 3.0 - 0.5), bloom) + dm_display(vUv + dir * (2.0 / 3.0 - 0.5), bloom));',
    '    vec3 b = a * 0.5 + 0.25 * (dm_display(vUv - dir * 0.5, bloom) + dm_display(vUv + dir * 0.5, bloom));',
    '    float lB = dm_luma(b);',
    '    c = (lB < lMin || lB > lMax) ? a : b;',
    '  }',
    '#endif',
    '  c = mix(vec3(dm_luma(c)), c, uP1.y);',
    '  vec2 q = vUv - 0.5;',
    '  c *= 1.0 - uP0.z * smoothstep(0.18, 0.78, dot(q, q) * 1.9);',
    '  c = mix(c, uFlashTint.rgb, uFlashTint.a);',
    '  c *= 1.0 - uP1.x;',
    '  float n = hash13(vec3(gl_FragCoord.xy, floor(uTime.x * 60.0)));',
    '  c += (n - 0.5) * (uP0.w + 0.004) * (1.0 - uP1.x);',
    '  oColor = vec4(clamp(c, 0.0, 1.0), 1.0);',
    '}',
  ].join('\n');

  // ------------------------------------------------------------------------------------------ lifecycle
  Post.init = function () {
    const GL = DM.GL;
    prog = {
      pre: GL.program('post.bloomPre', GL.FS_VS, FS_PRE),
      down: GL.program('post.bloomDown', GL.FS_VS, FS_DOWN),
      up: GL.program('post.bloomUp', GL.FS_VS, FS_UP),
      tilt: GL.program('post.tilt', GL.FS_VS, FS_TILT),
      final: GL.program('post.final', GL.FS_VS, FS_FINAL),
      finalFxaa: GL.program('post.finalFxaa', GL.FS_VS, '#define DM_FXAA\n' + FS_FINAL),
    };
    // targets of a previous context are gone with it; resize() builds new ones
    Post.scene = null; resolve = null; tiltA = null; chain = [];
    builtQ = -1; builtW = builtH = 0; builtHdr = null;
    if (GL.canvas && GL.canvas.width > 0) Post.resize(GL.canvas.width, GL.canvas.height);
  };

  function freeTargets() {
    if (Post.scene) Post.scene.free();
    if (resolve) resolve.free();
    if (tiltA) tiltA.free();
    for (let i = 0; i < chain.length; i++) chain[i].free();
    Post.scene = null; resolve = null; tiltA = null; chain = [];
  }

  Post.resize = function (w, h) {
    const GL = DM.GL;
    w = Math.max(1, w | 0); h = Math.max(1, h | 0);
    const q = GL.quality | 0, hdr = !!GL.caps.hdr;
    if (Post.scene && q === builtQ && w === builtW && h === builtH && hdr === builtHdr) return;
    freeTargets();
    builtQ = q; builtW = w; builtH = h; builtHdr = hdr;
    // `deep`: without float targets the scene is RGB10_A2 rather than RGBA8 (linear light in 8 bits bands badly on the night side)
    Post.scene = GL.target(w, h, { hdr: true, deep: true, depth: true, samples: q >= 1 ? 4 : 0 });
    if (Post.scene.samples > 0) resolve = GL.target(w, h, { hdr: true, deep: true });
    if (q >= 1) tiltA = GL.target(w, h, { hdr: true, deep: true });
    if (hdr) {
      const levels = q >= 1 ? 5 : 3;
      let cw = w, ch = h;
      for (let i = 0; i < levels; i++) {
        cw = Math.max(1, cw >> 1); ch = Math.max(1, ch >> 1);
        if (i > 0 && (cw < 4 || ch < 4)) break;
        chain.push(GL.target(cw, ch, { hdr: true }));
      }
    }
  };

  Post.beginScene = function () {
    const GL = DM.GL, gl = GL.gl;
    if (!Post.scene) Post.resize(GL.canvas.width, GL.canvas.height);
    Post.scene.bind();
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(true, true, true, true);
    gl.depthMask(true);
    gl.clearColor(0, 0, 0, 1);
    gl.clearDepth(1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  };

  function pass(p, tex, pxW, pxH) {
    const GL = DM.GL, gl = GL.gl;
    p.use();
    GL.bindTex(0, tex);
    if (p.u.uTex) gl.uniform1i(p.u.uTex, 0);
    if (p.u.uPx) gl.uniform2f(p.u.uPx, 1 / pxW, 1 / pxH);
    return p;
  }

  Post.run = function () {
    const GL = DM.GL, gl = GL.gl, S = Post.set, scene = Post.scene;
    if (!scene || !prog) return;
    const q = GL.quality | 0, hdr = scene.hdr, info = Post.info;
    const W = scene.w, H = scene.h;

    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(false);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.colorMask(true, true, true, true);

    // 1. resolve
    let src = scene;
    if (scene.samples > 0) { scene.blitTo(resolve); src = resolve; }

    // 2. bloom
    let bloomTex = null, levels = 0;
    const bloomGain = Math.max(0, +S.bloom || 0);
    if (hdr && chain.length > 0 && bloomGain > 0) {
      levels = chain.length;
      chain[0].bind();
      pass(prog.pre, src.tex, src.w, src.h);
      GL.fsTri();
      for (let i = 1; i < levels; i++) {
        chain[i].bind();
        pass(prog.down, chain[i - 1].tex, chain[i - 1].w, chain[i - 1].h);
        GL.fsTri();
      }
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE);
      for (let i = levels - 2; i >= 0; i--) {
        chain[i].bind();
        pass(prog.up, chain[i + 1].tex, chain[i + 1].w, chain[i + 1].h);
        GL.fsTri();
      }
      gl.disable(gl.BLEND);
      bloomTex = chain[0].tex;
    }

    // 3. tilt-shift, horizontal half (the vertical half rides in the final pass)
    const zoom = GL.view ? +GL.view.zoom01 || 0 : 0;
    const zs = Math.min(1, Math.max(0, (zoom - 0.45) / 0.55));
    let tilt = q >= 1 && tiltA ? Math.max(0, +S.tilt || 0) * zs * zs * (3 - 2 * zs) : 0;
    if (tilt < 0.004) tilt = 0;
    const focus = 1 - Math.min(1, Math.max(0, S.focusY === undefined ? 0.5 : +S.focusY));
    const maxR = 0.0105 * H;
    if (tilt > 0) {
      tiltA.bind();
      const p = pass(prog.tilt, src.tex, W, H);
      gl.uniform4f(p.u.uTilt, tilt, focus, maxR, 0);
      GL.fsTri();
      src = tiltA;
    }

    // 4. composite, grade, to the canvas
    GL.toScreen();
    const fxaa = q === 0;
    const p = pass(fxaa ? prog.finalFxaa : prog.final, src.tex, W, H);
    GL.bindTex(1, bloomTex || src.tex);                       // never leave a sampler without a texture (gain is 0 then)
    if (p.u.uBloom) gl.uniform1i(p.u.uBloom, 1);
    const exposure = (+S.exposure || 0) / (hdr ? 1 : LDR_SCALE);
    if (p.u.uP0) gl.uniform4f(p.u.uP0, exposure, bloomTex ? bloomGain / levels : 0, +S.vignette || 0, +S.grain || 0);
    if (p.u.uP1) gl.uniform4f(p.u.uP1, Math.min(1, Math.max(0, +S.fade || 0)), S.saturation === undefined ? 1 : +S.saturation, 0, 0);
    if (p.u.uTilt) gl.uniform4f(p.u.uTilt, tilt, focus, maxR, 0);
    const f = S.flash || [0, 0, 0, 0];
    const fs = DM.Save && DM.Save.settings && typeof DM.Save.settings.flashes === 'number' ? DM.Save.settings.flashes : 1;
    if (p.u.uFlashTint) gl.uniform4f(p.u.uFlashTint, +f[0] || 0, +f[1] || 0, +f[2] || 0, Math.min(1, Math.max(0, (+f[3] || 0) * fs)));
    GL.fsTri();

    GL.bindTex(1, null);
    GL.bindTex(0, null);
    gl.enable(gl.DEPTH_TEST);
    gl.depthMask(true);

    info.samples = scene.samples; info.hdr = hdr; info.bloomLevels = levels; info.tilt = tilt; info.fxaa = fxaa; info.w = W; info.h = H;
  };
})(globalThis.DM = globalThis.DM || {});
