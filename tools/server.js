// DEMIURGE — tools/server.js : static dev server for the project root, plus POST /__save.
//   node tools/server.js [port]      default port 5174; the PORT environment variable or a numeric first
//                                    argument overrides it (the argument wins); port 0 = any free port
//   GET  /path                       serves <project>/path with no-cache headers and the right MIME type
//   POST /__save?name=<file>         writes the request body to shots/<file> (name sanitized to [A-Za-z0-9._-]);
//                                    a body that is a data: URL is decoded first. Replies { ok, file, path, bytes }.
// Module:
//   const { start } = require('./server.js');
//   const s = await start({ port: 0, quiet: true });   // → { server, port, url, root, close() }
// Documentation: docs/TOOLS.md
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const DEFAULT_PORT = 5174;
const MAX_BODY = 256 * 1024 * 1024;

const MIME = {
  '.html': 'text/html; charset=utf-8', '.htm': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8', '.map': 'application/json; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8', '.md': 'text/markdown; charset=utf-8', '.csv': 'text/csv; charset=utf-8',
  '.glsl': 'text/plain; charset=utf-8', '.vert': 'text/plain; charset=utf-8', '.frag': 'text/plain; charset=utf-8',
  '.xml': 'application/xml; charset=utf-8', '.svg': 'image/svg+xml',
  '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.gif': 'image/gif', '.webp': 'image/webp',
  '.ico': 'image/x-icon', '.bmp': 'image/bmp', '.avif': 'image/avif',
  '.wav': 'audio/wav', '.mp3': 'audio/mpeg', '.ogg': 'audio/ogg', '.mp4': 'video/mp4', '.webm': 'video/webm',
  '.woff': 'font/woff', '.woff2': 'font/woff2', '.ttf': 'font/ttf', '.otf': 'font/otf',
  '.wasm': 'application/wasm', '.bin': 'application/octet-stream', '.pdf': 'application/pdf',
};

const NO_CACHE = {
  'Cache-Control': 'no-store, no-cache, must-revalidate, max-age=0',
  'Pragma': 'no-cache',
  'Expires': '0',
};
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, HEAD, POST, OPTIONS',
  'Access-Control-Allow-Headers': '*',
};
const DEVICE_RE = /^(con|prn|aux|nul|com\d|lpt\d)(\..*)?$/i;

function send(res, status, body, headers) {
  const buf = Buffer.isBuffer(body) ? body : Buffer.from(String(body), 'utf8');
  res.writeHead(status, Object.assign({ 'Content-Type': 'text/plain; charset=utf-8', 'Content-Length': buf.length }, NO_CACHE, CORS, headers || {}));
  res.end(buf);
}

function sendJson(res, status, obj) {
  send(res, status, JSON.stringify(obj), { 'Content-Type': 'application/json; charset=utf-8' });
}

/** Reduce a client-supplied file name to [A-Za-z0-9._-], never empty, never dot-leading, never a device name. */
function sanitizeName(name) {
  let n = String(name || '').replace(/[^A-Za-z0-9._-]/g, '_').replace(/^\.+/, '').slice(0, 120);
  if (!n || /^_*$/.test(n)) n = 'save-' + Date.now() + '.bin';
  if (DEVICE_RE.test(n)) n = '_' + n;
  return n;
}

/** If `buf` holds a data: URL return the decoded payload, otherwise return `buf` unchanged. */
function decodeBody(buf) {
  if (buf.length < 5 || buf.toString('latin1', 0, 5).toLowerCase() !== 'data:') return buf;
  const comma = buf.indexOf(0x2c);
  if (comma < 0) return buf;
  const head = buf.toString('latin1', 5, comma);
  const payload = buf.toString('latin1', comma + 1);
  if (/;base64\s*$/i.test(head)) return Buffer.from(payload.replace(/\s+/g, ''), 'base64');
  try { return Buffer.from(decodeURIComponent(payload), 'utf8'); } catch (e) { return Buffer.from(payload, 'latin1'); }
}

function handleSave(req, res, url, root, log) {
  if (req.method !== 'POST') return send(res, 405, 'POST only\n', { Allow: 'POST, OPTIONS' });
  const name = sanitizeName(url.searchParams.get('name'));
  const chunks = [];
  let size = 0, aborted = false;
  req.on('data', c => {
    if (aborted) return;
    size += c.length;
    if (size > MAX_BODY) { aborted = true; send(res, 413, 'body too large\n'); req.destroy(); return; }
    chunks.push(c);
  });
  req.on('error', () => { aborted = true; });
  req.on('end', () => {
    if (aborted) return;
    try {
      const data = decodeBody(Buffer.concat(chunks));
      const dir = path.join(root, 'shots');
      fs.mkdirSync(dir, { recursive: true });
      const file = path.join(dir, name);
      fs.writeFileSync(file, data);
      log('saved shots/' + name + ' (' + data.length + ' bytes)');
      sendJson(res, 200, { ok: true, file: 'shots/' + name, path: file, bytes: data.length });
    } catch (e) {
      sendJson(res, 500, { ok: false, error: e.message });
    }
  });
}

