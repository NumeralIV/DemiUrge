// DEMIURGE — tools/shot.js : headless-browser rig on Microsoft Edge's DevTools protocol (CDP).
// Zero dependencies: Node's built-in WebSocket, fetch, child_process and zlib. Documentation: docs/TOOLS.md
//
// Module:
//   const { launch, decodePng } = require('./shot.js');
//   const browser = await launch({ width: 1280, height: 720, dpr: 1, headless: true, timeout: 120000 });
//   const page = await browser.page();
//   await page.goto(urlOrPath);  await page.eval('js');  await page.wait(ms);  await page.waitFor('js', ms);
//   await page.shot('name.png'); await page.mouse(type, x, y, opts);  await page.drag(x1, y1, x2, y2, opts);
//   await page.key(type, code);  await page.touch(type, points);      await page.setViewport({ ... });
//   page.errors, page.logs;      await browser.close();
//   extras: page.png(), page.fps(ms), page.type(text), page.clearLogs(), page.close(), launch({ ignore, network, args, exe }),
//           browser.errors, browser.logs, browser.ignore, browser.downloads, browser.pid, browser.version, decodePng(fileOrBuffer)
// CLI:
//   node tools/shot.js <url-or-file> [--size 1280x720] [--dpr 1] [--wait 1500] [--wait-for "js"] [--eval "js"]
//                      [--settle 0] [--out name.png] [--no-shot] [--allow-errors] [--ignore "regex"] [--timeout 120000]
//                      [--headed] [--mobile] [--touch]
//   node tools/shot.js --run scenario.js [--size WxH] [--dpr 1] [--allow-errors] [--ignore "regex"] [--timeout ms] [--headed] [args…]
// Internal (spawned by the rig itself): --watchdog, --janitor
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const { spawn, spawnSync } = require('child_process');
const { pathToFileURL } = require('url');

const ROOT = path.resolve(__dirname, '..');
const SHOTS = path.join(ROOT, 'shots');
const PREFIX = 'demiurge-shot-';
const WIN = process.platform === 'win32';
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ------------------------------------------------------------------------------------------------
// Process and profile housekeeping
// ------------------------------------------------------------------------------------------------

function findBrowser(explicit) {
  const env = process.env;
  const pf86 = env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)';
  const pf = env.ProgramFiles || 'C:\\Program Files';
  const wanted = explicit || env.SHOT_BROWSER;
  if (wanted) {
    if (fs.existsSync(wanted)) return wanted;
    throw new Error('shot.js: browser executable not found: ' + wanted);
  }
  const candidates = [
    path.join(pf86, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(pf, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    env.LOCALAPPDATA && path.join(env.LOCALAPPDATA, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(pf, 'Google', 'Chrome', 'Application', 'chrome.exe'),
    path.join(pf86, 'Google', 'Chrome', 'Application', 'chrome.exe'),
  ];
  for (const c of candidates) if (c && fs.existsSync(c)) return c;
  throw new Error('shot.js: msedge.exe not found. Set the SHOT_BROWSER environment variable to a Chromium-based browser executable.');
}

function pidAlive(pid) {
  try { process.kill(pid, 0); return true; } catch (e) { return e.code === 'EPERM'; }
}

function killTree(pid) {
  if (WIN) {
    spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true });
  } else {
    try { process.kill(-pid, 'SIGKILL'); } catch (e) { /* no such group */ }
    try { process.kill(pid, 'SIGKILL'); } catch (e) { /* already gone */ }
  }
}

// Kill every browser process whose command line mentions one of the given profile directory names.
// Slow (starts PowerShell) but immune to PID reuse; used only as a fallback and by the watchdog/janitor.
function killByProfile(names, image) {
  names = names.filter(n => /^[A-Za-z0-9_-]+$/.test(n));
  if (!names.length) return;
  if (WIN) {
    const cond = names.map(n => "$_.CommandLine -like '*" + n + "*'").join(' -or ');
    const filter = image ? " -Filter \"Name='" + image.replace(/[^A-Za-z0-9._-]/g, '') + "'\"" : '';
    const ps = 'Get-CimInstance Win32_Process' + filter + ' | Where-Object { ' + cond +
      ' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }';
    spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps], { stdio: 'ignore', windowsHide: true, timeout: 30000 });
  } else {
    for (const n of names) spawnSync('pkill', ['-9', '-f', n], { stdio: 'ignore' });
  }
}

function removeDir(dir, retries) {
  try { fs.rmSync(dir, { recursive: true, force: true, maxRetries: retries === undefined ? 8 : retries, retryDelay: 50 }); } catch (e) { /* still locked */ }
  return !fs.existsSync(dir);
}

// Profiles left behind by rigs whose Node process no longer exists (hard-killed together with its watchdog).
function staleProfiles() {
  const tmp = os.tmpdir();
  let names;
  try { names = fs.readdirSync(tmp); } catch (e) { return []; }
  const out = [];
  for (const n of names) {
    if (!n.startsWith(PREFIX)) continue;
    const owner = parseInt(n.slice(PREFIX.length), 10);
    let old = false;
    try { old = Date.now() - fs.statSync(path.join(tmp, n)).mtimeMs > 12 * 3600 * 1000; } catch (e) { continue; }
    if (old || !(owner > 0) || !pidAlive(owner)) out.push(n);
  }
  return out;
}

function janitorMain() {
  const names = staleProfiles();
  if (!names.length) return;
  killByProfile(names, null);
  for (const n of names) removeDir(path.join(os.tmpdir(), n), 20);
}

// Detached helper process: stdin is a pipe held by the rig's Node process. When that pipe closes (clean
// close, crash or hard kill of the parent) or the deadline passes, make sure the browser and profile are gone.
function watchdogMain(profile, image, maxMs) {
  let buf = '';
  let done = false;
  const finish = () => {
    if (done) return;
    done = true;
    const name = path.basename(profile);
    if (!/closed/.test(buf) || !removeDir(profile, 4)) {
      killByProfile([name], image);
      removeDir(profile, 40);
    }
    process.exit(0);
  };
  process.stdin.on('data', d => { buf += d; });
  process.stdin.on('end', finish);
  process.stdin.on('close', finish);
  process.stdin.on('error', finish);
  process.stdin.resume();
  if (maxMs > 0) setTimeout(finish, maxMs);
}

const LIVE = new Set();
let hooked = false;
function installHooks() {
  if (hooked) return;
  hooked = true;
  const killAll = () => { for (const b of Array.from(LIVE)) b._kill(new Error('shot.js: process exiting')); };
  process.on('exit', killAll);
  const codes = { SIGINT: 130, SIGTERM: 143, SIGHUP: 129, SIGBREAK: 149 };
  for (const sig of Object.keys(codes)) {
    try { process.on(sig, () => { killAll(); process.exit(codes[sig]); }); } catch (e) { /* signal not supported here */ }
  }
}

