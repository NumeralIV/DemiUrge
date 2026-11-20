// Renders board.html?b=<board> to <out.png> at 1920x1080 with headless Chrome.
// Paths come from the environment (T-019 review note), with fallbacks for the box this was made on:
//   PUPPETEER_CORE  module path or name of puppeteer-core  (fallback: 'puppeteer-core', then /workspace/proto-tools/node_modules/puppeteer-core)
//   CHROME_PATH     Chrome / Chromium executable            (fallback: /usr/bin/google-chrome)
// Usage: node render.js <board> <out.png>
const path = require('path');
function loadPuppeteer() {
  const tries = [process.env.PUPPETEER_CORE, 'puppeteer-core', '/workspace/proto-tools/node_modules/puppeteer-core'].filter(Boolean);
  for (const t of tries) { try { return require(t); } catch (e) { /* try next */ } }
  throw new Error('puppeteer-core not found; set PUPPETEER_CORE');
}
(async () => {
  const [board, out] = process.argv.slice(2);
  const puppeteer = loadPuppeteer();
  const browser = await puppeteer.launch({ executablePath: process.env.CHROME_PATH || '/usr/bin/google-chrome',
    args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 1920, height: 1080 });
  await page.goto('file://' + path.join(__dirname, 'board.html') + '?b=' + board);
  await page.waitForFunction(() => document.title.startsWith('ready'), { timeout: 30000 });
  await page.screenshot({ path: out });
  console.log('title=' + await page.title());
  await browser.close();
})();
