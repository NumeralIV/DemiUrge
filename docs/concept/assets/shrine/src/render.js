// node render.js [page ...]  -> renders src/<page>.html to ../<page>.png at 1920x1080
const puppeteer = require('/workspace/proto-tools/node_modules/puppeteer-core');
const path = require('path');
const pages = process.argv.slice(2);
(async () => {
  const b = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome', headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  for (const p of pages) {
    const pg = await b.newPage();
    await pg.setViewport({ width: 1920, height: 1080, deviceScaleFactor: 1 });
    const errs = []; pg.on('pageerror', e => errs.push(e.message)); pg.on('console', m => errs.push(m.text()));
    await pg.goto('file://' + path.join(__dirname, p + '.html'), { waitUntil: 'networkidle0' });
    await pg.evaluate(() => document.fonts.ready);
    await new Promise(r => setTimeout(r, 300));
    await pg.screenshot({ path: path.join(__dirname, '..', p + '.png') });
    console.log(p, errs.join(' | '));
    await pg.close();
  }
  await b.close();
})();