function browserArgs(o, profile) {
  const a = [
    '--remote-debugging-port=0',
    '--user-data-dir=' + profile,
    '--window-size=' + o.width + ',' + o.height,
    '--no-first-run', '--no-default-browser-check', '--no-service-autorun',
    '--disable-extensions', '--disable-component-extensions-with-background-pages',
    '--disable-background-networking', '--disable-component-update', '--disable-sync', '--disable-default-apps',
    '--disable-domain-reliability', '--disable-client-side-phishing-detection', '--no-pings',
    '--disable-breakpad', '--disable-crash-reporter', '--noerrdialogs',
    '--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
    '--disable-hang-monitor', '--disable-popup-blocking', '--disable-prompt-on-repost',
    '--disable-features=Translate,OptimizationHints,MediaRouter,CalculateNativeWinOcclusion,SpareRendererForSitePerProcess,' +
      'msEdgeSidebarV2,msSmartScreenProtection,EdgeCollections,msEdgeShoppingAssistant,msImplicitSignin',
    // WebGL: the real GPU through ANGLE/D3D11 is the default; these only widen the fallbacks.
    '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader',
    '--force-color-profile=srgb', '--force-device-scale-factor=1',
    // Audio: silent, and rendered into a null sink so WebAudio clocks run without touching the real sound device.
    // The autoplay policy is left at the browser default (an AudioContext created before any input is 'suspended').
    '--mute-audio', '--disable-audio-output',
    '--enable-precise-memory-info',
    '--password-store=basic', '--use-mock-keychain',
  ];
  // Offline by default: every host name except loopback fails to resolve, so neither the page nor the
  // browser itself (help pages, update checks, telemetry) can reach the network.
  if (!o.network) a.push('--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1, EXCLUDE localhost');
  if (o.headless) a.push('--headless=new');
  for (const x of o.args || []) a.push(x);
  a.push('about:blank');
  return a;
}

// ------------------------------------------------------------------------------------------------
// CDP connection (one WebSocket to the browser endpoint, flat sessions per page)
// ------------------------------------------------------------------------------------------------

function restack(err, name, message) {
  const tail = String(err.stack || '').split('\n').slice(1).filter(l => !/[\\/]tools[\\/]shot\.js/.test(l) || /cli|scenario/i.test(l)).join('\n');
  err.name = name;
  err.message = message;
  err.stack = name + ': ' + message + (tail ? '\n' + tail : '');
  return err;
}

class Conn {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.nextId = 1;
    this.pending = new Map();
    this.handlers = new Map();
    this.closed = false;
    this.closeError = null;
  }

  open(timeoutMs) {
    return new Promise((resolve, reject) => {
      let settled = false;
      const ws = this.ws = new WebSocket(this.url);
      const timer = setTimeout(() => { if (!settled) { settled = true; reject(new Error('shot.js: timed out connecting to ' + this.url)); } }, timeoutMs || 10000);
      ws.addEventListener('open', () => { if (!settled) { settled = true; clearTimeout(timer); resolve(); } });
      ws.addEventListener('error', ev => {
        const msg = 'shot.js: DevTools WebSocket error' + (ev && ev.message ? ': ' + ev.message : '');
        if (!settled) { settled = true; clearTimeout(timer); reject(new Error(msg)); }
        this._fail(new Error(msg));
      });
      ws.addEventListener('close', () => this._fail(new Error('shot.js: browser connection closed')));
      ws.addEventListener('message', ev => this._message(ev.data));
    });
  }

  _message(data) {
    let m;
    try { m = JSON.parse(typeof data === 'string' ? data : Buffer.from(data).toString('utf8')); } catch (e) { return; }
    if (m.id !== undefined) {
      const p = this.pending.get(m.id);
      if (!p) return;
      this.pending.delete(m.id);
      if (m.error) p.reject(restack(p.err, 'CDPError', p.method + ': ' + m.error.message + (m.error.data ? ' (' + m.error.data + ')' : '')));
      else p.resolve(m.result);
    } else if (m.method) {
      const h = this.handlers.get(m.sessionId || '');
      if (h) { try { h(m.method, m.params || {}); } catch (e) { /* an event handler must never break the socket */ } }
    }
  }

  send(method, params, sessionId) {
    if (this.closed) return Promise.reject(this.closeError || new Error('shot.js: browser is closed'));
    return new Promise((resolve, reject) => {
      const id = this.nextId++;
      this.pending.set(id, { resolve, reject, method, sessionId: sessionId || '', err: new Error(method) });
      const msg = { id, method, params: params || {} };
      if (sessionId) msg.sessionId = sessionId;
      try { this.ws.send(JSON.stringify(msg)); } catch (e) { this.pending.delete(id); reject(e); }
    });
  }

  failSession(sessionId, err) {
    for (const [id, p] of this.pending) if (p.sessionId === sessionId) { this.pending.delete(id); p.reject(err); }
  }

  _fail(err) {
    if (this.closed) return;
    this.closed = true;
    this.closeError = err;
    for (const p of this.pending.values()) p.reject(err);
    this.pending.clear();
    if (this.onclose) this.onclose(err);
  }

  close(err) {
    this._fail(err || new Error('shot.js: browser is closed'));
    try { this.ws.close(); } catch (e) { /* already closed */ }
  }
}

// ------------------------------------------------------------------------------------------------
// Browser
// ------------------------------------------------------------------------------------------------

class Browser {
  constructor(opts) {
    this.opts = opts;
    this.pages = [];
    this.downloads = [];
    this.closed = false;
    this.closeReason = null;
    this.pid = 0;
    this.port = 0;
    this.version = '';
    this.profile = '';
    this._firstUsed = false;
    /** RegExps: an error message matching any of them is filed in page.logs as "[ignored error] …" (push more at any time) */
    this.ignore = [].concat(opts.ignore || []).map(x => x instanceof RegExp ? x : new RegExp(String(x)));
  }

