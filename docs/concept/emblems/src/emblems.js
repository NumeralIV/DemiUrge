// DEMIURGE culture emblems (T-019, Grok). Same line style as the UI icons (docs/concept/ui/src/icons.js):
// 24x24 viewBox, stroke currentColor, 1.6 stroke, round caps and joins, no fills.
// Only exception: small dots drawn as r <= 1.2 circles, which the sheet fills with currentColor.
const EMBLEMS = [
  ['swell',    'M3 15c2.5-5.5 9-8 13-5 2.2 1.6 1.6 4.8-1 4.8-1.6 0-2.4-1.4-1.6-2.6M3 19c2-1.6 4-1.6 6 0s4 1.6 6 0 4-1.6 6 0'],
  ['anvil',    'M7 9.5 12 4.5l5 5M5 12.5h12c0 2-1.8 3.2-4 3.2V18h3v2.5H8V18h3v-2.3c-3.2 0-6-1.2-6-3.2z'],
  ['birch',    'M12 3v12.5M12 7.5l3-2M12 10.5l-3-2', 'ellipse cx="12" cy="17.5" rx="7.5" ry="2.4"'],
  ['starroad', 'M3 19c3-3 6-3 9 0s6 3 9 0M4.5 14a7.5 7.5 0 0 1 15 0', 'circle cx="6.7" cy="8.7" r="1.1"', 'circle cx="12" cy="6.5" r="1.1"', 'circle cx="17.3" cy="8.7" r="1.1"'],
  ['sail',     'M11.5 16.5C9.5 11 11 6 16.5 3c-1 5 0 9.5 2.5 12.5zM3 18h18c-1.2 2-3 3-5 3H8c-2 0-3.8-1-5-3z'],
  ['tree',     'M12 21v-8.5M12 16.5l-3-2.5M12 16.5l3-2.5M7 21h10M4.2 11.5C2.6 9 4.8 6.4 7.6 7.2 8.6 4.6 15.4 4.6 16.4 7.2c2.8-.8 5 1.8 3.4 4.3-1 1.4-4.2 2-7.8 2s-6.8-.6-7.8-2z'],
  ['prow',     'M3 14h14c2 0 3-1.8 3-4.5V6.5c0-1.6-1.6-2.4-2.7-1.6-1 .8-.6 2.4.7 2.4M3 14l2.5 5h10l2.2-4.6M8 14v-3M12 14v-3M3 21.5c2-1 4-1 6 0s4 1 6 0 4-1 6 0'],
  ['lamp',     'M3.5 13c.5 3 4 4.5 8.5 4.5s7.5-1.5 8-4.5zM20 13l1.5-2.5M12 13v-1.5M12 9.5c1.3-1.2 1.3-3 0-5-1.3 2-1.3 3.8 0 5zM8.5 21h7M12 17.5V21'],
  ['spire',    'M12 7.3V3', 'ellipse cx="12" cy="18" rx="7.5" ry="2.2"', 'ellipse cx="12" cy="13" rx="5" ry="1.7"', 'ellipse cx="12" cy="8.8" rx="2.8" ry="1.3"'],
  ['terraces', 'M3 21h18V5h-3v4h-5v4H8v4H3zM5.5 21v-1.8a1.2 1.2 0 0 1 2.4 0V21'],
  ['braid',    'M3 9c3 0 3 6 6 6s3-6 6-6 3 6 6 6M3 15c3 0 3-6 6-6s3 6 6 6 3-6 6-6'],
  ['antlers',  'M12 13.5c-4 0-6-3-6-8.5M6 9.5 3.5 7.5M7.6 7 7 4M12 13.5c4 0 6-3 6-8.5M18 9.5l2.5-2M16.4 7 17 4M10 13.5h4l-1 5h-2z'],
  ['sheaf',    'M12 21V8M12 21 8.5 10M12 21l3.5-11M9.5 15.5h5', 'ellipse cx="12" cy="5" rx="1.3" ry="2.6"', 'ellipse cx="8" cy="7.2" rx="1.3" ry="2.6" transform="rotate(-20 8 7.2)"', 'ellipse cx="16" cy="7.2" rx="1.3" ry="2.6" transform="rotate(20 16 7.2)"'],
  ['rainstair','M7 11h10a3 3 0 0 0 0-6 4.5 4.5 0 0 0-8.6-1A3 3 0 0 0 7 11zM9 13.5v7.5M15 13.5v7.5M9 16h6M9 19h6'],
  ['hearth',   'M12 3.5c3 3 3.6 6 2.6 8.6-.7 1.8-4.5 1.8-5.2 0C8.4 9.5 9 6.5 12 3.5z', 'circle cx="4.5" cy="16" r="1.2"', 'circle cx="8" cy="18.8" r="1.2"', 'circle cx="12" cy="19.8" r="1.2"', 'circle cx="16" cy="18.8" r="1.2"', 'circle cx="19.5" cy="16" r="1.2"'],
  ['hive',     'M5 20.5V14a7 7 0 0 1 14 0v6.5zM3.5 20.5h17M5.6 11h12.8M12 7V4M10.5 20.5V18a1.5 1.5 0 0 1 3 0v2.5'],
];
function inner(e) {
  return e.slice(1).map((p, i) => i === 0 && p.startsWith('M') ? `<path d="${p}"/>` : `<${p}/>`).join('');
}
function svg(e, size = 24, dotsFilled = true) {
  const body = inner(e).replace(/<circle ([^>]*r="1(\.\d)?")\/>/g, dotsFilled ? '<circle $1 fill="currentColor"/>' : '<circle $1/>');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">${body}</svg>`;
}
if (typeof module !== 'undefined') module.exports = { EMBLEMS, svg };
