// DEMIURGE — tools/smoke/example.js : the module example from docs/TOOLS.md, runnable as is.
//   node tools/smoke/example.js
// Loads a page, waits for a condition, evaluates something, takes a screenshot, checks it, cleans up.
'use strict';
const { launch, decodePng } = require('../shot.js');   // from tests/ or dev/ this is '../tools/shot.js'

(async () => {
  const browser = await launch({ width: 1280, height: 720 });
  try {
    const page = await browser.page();
    await page.goto('tools/smoke/webgl.html');                    // path relative to the project root (or absolute, or a URL)
    await page.waitFor('window.SMOKE && SMOKE.done', 15000);      // poll a JS expression until it is truthy
    const info = await page.eval('({ renderer: SMOKE.report.renderer, fps: SMOKE.report.raf.fps })');
    await page.eval('SMOKE.freeze(0.5)');                         // put the page into a known state …
    await page.wait(100);                                         // … and give it a few frames to draw it
    const file = await page.shot('example.png');                  // → <project>/shots/example.png
    const centre = decodePng(file).px(640, 360);                  // [r, g, b, a] of one pixel of the PNG
    console.log(JSON.stringify({ info, file, centre, errors: page.errors }, null, 2));
    if (page.errors.length) process.exitCode = 1;
  } finally {
    await browser.close();                                        // always: kills the browser, deletes its profile
  }
})().catch(e => { console.error(e); process.exitCode = 1; });
