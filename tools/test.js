// DEMIURGE — tools/test.js : run tests/*.test.js, each file in its own Node process.
//   node tools/test.js            all tests
//   node tools/test.js geo        only files whose name contains "geo"
//   T_VERBOSE=1 node tools/test.js   list passing tests too
// Exit code is non-zero if any test fails or any file crashes.
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const dir = path.resolve(__dirname, '..', 'tests');
const filter = process.argv[2] || '';
const files = fs.existsSync(dir) ? fs.readdirSync(dir).filter(f => f.endsWith('.test.js') && f.includes(filter)).sort() : [];
if (!files.length) { console.log('no test files match "' + filter + '"'); process.exit(1); }
let passed = 0, failed = 0, crashed = 0;
const t0 = Date.now();
for (const f of files) {
  const start = Date.now();
  const r = spawnSync(process.execPath, [path.join(dir, f)], {
    cwd: path.resolve(__dirname, '..'), encoding: 'utf8', env: Object.assign({}, process.env, { T_CHILD: '1' }),
    maxBuffer: 64 * 1024 * 1024, timeout: 10 * 60 * 1000,
  });
  const out = (r.stdout || ''), m = out.match(/##T (\{.*\})\s*$/m);
  let p = 0, fl = 0, crash = false;
  if (m) { try { const j = JSON.parse(m[1]); p = j.passed; fl = j.failed; } catch (e) { crash = true; } }
  else crash = true;
  if (r.status !== 0 && fl === 0) crash = true;
  const body = out.replace(/\n?##T \{.*\}\s*$/m, '').trimEnd();
  if (fl || crash || process.env.T_VERBOSE) { if (body) console.log(body); }
  if (crash) {
    crashed++;
    console.log('CRASH ' + f + '  (exit ' + r.status + (r.signal ? ', ' + r.signal : '') + ')\n' + String(r.stderr || '').split('\n').slice(0, 12).join('\n'));
  } else if ((r.stderr || '').trim() && (fl || process.env.T_VERBOSE)) console.log(String(r.stderr).trimEnd());
  passed += p; failed += fl;
  console.log((fl || crash ? 'FAIL ' : 'ok   ') + f + '  ' + p + '/' + (p + fl) + '  ' + (Date.now() - start) + ' ms');
}
console.log('\n' + passed + ' passed, ' + failed + ' failed' + (crashed ? ', ' + crashed + ' file(s) crashed' : '') + ', ' + files.length + ' files, ' + (Date.now() - t0) + ' ms');
process.exit(failed || crashed ? 1 : 0);
