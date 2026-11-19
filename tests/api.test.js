// tests/api.test.js — every function named in the specs exists on every namespace that is built.
//   node tools/test.js api                 namespaces that are still placeholders are listed, not failed
//   API_STRICT=1 node tools/test.js api    (integration) a missing namespace is a failure too
'use strict';
const T = require('../tools/t.js');
const DM = require('../tools/load.js')();
const API = require('../tools/api.js');

T.test('documented functions exist on built namespaces', () => {
  const r = API.check(DM, API.node);
  if (r.absent.length) console.log('  not built yet: ' + r.absent.join(', '));
  T.eq(r.missing, [], 'missing functions');
  if (process.env.API_STRICT) T.eq(r.absent, [], 'namespaces not built');
});

T.test('shared tables exist', () => {
  T.ok(Array.isArray(DM.BUILDINGS) && DM.BUILDINGS.length === 12, 'DM.BUILDINGS');
  T.ok(DM.EVENTS && DM.EVENTS['settlement:founded'], 'DM.EVENTS');
  T.ok(DM.CULTURE_COLORS.length === 6, 'DM.CULTURE_COLORS');
  if (DM.State && DM.State.create) {
    T.ok(Array.isArray(DM.State.VEG_T), 'State.VEG_T');
    T.ok(DM.BIOME && DM.AGES && DM.AGES.length === 7, 'DM.BIOME / DM.AGES');
  }
  if (DM.Powers && DM.Powers.LIST) {
    const ids = DM.Powers.LIST.map(p => p.id).join(',');
    T.eq(ids, 'comet,seed,awaken,raise,lower,rain,bloom,lightning,quake,plague,meteor,volcano,heal,inspire,prophet,peace', 'Powers.LIST ids and order');
    const keys = DM.Powers.LIST.map(p => p.key).join('');
    T.eq(keys, 'CGKUJNBLZXMVTIPO', 'power hotkeys');
  }
  if (DM.Sim && DM.Sim.TICK !== undefined) { T.eq(DM.Sim.TICK, 0.25); T.eq(DM.Sim.REAL_PER_TICK, 0.125); }
});

T.test('entity factories fill every documented field', () => {
  if (!(DM.State && DM.State.create)) return;
  const w = DM.State.create({ seed: 1, L: 2 });
  const s = DM.State.newSettlement(w, { cell: 3, b: { shrine: 1 } });
  T.eq(Object.keys(s).sort().join(','), 'K,apart,ark,b,blessed,capital,cause,cell,culture,dead,deadYear,devotion,famine,founded,growth,id,immune,inspire,jaded,lastExpand,love,name,plague,pop,prayer,radius,tier');
  T.eq(s.b.shrine, 1); T.eq(s.b.temple, 0); T.eq(Object.keys(s.b).length, 12); T.eq(w.settlements[s.id], s);
  const c = DM.State.newCulture(w, { traits: { piety: 0.9 } });
  T.eq(Object.keys(c).sort().join(','), 'adj,age,born,capital,color,dead,edict,edictSince,epithet,exhaustion,godName,id,lang,lastSplit,name,parent,plural,pop,prophet,science,seen,towns,traits');
  T.eq(c.traits.piety, 0.9); T.eq(c.traits.aggression, 0.5);
  const u = DM.State.newUnit(w, { type: 'army' });
  T.eq(Object.keys(u).sort().join(','), 'born,culture,data,dest,from,i,id,naval,path,sea,size,speed,t,to,type');
  T.eq(u.id, 1); T.eq(w.units.length, 1);
  const d = DM.State.newDisaster(w, { type: 'storm', data: { x: 1 } });
  T.eq(Object.keys(d).sort().join(','), 'ang,cell,data,dur,id,natural,power,t,type');
  // a second culture gets a different colour
  const c2 = DM.State.newCulture(w, {});
  T.ok(c2.color.join() !== c.color.join(), 'distinct culture colours');
  // factories produce JSON-safe data
  T.eq(JSON.parse(JSON.stringify(s)), s);
});
