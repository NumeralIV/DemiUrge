// DEMIURGE — tests/browser/rendercore.scn.js : acceptance scenario for the render core (BUILD.md A15; SPEC-RENDER §0–5, §11, §12).
//
//   node tools/shot.js --run tests/browser/rendercore.scn.js            (about 40 s; add --timeout 300000 on a busy machine)
//
// Drives dev/view.html with NO planet / sky / objects / fx module (only=none) and the viewer's own noise planet (world=noise, so
// nothing depends on the sim or the fixtures): the built-in test scene is a lit grey sphere of radius 100 with boxes on it (one
// white, one emissive, a tall pillar), a village of instanced houses and the stand-in atmosphere halo.
// Every check runs; then the scenario throws with the list of the ones that failed, or returns the measured numbers.
// Screenshots: shots/rendercore-<view>.png (orbit, mid, close, night, terminator, noon, q0, q1, q2, nohdr, nohdr-q0, planet).
'use strict';
const path = require('path');
const { decodePng } = require('../../tools/shot.js');

const W = 1280, H = 720, DEG = Math.PI / 180;
const BASE = 'dev/view.html?only=none&world=noise&t=5&size=' + W + 'x' + H;
const SITE = '&lat=14&lon=24';                                  // the test scene's cluster of boxes
const SUN_LON = 24;                                             // sunAngle used by the orbit-sun views (noon over the boxes)

const luma = p => 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
const aces = x => Math.min(1, Math.max(0, (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)));
const display = (v, exposure) => 255 * Math.pow(aces(v * exposure), 1 / 2.2);
const r1 = v => Math.round(v * 10) / 10, r3 = v => Math.round(v * 1000) / 1000;

/** Mean colour of the (2r+1)² pixels round (x, y): averages the film grain away. */
function mean(img, x, y, r) {
  r = r === undefined ? 2 : r;
  const cx = Math.round(x), cy = Math.round(y), s = [0, 0, 0];
  let n = 0;
  for (let j = -r; j <= r; j++) for (let i = -r; i <= r; i++) {
    const px = cx + i, py = cy + j;
    if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
    const p = img.px(px, py);
    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; n++;
  }
  return n ? [s[0] / n, s[1] / n, s[2] / n] : [0, 0, 0];
}
/** Mean absolute difference per channel (0..255) between two images over a rectangle. */
function mad(a, b, x0, y0, x1, y1) {
  x0 = Math.max(0, Math.round(x0)); y0 = Math.max(0, Math.round(y0)); x1 = Math.min(a.width, Math.round(x1)); y1 = Math.min(a.height, Math.round(y1));
  let s = 0, n = 0;
  const da = a.data, db = b.data, w = a.width;
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const o = (y * w + x) * 4;
    s += Math.abs(da[o] - db[o]) + Math.abs(da[o + 1] - db[o + 1]) + Math.abs(da[o + 2] - db[o + 2]); n += 3;
  }
  return n ? s / n : 0;
}
function maxLuma(img, x, y, r) {
  let m = 0;
  for (let j = -r; j <= r; j++) for (let i = -r; i <= r; i++) {
    const px = Math.round(x) + i, py = Math.round(y) + j;
    if (px >= 0 && py >= 0 && px < img.width && py < img.height) m = Math.max(m, luma(img.px(px, py)));
  }
  return m;
}