  async _start() {
    const o = this.opts;
    this.exe = findBrowser(o.exe);
    this.image = path.basename(this.exe);
    this.profile = fs.mkdtempSync(path.join(os.tmpdir(), PREFIX + process.pid + '-'));
    const child = this.child = spawn(this.exe, browserArgs(o, this.profile), { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: !!o.headless, detached: !WIN });
    this.pid = child.pid;
    LIVE.add(this);
    let stderr = '';
    let spawnError = null;
    child.stderr.on('data', d => { stderr = (stderr + d).slice(-3000); });
    child.on('error', e => { spawnError = e; });
    child.on('exit', code => {
      this._exited = true;
      if (!this.closed) this._kill(new Error('shot.js: browser process exited unexpectedly (code ' + code + ')'));
    });

    // Last line of defence against orphans: a detached watchdog that outlives a hard-killed parent.
    try {
      const wd = this._wd = spawn(process.execPath, [__filename, '--watchdog', this.profile, this.image, String(o.timeout > 0 ? o.timeout + 15000 : 0)],
        { stdio: ['pipe', 'ignore', 'ignore'], detached: true, windowsHide: true });
      wd.on('error', () => {});
      wd.stdin.on('error', () => {});
      wd.unref();
      if (wd.stdin.unref) wd.stdin.unref();
    } catch (e) { this._wd = null; }

    if (o.timeout > 0) {
      this._timer = setTimeout(() => this._kill(new Error('shot.js: session timeout: the browser was killed after ' + o.timeout +
        ' ms (launch option "timeout" / CLI --timeout)')), o.timeout);
      if (this._timer.unref) this._timer.unref();
    }

    // The real debugging port is published in <profile>/DevToolsActivePort: "<port>\n/devtools/browser/<guid>".
    const portFile = path.join(this.profile, 'DevToolsActivePort');
    const t0 = Date.now();
    let wsPath = '';
    for (;;) {
      if (this.closed) throw this.closeReason || new Error('shot.js: browser closed during startup');
      if (spawnError) throw new Error('shot.js: cannot start ' + this.exe + ': ' + spawnError.message);
      try {
        const lines = fs.readFileSync(portFile, 'utf8').split('\n');
        if (lines.length >= 2 && /^\d+$/.test(lines[0].trim()) && lines[1].trim().startsWith('/devtools/')) {
          this.port = parseInt(lines[0], 10);
          wsPath = lines[1].trim();
          break;
        }
      } catch (e) { /* not written yet */ }
      if (Date.now() - t0 > 30000) throw new Error('shot.js: browser did not open a DevTools port within 30 s. stderr: ' + stderr.trim());
      await sleep(15);
    }
    try {
      const v = await (await fetch('http://127.0.0.1:' + this.port + '/json/version')).json();
      this.version = v.Browser || '';
      if (v.webSocketDebuggerUrl) wsPath = v.webSocketDebuggerUrl.replace(/^ws:\/\/[^/]+/, '');
    } catch (e) { /* the port file already told us what we need */ }
    this.wsUrl = 'ws://127.0.0.1:' + this.port + wsPath;
    this.conn = new Conn(this.wsUrl);
    this.conn.onclose = err => { if (!this.closed) this._kill(err); };
    await this.conn.open(15000);
    this.conn.handlers.set('', (method, p) => this._event(method, p));

    // Page-initiated downloads (e.g. <a download>) go to shots/downloads instead of the user's Downloads folder.
    this.downloadDir = path.join(SHOTS, 'downloads');
    try {
      await this.conn.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: this.downloadDir, eventsEnabled: true });
    } catch (e) { /* not fatal */ }
  }

  _event(method, p) {
    if (method === 'Browser.downloadWillBegin') {
      this.downloads.push({ guid: p.guid, name: p.suggestedFilename, file: path.join(this.downloadDir, p.suggestedFilename), url: String(p.url).slice(0, 200), state: 'inProgress', bytes: 0 });
    } else if (method === 'Browser.downloadProgress') {
      const d = this.downloads.find(x => x.guid === p.guid);
      if (d) { d.state = p.state; d.bytes = p.receivedBytes; }
    }
  }

  _check() {
    if (this.closed) throw this.closeReason || new Error('shot.js: browser is closed');
  }

  /** Open a page (the first call reuses the browser's initial blank tab). */
  async page() {
    this._check();
    let targetId = null;
    if (!this._firstUsed) {
      this._firstUsed = true;
      for (let i = 0; i < 150 && !targetId; i++) {
        const r = await this.conn.send('Target.getTargets');
        const t = r.targetInfos.find(x => x.type === 'page');
        if (t) targetId = t.targetId; else await sleep(20);
      }
    }
    if (!targetId) targetId = (await this.conn.send('Target.createTarget', { url: 'about:blank', newWindow: true })).targetId;
    const { sessionId } = await this.conn.send('Target.attachToTarget', { targetId, flatten: true });
    const page = new Page(this, targetId, sessionId);
    this.pages.push(page);
    await page._init();
    return page;
  }

  /** All errors / logs of all pages of this browser. */
  get errors() { return [].concat(...this.pages.map(p => p.errors)); }
  get logs() { return [].concat(...this.pages.map(p => p.logs)); }

  /** Kill the browser process tree and delete its temp profile. Safe to call more than once. */
  async close() {
    this._kill(null);
  }

  _kill(err) {
    if (this.closed) return;
    this.closed = true;
    this.closeReason = err || null;
    if (this._timer) clearTimeout(this._timer);
    LIVE.delete(this);
    const reason = err || new Error('shot.js: browser is closed');
    for (const p of this.pages) p._fail(reason);
    if (this.conn) this.conn.close(reason);
    if (this.child && !this._exited && this.child.exitCode === null && this.child.signalCode === null) killTree(this.pid);
    if (this.child && this.child.stderr) { try { this.child.stderr.destroy(); } catch (e) { /* ignore */ } }
    if (this.profile && !removeDir(this.profile)) {
      killByProfile([path.basename(this.profile)], this.image);
      removeDir(this.profile, 20);
    }
    if (this._wd) { try { this._wd.stdin.end('closed\n'); } catch (e) { /* watchdog already gone */ } }
  }
}

// ------------------------------------------------------------------------------------------------
// Page
// ------------------------------------------------------------------------------------------------

const URL_RE = /^(https?|file|about|data|blob|edge|chrome|view-source):/i;

