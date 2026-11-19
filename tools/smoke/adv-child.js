// DEMIURGE — tools/smoke/adv-child.js : misbehaving rig users, run as child processes by adv-robust.js.
//   node tools/smoke/adv-child.js <mode>      modes: uncaught-wait | idle | exit | throw
// Each mode launches a browser, prints {"pid": <browser pid>} on stdout, and then leaves WITHOUT calling browser.close().
'use strict';
const { launch } = require('../shot.js');
const mode = process.argv[2];

(async () => {
  const browser = await launch({ timeout: 60000 });
  const page = await browser.page();
  await page.goto('tools/smoke/adv-never.html');
  process.stdout.write(JSON.stringify({ pid: browser.pid }) + '\n');
  if (mode === 'uncaught-wait') await page.waitFor('window.NEVER', 1000);       // rejects; nobody catches it
  else if (mode === 'idle') await new Promise(() => { setInterval(() => {}, 1000); });   // parent kills us hard
  else if (mode === 'exit') process.exit(0);
  else if (mode === 'throw') setTimeout(() => { throw new Error('adv-child: script bug'); }, 50);
  else throw new Error('unknown mode ' + mode);
})();
