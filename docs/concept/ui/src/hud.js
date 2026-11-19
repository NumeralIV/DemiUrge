// Shared builders for the DEMIURGE HUD mockups (T-007, Grok).
const C = { U:'#e69f00', V:'#56b4e9', Q:'#009e73', T:'#cc79a7' };
const POW = {
  Shape:[['raise','Raise','U','6/s'],['lower','Lower','J','6/s']],
  Sky:[['rain','Rain','N','5/s'],['bloom','Bloom','B','6/s']],
  Wrath:[['lightning','Lightning','L','15'],['quake','Quake','Z','60'],['plague','Plague','X','80'],['meteor','Meteor','M','150'],['volcano','Volcano','V','220']],
  Grace:[['heal','Heal','T','40'],['inspire','Inspire','I','60'],['prophet','Prophet','P','120'],['peace','Peace','O','100'],['awaken','Awaken','K','250']],
};
const h = (html) => { const d = document.createElement('div'); d.innerHTML = html; while (d.firstChild) document.body.appendChild(d.firstChild); };
function ring(p, icon){ const r=21, c=2*Math.PI*r; return `<div class="ring"><svg class="prog" width="48" height="48" viewBox="0 0 48 48"><circle cx="24" cy="24" r="${r}" fill="none" stroke="rgba(232,205,150,.18)" stroke-width="2.5"/><circle cx="24" cy="24" r="${r}" fill="none" stroke="#e8cd96" stroke-width="2.5" stroke-linecap="round" stroke-dasharray="${c*p} ${c}" transform="rotate(-90 24 24)"/></svg>${svg(icon,24)}</div>`; }
function topbar(o){
  let L = `<div class="tb" style="left:24px"><div class="btn icon" id="hud-menu"><svg class="ic" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"><path d="M4 7h16M4 12h16M4 17h16"/></svg></div>
    <div class="body" style="font-size:26px;font-weight:500;color:var(--gold-hi);letter-spacing:.03em">${o.world}</div><div class="sep"></div>
    ${ring(o.ageP, o.ageIcon)}<div><div class="serif num" id="hud-year" style="font-size:22px;font-weight:600;line-height:1">${o.year}</div><div id="hud-age" class="small-caps" style="margin-top:4px">${o.age}</div></div>`;
  if (o.stats) L += `<div class="sep"></div>` + o.stats.map(([i,v,t])=>`<div class="stat" title="${t}">${svg(i,20)}<span class="num">${v}</span></div>`).join('');
  L += `</div>`;
  let M = '';
  if (o.awe) M = `<div class="tb" style="left:50%;transform:translateX(-50%);top:10px;gap:12px"><div class="orb"></div><div><div style="display:flex;align-items:baseline;gap:10px"><span id="hud-awe" class="num" style="font-size:26px;font-weight:600;color:var(--awe)">${o.awe}</span><span class="num dim" style="font-size:13px">of ${o.aweMax}</span><span class="num good" style="font-size:14px">+${o.inc} / yr</span></div>
    <div style="display:flex;align-items:center;gap:8px;margin-top:1px"><span class="serif" style="font-style:italic;font-size:18px;color:var(--text)">${o.epithet}</span><span style="color:var(--line-hi)">·</span><span style="display:flex;align-items:center;gap:4px;color:var(--gold)">${svg(o.regardIcon,15)}<span class="serif" style="font-size:17px;font-weight:600">${o.regard}</span></span></div></div></div>`;
  let R = `<div class="tb" style="right:24px">`;
  if (o.ark) R += `<div style="width:236px;cursor:pointer"><div style="display:flex;justify-content:space-between;align-items:center"><span class="serif" style="font-size:16px;font-style:italic;color:var(--text)">They will leave you ${o.ark[1]}</span><span style="color:var(--gold)">${svg('age6',16)}</span></div><div class="bar" style="margin-top:6px"><i style="width:${o.ark[0]}%"></i></div><div class="label-sm" style="font-size:12px;margin-top:3px">The Ark at Tel Asha · ${o.ark[0]} %</div></div><div class="sep"></div>`;
  if (o.speed !== undefined) R += `<div class="tc">` + [['0','pause'],['0.25','linger'],['1','play'],['2','fast'],['4','faster'],['8','fastest']].map(([k,i])=>`<div class="btn ${o.speed==k?'on':''}" data-speed="${k}">${svg(i,18)}</div>`).join('') + `</div>`;
  R += `</div>`;
  h(`<div class="topbar"></div>${L}${M}${R}`);
}
function palette(o){
  let s = `<div class="palette">`;
  for (const [g, list] of Object.entries(POW)) {
    s += `<div class="group"><div class="gname">${g.toUpperCase()}</div><div class="row">` + list.map(([id,nm,key,cost])=>{
      const st = (o.state||{})[id]||'';
      return `<div class="pw ${st}" data-power="${id}">${st=='locked'?`<span class="lock">${svg('lock',13)}</span>`:''}<span class="key num">${key}</span>${svg(id,26)}<span class="nm">${nm}</span><span class="ct num">${svg('awe',12)}${cost}</span></div>`;}).join('') + `</div></div>`;
  }
  h(s + `</div>`);
}
function label(x,y,name,c,opts={}){
  const big = opts.big ? 'font-size:21px' : 'font-size:18px';
  const glyph = (opts.glyphs||[]).map(g=>`<span class="g">${svg(g,15)}</span>`).join('');
  h(`<div class="plabel" style="left:${x-9}px;top:${y-9}px;${big}"><span class="sigil" style="background:${C[c]}">${c}</span><span>${name}</span>${glyph}${opts.cap?`<span style="color:var(--gold)">${svg('culture',13)}</span>`:''}</div>`);
}
function cursor(x,y){ h(`<svg class="abs" style="left:${x}px;top:${y}px" width="22" height="28" viewBox="0 0 22 28"><path d="M2 2v20l5.5-5 3.5 8 3-1.3-3.4-7.8H18z" fill="#fff" stroke="#0b0f17" stroke-width="1.5" stroke-linejoin="round"/></svg>`); }
