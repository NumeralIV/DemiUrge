// DEMIURGE — tools/smoke/example-scenario.js : the scenario example from docs/TOOLS.md, runnable as is.
//   node tools/shot.js --run tools/smoke/example-scenario.js --size 1280x720
// A scenario exports one async function. The rig launches the browser, hands over a blank page, prints the
// return value as JSON, closes the browser, and exits with code 1 if the scenario threw or the page logged errors.
'use strict';
module.exports = async (page, browser, args) => {
  await page.goto('tools/smoke/rig.html');
  await page.waitFor('window.READY');
  await page.mouse('click', 450, 350);                       // real input events, in CSS pixels
  await page.drag(200, 300, 600, 500, { steps: 8 });
  await page.key('press', 'KeyW');
  await page.mouse('wheel', 640, 360, { deltaY: -240 });
  await page.wait(50);
  const events = await page.eval('EV.map(e => e.t)');        // what the page saw
  const shot = await page.shot('example-scenario.png');
  return { clicks: events.filter(t => t === 'click').length, wheels: events.filter(t => t === 'wheel').length,
    keys: events.filter(t => t === 'keydown').length, shot, args };
};
