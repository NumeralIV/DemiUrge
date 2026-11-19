// Silhouette primitives. All coordinates in screen px (1:1 with the final 1920x1080 sheet).
// D = body colour, A = culture accent, R = light rim for readability, S = ground shadow.
const D = '#17130e', R = 'rgba(255,240,205,.55)', S = 'rgba(0,0,0,.38)';
const P = (d, f = D, extra = '') => `<path d="${d}" fill="${f}" ${extra}/>`;
const shadow = (cx, cy, rx, ry) => `<ellipse cx="${cx}" cy="${cy}" rx="${rx}" ry="${ry}" fill="${S}"/>`;
const rim = (inner) => `<g stroke="${R}" stroke-width=".8" stroke-linejoin="round">${inner}</g>`;
function person(x, y, h, f = D) { // feet at (x,y)
  const hw = h * 0.17, r = h * 0.13;
  return `<circle cx="${x}" cy="${y - h + r}" r="${r}" fill="${f}"/>` + P(`M${x - hw} ${y - h * 0.72} L${x + hw} ${y - h * 0.72} L${x + hw * 0.8} ${y} L${x - hw * 0.8} ${y} Z`, f);
}
function quad(x, y, l, h, f = D, hump = 0) { // four-legged animal, facing right, feet at y
  const by = y - h * 0.45;
  let s = `<ellipse cx="${x}" cy="${by}" rx="${l / 2}" ry="${h * 0.22}" fill="${f}"/>`;
  if (hump) s += `<ellipse cx="${x}" cy="${by - h * 0.22}" rx="${l * 0.2}" ry="${h * 0.18}" fill="${f}"/>`;
  s += `<rect x="${x - l * 0.4}" y="${by}" width="${l * 0.1}" height="${h * 0.45}" fill="${f}"/><rect x="${x + l * 0.3}" y="${by}" width="${l * 0.1}" height="${h * 0.45}" fill="${f}"/>`;
  s += P(`M${x + l * 0.4} ${by - h * 0.05} L${x + l * 0.62} ${by - h * 0.42} L${x + l * 0.72} ${by - h * 0.34} L${x + l * 0.5} ${by + h * 0.05} Z`, f);
  return s;
}
function pennant(x, y, h, A) { return `<line x1="${x}" y1="${y}" x2="${x}" y2="${y - h}" stroke="${D}" stroke-width="1.4"/>` + P(`M${x} ${y - h} L${x + h * 0.55} ${y - h * 0.85} L${x} ${y - h * 0.68} Z`, A); }
function banner(x, y, h, A) { return `<line x1="${x}" y1="${y}" x2="${x}" y2="${y - h}" stroke="${D}" stroke-width="1.4"/><rect x="${x}" y="${y - h}" width="${h * 0.38}" height="${h * 0.42}" fill="${A}"/>`; }
function wheel(x, y, r) { return `<circle cx="${x}" cy="${y}" r="${r}" fill="none" stroke="${D}" stroke-width="1.6"/>`; }

