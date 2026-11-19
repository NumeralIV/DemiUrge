// DEMIURGE — tools/smoke/smoke.js : WebGL2 smoke test of the headless rig.
//   node tools/smoke/smoke.js [--headed] [--size WxH]
// Loads tools/smoke/webgl.html in headless Edge, prints the capability report as JSON on stdout and saves a
// screenshot of the lit rotating triangle to shots/smoke.png. Exit code 0 if every check passed, 1 otherwise.
// What the report contains is described in docs/TOOLS.md.
'use strict';
const path = require('path');
const { launch, decodePng } = require('../shot.js');

const argv = process.argv.slice(2);
const size = /^(\d+)x(\d+)$/.exec(argv[argv.indexOf('--size') + 1] || '') || [0, 1280, 720];
const near = (a, b, eps) => a.every((v, i) => Math.abs(v - b[i]) <= eps);

(async () => {
  const t0 = Date.now();
  const browser = await launch({ width: +size[1], height: +size[2], dpr: 1, headless: !argv.includes('--headed'), timeout: 90000 });
  let report = { ok: false };
  try {
    const page = await browser.page();
    await page.goto(path.join(__dirname, 'webgl.html'));
    await page.waitFor('window.SMOKE && SMOKE.done', 60000);
    report = await page.eval('SMOKE.report');
    report.browser = browser.version;
    report.viewport = await page.eval('({ width: innerWidth, height: innerHeight, dpr: devicePixelRatio, canvas: [document.getElementById("c").width, document.getElementById("c").height] })');
    if (report.webgl2) {
      const W = report.viewport.width, H = report.viewport.height;
      const settle = async () => { const f = await page.eval('SMOKE.frames'); await page.waitFor('SMOKE.frames >= ' + (f + 3), 5000); };
      const shot = {};

      // 1. a known colour must come back from the PNG exactly: proves screenshots contain the WebGL canvas
      await page.eval('SMOKE.solid([255, 128, 0])');
      await settle();
      const solid = decodePng(await page.png());
      shot.size = [solid.width, solid.height];
      shot.knownColour = { drawn: [255, 128, 0], centre: solid.px(W >> 1, H >> 1).slice(0, 3), corner: solid.px(5, 5).slice(0, 3), mean: solid.stats().mean };
      shot.knownColourOk = near(shot.knownColour.centre, [255, 128, 0], 2) && near(shot.knownColour.corner, [255, 128, 0], 2) && near(shot.knownColour.mean, [255, 128, 0], 2);

      // 2. the lit triangle at a pinned angle → shots/smoke.png
      await page.eval('SMOKE.solid(null); SMOKE.freeze(0.5)');
      await settle();
      shot.file = await page.shot('smoke.png');
      const a = decodePng(shot.file);
      const sa = a.stats();
      shot.triangle = { centre: a.px(W >> 1, H >> 1).slice(0, 3), background: a.px(W - 6, H - 6).slice(0, 3), litFraction: sa.lit, colours: sa.colors };
      const c = shot.triangle.centre;
      shot.triangleOk = Math.max(c[0], c[1], c[2]) > 90 && near(shot.triangle.background, [5, 8, 14], 6) && sa.lit > 0.04 && sa.lit < 0.6 && sa.colors > 300;

      // 3. a different angle must give a different picture (narrower triangle), and free spinning must animate
      await page.eval('SMOKE.freeze(1.2)');
      await settle();
      const b = decodePng(await page.png());
      shot.litFractionRotated = b.stats().lit;
      await page.eval('SMOKE.freeze(null)');
      const f0 = await page.eval('SMOKE.frames');
      const spinA = await page.png();
      await page.wait(250);
      const spinB = await page.png();
      shot.framesWhileSpinning = (await page.eval('SMOKE.frames')) - f0;
      shot.animates = !spinA.equals(spinB) && shot.framesWhileSpinning > 5;
      shot.rotates = shot.litFractionRotated < sa.lit * 0.75;
      shot.ok = shot.knownColourOk && shot.triangleOk && shot.rotates && shot.animates;
      report.screenshot = shot;

      // 4. rAF as seen from the rig (the page also measured it itself: report.raf)
      report.rafFromRig = await page.fps(1000);
    }
    await page.wait(100);
    report.pageErrors = page.errors.slice();
    report.pageLogs = page.logs.slice();
    report.ok = !!(report.ok && report.screenshot && report.screenshot.ok && report.rafFromRig.fps > 20 && !report.pageErrors.length);
  } catch (e) {
    report.ok = false;
    report.failure = String(e && e.stack || e);
  } finally {
    await browser.close();
  }
  report.elapsedMs = Date.now() - t0;
  process.stdout.write(JSON.stringify(report, null, 2) + '\n', () => process.exit(report.ok ? 0 : 1));
})();
