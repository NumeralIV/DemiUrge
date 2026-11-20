// node docs/concept/emblems/src/write-svgs.js  -> writes ../emblem-01.svg … ../emblem-16.svg
const fs = require('fs'), path = require('path');
const { EMBLEMS, svg } = require('./emblems.js');
EMBLEMS.forEach((e, i) => fs.writeFileSync(path.join(__dirname, '..', `emblem-${String(i + 1).padStart(2, '0')}.svg`), svg(e) + '\n'));
console.log(EMBLEMS.length, 'svgs');
