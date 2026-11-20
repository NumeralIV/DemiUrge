// node render.js page [width height]  -> renders src/<page>.html to ../<page>.png
const puppeteer = require('/workspace/proto-tools/node_modules/puppeteer-core');
const path = require('path');
const [p, w = 1920, h = 1080] = process.argv.slice(2);
(async () => {
  const b = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const pg = await b.newPage();
  await pg.setViewport({ width: +w, height: +h, deviceScaleFactor: 1 });
  const errs = []; pg.on('pageerror', e => errs.push(e.message)); pg.on('console', m => errs.push(m.text()));
  await pg.goto('file://' + path.join(__dirname, p + '.html'), { waitUntil: 'networkidle0' });
  await pg.evaluate(() => document.fonts.ready);
  await new Promise(r => setTimeout(r, 300));
  await pg.screenshot({ path: path.join(__dirname, '..', p + '.png') });
  console.log(p, 'title=' + await pg.title(), errs.join(' | '));
  await b.close();
})();