function listing(dir, urlPath) {
  const esc = s => s.replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const items = fs.readdirSync(dir, { withFileTypes: true })
    .sort((a, b) => (b.isDirectory() - a.isDirectory()) || a.name.localeCompare(b.name))
    .map(d => { const n = d.name + (d.isDirectory() ? '/' : ''); return '<li><a href="' + esc(encodeURIComponent(d.name) + (d.isDirectory() ? '/' : '')) + '">' + esc(n) + '</a></li>'; });
  return '<!doctype html><meta charset="utf-8"><title>' + esc(urlPath) + '</title><body style="font:14px/1.6 monospace;background:#111;color:#ddd">' +
    '<h3>' + esc(urlPath) + '</h3><ul>' + (urlPath !== '/' ? '<li><a href="../">../</a></li>' : '') + items.join('') + '</ul><style>a{color:#e8c66a}</style>';
}

function handleStatic(req, res, url, root) {
  if (req.method !== 'GET' && req.method !== 'HEAD') return send(res, 405, 'method not allowed\n', { Allow: 'GET, HEAD, POST, OPTIONS' });
  let rel;
  try { rel = decodeURIComponent(url.pathname); } catch (e) { return send(res, 400, 'bad request\n'); }
  if (rel.indexOf('\0') >= 0) return send(res, 400, 'bad request\n');
  const file = path.join(root, rel);
  if (file !== root && !file.startsWith(root + path.sep)) return send(res, 403, 'forbidden\n');
  if (path.relative(root, file).split(path.sep).some(seg => DEVICE_RE.test(seg))) return send(res, 404, 'not found\n');
  let target = file, st;
  try { st = fs.statSync(target); } catch (e) {
    // browsers ask for /favicon.ico on their own; a 404 would show up as a page error in every test
    if (url.pathname === '/favicon.ico') { res.writeHead(204, Object.assign({}, NO_CACHE, CORS)); return res.end(); }
    return send(res, 404, 'not found: ' + url.pathname + '\n');
  }
  if (st.isDirectory()) {
    if (!url.pathname.endsWith('/')) return send(res, 301, '', { Location: url.pathname + '/' + url.search });
    const index = path.join(target, 'index.html');
    if (fs.existsSync(index)) { target = index; st = fs.statSync(target); }
    else return send(res, 200, req.method === 'HEAD' ? '' : listing(target, url.pathname), { 'Content-Type': 'text/html; charset=utf-8' });
  }
  if (!st.isFile()) return send(res, 404, 'not found\n');
  const type = MIME[path.extname(target).toLowerCase()] || 'application/octet-stream';
  res.writeHead(200, Object.assign({ 'Content-Type': type, 'Content-Length': st.size }, NO_CACHE, CORS));
  if (req.method === 'HEAD') return res.end();
  const stream = fs.createReadStream(target);
  stream.on('error', () => res.destroy());
  stream.pipe(res);
}

/**
 * Start the server. opts: { port = 5174 (0 = any free port), root = project root, host = '127.0.0.1', quiet = false }
 * Resolves with { server, port, url, root, close() } once it is listening; rejects if the port is taken.
 */
function start(opts) {
  const o = Object.assign({ port: DEFAULT_PORT, root: ROOT, host: '127.0.0.1', quiet: false }, opts || {});
  const root = path.resolve(o.root);
  const log = o.quiet ? () => {} : msg => console.log(msg);
  const server = http.createServer((req, res) => {
    let url;
    try { url = new URL(req.url, 'http://127.0.0.1'); } catch (e) { return send(res, 400, 'bad request\n'); }
    res.on('finish', () => log(req.method + ' ' + req.url + ' ' + res.statusCode));
    if (req.method === 'OPTIONS') { res.writeHead(204, Object.assign({ 'Content-Length': 0 }, CORS)); return res.end(); }
    try {
      if (url.pathname === '/__save') return handleSave(req, res, url, root, log);
      return handleStatic(req, res, url, root);
    } catch (e) {
      if (!res.headersSent) send(res, 500, 'server error: ' + e.message + '\n'); else res.destroy();
    }
  });
  return new Promise((resolve, reject) => {
    server.once('error', e => reject(e.code === 'EADDRINUSE'
      ? new Error('port ' + o.port + ' is already in use on ' + o.host + ' (another dev server is probably running; pass a different port, or 0 for any free port)')
      : e));
    server.listen(o.port, o.host, () => {
      const port = server.address().port;
      resolve({
        server, port, root,
        url: 'http://' + o.host + ':' + port + '/',
        close: () => new Promise(res => { server.close(() => res()); if (server.closeAllConnections) server.closeAllConnections(); }),
      });
    });
  });
}

module.exports = { start, sanitizeName, decodeBody, MIME, ROOT, DEFAULT_PORT };

if (require.main === module) {
  const arg = process.argv[2];
  const port = arg !== undefined && /^\d+$/.test(arg) ? +arg : process.env.PORT !== undefined && /^\d+$/.test(process.env.PORT) ? +process.env.PORT : DEFAULT_PORT;
  start({ port }).then(s => {
    console.log('DEMIURGE dev server: ' + s.url + '   (root: ' + s.root + ')');
    console.log('POST ' + s.url + '__save?name=<file> writes the body to shots/<file>.  Ctrl+C to stop.');
  }).catch(e => { console.error('server.js: ' + e.message); process.exit(1); });
}