const U = {
  // ---------- ANCIENT ----------
  settlerAncient(A) { let s = shadow(70, 41, 62, 4); for (let i = 0; i < 6; i++) s += person(14 + i * 9, 40 - (i % 2) * 2, 15); s += `<rect x="18" y="20" width="7" height="6" fill="${A}"/>`;
    s += quad(98, 40, 18, 16) + `<rect x="104" y="20" width="18" height="10" rx="2" fill="#6a4a2a"/>` + wheel(113, 34, 5) + pennant(28, 26, 20, A); return [130, 46, s]; },
  traderAncient(A) { let s = shadow(64, 37, 60, 4); for (let i = 0; i < 4; i++) { const x = 14 + i * 28; s += quad(x, 36, 20, 18) + `<rect x="${x - 8}" y="${36 - 18 * 0.75}" width="16" height="8" rx="3" fill="${A}"/>`; } s += person(120, 36, 15); return [130, 42, s]; },
  armyAncient(A) { let s = shadow(48, 41, 46, 5); for (let r = 0; r < 3; r++) for (let c = 0; c < 8; c++) s += person(14 + c * 9 + r * 3, 40 - r * 5, 13); for (let c = 0; c < 8; c++) s += `<line x1="${17 + c * 9}" y1="${20}" x2="${15 + c * 9}" y2="${10}" stroke="${D}" stroke-width="1"/>`; s += pennant(48, 22, 22, A); return [100, 46, s]; },
  prophetAll(A) { let s = shadow(40, 44, 36, 4) + `<circle cx="18" cy="24" r="13" fill="none" stroke="#ffd98a" stroke-width="1.6" opacity=".9"/>` + person(18, 43, 24, '#efe6d0') + `<line x1="25" y1="43" x2="27" y2="15" stroke="${D}" stroke-width="1.6"/>`;
    for (let i = 0; i < 4; i++) s += person(38 + i * 9, 43, 14); s += `<rect x="12" y="30" width="12" height="3" fill="${A}"/>`; return [80, 48, s]; },
  boatAncient(A) { let s = shadow(48, 30, 46, 4) + P('M6 18 Q10 30 30 30 L66 30 Q86 30 92 16 Q84 24 64 24 L32 24 Q14 24 6 18 Z', '#b08a4a') + rim(P('M6 18 Q10 30 30 30 L66 30 Q86 30 92 16 Q84 24 64 24 L32 24 Q14 24 6 18 Z','none')) + person(36, 24, 11) + person(52, 24, 11) + person(66, 24, 11) + `<rect x="22" y="18" width="6" height="6" fill="${A}"/>` + `<path d="M0 34 Q48 40 100 34" stroke="rgba(255,255,255,.55)" stroke-width="1.2" fill="none"/>`; return [100, 40, s]; },
  // ---------- CLASSICAL ----------
  settlerClassical(A) { let s = shadow(64, 41, 60, 4) + quad(18, 40, 18, 18) + `<rect x="30" y="26" width="34" height="9" fill="#5a3d22"/>` + P('M30 26 Q47 8 64 26 Z', '#e8dcc2') + `<rect x="30" y="22" width="34" height="3" fill="${A}"/>` + wheel(36, 37, 4.5) + wheel(58, 37, 4.5);
    for (let i = 0; i < 4; i++) s += person(76 + i * 9, 40, 15); return [118, 46, s]; },
  traderClassical(A) { let s = shadow(70, 39, 66, 4); for (let i = 0; i < 4; i++) { const x = 14 + i * 32; s += quad(x, 38, 22, 24, D, 1) + `<rect x="${x - 7}" y="${38 - 24 * 0.85}" width="14" height="6" rx="2" fill="${A}"/>`; } return [140, 44, s]; },
  armyClassical(A) { let s = shadow(70, 46, 66, 5) + P('M6 44 L120 44 L132 30 L18 30 Z') + rim(P('M6 44 L120 44 L132 30 L18 30 Z', 'none'));
    for (let r = 0; r < 3; r++) for (let c = 0; c < 22; c++) s += `<circle cx="${10 + c * 5 + r * 4}" cy="${42 - r * 4.6}" r="1.1" fill="rgba(230,220,200,.75)"/>`;
    s += banner(30, 30, 26, A) + banner(66, 30, 26, A) + banner(102, 30, 26, A) + quad(140, 44, 14, 16) + quad(156, 44, 14, 16); return [166, 50, s]; },
  galley(A) { let s = shadow(60, 36, 58, 4) + P('M4 26 L108 26 Q114 26 118 20 L112 32 L10 32 Q4 30 4 26 Z'); for (let i = 0; i < 12; i++) s += `<line x1="${16 + i * 7.5}" y1="32" x2="${12 + i * 7.5}" y2="40" stroke="${D}" stroke-width="1"/>`;
    s += `<line x1="58" y1="26" x2="58" y2="4" stroke="${D}" stroke-width="1.6"/><rect x="46" y="6" width="24" height="16" fill="#e8dcc2"/><rect x="46" y="11" width="24" height="5" fill="${A}"/>` + `<path d="M118 30 Q60 46 0 38" stroke="rgba(255,255,255,.6)" stroke-width="1.4" fill="none"/>`; return [122, 46, s]; },
  sailship(A) { let s = shadow(48, 50, 44, 4) + P('M6 38 L88 38 L80 48 L14 48 Z'); [24, 46, 68].forEach((x, i) => { s += `<line x1="${x}" y1="38" x2="${x}" y2="${6 + i * 2}" stroke="${D}" stroke-width="1.4"/><path d="M${x - 9} ${10 + i * 2} Q${x} ${7 + i * 2} ${x + 9} ${10 + i * 2} L${x + 8} ${34} Q${x} ${31} ${x - 8} 34 Z" fill="#efe6d0"/>`; });
    s += pennant(46, 8, 12, A) + `<rect x="14" y="40" width="70" height="2.5" fill="${A}"/>` + `<path d="M0 52 Q46 56 96 50" stroke="rgba(255,255,255,.6)" stroke-width="1.4" fill="none"/>`; return [96, 58, s]; },
  // ---------- MODERN ----------
  settlerModern(A) { let s = shadow(64, 36, 60, 4); for (let i = 0; i < 3; i++) { const x = 6 + i * 42; s += `<rect x="${x}" y="18" width="36" height="14" rx="3" fill="#d9d2c2"/><rect x="${x}" y="22" width="36" height="3" fill="${A}"/>` + wheel(x + 8, 33, 3.2) + wheel(x + 28, 33, 3.2); } return [134, 40, s]; },
  train(A) { let s = shadow(80, 36, 78, 4) + `<rect x="2" y="20" width="26" height="13" rx="2" fill="${D}"/><rect x="18" y="12" width="8" height="9" fill="${D}"/><rect x="5" y="15" width="5" height="6" fill="${D}"/>` + `<circle cx="10" cy="9" r="4" fill="rgba(235,230,220,.8)"/><circle cx="4" cy="4" r="3" fill="rgba(235,230,220,.6)"/>`;
    for (let i = 0; i < 5; i++) s += `<rect x="${31 + i * 26}" y="20" width="24" height="12" fill="#3a2b20"/><rect x="${31 + i * 26}" y="20" width="24" height="2.5" fill="${A}"/>`; s += `<line x1="0" y1="35" x2="162" y2="35" stroke="#2a2a2a" stroke-width="1.4"/>`; return [162, 40, s]; },
  trucks(A) { let s = shadow(80, 30, 78, 4); for (let i = 0; i < 6; i++) { const x = 4 + i * 26; s += `<rect x="${x}" y="16" width="16" height="10" fill="#4b4f2e"/><rect x="${x + 16}" y="18" width="6" height="8" fill="#3a3d24"/>` + wheel(x + 5, 27, 2.6) + wheel(x + 17, 27, 2.6); } s += pennant(10, 16, 14, A); return [162, 34, s]; },
  freighter(A) { let s = shadow(80, 34, 78, 4) + P('M2 22 L150 22 L144 32 L8 32 Z', '#2b3138'); const cols = ['#b4492f', '#2f5f8a', '#c9a23a', '#6b7a3a', '#9a3a3a', '#3a6f6f']; for (let i = 0; i < 9; i++) s += `<rect x="${12 + i * 12}" y="${14 + (i % 2) * 2}" width="11" height="${8 - (i % 2) * 2}" fill="${cols[i % 6]}"/>`;
    s += `<rect x="124" y="8" width="14" height="14" fill="#e8e4dc"/><rect x="128" y="2" width="5" height="7" fill="${A}"/>` + `<path d="M0 34 Q80 42 160 32" stroke="rgba(255,255,255,.6)" stroke-width="1.4" fill="none"/>`; return [160, 42, s]; },
  steamship(A) { let s = shadow(56, 34, 52, 4) + P('M2 24 L104 24 L96 32 L8 32 Z') + `<rect x="30" y="14" width="40" height="10" fill="#e8e4dc"/><rect x="46" y="4" width="8" height="12" fill="${A}"/><rect x="46" y="4" width="8" height="3" fill="${D}"/>` + `<circle cx="38" cy="2" r="4" fill="rgba(60,55,50,.7)"/><circle cx="28" cy="0" r="5" fill="rgba(60,55,50,.5)"/>`; return [108, 38, s]; },
  aircraft(A) { const plane = (x, y, k, f) => P(`M${x} ${y} L${x + 30 * k} ${y - 2 * k} L${x + 36 * k} ${y} L${x + 30 * k} ${y + 2 * k} Z M${x + 12 * k} ${y} L${x + 18 * k} ${y - 16 * k} L${x + 22 * k} ${y - 16 * k} L${x + 20 * k} ${y} L${x + 22 * k} ${y + 16 * k} L${x + 18 * k} ${y + 16 * k} Z M${x + 2 * k} ${y} L${x + 4 * k} ${y - 7 * k} L${x + 7 * k} ${y - 7 * k} L${x + 7 * k} ${y} L${x + 7 * k} ${y + 7 * k} L${x + 4 * k} ${y + 7 * k} Z`, f);
    return [96, 56, plane(50, 46, 1, 'rgba(0,0,0,.35)') + plane(30, 22, 1, '#e8e4dc') + `<rect x="${30 + 3}" y="${22 - 1}" width="4" height="2" fill="${A}"/><path d="M30 22 L0 22" stroke="rgba(255,255,255,.7)" stroke-width="1.4"/>`]; },
  // ---------- SPACE ----------
  transport(A) { let s = `<ellipse cx="50" cy="44" rx="30" ry="4" fill="${S}"/>` + P('M10 20 Q40 6 74 16 L80 22 Q40 30 10 24 Z', '#e8e4dc') + `<rect x="30" y="17" width="26" height="2.5" fill="${A}"/><circle cx="16" cy="25" r="3.5" fill="#8fd0ff"/><circle cx="72" cy="22" r="3.5" fill="#8fd0ff"/><circle cx="16" cy="25" r="7" fill="rgba(143,208,255,.25)"/>`; return [86, 48, s]; },
  maglev(A) { let s = `<rect x="0" y="28" width="170" height="4" fill="#5a5a5a"/>` + `<ellipse cx="85" cy="40" rx="80" ry="3" fill="${S}"/>`; for (let i = 0; i < 4; i++) s += `<rect x="${6 + i * 38}" y="16" width="36" height="11" rx="${i === 0 ? 6 : 2}" fill="#eceae4"/><rect x="${6 + i * 38}" y="22" width="36" height="2" fill="${A}"/>`; return [170, 44, s]; },
  rocket(A) { let s = `<path d="M20 60 Q14 46 22 36 Q30 46 24 60 Z" fill="#fff1c9"/><circle cx="22" cy="40" r="7" fill="rgba(255,217,138,.45)"/>` + P('M17 34 L17 10 Q22 0 27 10 L27 34 Z', '#eceae4') + `<rect x="17" y="14" width="10" height="3" fill="${A}"/>` + `<path d="M22 60 Q30 66 46 64" stroke="rgba(240,240,240,.6)" stroke-width="5" fill="none"/>`; return [50, 66, s]; },
  ark(A) { let s = `<ellipse cx="30" cy="62" rx="26" ry="3" fill="${S}"/>`; for (let i = 0; i < 6; i++) s += `<line x1="${8 + i * 2}" y1="60" x2="${8 + i * 2}" y2="${4 + i}" stroke="#3b3b3b" stroke-width=".8"/>`;
    s += `<rect x="6" y="4" width="12" height="56" fill="none" stroke="#3b3b3b" stroke-width="1"/>` + P('M24 60 L24 14 Q31 -2 38 14 L38 60 Z', '#eceae4') + `<rect x="24" y="22" width="14" height="3" fill="${A}"/><rect x="24" y="42" width="14" height="3" fill="#e8cd96"/>`; return [50, 66, s]; },
  gunship(A) { let s = `<ellipse cx="46" cy="44" rx="30" ry="4" fill="${S}"/>` + P('M6 22 L40 10 L82 18 L86 24 L40 30 Z', '#3a3f46') + rim(P('M6 22 L40 10 L82 18 L86 24 L40 30 Z','none')) + P('M36 12 L48 2 L54 4 L46 14 Z', A) + `<circle cx="12" cy="24" r="3.5" fill="#8fd0ff"/><circle cx="12" cy="24" r="7" fill="rgba(143,208,255,.25)"/>`; return [90, 48, s]; },
  seaSpace(A) { let s = shadow(70, 30, 66, 4) + P('M2 22 Q70 12 136 18 L140 22 Q70 30 2 26 Z', '#eceae4') + `<rect x="30" y="17" width="60" height="2.5" fill="${A}"/>` + `<path d="M0 30 Q70 40 140 28" stroke="rgba(255,255,255,.7)" stroke-width="1.6" fill="none"/>`; return [140, 38, s]; },
  // ---------- WILDLIFE ----------
  herd() { let s = shadow(70, 40, 68, 5); for (let i = 0; i < 22; i++) { const x = 8 + (i * 37) % 128, y = 26 + ((i * 11) % 14); s += quad(x, y, 10, 10, '#2b2018'); } return [140, 46, s]; },
  deer() { let s = shadow(34, 34, 30, 4); [[10, 32], [28, 30], [46, 33]].forEach(([x, y]) => s += quad(x, y, 12, 14, '#5a3a22') + `<path d="M${x + 7} ${y - 13} l2 -5 M${x + 7} ${y - 13} l-1 -5" stroke="#5a3a22" stroke-width="1"/>`); return [64, 38, s]; },
  flock() { let s = ''; for (let i = 0; i < 70; i++) { const a = i * 2.4, r = 3 + (i % 17) * 1.4; s += `<circle cx="${40 + Math.cos(a) * r * 1.8}" cy="${22 + Math.sin(a) * r * 0.8}" r="1.1" fill="#17130e"/>`; }
    for (let i = 0; i < 7; i++) { const x = 92 + i * 6, y = 14 + Math.abs(i - 3) * 5; s += `<path d="M${x - 4} ${y - 2} L${x} ${y} L${x + 4} ${y - 2}" stroke="#f5f2ea" stroke-width="1.6" fill="none"/>`; } return [140, 44, s]; },
  whale() { let s = `<ellipse cx="44" cy="24" rx="40" ry="8" fill="rgba(20,40,55,.55)"/>` + P('M8 24 Q30 12 70 20 L84 14 L80 24 L84 34 L70 28 Q30 36 8 24 Z', '#1c2a35') + `<path d="M30 28 L22 40 M30 20 L22 8" stroke="#e8edf0" stroke-width="2.4" stroke-linecap="round"/><circle cx="12" cy="18" r="3" fill="rgba(255,255,255,.55)"/>`; return [90, 44, s]; },
  shoal() { let s = ''; for (let i = 0; i < 160; i++) { const a = i * 0.39, r = (i % 40) * 0.7; s += `<rect x="${40 + Math.cos(a) * r}" y="${22 + Math.sin(a) * r * 0.75}" width="2.2" height=".9" fill="${i % 5 ? 'rgba(20,30,40,.75)' : 'rgba(230,240,245,.9)'}" transform="rotate(${a * 57 + 90} ${40 + Math.cos(a) * r} ${22 + Math.sin(a) * r * 0.75})"/>`; } return [84, 46, s]; },
};
function svgOf(fn, A, scale = 1) { const [w, h, s] = U[fn](A); return `<svg width="${w * scale}" height="${h * scale}" viewBox="0 0 ${w} ${h}" style="overflow:visible;display:block">${s}</svg>`; }