function toUrl(target) {
  const t = String(target).trim();
  if (URL_RE.test(t)) return t;
  const tryPath = p => {
    for (const base of [process.cwd(), ROOT]) {
      const abs = path.resolve(base, p);
      if (fs.existsSync(abs)) return abs;
    }
    return null;
  };
  let abs = tryPath(t);
  let suffix = '';
  if (!abs) {
    const m = /^(.*?)([?#].*)$/.exec(t);
    if (m) { abs = tryPath(m[1]); suffix = m[2]; }
  }
  if (!abs) throw new Error('page.goto: file not found: ' + path.resolve(t));
  if (fs.statSync(abs).isDirectory()) abs = path.join(abs, 'index.html');
  return pathToFileURL(abs).href + suffix;
}

// Runs in the page: turn any value into plain JSON-compatible data.
const SERIALIZE = `function () {
  const seen = new Set();
  const conv = (v, d) => {
    if (v === null || v === undefined) return v;
    const t = typeof v;
    if (t === 'number' || t === 'string' || t === 'boolean') return v;
    if (t === 'bigint') return v.toString() + 'n';
    if (t === 'function' || t === 'symbol') return undefined;
    if (seen.has(v)) return '[Circular]';
    if (d > 40) return '[Too deep]';
    if (ArrayBuffer.isView(v) && !(v instanceof DataView)) return Array.from(v, x => typeof x === 'bigint' ? x.toString() + 'n' : x);
    if (v instanceof ArrayBuffer) return Array.from(new Uint8Array(v));
    if (v instanceof Date) return isNaN(v) ? null : v.toISOString();
    if (v instanceof RegExp) return String(v);
    if (v instanceof Error) return { name: v.name, message: v.message, stack: v.stack };
    if (typeof Node !== 'undefined' && v instanceof Node) {
      return v.nodeType === 1 ? '<' + v.tagName.toLowerCase() + (v.id ? '#' + v.id : '') + (typeof v.className === 'string' && v.className ? '.' + v.className.trim().split(/\\s+/).join('.') : '') + '>' : v.nodeName;
    }
    if (typeof Window !== 'undefined' && v instanceof Window) return '[Window]';
    seen.add(v);
    let out;
    if (Array.isArray(v)) out = v.map(x => { const c = conv(x, d + 1); return c === undefined ? null : c; });
    else if (v instanceof Map) out = Array.from(v, e => [conv(e[0], d + 1), conv(e[1], d + 1)]);
    else if (v instanceof Set) out = Array.from(v, x => conv(x, d + 1));
    else { out = {}; for (const k of Object.keys(v)) { let c; try { c = conv(v[k], d + 1); } catch (e) { c = '[Throws: ' + e.message + ']'; } if (c !== undefined) out[k] = c; } }
    seen.delete(v);
    return out;
  };
  return conv(this, 0);
}`;

function primitive(r) {
  if ('value' in r) return r.value;
  if (r.unserializableValue !== undefined) {
    const u = r.unserializableValue;
    if (u === 'NaN') return NaN;
    if (u === 'Infinity') return Infinity;
    if (u === '-Infinity') return -Infinity;
    if (u === '-0') return -0;
    return u; // bigint literal such as "10n"
  }
  return undefined;
}

function fmtPreview(p) {
  const list = p.subtype === 'array' || p.subtype === 'typedarray';
  const items = (p.properties || []).map(q => {
    const v = q.type === 'string' ? JSON.stringify(q.value) : q.value;
    return list ? v : q.name + ': ' + v;
  });
  if (p.overflow) items.push('…');
  if (list) return (p.subtype === 'typedarray' ? p.description + ' ' : '') + '[' + items.join(', ') + ']';
  return (p.description && p.description !== 'Object' ? p.description + ' ' : '') + '{' + items.join(', ') + '}';
}

function fmtRemote(o) {
  if (!o) return '';
  if (o.type === 'string') return o.value;
  if (o.type === 'undefined') return 'undefined';
  if (o.unserializableValue !== undefined) return o.unserializableValue;
  if ('value' in o) return o.value !== null && typeof o.value === 'object' ? JSON.stringify(o.value) : String(o.value);
  if (o.subtype === 'error') return o.description || 'Error';
  if (o.preview && o.type === 'object' && o.subtype !== 'node') return fmtPreview(o.preview);
  return o.description || o.type;
}

function frameLoc(st) {
  const f = st && st.callFrames && st.callFrames.find(x => x.url) || (st && st.callFrames && st.callFrames[0]);
  if (!f) return '';
  return (f.url || '<eval>') + ':' + (f.lineNumber + 1) + ':' + (f.columnNumber + 1);
}

function describeException(d) {
  const ex = d.exception;
  let msg;
  if (ex && ex.description) msg = ex.description;
  else if (ex && 'value' in ex) msg = typeof ex.value === 'string' ? ex.value : JSON.stringify(ex.value);
  else if (ex && ex.unserializableValue !== undefined) msg = ex.unserializableValue;
  else msg = '';
  let text = d.text || 'Uncaught';
  if (msg) text = /^Uncaught/.test(text) ? text + ' ' + msg : msg;
  const loc = d.url ? d.url + ':' + (d.lineNumber + 1) + ':' + (d.columnNumber + 1) : frameLoc(d.stackTrace);
  if (loc && text.indexOf(loc) < 0 && !/\n\s+at /.test(text)) text += '\n    at ' + loc;
  else if (d.url && text.indexOf(d.url) < 0) text += '\n    at ' + loc;
  return text;
}

const GL_ERROR_RE = /WebGL: (INVALID_|OUT_OF_MEMORY|CONTEXT_LOST)|GL_INVALID_|GL_OUT_OF_MEMORY/;

// Key table: DOM `code` → key, Windows virtual key, text.
const KEYS = {};
(function () {
  const def = (code, key, vk, text, shiftKey, location) => { KEYS[code] = { code, key, vk, text: text || '', shiftKey: shiftKey || null, location: location || 0 }; };
  for (let i = 0; i < 26; i++) { const ch = String.fromCharCode(97 + i); def('Key' + ch.toUpperCase(), ch, 65 + i, ch, ch.toUpperCase()); }
  const shiftDigits = ')!@#$%^&*(';
  for (let i = 0; i < 10; i++) { def('Digit' + i, String(i), 48 + i, String(i), shiftDigits[i]); def('Numpad' + i, String(i), 96 + i, String(i), null, 3); }
  for (let i = 1; i <= 12; i++) def('F' + i, 'F' + i, 111 + i);
  def('Escape', 'Escape', 27); def('Enter', 'Enter', 13, '\r'); def('Tab', 'Tab', 9); def('Space', ' ', 32, ' ');
  def('Backspace', 'Backspace', 8); def('Delete', 'Delete', 46); def('Insert', 'Insert', 45);
  def('Home', 'Home', 36); def('End', 'End', 35); def('PageUp', 'PageUp', 33); def('PageDown', 'PageDown', 34);
  def('ArrowLeft', 'ArrowLeft', 37); def('ArrowUp', 'ArrowUp', 38); def('ArrowRight', 'ArrowRight', 39); def('ArrowDown', 'ArrowDown', 40);
  def('ShiftLeft', 'Shift', 16, '', null, 1); def('ShiftRight', 'Shift', 16, '', null, 2);
  def('ControlLeft', 'Control', 17, '', null, 1); def('ControlRight', 'Control', 17, '', null, 2);
  def('AltLeft', 'Alt', 18, '', null, 1); def('AltRight', 'Alt', 18, '', null, 2);
  def('MetaLeft', 'Meta', 91, '', null, 1); def('MetaRight', 'Meta', 92, '', null, 2);
  def('CapsLock', 'CapsLock', 20); def('ContextMenu', 'ContextMenu', 93); def('Pause', 'Pause', 19);
  def('Minus', '-', 189, '-', '_'); def('Equal', '=', 187, '=', '+');
  def('BracketLeft', '[', 219, '[', '{'); def('BracketRight', ']', 221, ']', '}'); def('Backslash', '\\', 220, '\\', '|');
  def('Semicolon', ';', 186, ';', ':'); def('Quote', '\'', 222, '\'', '"');
  def('Comma', ',', 188, ',', '<'); def('Period', '.', 190, '.', '>'); def('Slash', '/', 191, '/', '?'); def('Backquote', '`', 192, '`', '~');
  def('NumpadAdd', '+', 107, '+', null, 3); def('NumpadSubtract', '-', 109, '-', null, 3); def('NumpadMultiply', '*', 106, '*', null, 3);
  def('NumpadDivide', '/', 111, '/', null, 3); def('NumpadDecimal', '.', 110, '.', null, 3); def('NumpadEnter', 'Enter', 13, '\r', null, 3);
})();
const KEY_ALIAS = { Shift: 'ShiftLeft', Control: 'ControlLeft', Ctrl: 'ControlLeft', Alt: 'AltLeft', Meta: 'MetaLeft', Esc: 'Escape', Return: 'Enter',
  ' ': 'Space', Spacebar: 'Space', Left: 'ArrowLeft', Right: 'ArrowRight', Up: 'ArrowUp', Down: 'ArrowDown', Del: 'Delete' };

function keyInfo(name) {
  name = String(name);
  if (KEYS[name]) return { k: KEYS[name], shifted: false };
  if (KEY_ALIAS[name]) return { k: KEYS[KEY_ALIAS[name]], shifted: false };
  if (name.length === 1) {
    for (const code of Object.keys(KEYS)) {
      const k = KEYS[code];
      if (k.location === 3) continue;
      if (k.key === name) return { k, shifted: false };
      if (k.shiftKey === name) return { k, shifted: true };
    }
    return { k: { code: '', key: name, vk: 0, text: name, shiftKey: null, location: 0 }, shifted: false };
  }
  throw new Error('page.key: unknown key "' + name + '" (use DOM codes such as KeyW, Digit1, Space, Escape, ArrowLeft, ShiftLeft, F5)');
}

const BUTTON_BIT = { left: 1, right: 2, middle: 4, back: 8, forward: 16 };

class Page {
  constructor(browser, targetId, sessionId) {
    this.browser = browser;
    this.targetId = targetId;
    this.sessionId = sessionId;
    /** uncaught exceptions, console.error / console.assert output, failed resource loads, WebGL errors */
    this.errors = [];
    /** all other console output, prefixed with its level when it is not plain "log" */
    this.logs = [];
    this.viewport = { width: browser.opts.width, height: browser.opts.height, dpr: browser.opts.dpr, mobile: !!browser.opts.mobile, touch: !!browser.opts.touch };
    this.url = 'about:blank';
    this.closed = false;
    this._life = [];
    this._waiters = new Set();
    this._last = { errors: null, logs: null };
    this._exc = new Map();
    this._mx = 0;
    this._my = 0;
    this._buttons = 0;
    this._mods = 0;
    this._keysDown = new Set();
    this._touches = new Map();
  }

  _send(method, params) {
    if (this.closed) return Promise.reject(this._closeError || new Error('shot.js: page is closed'));
    return this.browser.conn.send(method, params, this.sessionId);
  }

  async _init() {
    this.browser.conn.handlers.set(this.sessionId, (method, p) => this._event(method, p));
    await Promise.all([
      this._send('Page.enable'),
      this._send('Runtime.enable'),
      this._send('Log.enable'),
      this._send('Page.setLifecycleEventsEnabled', { enabled: true }),
      this._send('Emulation.setFocusEmulationEnabled', { enabled: true }),
    ]);
    await this.setViewport({});
  }

  _fail(err) {
    if (this.closed) return;
    this.closed = true;
    this._closeError = err;
    for (const w of Array.from(this._waiters)) w.reject(err);
    this._waiters.clear();
  }

  // Returns true if `text` ended up in page.errors (false for logs and for errors matched by the `ignore` option).
  _push(kind, text) {
    if (kind === 'errors') {
      const ig = this.browser.ignore;
      for (let i = 0; i < ig.length; i++) {
        if (ig[i].test(text)) { kind = 'logs'; text = '[ignored error] ' + text; break; }
      }
    }
    const isError = kind === 'errors';
    const arr = this[kind];
    const last = this._last[kind];
    if (last && last.text === text && last.index === arr.length - 1) {
      last.count++;
      arr[last.index] = text + '  [x' + last.count + ']';
    } else {
      this._last[kind] = { text, count: 1, index: arr.length };
      arr.push(text);
    }
    return isError;
  }

  _event(method, p) {
    switch (method) {
      case 'Runtime.consoleAPICalled': {
        const text = (p.args || []).map(fmtRemote).join(' ');
        if (p.type === 'error' || p.type === 'assert') {
          const loc = frameLoc(p.stackTrace);
          this._push('errors', (p.type === 'assert' ? 'Assertion failed: ' : '') + text + (loc && text.indexOf(loc) < 0 ? '\n    at ' + loc : ''));
        } else {
          this._push('logs', (p.type === 'log' ? '' : '[' + p.type + '] ') + text);
        }
        break;
      }
      case 'Runtime.exceptionThrown': {
        const d = p.exceptionDetails || {};
        if (this._push('errors', describeException(d))) this._exc.set(d.exceptionId, this.errors.length - 1);
        break;
      }
      case 'Runtime.exceptionRevoked': {
        // a rejected promise got a handler after all (e.g. page.eval awaiting it): not an uncaught error
        const i = this._exc.get(p.exceptionId);
        if (i !== undefined && i === this.errors.length - 1) { this.errors.pop(); this._last.errors = null; }
        else if (i !== undefined) this.errors[i] = '(handled later) ' + this.errors[i];
        this._exc.delete(p.exceptionId);
        break;
      }
      case 'Log.entryAdded': {
        const e = p.entry || {};
        const loc = e.url ? e.url + (e.lineNumber ? ':' + e.lineNumber : '') : frameLoc(e.stackTrace);
        const text = '[' + e.source + '] ' + e.text + (loc && String(e.text).indexOf(loc) < 0 ? '\n    at ' + loc : '');
        if (e.level === 'error' || (e.source === 'rendering' && GL_ERROR_RE.test(e.text))) this._push('errors', text);
        else this._push('logs', '[' + e.level + '] ' + text);
        break;
      }
      case 'Page.lifecycleEvent':
        this._life.push(p);
        for (const w of Array.from(this._waiters)) w.check();
        break;
      case 'Page.frameNavigated':
        if (p.frame && !p.frame.parentId) this.url = p.frame.url;
        break;
      case 'Page.javascriptDialogOpening':
        this._push('logs', '[dialog] ' + p.type + ': ' + p.message);
        this._send('Page.handleJavaScriptDialog', { accept: true }).catch(() => {});
        break;
      case 'Inspector.targetCrashed': {
        this._push('errors', 'PAGE CRASHED: the renderer process died');
        const err = new Error('shot.js: page crashed (renderer process died)');
        this.browser.conn.failSession(this.sessionId, err);
        for (const w of Array.from(this._waiters)) w.reject(err);
        break;
      }
      default:
    }
  }

  /** Empty page.errors and page.logs (same arrays, so references stay valid). */
  clearLogs() {
    this.errors.length = 0;
    this.logs.length = 0;
    this._last = { errors: null, logs: null };
    this._exc.clear();
  }

  /**
   * Navigate. `target` is an http(s)/file/data/about URL or a plain file path (absolute, or relative to the
   * current directory or the project root; may contain spaces and may carry a ?query or #hash suffix).
   * Resolves with the final URL after the load event.
   */
  async goto(target, opts) {
    const url = toUrl(target);
    const timeout = opts && opts.timeout !== undefined ? opts.timeout : 30000;
    this._life.length = 0;
    const r = await this._send('Page.navigate', { url });
    if (r.errorText) throw new Error('page.goto: ' + r.errorText + ' (' + url + ')');
    if (r.loaderId) {
      await new Promise((resolve, reject) => {
        const w = {
          check: () => { if (this._life.some(e => e.loaderId === r.loaderId && e.name === 'load')) { clearTimeout(timer); this._waiters.delete(w); resolve(); } },
          reject: e => { clearTimeout(timer); this._waiters.delete(w); reject(e); },
        };
        const timer = setTimeout(() => w.reject(new Error('page.goto: no load event within ' + timeout + ' ms (' + url + ')')), timeout);
        this._waiters.add(w);
        w.check();
      });
    }
    this.url = url;
    return url;
  }

  async _evaluate(expression) {
    let r = await this._send('Runtime.evaluate', { expression, replMode: true, awaitPromise: true, returnByValue: false, userGesture: true, objectGroup: 'shot' });
    // replMode resolves top-level await but hands back a promise-valued completion as an object: await it too
    for (let i = 0; i < 8 && !r.exceptionDetails && r.result && r.result.subtype === 'promise'; i++) {
      r = await this._send('Runtime.awaitPromise', { promiseObjectId: r.result.objectId, returnByValue: false, generatePreview: false });
    }
    return r;
  }

  /**
   * Evaluate an expression or statements in the page. Top-level await and `return` are allowed, promises are
   * awaited, and the value comes back by value (typed arrays → arrays, Map → entries, Set → array, DOM nodes →
   * a short description). A page-side exception rejects with a PageError whose message is the page's message + stack.
   */
  async eval(src) {
    const at = new Error('eval');
    src = String(src);
    let code = src;
    if (/^\s*\{[\s\S]*\}\s*;?\s*$/.test(src)) {
      // "{a: 1}" is a block statement to the parser; prefer the object-literal reading when it compiles
      const wrapped = '(' + src.replace(/;\s*$/, '') + '\n)';
      const c = await this._send('Runtime.compileScript', { expression: wrapped, sourceURL: '', persistScript: false });
      if (!c.exceptionDetails) code = wrapped;
    }
    let r = await this._evaluate(code);
    if (r.exceptionDetails && r.exceptionDetails.exception && /^SyntaxError: Illegal return statement$/.test(r.exceptionDetails.exception.description || '')) {
      r = await this._evaluate('(async () => {' + src + '\n})()');
    }
    if (r.exceptionDetails) {
      const d = r.exceptionDetails;
      const ex = d.exception;
      let msg = ex && ex.description ? ex.description : ex && 'value' in ex ? (typeof ex.value === 'string' ? ex.value : JSON.stringify(ex.value)) : d.text;
      if (ex && !/\n\s+at /.test(msg) && d.lineNumber !== undefined && ex.className === 'SyntaxError') msg += '\n    at <eval>:' + (d.lineNumber + 1) + ':' + (d.columnNumber + 1);
      const e = restack(at, 'PageError', msg);
      e.pageMessage = msg.split('\n')[0];
      throw e;
    }
    const res = r.result || {};
    let value;
    if (res.objectId) {
      const s = await this._send('Runtime.callFunctionOn', { objectId: res.objectId, functionDeclaration: SERIALIZE, returnByValue: true, awaitPromise: false });
      if (s.exceptionDetails) throw restack(at, 'PageError', 'page.eval: result is not serializable: ' + describeException(s.exceptionDetails));
      value = primitive(s.result);
      this._send('Runtime.releaseObjectGroup', { objectGroup: 'shot' }).catch(() => {});
    } else {
      value = primitive(res);
    }
    return value;
  }

  /** Sleep in Node; the page keeps running. */
  wait(ms) {
    return sleep(ms);
  }

  /** Poll `expr` until it is truthy; resolves with that value. A throwing expression counts as "not yet". */
  async waitFor(expr, timeoutMs, intervalMs) {
    const at = new Error('waitFor');
    timeoutMs = timeoutMs === undefined ? 10000 : timeoutMs;
    const t0 = Date.now();
    let lastError = null;
    for (;;) {
      try {
        const v = await this.eval(expr);
        if (v) return v;
        lastError = null;
      } catch (e) {
        if (e.name !== 'PageError') throw e;
        lastError = e;
      }
      if (Date.now() - t0 >= timeoutMs) {
        throw restack(at, 'TimeoutError', 'page.waitFor: "' + String(expr).slice(0, 200) + '" was not truthy within ' + timeoutMs + ' ms' +
          (lastError ? ' (last evaluation threw: ' + lastError.pageMessage + ')' : ''));
      }
      await sleep(intervalMs || 50);
    }
  }

  /** Screenshot of the viewport as a PNG Buffer. opts: { clip: { x, y, width, height }, format: 'png'|'jpeg'|'webp', quality } */
  async png(opts) {
    opts = opts || {};
    const params = { format: opts.format || 'png', captureBeyondViewport: false };
    if (params.format !== 'png') params.quality = opts.quality || 90;
    if (opts.clip) params.clip = { x: opts.clip.x, y: opts.clip.y, width: opts.clip.width, height: opts.clip.height, scale: 1 };
    const r = await this._send('Page.captureScreenshot', params);
    return Buffer.from(r.data, 'base64');
  }

  /** Save a screenshot of the viewport. A relative name lands in <project>/shots/. Returns the absolute path. */
  async shot(name, opts) {
    name = name || 'shot.png';
    if (!path.extname(name)) name += '.png';
    const file = path.isAbsolute(name) ? name : path.join(SHOTS, name);
    const ext = path.extname(file).toLowerCase();
    const format = ext === '.jpg' || ext === '.jpeg' ? 'jpeg' : ext === '.webp' ? 'webp' : 'png';
    const buf = await this.png(Object.assign({ format }, opts || {}));
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, buf);
    return file;
  }

  /** type: 'move' | 'down' | 'up' | 'click' | 'wheel'. opts: { button, deltaX, deltaY, clickCount } */
  async mouse(type, x, y, opts) {
    opts = opts || {};
    if (x === undefined || x === null) x = this._mx;
    if (y === undefined || y === null) y = this._my;
    const button = opts.button || 'left';
    if (!BUTTON_BIT[button]) throw new Error('page.mouse: unknown button "' + button + '"');
    const base = { x, y, modifiers: this._mods, pointerType: 'mouse' };
    const held = () => this._buttons & 1 ? 'left' : this._buttons & 2 ? 'right' : this._buttons & 4 ? 'middle' : 'none';
    this._mx = x;
    this._my = y;
    switch (type) {
      case 'move':
        await this._send('Input.dispatchMouseEvent', Object.assign(base, { type: 'mouseMoved', button: held(), buttons: this._buttons }));
        break;
      case 'down':
        this._buttons |= BUTTON_BIT[button];
        await this._send('Input.dispatchMouseEvent', Object.assign(base, { type: 'mousePressed', button, buttons: this._buttons, clickCount: opts.clickCount || 1 }));
        break;
      case 'up':
        this._buttons &= ~BUTTON_BIT[button];
        await this._send('Input.dispatchMouseEvent', Object.assign(base, { type: 'mouseReleased', button, buttons: this._buttons, clickCount: opts.clickCount || 1 }));
        break;
      case 'click': {
        const n = opts.clickCount || 1;
        await this.mouse('move', x, y);
        for (let i = 1; i <= n; i++) {
          await this.mouse('down', x, y, { button, clickCount: i });
          await this.mouse('up', x, y, { button, clickCount: i });
        }
        break;
      }
      case 'wheel':
        await this._send('Input.dispatchMouseEvent', Object.assign(base, { type: 'mouseWheel', button: held(), buttons: this._buttons,
          deltaX: opts.deltaX || 0, deltaY: opts.deltaY === undefined ? 100 : opts.deltaY }));
        break;
      default:
        throw new Error('page.mouse: unknown type "' + type + '" (move, down, up, click, wheel)');
    }
  }

  /** Press at (x1,y1), move to (x2,y2) in `steps` moves, release. opts: { steps = 10, button = 'left', delay = 0 (ms between moves) } */
  async drag(x1, y1, x2, y2, opts) {
    opts = opts || {};
    const steps = Math.max(1, opts.steps || 10);
    const button = opts.button || 'left';
    await this.mouse('move', x1, y1);
    await this.mouse('down', x1, y1, { button });
    for (let i = 1; i <= steps; i++) {
      const t = i / steps;
      await this.mouse('move', x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
      if (opts.delay) await sleep(opts.delay);
    }
    await this.mouse('up', x2, y2, { button });
  }

  /** type: 'down' | 'up' | 'press'. code: DOM code ('KeyW', 'Digit1', 'Space', 'Escape', 'ArrowLeft', 'ShiftLeft', 'F5' …) or a single character. opts: { shift, ctrl, alt, meta, hold (ms, press only) } */
  async key(type, code, opts) {
    opts = opts || {};
    if (type === 'press') {
      await this.key('down', code, opts);
      if (opts.hold) await sleep(opts.hold);
      return this.key('up', code, opts);
    }
    if (type !== 'down' && type !== 'up') throw new Error('page.key: unknown type "' + type + '" (down, up, press)');
    const { k, shifted } = keyInfo(code);
    const MOD = { Alt: 1, Control: 2, Meta: 4, Shift: 8 };
    if (MOD[k.key]) { if (type === 'down') this._mods |= MOD[k.key]; else this._mods &= ~MOD[k.key]; }
    const mods = this._mods | (opts.alt ? 1 : 0) | (opts.ctrl ? 2 : 0) | (opts.meta ? 4 : 0) | (opts.shift || shifted ? 8 : 0);
    const shift = !!(mods & 8);
    const key = shift && k.shiftKey ? k.shiftKey : k.key;
    const text = mods & 7 ? '' : (shift && k.shiftKey ? k.shiftKey : k.text);
    const ev = { modifiers: mods, code: k.code, key, windowsVirtualKeyCode: k.vk, nativeVirtualKeyCode: k.vk, location: k.location, isKeypad: k.location === 3 };
    if (type === 'down') {
      // browser accelerators such as F6/F10 move keyboard focus into (invisible) browser UI; take it back
      if (!this._keysDown.size) await this._send('Page.bringToFront');
      ev.type = text ? 'keyDown' : 'rawKeyDown';
      ev.autoRepeat = this._keysDown.has(k.code || key);
      if (text) { ev.text = text; ev.unmodifiedText = text; }
      this._keysDown.add(k.code || key);
    } else {
      ev.type = 'keyUp';
      this._keysDown.delete(k.code || key);
    }
    await this._send('Input.dispatchKeyEvent', ev);
  }

  /** Insert text into the focused element (as if pasted / typed by an IME). */
  async type(text) {
    await this._send('Input.insertText', { text: String(text) });
  }

  /**
   * Touch input. points: [{ x, y, id }]. 'start' adds fingers, 'move' moves the listed fingers,
   * 'end' / 'cancel' lifts the listed fingers (all fingers when the list is empty or omitted).
   */
  async touch(type, points) {
    const T = this._touches;
    const list = (points || []).map((p, i) => ({ id: p.id === undefined ? i : p.id, x: p.x, y: p.y }));
    let cdp;
    if (type === 'start') {
      if (!list.length) throw new Error('page.touch: "start" needs at least one point');
      for (const p of list) T.set(p.id, p);
      cdp = 'touchStart';
    } else if (type === 'move') {
      for (const p of list) { if (!T.has(p.id)) throw new Error('page.touch: finger id ' + p.id + ' is not down'); T.set(p.id, p); }
      cdp = 'touchMove';
    } else if (type === 'end' || type === 'cancel') {
      // CDP: touchEnd/touchCancel with a list lifts exactly those fingers; with an empty list, all of them
      cdp = type === 'cancel' ? 'touchCancel' : 'touchEnd';
      const lifted = [];
      for (const p of list) { const cur = T.get(p.id); if (cur) { lifted.push(p.x === undefined ? cur : p); T.delete(p.id); } }
      if (!list.length || !T.size) { T.clear(); lifted.length = 0; }
      const pts = lifted.map(p => ({ x: p.x, y: p.y, id: p.id, radiusX: 10, radiusY: 10, force: 0 }));
      await this._send('Input.dispatchTouchEvent', { type: cdp, touchPoints: pts, modifiers: this._mods });
      return;
    } else {
      throw new Error('page.touch: unknown type "' + type + '" (start, move, end, cancel)');
    }
    const touchPoints = Array.from(T.values()).map(p => ({ x: p.x, y: p.y, id: p.id, radiusX: 10, radiusY: 10, force: 1 }));
    await this._send('Input.dispatchTouchEvent', { type: cdp, touchPoints, modifiers: this._mods });
  }

  /** Change the emulated viewport. Omitted fields keep their current value. */
  async setViewport(v) {
    const cur = Object.assign(this.viewport, v || {});
    await this._send('Emulation.setDeviceMetricsOverride', { width: Math.round(cur.width), height: Math.round(cur.height), deviceScaleFactor: cur.dpr || 1,
      mobile: !!cur.mobile, screenWidth: Math.round(cur.width), screenHeight: Math.round(cur.height) });
    await this._send('Emulation.setTouchEmulationEnabled', cur.touch ? { enabled: true, maxTouchPoints: 5 } : { enabled: false });
    return cur;
  }

  /** Count requestAnimationFrame callbacks for `ms` milliseconds → { frames, ms, fps, meanMs, maxMs }. */
  fps(ms) {
    ms = ms || 1000;
    return this.eval('new Promise(res => { let n = 0, max = 0, last = 0; const t0 = performance.now(); ' +
      'function f() { const t = performance.now(); if (n) max = Math.max(max, t - last); last = t; n++; ' +
      'if (t - t0 < ' + ms + ') requestAnimationFrame(f); else { const d = t - t0; res({ frames: n - 1, ms: +d.toFixed(1), fps: +((n - 1) * 1000 / d).toFixed(1), meanMs: +(d / Math.max(1, n - 1)).toFixed(2), maxMs: +max.toFixed(2) }); } } ' +
      'requestAnimationFrame(f); })');
  }

  /** Close this tab (the browser stays up). */
  async close() {
    if (this.closed) return;
    try { await this.browser.conn.send('Target.closeTarget', { targetId: this.targetId }); } catch (e) { /* browser already gone */ }
    this._fail(new Error('shot.js: page is closed'));
  }
}

// ------------------------------------------------------------------------------------------------
// launch
// ------------------------------------------------------------------------------------------------

/**
 * Start a private browser instance.
 * opts: { width = 1280, height = 720, dpr = 1, headless = true, timeout = 120000 (ms, whole session; 0 = none),
 *         mobile = false, touch = false, network = false (true lets the browser reach non-loopback hosts),
 *         ignore = [] (RegExp | string | array: matching error messages go to page.logs instead of page.errors),
 *         args = [] (extra browser flags), exe (browser path) }
 */
async function launch(opts) {
  const o = Object.assign({ width: 1280, height: 720, dpr: 1, headless: true, timeout: 120000, mobile: false, touch: false, network: false, ignore: [], args: [], exe: null }, opts || {});
  installHooks();
  try {
    if (staleProfiles().length) {
      const j = spawn(process.execPath, [__filename, '--janitor'], { stdio: 'ignore', detached: true, windowsHide: true });
      j.on('error', () => {});
      j.unref();
    }
  } catch (e) { /* housekeeping only */ }
  let lastErr = null;
  for (let attempt = 0; attempt < 2; attempt++) {
    const b = new Browser(o);
    try {
      await b._start();
      return b;
    } catch (e) {
      lastErr = e;
      b._kill(e);
      if (/session timeout|not found/.test(e.message)) break;
    }
  }
  throw lastErr;
}

// ------------------------------------------------------------------------------------------------
// PNG decoding (for asserting on screenshots)
// ------------------------------------------------------------------------------------------------

/**
 * Decode an 8-bit non-interlaced PNG (what page.shot / page.png produce).
 * → { width, height, data: Uint8Array RGBA, px(x, y) → [r, g, b, a], stats() → { mean: [r, g, b], min, max, lit, colors } }
 */
function decodePng(src) {
  const buf = Buffer.isBuffer(src) ? src : fs.readFileSync(src);
  if (buf.length < 8 || buf.readUInt32BE(0) !== 0x89504e47 || buf.readUInt32BE(4) !== 0x0d0a1a0a) throw new Error('decodePng: not a PNG');
  let off = 8, width = 0, height = 0, depth = 0, ctype = 0, interlace = 0;
  const idat = [];
  while (off + 8 <= buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString('latin1', off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') { width = data.readUInt32BE(0); height = data.readUInt32BE(4); depth = data[8]; ctype = data[9]; interlace = data[12]; }
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  const ch = { 0: 1, 2: 3, 4: 2, 6: 4 }[ctype];
  if (depth !== 8 || !ch || interlace) throw new Error('decodePng: unsupported PNG (depth ' + depth + ', colour type ' + ctype + ', interlace ' + interlace + ')');
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * ch;
  const data = new Uint8Array(width * height * 4);
  let prev = new Uint8Array(stride), cur = new Uint8Array(stride), p = 0;
  for (let y = 0; y < height; y++) {
    const f = raw[p++];
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? cur[i - ch] : 0, b = prev[i], c = i >= ch ? prev[i - ch] : 0;
      let v = raw[p++];
      if (f === 1) v += a;
      else if (f === 2) v += b;
      else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const q = a + b - c, pa = Math.abs(q - a), pb = Math.abs(q - b), pc = Math.abs(q - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[i] = v & 255;
    }
    let o = y * width * 4;
    for (let x = 0; x < width; x++, o += 4) {
      const i = x * ch;
      if (ch >= 3) { data[o] = cur[i]; data[o + 1] = cur[i + 1]; data[o + 2] = cur[i + 2]; data[o + 3] = ch === 4 ? cur[i + 3] : 255; }
      else { data[o] = data[o + 1] = data[o + 2] = cur[i]; data[o + 3] = ch === 2 ? cur[i + 1] : 255; }
    }
    const t = prev; prev = cur; cur = t;
  }
  return {
    width, height, data,
    px(x, y) { const o = ((y | 0) * width + (x | 0)) * 4; return [data[o], data[o + 1], data[o + 2], data[o + 3]]; },
    stats() {
      const n = width * height;
      let r = 0, g = 0, b = 0, lit = 0, min = 255, max = 0;
      const colors = new Set();
      for (let o = 0; o < n * 4; o += 4) {
        const R = data[o], G = data[o + 1], B = data[o + 2], m = Math.max(R, G, B);
        r += R; g += G; b += B;
        if (m > 16) lit++;
        if (m > max) max = m;
        const lo = Math.min(R, G, B);
        if (lo < min) min = lo;
        if (colors.size < 4096) colors.add((R << 16) | (G << 8) | B);
      }
      return { mean: [+(r / n).toFixed(2), +(g / n).toFixed(2), +(b / n).toFixed(2)], min, max, lit: +(lit / n).toFixed(4), colors: colors.size };
    },
  };
}

// ------------------------------------------------------------------------------------------------
// CLI
// ------------------------------------------------------------------------------------------------

const USAGE = [
  'usage:',
  '  node tools/shot.js <url-or-file> [--size 1280x720] [--dpr 1] [--wait 1500] [--wait-for "js"] [--eval "js"]',
  '                     [--settle 0] [--out name.png] [--no-shot] [--allow-errors] [--ignore "regex"] [--timeout 120000]',
  '                     [--headed] [--mobile] [--touch]',
  '      prints { result, errors, logs, shot } as JSON; exit code 1 on page errors (unless --allow-errors) or failure',
  '      --ignore "regex": page errors matching it are reported under logs as "[ignored error] …" and do not fail the run',
  '  node tools/shot.js --run scenario.js [--size WxH] [--dpr 1] [--allow-errors] [--ignore "regex"] [--timeout ms] [--headed] [args…]',
  '      scenario.js: module.exports = async (page, browser, args) => result;  the result is printed as JSON',
  'see docs/TOOLS.md',
].join('\n');

function parseArgs(argv) {
  const valued = new Set(['size', 'dpr', 'wait', 'wait-for', 'eval', 'settle', 'out', 'run', 'timeout', 'ignore']);
  const o = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith('--')) {
      const eq = a.indexOf('=');
      const name = eq > 0 ? a.slice(2, eq) : a.slice(2);
      if (valued.has(name)) {
        if (eq > 0) o[name] = a.slice(eq + 1);
        else if (i + 1 < argv.length) o[name] = argv[++i];
        else throw new Error('missing value for --' + name);
      } else {
        o[name] = true;
      }
    } else {
      o._.push(a);
    }
  }
  return o;
}

async function cli(argv) {
  let a;
  try { a = parseArgs(argv); } catch (e) { process.stderr.write(e.message + '\n' + USAGE + '\n'); process.exit(1); }
  if (a.help || a.h || (!a.run && !a._.length)) {
    process.stdout.write(USAGE + '\n');
    process.exit(a.help || a.h ? 0 : 1);
  }
  const m = /^(\d+)\s*[x,]\s*(\d+)$/i.exec(a.size || '1280x720');
  if (!m) { process.stderr.write('bad --size "' + a.size + '" (expected WxH, e.g. 1280x720)\n'); process.exit(1); }
  const lo = { width: +m[1], height: +m[2], dpr: +a.dpr || 1, headless: !a.headed, timeout: a.timeout !== undefined ? +a.timeout : 120000, mobile: !!a.mobile, touch: !!a.touch };
  if (a.ignore !== undefined) {
    try { lo.ignore = [new RegExp(a.ignore)]; } catch (e) { process.stderr.write('bad --ignore regex: ' + e.message + '\n'); process.exit(1); }
  }
  const allow = !!a['allow-errors'];
  let browser = null;
  let code = 0;
  let stdout = '';

  if (a.run) {
    let ret, failed = null;
    try {
      const fn = require(path.resolve(a.run));
      if (typeof fn !== 'function') throw new Error(a.run + ' must export a function: module.exports = async (page, browser) => result');
      browser = await launch(lo);
      const page = await browser.page();
      ret = await fn(page, browser, a._);
    } catch (e) {
      failed = e;
    }
    const errors = browser ? browser.errors : [];
    if (browser) await browser.close();
    if (failed) {
      process.stderr.write('scenario failed: ' + (failed && failed.stack || failed) + '\n');
      code = 1;
    } else {
      let json;
      try { json = JSON.stringify(ret === undefined ? null : ret, null, 2); } catch (e) { json = undefined; process.stderr.write('scenario result is not JSON-serializable: ' + e.message + '\n'); code = 1; }
      if (json !== undefined) stdout = json + '\n';
    }
    if (errors.length) {
      process.stderr.write('page errors (' + errors.length + ')' + (allow ? ' [allowed]' : '') + ':\n' + errors.map(e => '  ' + e.replace(/\n/g, '\n  ')).join('\n') + '\n');
      if (!allow) code = 1;
    }
  } else {
    const out = { result: null, errors: [], logs: [], shot: null };
    try {
      const target = a._[0];
      browser = await launch(lo);
      const page = await browser.page();
      await page.goto(target);
      if (a['wait-for'] !== undefined) await page.waitFor(a['wait-for'], Math.max(1000, (lo.timeout || 120000) - 5000));
      await page.wait(a.wait !== undefined ? +a.wait : 1500);
      if (a.eval !== undefined) { const v = await page.eval(a.eval); out.result = v === undefined ? null : v; }
      if (+a.settle > 0) await page.wait(+a.settle);
      if (!a['no-shot']) {
        const base = path.basename(String(target).replace(/[?#].*$/, '')).replace(/\.[^.]*$/, '').replace(/[^A-Za-z0-9._-]+/g, '_') || 'page';
        out.shot = await page.shot(a.out || base + '-' + process.pid + '.png');
      }
    } catch (e) {
      out.error = String(e && e.message || e);
      code = 1;
    }
    if (browser) {
      out.errors = browser.errors;
      out.logs = browser.logs;
      await browser.close();
    }
    if (out.errors.length && !allow) code = 1;
    stdout = JSON.stringify(out, (k, v) => typeof v === 'number' && !Number.isFinite(v) ? String(v) : v, 2) + '\n';
  }
  process.stdout.write(stdout, () => process.exit(code));
}

module.exports = { launch, decodePng, toUrl, ROOT, SHOTS };

if (require.main === module) {
  const argv = process.argv.slice(2);
  if (argv[0] === '--watchdog') watchdogMain(argv[1], argv[2], +argv[3] || 0);
  else if (argv[0] === '--janitor') janitorMain();
  else cli(argv).catch(e => { process.stderr.write('shot.js: ' + (e && e.stack || e) + '\n'); process.exit(1); });
}
