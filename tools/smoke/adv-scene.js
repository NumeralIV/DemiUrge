// DEMIURGE — tools/smoke/adv-scene.js : drives tools/smoke/adv-scene.html through the rig (docs/TOOLS.md API only).
//   node tools/smoke/adv-scene.js [--size WxH] [--tag name]
// Loads the scene, waits for its ready flag, reads frame timings back from the page, takes screenshots of three
// views (planet, close-up, close-up with shadows off) and checks them numerically. Prints one JSON object;
// exit code 0 only if every check passed. Screenshots: shots/adv-scene-<tag>planet.png, -close.png, -close-noshadow.png
'use strict';
const { launch, decodePng } = require('../shot.js');

const argv = process.argv.slice(2);
const opt = name => { const i = argv.indexOf('--' + name); return i >= 0 ? argv[i + 1] : undefined; };
const size = /^(\d+)x(\d+)$/.exec(opt('size') || '1280x720');
const tag = opt('tag') ? opt('tag') + '-' : '';

(async () => {
  const t0 = Date.now();
  const out = { ok: false, checks: {} };
  const browser = await launch({ width: +size[1], height: +size[2] });
  try {
    const page = await browser.page();
    const tLoad = Date.now();
    await page.goto('tools/smoke/adv-scene.html?hud=0');
    await page.waitFor('window.ADV_READY', 20000);
    out.readyMs = Date.now() - tLoad;
    out.info = await page.eval('ADV.info');
    out.renderer = out.info.renderer;

    // frame-time measurement read back from the page: 2 s of free-running rAF, then a forced-completion benchmark
    await page.eval('ADV.preset("planet")');
    await page.wait(2500);
    out.raf = await page.eval('ADV.stats()');
    out.bench = await page.eval('ADV.bench(120)');
    out.rafFromRig = await page.fps(1000);

    // screenshots
    const shoot = async (name, js) => {
      await page.eval(js);
      await page.wait(150);
      const file = await page.shot('adv-scene-' + tag + name + '.png');
      const img = decodePng(file);
      let bright = 0;
      for (let o = 0; o < img.data.length; o += 4) if (Math.max(img.data[o], img.data[o + 1], img.data[o + 2]) > 64) bright++;
      return { file, img, stats: Object.assign(img.stats(), { bright: +(bright / (img.data.length / 4)).toFixed(4) }) };
    };
    await page.eval('ADV.freeze(true)');
    const planet = await shoot('planet', 'ADV.setShadows(true); ADV.preset("planet")');
    const close = await shoot('close', 'ADV.preset("close")');
    const closeNo = await shoot('close-noshadow', 'ADV.setShadows(false)');
    const night = await shoot('night', 'ADV.setShadows(true); ADV.preset("night")');
    await page.eval('ADV.freeze(false)');

    // how many pixels of the close-up change when the shadow term is switched off, and in which direction
    let changed = 0, darker = 0, sumOn = 0, sumOff = 0;
    const a = close.img.data, b = closeNo.img.data;
    for (let o = 0; o < a.length; o += 4) {
      const la = a[o] + a[o + 1] + a[o + 2], lb = b[o] + b[o + 1] + b[o + 2];
      sumOn += la; sumOff += lb;
      if (Math.abs(la - lb) > 24) { changed++; if (la < lb) darker++; }
    }
    const n = a.length / 4;
    out.shots = {
      planet: { file: planet.file, stats: planet.stats },
      close: { file: close.file, stats: close.stats },
      closeNoShadow: { file: closeNo.file, stats: closeNo.stats },
      night: { file: night.file, stats: night.stats },
    };
    out.shadowDiff = { pixels: n, changedFraction: +(changed / n).toFixed(4), darkerWithShadows: +(darker / Math.max(1, changed)).toFixed(4),
      meanLumaOn: +(sumOn / n / 3).toFixed(2), meanLumaOff: +(sumOff / n / 3).toFixed(2) };

    const c = out.checks;
    c.ready = out.info.glError === null && out.info.msaaFramebuffer === 'COMPLETE' && out.info.resolveFramebuffer === 'COMPLETE' && out.info.shadowFramebuffer === 'COMPLETE';
    c.msaa4x = out.info.samples === 4;
    c.cones = out.info.cones === 5000;
    c.blockShared = Object.values(out.info.uniformBlock).length === 5 &&
      Object.values(out.info.uniformBlock).every(b2 => b2.size === 176 && b2.offsets.join() === '0,64,128,144,160');
    c.screenshotSize = planet.img.width === +size[1] && planet.img.height === +size[2];
    c.planetNotBlank = planet.stats.bright > 0.08 && planet.stats.bright < 0.7 && planet.stats.colors > 1000;
    c.closeNotBlank = close.stats.bright > 0.3 && close.stats.colors > 1000;
    c.shadowsVisible = out.shadowDiff.changedFraction > 0.01 && out.shadowDiff.darkerWithShadows > 0.95;
    c.nightDarkerThanDay = night.stats.mean[1] < planet.stats.mean[1] * 0.5;
    c.benchNoGlError = out.bench.glError === null;
    c.noPageErrors = page.errors.length === 0;
    out.pageErrors = page.errors;
    out.pageLogs = page.logs;
    out.ok = Object.values(c).every(Boolean);
  } catch (e) {
    out.error = String(e && e.stack || e);
  } finally {
    await browser.close();
  }
  out.elapsedMs = Date.now() - t0;
  console.log(JSON.stringify(out, null, 2));
  process.exitCode = out.ok ? 0 : 1;
})().catch(e => { console.error(e); process.exitCode = 1; });