module.exports = async (page, browser) => {
  const t0 = Date.now();
  const out = { ok: false, checks: {}, numbers: {}, frameMs: {}, shots: [], seconds: 0 };
  const N = out.numbers, failed = [];
  function check(name, cond, detail) {
    out.checks[name] = cond ? 'ok' : 'FAIL';
    if (!cond) failed.push(name + (detail !== undefined ? '  ' + JSON.stringify(detail) : ''));
    return !!cond;
  }
  const ev = js => page.eval(js);
  const settle = n => page.eval('DV.settle(' + (n || 3) + ')');
  const png = async () => decodePng(await page.png());
  async function open(query) {
    await page.goto(BASE + query);
    await page.waitFor('window.DV && DV.ready', 45000);
    await settle(3);
  }
  async function shot(name) {
    const file = await page.shot('rendercore-' + name + '.png');
    out.shots.push(path.basename(file));
    return decodePng(file);
  }
  const hdrAt = pts => ev('DV.check.hdr(' + JSON.stringify(pts.map(p => [p.x, p.y])) + ')');
  const frameMs = async name => { out.frameMs[name] = await ev('DV.frameMs(30)'); };

  // ================================================================================================ 1. the main page: mid view
  await open('&zoom=0.5' + SITE + '&debug=1');
  let st = await ev('DV.stats()');
  N.renderer = await ev('(() => { const gl = DM.GL.gl, e = gl.getExtension("WEBGL_debug_renderer_info"); return e ? gl.getParameter(e.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()');
  N.caps = await ev('DM.GL.caps');
  check('viewer: no planet / sky / objects / fx module, the test scene stands in', Object.values(st.modules).every(m => m !== 'ok') && st.stand === 'test' && st.how === 'noise', st.modules);
  check('caps: HDR targets, MSAA 4, linear float', N.caps.hdr === true && N.caps.maxSamples === 4 && N.caps.maxTex >= 4096, N.caps);
  check('quality 2: HDR scene, MSAA 4x, 5 bloom levels, 4096 shadow map, no FXAA', st.hdr && st.samples === 4 && st.bloomLevels === 5 && st.shadowSize === 4096 && !st.fxaa && st.quality === 2,
    { hdr: st.hdr, samples: st.samples, bloom: st.bloomLevels, shadow: st.shadowSize });
  check('drawing buffer matches the CSS size', st.buffer[0] === W && st.buffer[1] === H, st.buffer);

  // --- Frame block, chunks, error reporting, the helpers
  const fo = await ev('DV.check.frameOffsets()');
  N.frameOffsets = {};
  for (const k in fo.table) N.frameOffsets[k] = fo.table[k][1];
  N.frameBlockBytes = fo.glSize;
  check('Frame block: CPU offsets equal gl.getActiveUniforms(UNIFORM_OFFSET), 576 bytes, binding point 0', fo.ok && fo.glSize === 576 && fo.binding === 0, fo);
  const ch = await ev('DV.check.allChunks()');
  check('a program including every chunk (both stages) compiles; uShadowMap on unit 7, aPos at location 0', ch.ok, ch);
  const bad = await ev('DV.check.badShader()');
  N.badShaderError = bad.head;
  check('a failing shader throws with the program name, the driver log and numbered source', bad.threw && bad.hasName && bad.hasNumbered && bad.hasLog, bad);
  const hp = await ev('DV.check.helpers()');
  N.helpers = Object.keys(hp.results).length + ' helper checks';
  check('GL helpers: mesh / instances / target / depthTarget / program / frame self-test', hp.ok, hp.failed.map(k => k + ' → ' + hp.results[k]));

  // --- shadow pass and unit 7
  const u7 = await ev('DV.check.unit7()');
  check('shadow pass: unit 7 empty, colour mask off, polygon offset on; scene pass: unit 7 = shadow map with hardware compare',
    u7.shadowPass && u7.scenePass && !u7.shadowPass.bound && u7.shadowPass.fbo && u7.shadowPass.colorMask === false && u7.shadowPass.offset === true &&
    u7.scenePass.bound && u7.scenePass.compare && u7.scenePass.colorMask === true && u7.scenePass.offset === false && u7.glError === 0, u7);
  const canon = await ev('DV.check.canonical()');
  check('canonical GL state after a frame', canon.depthTest && canon.depthFunc && canon.depthMask && !canon.blend && !canon.cull && canon.cullBack && canon.ccw && !canon.scissor && canon.vao &&
    !canon.polygonOffset && canon.colorMask.every(Boolean) && canon.glError === 0, canon);

  // --- the scene target, HDR values, shadows
  const mk = await ev('({ white: DV.project(DV.marks.white.top), emissive: DV.project(DV.marks.emissive.centre), lamp: DV.project(DV.marks.lamp.top), probe: DV.check.shadowProbe(3.5),' +
    ' house: DV.project(DV.at(18, 0, 0.5)) })');
  const P = { white: mk.white, emissive: mk.emissive, lit: mk.probe.lit, shadow: mk.probe.shadow };
  check('probe points are on screen', [P.white, P.emissive, P.lit, P.shadow].every(p => p.ok && p.x > 5 && p.y > 5 && p.x < W - 5 && p.y < H - 5), P);
  let hdr = await hdrAt([P.white, P.emissive, P.lit, P.shadow]);
  N.sceneTarget = { float: hdr.float, redBits: hdr.redBits, depthBits: hdr.depthBits, samples: hdr.samples, size: [hdr.w, hdr.h] };
  N.sceneUnits = { whiteTop: hdr.values[0].map(r3), emissive: hdr.values[1].map(r3), sphereLit: hdr.values[2].map(r3), sphereInShadow: hdr.values[3].map(r3), sunElevationDeg: r1(mk.probe.sunElevation) };
  check('scene target: RGBA16F, 4 samples, 24-bit depth', hdr.float && hdr.redBits === 16 && hdr.samples === 4 && hdr.depthBits === 24 && hdr.glError === 0, N.sceneTarget);
  check('HDR: the emissive box is above the bloom threshold (3.0), the sun-lit white box below it', Math.max(...hdr.values[1]) > 4 && Math.max(...hdr.values[0]) < 2.6 && Math.max(...hdr.values[0]) > 0.8, N.sceneUnits);
  check('shadow: the pillar shadows the sphere (linear scene values)', luma(hdr.values[3]) < 0.6 * luma(hdr.values[2]), [hdr.values[3], hdr.values[2]]);
  const mid = await shot('mid');
  N.shadowOnScreen = { inShadow: mean(mid, P.shadow.x, P.shadow.y).map(Math.round), lit: mean(mid, P.lit.x, P.lit.y).map(Math.round) };
  check('shadow: visible in the picture, and bluer than the sun-lit ground (sky fill)', luma(N.shadowOnScreen.inShadow) < 0.8 * luma(N.shadowOnScreen.lit) &&
    N.shadowOnScreen.inShadow[2] - N.shadowOnScreen.inShadow[0] > N.shadowOnScreen.lit[2] - N.shadowOnScreen.lit[0] + 10, N.shadowOnScreen);
  const midStats = mid.stats();
  N.midStats = { lit: r3(midStats.lit), colors: midStats.colors, mean: midStats.mean.map(Math.round) };
  check('mid view: a full, colourful picture', midStats.lit > 0.95 && midStats.colors > 1500, N.midStats);
  const houseHere = mean(mid, mk.house.x, mk.house.y, 12), groundHere = mean(mid, P.lit.x, P.lit.y, 3);
  check('instanced houses are drawn (GL.instances inside the frame)', mk.house.ok && Math.abs(luma(houseHere) - luma(groundHere)) + Math.abs(houseHere[0] - houseHere[2] - groundHere[0] + groundHere[2]) > 6, [houseHere.map(Math.round), groundHere.map(Math.round)]);
  await ev('DM.Renderer.shadows = false; DV.settle(3)');
  const noShadow = await png();
  const u7off = await ev('({ size: DM.Renderer.shadowSize, u7: DV.check.unit7() })');
  await ev('DM.Renderer.shadows = true; DV.settle(3)');
  N.shadowsOff = { atShadowPoint: mean(noShadow, P.shadow.x, P.shadow.y).map(Math.round), atLitPoint: mean(noShadow, P.lit.x, P.lit.y).map(Math.round) };
  check('shadows off: the shadow is gone and unit 7 holds the 1x1 fully-lit depth texture', luma(N.shadowsOff.atShadowPoint) > 0.93 * luma(N.shadowsOff.atLitPoint) && u7off.size === 0 && !u7off.u7.shadowPass &&
    u7off.u7.scenePass.bound && u7off.u7.scenePass.compare, [N.shadowsOff, u7off]);

  // --- tone-mapping: predicted from the linear scene values (ACES fit, exposure 0.6, gamma 2.2)
  await ev('Object.assign(DM.Post.set, { bloom: 0, vignette: 0, grain: 0, saturation: 1 }); DV.settle(3)');
  const flat = await png();
  N.tonemap = [];
  let worstTone = 0;
  for (const [name, p, v] of [['white box', P.white, hdr.values[0]], ['sphere lit', P.lit, hdr.values[2]], ['sphere in shadow', P.shadow, hdr.values[3]]]) {
    const want = v.map(c => display(c, 0.6)), got = mean(flat, p.x, p.y);
    for (let i = 0; i < 3; i++) worstTone = Math.max(worstTone, Math.abs(want[i] - got[i]));
    N.tonemap.push({ at: name, scene: v.map(r3), predicted: want.map(Math.round), screen: got.map(Math.round) });
  }
  check('tonemap: screen = gamma(ACES(scene x exposure 0.6)) within 3/255', worstTone <= 3, N.tonemap);
  await ev('Object.assign(DM.Post.set, { bloom: 0.9, vignette: 0.35, grain: 0.02, saturation: 1.05 }); DV.settle(2)');

  // --- uFlash: a transient light, with decay
  const flashAt = mk.probe.litPos.map(v => v * 1.02);
  await ev('DM.Renderer.flash(' + JSON.stringify(flashAt) + ', 12, [30, 28, 24], 0.5)');
  const fl1 = await hdrAt([P.lit]);
  const flU = await ev('Array.from(DM.GL.frame.get("uFlash")).concat(Array.from(DM.GL.frame.get("uFlashCol")))');
  await settle(12);
  const fl2 = await hdrAt([P.lit]);
  await settle(45);
  const fl3 = await hdrAt([P.lit]);
  N.flash = { before: r3(luma(hdr.values[2])), atOnce: r3(luma(fl1.values[0])), aFewFramesLater: r3(luma(fl2.values[0])), afterItsLife: r3(luma(fl3.values[0])) };
  check('Renderer.flash: uFlash lights the ground, decays and ends', flU[3] === 12 && flU[4] > 20 && luma(fl1.values[0]) > luma(hdr.values[2]) + 3 && luma(fl2.values[0]) < luma(fl1.values[0]) &&
    Math.abs(luma(fl3.values[0]) - luma(hdr.values[2])) < 0.02, [N.flash, flU]);

  // --- uBrush / uBrushCol
  const BRUSH = 0.04;
  const brushDir = await ev('DV.at(-6.5, 8, 0).map(v => v / 100)');           // open ground between the boxes
  const ring = await ev('DV.check.ringProbe(' + JSON.stringify(brushDir) + ', ' + BRUSH + ', 24)');
  const yellow = img => ring.filter(p => { const c = mean(img, p.x, p.y, 1); return c[0] - c[2] > 30 && c[0] > 180; }).length;
  await ev('DM.Renderer.setBrush(' + JSON.stringify(brushDir) + ', ' + BRUSH + ', [1, 0.85, 0.3, 1]); DV.settle(3)');
  const ringSolid = yellow(await png());
  const brushU = await ev('Array.from(DM.GL.frame.get("uBrush")).concat(Array.from(DM.GL.frame.get("uBrushCol")))');
  await ev('DM.Renderer.setBrush(' + JSON.stringify(brushDir) + ', ' + BRUSH + ', [1, 0.85, 0.3, 0.35]); DV.settle(3)');
  const ringDashed = yellow(await png());
  await ev('DM.Renderer.setBrush(null, 0, null); DV.settle(3)');
  const ringOff = yellow(await png());
  N.brush = { ringPointsLit: { solid: ringSolid, dashed: ringDashed, hidden: ringOff, of: ring.length } };
  check('Renderer.setBrush: uBrush / uBrushCol reach the shaders (solid ring, dashed when alpha < 0.5, hidden)', ring.every(p => p.ok) && ringSolid >= 22 && ringDashed >= 9 && ringDashed <= 15 && ringOff === 0 &&
    Math.abs(brushU[3] - BRUSH) < 1e-6 && Math.abs(brushU[5] - 0.85) < 1e-6, [N.brush, brushU]);

  // --- bloom: under the noon sun (white as bright as diffuse gets) only the emissive boxes glow
  await ev('DM.Renderer.sun.mode = "orbit"; DM.Renderer.sun.angle = ' + (SUN_LON * DEG) + '; DV.settle(3)');
  const nk = await ev('({ white: DV.project(DV.marks.white.top), emissive: DV.project(DV.marks.emissive.centre) })');
  const hdrNoon = await hdrAt([nk.white, nk.emissive]);
  const noon = await shot('noon');
  await ev('DM.Post.set.bloom = 0; DV.settle(3)');
  const noBloom = await png();
  await ev('DM.Post.set.bloom = 0.9; DM.Renderer.sun.mode = "follow"; DV.freeze(5); DV.settle(3)');
  N.bloom = {
    whiteTopSceneUnits: hdrNoon.values[0].map(r3), emissiveSceneUnits: hdrNoon.values[1].map(r3),
    diffAroundEmissive: r3(mad(noon, noBloom, nk.emissive.x - 90, nk.emissive.y - 90, nk.emissive.x + 90, nk.emissive.y + 90)),
    diffAroundWhite: r3(mad(noon, noBloom, nk.white.x - 70, nk.white.y - 70, nk.white.x + 70, nk.white.y + 70)),
    diffFarCorner: r3(mad(noon, noBloom, 0, H - 120, 160, H)),
  };
  check('noon: a sun-lit white top is about 0.85 x 2.5 scene units, under the bloom knee', Math.max(...hdrNoon.values[0]) > 1.9 && Math.max(...hdrNoon.values[0]) < 2.45, N.bloom.whiteTopSceneUnits);
  check('bloom: the emissive box glows', N.bloom.diffAroundEmissive > 2, N.bloom);
  check('bloom: the sun-lit white box does NOT bloom (difference with bloom off under 1/255)', N.bloom.diffAroundWhite < 1 && N.bloom.diffFarCorner < 1, N.bloom);

  // --- fade and the full-screen flash tint
  await ev('DM.Post.set.fade = 1; DV.settle(3)');
  const black = (await png()).stats();
  await ev('DM.Post.set.fade = 0; DM.Post.set.flash = [1, 0.2, 0.1, 1]; DV.settle(3)');
  const tint = (await png()).stats();
  await ev('DM.Post.set.flash = [1, 1, 1, 0]; DV.settle(2)');
  N.fade = { fadeMean: black.mean.map(Math.round), flashMean: tint.mean.map(Math.round) };
  check('Post.set.fade = 1 is black; Post.set.flash tints the whole screen', Math.max(...black.mean) < 3 && tint.mean[0] > 245 && Math.abs(tint.mean[1] - 51) < 8 && Math.abs(tint.mean[2] - 25.5) < 8, N.fade);

  // --- tilt-shift (close view)
  await ev('DV.cam(14, 24, 0.85); DV.settle(3)');
  st = await ev('DV.stats()');
  const close = await shot('close');
  await ev('DM.Post.set.tilt = 0; DV.settle(3)');
  const sharp = await png();
  await ev('DM.Post.set.tilt = 1; DM.Post.set.focusY = 0.15; DV.settle(3)');
  const moved = await png();
  await ev('DM.Post.set.focusY = 0.5; DV.cam(14, 24, 0.3); DV.settle(3)');
  const stFar = await ev('DV.stats()');
  N.tilt = {
    strengthAtZoom085: r3(st.tilt), strengthAtZoom03: stFar.tilt,
    blurTopRows: r3(mad(close, sharp, 0, 0, W, 70)), blurBottomRows: r3(mad(close, sharp, 0, H - 70, W, H)), blurFocusBand: r3(mad(close, sharp, 0, H / 2 - 45, W, H / 2 + 45)),
    focusY015: { bandAt15pct: r3(mad(moved, sharp, 0, 0.15 * H - 40, W, 0.15 * H + 40)), bottomRows: r3(mad(moved, sharp, 0, H - 70, W, H)) },
  };
  check('tilt-shift: on when zoomed in (blurs top and bottom, keeps the focus band), zero when zoomed out', st.tilt > 0.5 && stFar.tilt === 0 && N.tilt.blurTopRows > 0.5 && N.tilt.blurBottomRows > 0.5 && N.tilt.blurFocusBand < 0.1, N.tilt);
  check('tilt-shift: focusY 0 = top of the screen (the sharp band follows it)', N.tilt.focusY015.bandAt15pct < 0.1 && N.tilt.focusY015.bottomRows > N.tilt.blurBottomRows, N.tilt.focusY015);

  // --- project → pick at three zooms
  N.roundTrip = {};
  for (const z of [0, 0.5, 0.9]) {
    await ev('DV.cam(14, 24, ' + z + ')');
    const rt = await ev('DV.check.roundTrip(200)');
    N.roundTrip['zoom ' + z] = { points: rt.tried, hits: rt.hits, worstUnits: rt.worst };
    check('project then pick round-trips within 0.5 units at zoom ' + z, rt.tried >= 150 && rt.hits === rt.tried && rt.worst < 0.5, rt);
  }
  const hid = await ev('({ far: DV.project(DV.pos(-14, 204)), behind: DV.project(DV.pos(14, 24, 2000)), near: DV.project(DV.pos(14, 24)) })');
  check('project: false for a point hidden by the planet and for one behind the camera', hid.far.ok === false && hid.behind.ok === false && hid.near.ok === true, hid);
  await ev('DV.cam(14, 24, 0.5); DV.settle(2)');

  // --- showSnapshot
  const sn = await ev('DV.check.snapshot()');
  N.snapshot = { during: sn.during, after: sn.after };
  check('showSnapshot: Planet and Objects told once, sea level / year / heat / night lights from the snapshot, FX skipped; null restores',
    sn.during.log.join() === 'Planet:snap,Objects:snap' && sn.during.fx.update === 0 && sn.during.fx.draw === 0 && Math.abs(sn.during.seaShown - sn.during.seaWanted) < 1e-3 &&
    sn.during.year === 1234 && sn.during.heat === 0.5 && Math.abs(sn.during.night - 0.85) < 0.01 && sn.during.isSnapshot &&
    sn.after.log.join() === 'Planet:null,Objects:null' && sn.after.fx.update === 90 && sn.after.fx.draw === 180 && Math.abs(sn.after.seaShown - sn.after.seaWanted) < 1e-6 &&
    sn.after.year === 0 && sn.after.heat === 0 && Math.abs(sn.after.night - sn.nightBefore) < 1e-6 && !sn.after.isSnapshot, sn);

  // --- module isolation (the renderer reports the failure with one console.error: expected, so it is filed as a log)
  browser.ignore.push(/DM\.Renderer: module FX disabled after an error in FX\.draw/);
  const iso = await ev('DV.check.isolation()');
  await settle(2);
  const reported = page.logs.filter(l => /\[ignored error\] DM\.Renderer: module FX disabled/.test(l));
  N.isolation = { disabled: iso.disabled, callsInTheFirstFrame: iso.first, callsAfterThreeFrames: iso.afterThreeFrames, centrePixel: iso.centre, draws: iso.draws, consoleErrors: reported.length };
  check('isolation: a throwing module is disabled after one call, reported once, and the frame still renders in canonical state',
    /deliberate failure/.test(iso.disabled || '') && iso.first.draw0 === 1 && iso.afterThreeFrames.draw0 === 1 && iso.afterThreeFrames.update === 1 && iso.afterThreeFrames.draw1 === 0 &&
    luma(iso.centre) > 40 && iso.draws > 5 && !iso.blendLeftOn && iso.depthMask && iso.glError === 0 && reported.length === 1 && !/\[x\d+\]/.test(reported[0]), [iso, reported.length]);

  // --- capture
  const cap = await ev('(async () => { const a = DM.Renderer.capture(), b = DM.Renderer.capture("image/png"); const im = new Image(); im.src = a; await im.decode();' +
    ' const c = document.createElement("canvas"); c.width = 64; c.height = 36; const g = c.getContext("2d"); g.drawImage(im, 0, 0, 64, 36); const d = g.getImageData(0, 0, 64, 36).data; let s = 0;' +
    ' for (let i = 0; i < d.length; i += 4) s += d[i] + d[i + 1] + d[i + 2]; return { head: a.slice(0, 23), jpegBytes: Math.floor((a.length - 23) * 0.75), pngHead: b.slice(0, 22), pngBytes: Math.floor((b.length - 22) * 0.75),' +
    ' size: [im.naturalWidth, im.naturalHeight], meanLevel: s / (d.length * 0.75) }; })()');
  N.capture = cap;
  check('capture: a JPEG data URL of the frame over 10 kB (and PNG on request)', cap.head === 'data:image/jpeg;base64,' && cap.jpegBytes > 10240 && cap.pngHead === 'data:image/png;base64,' && cap.pngBytes > 10240 &&
    cap.size[0] === W && cap.size[1] === H && cap.meanLevel > 60, cap);

  // --- quality switching and buffer sizing on a live page
  N.quality = {};
  const wantQ = [{ samples: 0, fxaa: true, bloom: 3, shadow: 1024 }, { samples: 4, fxaa: false, bloom: 5, shadow: 2048 }, { samples: 4, fxaa: false, bloom: 5, shadow: 4096 }];
  await ev('window.__obj = 0; DM.Objects = { sync(w, dt, dirty) { if (dirty.objects) window.__obj++; } }; DV.settle(2)');
  for (const q of [0, 1, 2]) {
    await ev('window.__obj = 0; DM.Renderer.setQuality(' + q + '); DV.settle(4)');
    const s = await ev('Object.assign(DV.stats(), { objectsDirty: window.__obj, uq: DM.GL.frame.get("uScreen")[3] })');
    N.quality['live q' + q] = { samples: s.samples, fxaa: s.fxaa, bloom: s.bloomLevels, shadow: s.shadowSize, tilt: s.tilt > 0, objectsDirtyFrames: s.objectsDirty };
    const w = wantQ[q];
    check('setQuality(' + q + '): targets, shadow map, uScreen.w and dirty.objects follow', s.quality === q && s.samples === w.samples && s.fxaa === w.fxaa && s.bloomLevels === w.bloom && s.shadowSize === w.shadow &&
      s.uq === q && s.objectsDirty === 1 && Object.keys(s.disabled).length === 0, N.quality['live q' + q]);
  }
  await ev('delete DM.Objects; delete window.__obj');
  N.sizing = {};
  await page.setViewport({ dpr: 2 });
  await ev('document.getElementById("view").style.width = "1920px"; document.getElementById("view").style.height = "1080px"; DV.settle(3)');
  for (const q of [2, 1, 0]) {
    await ev('DM.Renderer.setQuality(' + q + '); DV.settle(3)');
    const b = await ev('DV.stats().buffer');
    N.sizing['1920x1080 css, dpr 2, q' + q] = b[0] + 'x' + b[1] + ' = ' + r3(b[0] * b[1] / 1e6) + ' MP';
    const mpCap = [1.6, 3, 5][q], ratio = [1, 1.5, 2][q], mp = b[0] * b[1] / 1e6, full = 1920 * ratio * 1080 * ratio / 1e6;
    check('resize: pixel-ratio cap and megapixel budget at quality ' + q, mp <= mpCap * 1.01 && mp >= Math.min(mpCap, full) * 0.98 && Math.abs(b[0] / b[1] - 16 / 9) < 0.01, N.sizing);
  }
  await page.setViewport({ dpr: 1 });
  await ev('document.getElementById("view").style.width = "' + W + 'px"; document.getElementById("view").style.height = "' + H + 'px"; DM.Renderer.setQuality(2); DV.settle(3)');
  const back = await ev('DV.stats().buffer');
  check('resize: back to ' + W + 'x' + H, back[0] === W && back[1] === H, back);

  // --- context loss and restore (last on this page: everything is rebuilt)
  browser.ignore.push(/CONTEXT_LOST_WEBGL/);
  const cl = await ev('DV.check.contextLoss()');
  await settle(3);
  N.contextLoss = cl;
  const afterLoss = await png();
  check('context loss: drawing stops, then GL.init + every init() + setWorld bring the picture back', cl.supported && cl.whileLost.isContextLost && cl.after && !cl.after.isContextLost && cl.after.draws > 5 &&
    luma(cl.after.centre) > 40 && Object.keys(cl.after.disabled).length === 0 && cl.after.glError === 0 && mad(afterLoss, mid, 0, 0, W, H) < 1.5, [cl, r3(mad(afterLoss, mid, 0, 0, W, H))]);
  const mainErrors = (await ev('DV.stats()'));
  check('main page: no module disabled, no GL-state complaint, no viewer error', Object.keys(mainErrors.disabled).length === 0 && mainErrors.stateErrors.length === 0 && mainErrors.errors.length === 0,
    { disabled: mainErrors.disabled, stateErrors: mainErrors.stateErrors, errors: mainErrors.errors });

  // ================================================================================================ 2. the views, by URL
  // --- orbit (sun mode 'follow': the view is always lit)
  await open('&zoom=0' + SITE);
  const orbit = await shot('orbit');
  await frameMs('orbit q2');
  const os = orbit.stats();
  // the lit limb is up and to the left: sample the sky 4.5 units above the ground there, and the same on the far side
  const haloOf = async img => {
    const pts = await ev('(() => { const v = DM.GL.view, o = []; for (const s of [1, -1]) for (let k = -2; k <= 2; k++) { const a = k * 0.07, p = [0, 0, 0];' +
      ' const dx = (-0.72 * Math.cos(a) - 0.69 * Math.sin(a)) * s, dy = (0.69 * Math.cos(a) - 0.72 * Math.sin(a)) * s;' +
      ' for (let i = 0; i < 3; i++) p[i] = (v.right[i] * dx + v.up[i] * dy) * 104.5; o.push(DV.project(p)); } return o; })()');
    const lit = pts.slice(0, 5).map(p => mean(img, p.x, p.y, 1)).sort((a, b) => luma(a) - luma(b))[2];
    const dark = pts.slice(5).map(p => mean(img, p.x, p.y, 1)).sort((a, b) => luma(a) - luma(b))[2];
    return { lit, dark };
  };
  const halo = await haloOf(orbit);
  N.orbit = { lit: r3(os.lit), colors: os.colors, centre: mean(orbit, W / 2, H / 2 + 40, 3).map(Math.round), haloSunSide: halo.lit.map(Math.round), haloFarSide: halo.dark.map(Math.round), space: mean(orbit, 30, 30, 5).map(Math.round) };
  check('orbit: the planet fills the middle of a black sky', os.lit > 0.15 && os.lit < 0.5 && os.colors > 1000 && luma(N.orbit.space) < 6 && luma(N.orbit.centre) > 90, N.orbit);
  check('atmosphere: a blue halo outside the sun-lit limb, dim on the far side', halo.lit[2] > halo.lit[0] + 20 && luma(halo.lit) > 30 && luma(halo.dark) < 0.6 * luma(halo.lit), N.orbit);

  // --- night side: readable, cool, with the test scene's little lights; then the same ground at midday
  await open('&zoom=0&lat=-8&lon=204&sun=orbit&sunAngle=' + SUN_LON);
  const night = await shot('night');
  await frameMs('night q2');
  const np = await ev('({ a: DV.project(DV.pos(-15, 212)), b: DV.project(DV.pos(-26, 196)), c: DV.project(DV.pos(6, 236)), lights: DV.project(DV.pos(-8, 204)) })');
  const nightPx = [np.a, np.b, np.c].map(p => mean(night, p.x, p.y, 3));
  const nightHdr = await hdrAt([np.a]);
  await ev('DM.Renderer.sun.angle = ' + ((SUN_LON + 180) * DEG) + '; DV.settle(3)');
  const dayImg = await png();
  const dayPx2 = [np.a, np.b, np.c].map(p => mean(dayImg, p.x, p.y, 3));
  const dayHdr = await hdrAt([np.a]);
  const ratios = nightPx.map((p, i) => luma(p) / luma(dayPx2[i]));
  N.night = {
    moonlitGround: nightPx.map(p => p.map(Math.round)), sameGroundAtMidday: dayPx2.map(p => p.map(Math.round)), screenLumaRatio: ratios.map(r3),
    sceneUnits: { night: nightHdr.values[0].map(r3), midday: dayHdr.values[0].map(r3), linearRatio: r3(luma(nightHdr.values[0]) / luma(dayHdr.values[0])) },
    brightestLight: Math.round(maxLuma(night, np.lights.x, np.lights.y, 40)),
  };
  check('night side: moonlit ground keeps at least 30 % of its daylight luminance after tone-mapping', Math.min(...ratios) >= 0.30 && Math.max(...ratios) < 0.6, N.night);
  check('night side: cool (blue over red), emissive lights visible', nightPx.every(p => p[2] > p[0] + 12) && N.night.brightestLight > 150, N.night);

  // --- terminator: soft, with a warm band
  await open('&zoom=0&lat=10&lon=110&sun=orbit&sunAngle=' + SUN_LON);
  const term = await shot('terminator');
  await frameMs('terminator q2');
  const lons = [];
  for (let d = 40; d <= 150; d += 2) lons.push(d);
  const tp = await ev('[' + lons.join(',') + '].map(d => DV.project(DV.pos(16, ' + SUN_LON + ' + d)))');
  const prof = tp.map((p, i) => ({ d: lons[i], ok: p.ok && p.x > 2 && p.x < W - 2, c: mean(term, p.x, p.y, 2) })).filter(s => s.ok);
  const at = d => prof.reduce((best, s) => Math.abs(s.d - d) < Math.abs(best.d - d) ? s : best, prof[0]);
  // pillars and their long shadows cross this line: judge the envelope with a running median of 5
  const med = prof.map((s, i) => { const w = prof.slice(Math.max(0, i - 2), i + 3).map(q => luma(q.c)).sort((a, b) => a - b); return w[w.length >> 1]; });
  let step = 0;
  for (let i = 1; i < med.length; i++) step = Math.max(step, Math.abs(med[i] - med[i - 1]));
  const warm = prof.filter(s => s.d >= 84 && s.d <= 104).map(s => s.c[0] - s.c[2]);
  N.terminator = {
    sunAngleToColour: [40, 60, 76, 84, 88, 92, 96, 100, 106, 114, 130, 150].map(d => ({ deg: at(d).d, rgb: at(d).c.map(Math.round) })),
    largestStepPer2Deg: r1(step), dayLuma: r1(luma(at(40).c)), nightLuma: r1(luma(at(150).c)), warmestRedMinusBlue: r1(Math.max(...warm)),
  };
  check('terminator: day → warm band → cool night, without a hard edge', luma(at(40).c) > luma(at(88).c) && luma(at(88).c) > luma(at(106).c) * 0.95 && Math.max(...warm) > 14 &&
    at(40).c[2] >= at(40).c[0] - 4 && at(150).c[2] > at(150).c[0] + 12 && step < 0.14 * luma(at(40).c), N.terminator);

  // ================================================================================================ 3. quality levels and the fallback
  N.variants = {};
  const variants = [
    ['q2', '&q=2', { hdr: true, samples: 4, fxaa: false, bloom: 5, shadow: 4096 }],
    ['q1', '&q=1', { hdr: true, samples: 4, fxaa: false, bloom: 5, shadow: 2048 }],
    ['q0', '&q=0', { hdr: true, samples: 0, fxaa: true, bloom: 3, shadow: 1024 }],
    ['nohdr', '&nohdr=1', { hdr: false, samples: 4, fxaa: false, bloom: 0, shadow: 4096 }],
    ['nohdr-q0', '&nohdr=1&q=0', { hdr: false, samples: 0, fxaa: true, bloom: 0, shadow: 1024 }],
  ];
  let q2img = null;
  for (const [name, query, want] of variants) {
    await open('&zoom=0.5' + SITE + query);
    const s = await ev('Object.assign(DV.stats(), { lightScale: DM.GL.frame.get("uSunDir")[3] })');
    const img = await shot(name);
    await frameMs('mid ' + name);
    if (name === 'q2') {
      q2img = img;
      await ev('DV.cam(14, 24, 0.85); DV.settle(3)');
      await frameMs('close q2');
      await ev('DV.cam(14, 24, 0.5); DV.settle(2)');
    }
    const is = img.stats(), diff = mad(img, q2img, 0, 0, W, H);
    N.variants[name] = { hdr: s.hdr, samples: s.samples, fxaa: s.fxaa, bloomLevels: s.bloomLevels, shadowMap: s.shadowSize, tilt: s.tilt, lightScale: r3(s.lightScale), meanAbsDiffFromQ2: r3(diff), mean: is.mean.map(Math.round) };
    check('URL flag ' + (query.slice(1) || 'q=2') + ': targets as the quality table says', s.hdr === want.hdr && s.samples === want.samples && s.fxaa === want.fxaa && s.bloomLevels === want.bloom &&
      s.shadowSize === want.shadow && Math.abs(s.lightScale - (want.hdr ? 1 : 0.4)) < 1e-6 && Object.keys(s.disabled).length === 0 && s.errors.length === 0, N.variants[name]);
    check('URL flag ' + (query.slice(1) || 'q=2') + ': the picture matches quality 2 (mean abs difference under 4/255)', is.lit > 0.95 && diff < 4, [r3(is.lit), r3(diff)]);
  }
  check('mid view at quality 2 by URL equals the main page (frozen time is reproducible across runs)', mad(q2img, mid, 0, 0, W, H) < 1, r3(mad(q2img, mid, 0, 0, W, H)));

  // ================================================================================================ 4. freeze: two runs, one picture
  const planetUrl = '&stand=terrain&clouds=0.55&zoom=0&lat=10&lon=70&sun=orbit';
  await page.goto(BASE.replace('t=5', 't=42') + planetUrl);
  await page.waitFor('window.DV && DV.ready', 45000);
  await settle(3);
  const planetA = await shot('planet');
  await frameMs('planet stand-in q2');
  await page.goto(BASE.replace('t=5', 't=42') + planetUrl);
  await page.waitFor('window.DV && DV.ready', 45000);
  await settle(5);
  const planetB = await png();
  await ev('DV.freeze(90); DV.settle(3)');
  const planetC = await png();
  N.freeze = { meanAbsDiffTwoRuns: r3(mad(planetA, planetB, 0, 0, W, H)), meanAbsDiffOtherTime: r3(mad(planetA, planetC, 0, 0, W, H)), planet: { lit: r3(planetA.stats().lit), colors: planetA.stats().colors } };
  check('freeze: two frozen runs give the same picture (mean absolute difference under 1/255)', N.freeze.meanAbsDiffTwoRuns < 1, N.freeze);
  check('freeze: another time gives another picture (the sun and the clouds moved)', N.freeze.meanAbsDiffOtherTime > 3, N.freeze);
  check('terrain stand-in with clouds renders', N.freeze.planet.lit > 0.15 && N.freeze.planet.colors > 2000, N.freeze.planet);

  // ================================================================================================ wrap up
  N.pageErrors = page.errors.slice();
  check('zero entries in page.errors', page.errors.length === 0, page.errors);
  out.seconds = Math.round((Date.now() - t0) / 100) / 10;
  out.ok = failed.length === 0;
  if (failed.length) {
    const e = new Error('rendercore: ' + failed.length + ' of ' + Object.keys(out.checks).length + ' checks failed\n  - ' + failed.join('\n  - '));
    e.result = out;
    throw e;
  }
  out.summary = Object.keys(out.checks).length + ' checks passed, 0 failed, ' + out.shots.length + ' screenshots, ' + out.seconds + ' s';
  return out;
};
