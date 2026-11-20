// Offline tests of the current script bodies, with game builtins replaced by
// deterministic doubles. This does not emulate SQL, packets, timers or the VM.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const source = new TextDecoder('euc-kr').decode(fs.readFileSync(path.join(root, 'npc/NEED/instances/CharlestonCrisis.txt'))).replace(/\r\n/g, '\n');
const bodyCache = new Map();
const token = /"(?:\\.|[^"\\])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\/|[{}]/g;
function body(name) {
  if (bodyCache.has(name)) return bodyCache.get(name);
  const start = name.startsWith('F_') ? source.indexOf(`function\tscript\t${name}\t`) : source.indexOf(`::${name}\t`);
  assert(start >= 0, `Missing script ${name}`);
  const begin = source.indexOf('{', start);
  let depth = 0;
  token.lastIndex = begin;
  for (let m; (m = token.exec(source));) {
    if (m[0] === '{') depth++;
    if (m[0] === '}' && --depth === 0) {
      const text = source.slice(begin + 1, m.index);
      bodyCache.set(name, text);
      return text;
    }
  }
  throw Error(`Unclosed body ${name}`);
}
function compile(text) {
  const strings = [];
  let code = text.replace(/"(?:\\.|[^"\\])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, m => {
    if (!m.startsWith('"')) return '';
    return `__STR${strings.push(m) - 1}__`;
  });
  code = code.replace(/copyarray\s+(\.@\w+)\[0\],(\$@\w+)\[0\],([^;]+);/g, '$1 = $2.slice(0,$3);');
  code = code.replace(/^(\s*)(getpartymember|debugmes|mes|cutin|getitem|setquest|completequest|warp|enablenpc|disablenpc|donpcevent|mapannouncelang|monster|sleep2|npctalklang|viewpoint|cloakonnpc|emotion)\s+([^;]+);/gm, '$1$2($3);');
  code = code.replace(/\b(close3|close|end)\s*;/g, 'halt("$1");').replace(/\b(close2|next)\s*;/g, '$1();');
  code = code.replace(/\.@([\w]+\$?)|\$@([\w]+\$?)|#([\w]+)|'([\w]+\$?)/g,
    (_, local, global, account, instance) => local ? `L["${local}"]` : global ? `G["${global}"]` : account ? `A().vars["${account}"]` : `I["${instance}"]`);
  code = code.replace(/__STR(\d+)__/g, (_, n) => strings[n]);
  return new Function('E', 'L', 'G', 'I', 'A', 'halt', `with(E) { ${code} }`);
}
const zero = init => new Proxy(init || {}, { get: (o, k) => k in o ? o[k] : 0 });
const player = (aid, clear = 1, quest = 0, cid = aid + 10000) => ({ aid, cid, party: 9, vars: zero({ NEED_MD_CHARLESTON_CLEAR: clear }), quests: zero({ 13184: 1, 13186: quest }), online: true });
const stopped = Symbol('script stopped');
function world(players = [player(4001)], roster = players.map(p => ({ aid: p.aid, cid: p.cid }))) {
  const w = {
    players, roster, rid: players[0].aid, leader: players[0].cid, lang: 1,
    globals: zero({ partymembercount: 8, partymemberaid: [9999, 9998, 9997], partymembercid: [12] }),
    instance: zero({ ep15_1_mcd01: 0, need_charleston_fast: 0 }),
    logs: [], messages: [], items: [], warps: [], effects: [], choices: [], attaches: [],
  };
  const attached = () => { const p = w.players.find(p => p.aid === w.rid && p.online); assert(p, 'Read without valid RID'); return p; };
  const halt = why => { w.stopped = why; throw stopped; };
  const E = {
    getcharid: type => ({ 0: attached().cid, 1: attached().party, 3: attached().aid })[type],
    is_party_leader: () => attached().cid === w.leader,
    isbegin_quest: id => attached().quests[id],
    getpartymember: (party, type) => {
      const entries = w.rosterAt?.(type) ?? w.roster;
      const dest = type === 2 ? 'partymemberaid' : 'partymembercid';
      // Deliberately preserve trailing data, as the actual command does.
      entries.forEach((p, i) => { w.globals[dest][i] = type === 2 ? p.aid : p.cid; });
      w.globals.partymembercount = entries.length;
    },
    isloggedin: (aid, cid) => +w.players.some(p => p.aid === aid && p.cid === cid && p.online),
    attachrid: aid => {
      w.attaches.push(aid);
      if (w.failAttach?.(aid, w.attaches.length) || !w.players.some(p => p.aid === aid && p.online)) return 0;
      w.rid = aid;
      w.afterAttach?.(aid, w.attaches.length);
      return 1;
    },
    debugmes: message => w.logs.push(message),
    mes: message => w.messages.push(message),
    needtr: (ko, en) => w.lang ? en : ko,
    select: () => { assert(w.choices.length, 'Unplanned menu'); const choice = w.choices.shift(); w.afterMenu?.(); return choice; },
    next: () => { if (w.abortNext) halt('disconnected'); }, close2: () => {}, cutin: () => {},
    setquest: id => { attached().quests[id] = 1; },
    completequest: id => { attached().quests[id] = 2; },
    getitem: (id, count) => w.items.push([id, count]),
    warp: (...args) => w.warps.push(args),
    strcharinfo: () => 'Offline fixture',
    instance_npcname: name => name,
    instance_mapname: name => name,
    bc_map: 0,
  };
  for (const fn of ['enablenpc', 'disablenpc', 'donpcevent', 'mapannouncelang', 'monster', 'sleep2', 'npctalklang', 'cloakonnpc', 'emotion'])
    E[fn] = (...args) => w.effects.push([fn, ...args]);
  const invoke = text => compile(text)(E, zero(), w.globals, w.instance, attached, halt);
  E.callfunc = name => invoke(body(name));
  w.run = (name, text = body(name)) => {
    w.stopped = null;
    try { return invoke(text); } catch (e) { if (e !== stopped) throw e; }
  };
  w.check = () => { const result = w.run('F_CharlestonCanEnableFast'); return result === undefined ? result : +result; };
  return w;
}
let passed = 0;
function test(name, fn) { fn(); passed++; console.log(`PASS ${name}`); }
function restored(w) { assert.equal(w.rid, w.players[0].aid); }
test('solo completed account ignores stale global AIDs', () => { const w = world(); assert.equal(w.check(), 1); restored(w); assert.deepEqual(w.attaches, [4001, 4001]); });
test('all online members complete, leader restored', () => { const w = world([player(4001), player(4002)]); assert.equal(w.check(), 1); restored(w); });
test('mixed party rejected and logs only failed member state', () => { const w = world([player(4001), player(4002, 0)]); assert.equal(w.check(), 0); restored(w); assert.deepEqual(w.logs, ['[Charleston Fast] aid=4002 quest13186=0 clear=0 attached=1']); });
test('legacy completed quest unlocks its actual account only', () => { const w = world([player(4001), player(4002, 0, 2)]); assert.equal(w.check(), 1); assert.equal(w.players[1].vars.NEED_MD_CHARLESTON_CLEAR, 1); });
test('offline uncleared member is excluded', () => { const p = player(4002, 0); p.online = false; const w = world([player(4001), p]); assert.equal(w.check(), 1); });
test('online alternate character is not the offline party character', () => { const w = world([player(4001), player(4002, 0)], [{ aid: 4001, cid: 14001 }, { aid: 4002, cid: 99999 }]); assert.equal(w.check(), 1); assert.equal(w.players[1].vars.NEED_MD_CHARLESTON_CLEAR, 0); });
test('online attach failure rejects with unreadable-state sentinels', () => { const w = world([player(4001), player(4002)]); w.failAttach = aid => aid === 4002; assert.equal(w.check(), 0); restored(w); assert(w.logs.includes('[Charleston Fast] aid=4002 quest13186=-1 clear=-1 attached=0')); });
test('empty roster rejects', () => { const w = world(undefined, []); assert.equal(w.check(), 0); restored(w); });
test('roster missing leader rejects', () => { const w = world([player(4001), player(4002)], [{ aid: 4002, cid: 14002 }]); assert.equal(w.check(), 0); restored(w); });
test('all roster members offline rejects', () => { const w = world(undefined, [{ aid: 9999, cid: 8888 }]); assert.equal(w.check(), 0); });
test('roster count mismatch rejects', () => { const w = world(); w.rosterAt = type => type === 1 ? [] : w.roster; assert.equal(w.check(), 0); });
test('local snapshots survive later global-array pollution', () => { const w = world([player(4001), player(4002)]); w.afterAttach = () => { w.globals.partymembercount = 99; w.globals.partymemberaid = [9999]; w.globals.partymembercid = [9999]; }; assert.equal(w.check(), 1); restored(w); });
test('leadership/party rechecked after menu', () => { const w = world(); w.leader = 9999; assert.equal(w.check(), 0); assert.equal(w.attaches.length, 0); w.players[0].party = 0; assert.equal(w.check(), 0); });
test('caller restoration failure stops the entire caller', () => { const w = world([player(4001), player(4002)]); w.failAttach = (_, n) => n === 3; assert.equal(w.check(), undefined); assert.equal(w.stopped, 'end'); });
test('caller character mismatch stops the entire caller', () => { const w = world(); w.afterAttach = (_, n) => { if (n === 2) w.players[0].cid++; }; w.check(); assert.equal(w.stopped, 'end'); });
test('successful mode selection enables fast travel', () => { const w = world(); w.choices = [2]; w.run('Charleston Fast Start#1'); assert.equal(w.instance.need_charleston_fast, 1); assert.equal(w.effects.filter(e => e[0] === 'enablenpc').length, 17); });
test('failed restoration never enables fast mode or resumes dialogue', () => { const w = world(); w.choices = [2]; w.failAttach = (_, n) => n === 2; w.run('Charleston Fast Start#1'); assert.equal(w.stopped, 'end'); assert.equal(w.instance.need_charleston_fast, 0); assert(!w.messages.some(m => m.includes('portals are active'))); });
test('both activation callers use the same guard and diagnostic text', () => { assert.equal((source.match(/callfunc\("F_CharlestonCanEnableFast"\)/g) || []).length, 2); assert.equal((source.match(/A member may be uncleared, or verification may have failed/g) || []).length, 2); });
const entryTouch = body('Charleston 1#0').split('OnTouch_:')[1].split('OnMyMobDead:')[0];
test('entry touch enables fast mode but still spawns the guard fight', () => { const w = world(); w.choices = [2, 1]; w.run('entry', entryTouch); assert.equal(w.instance.need_charleston_fast, 1); assert.equal(w.instance.ep15_1_mcd01, 1); assert(w.effects.some(e => e[0] === 'monster' && e[5] === 3127)); });
test('mixed party entry touch falls back to normal without leaking AIDs', () => { const w = world([player(4001), player(4002, 0)]); w.choices = [2]; w.run('entry', entryTouch); assert.equal(w.instance.need_charleston_fast, 0); assert.equal(w.instance.ep15_1_mcd01, 1); assert(w.messages.some(m => m.includes('normal run'))); assert(w.messages.every(m => !m.includes('4001') && !m.includes('4002'))); });
test('entry touch stops before mode activation or guard spawn on RID failure', () => { const w = world(); w.choices = [2]; w.failAttach = (_, n) => n === 2; w.run('entry', entryTouch); assert.equal(w.stopped, 'end'); assert.equal(w.instance.need_charleston_fast, 0); assert.equal(w.effects.length, 0); });
test('active but incomplete quest does not unlock fast progress', () => { const w = world([player(4001, 0, 1)]); assert.equal(w.check(), 0); assert.equal(w.players[0].vars.NEED_MD_CHARLESTON_CLEAR, 0); });
for (const lang of [0, 1]) {
  for (let step = 0; step <= 16; step++) test(`Shalosh step ${step}, language ${lang}: guidance without completion`, () => {
    const w = world([player(4001, 0)]); w.lang = lang; w.instance.ep15_1_mcd01 = step; w.choices = [3]; w.run('Shalosh#1');
    assert.equal(w.instance.ep15_1_mcd01, step); assert.equal(w.items.length, 0); assert.equal(w.warps.length, 0); assert.equal(w.players[0].quests[13184], 1); assert.equal(w.players[0].vars.NEED_MD_CHARLESTON_CLEAR, 0);
    if (lang) assert(w.messages.some(m => m.includes(step < 12 ? 'investigation is not complete' : step < 14 ? 'battle area' : step === 14 ? 'Charleston 2 in the research lab' : step === 15 ? 'speak to him twice' : 'once more')));
  });
}
test('normal ending requires both Dr. Ve conversations', () => {
  const w = world([player(4001, 0)]); w.instance.ep15_1_mcd01 = 14; w.run('Charleston 2#7'); assert.equal(w.instance.ep15_1_mcd01, 15); assert(w.effects.some(e => e[1] === 'end#1::OnStart'));
  // The real timer makes Dr. Ve#4 visible after the cutscene; visibility is not emulated.
  w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 16); assert(w.messages.some(m => m.includes('One final conversation remains')));
  w.choices = [3]; w.run('Shalosh#1'); assert.equal(w.items.length, 0);
  w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 17); assert(w.messages.some(m => m.includes('final conversation is complete')));
  w.choices = [3, 1]; w.run('Shalosh#1'); assert.deepEqual(w.items, [[6752, 3]]); assert.deepEqual(w.warps, [['verus04', 107, 124]]); assert.equal(w.players[0].vars.NEED_MD_CHARLESTON_CLEAR, 1); assert.equal(w.players[0].quests[13184], 2); assert.equal(w.players[0].quests[13186], 2);
});
test('fast ending 14 -> 15 -> 17 still requires Shalosh reward dialogue', () => { const w = world(); w.instance.ep15_1_mcd01 = 14; w.instance.need_charleston_fast = 1; w.choices = [1, 1]; w.run('Charleston 2#7'); assert.equal(w.instance.ep15_1_mcd01, 15); w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 17); assert.equal(w.items.length, 0); w.choices = [3, 1]; w.run('Shalosh#1'); assert.deepEqual(w.items, [[6752, 3]]); });
test('watching final dialogue in fast mode preserves 15 -> 16 -> 17', () => { const w = world(); w.instance.ep15_1_mcd01 = 15; w.instance.need_charleston_fast = 1; w.choices = [2, 2]; w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 16); w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 17); });
test('fast progress can skip the remaining final conversation at step 16', () => { const w = world(); w.instance.ep15_1_mcd01 = 16; w.instance.need_charleston_fast = 1; w.choices = [1]; w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 17); assert.equal(w.items.length, 0); });
test('aborting final dialogue does not complete the ending', () => { const w = world(); w.instance.ep15_1_mcd01 = 16; w.abortNext = true; w.run('Dr. Ve#4'); assert.equal(w.instance.ep15_1_mcd01, 16); });
test('staying at Shalosh does not grant rewards or records', () => { const w = world([player(4001, 0)]); w.instance.ep15_1_mcd01 = 17; w.choices = [3, 2]; w.run('Shalosh#1'); assert.equal(w.items.length, 0); assert.equal(w.players[0].vars.NEED_MD_CHARLESTON_CLEAR, 0); });
test('new instance resets mode and progress but retains account unlock', () => { const w = world(); w.instance.need_charleston_fast = 1; const init = body('Charleston Assistant#1').split('OnInstanceInit:')[1].split('OnMyMobDead:')[0]; w.run('init', init); assert.equal(w.instance.need_charleston_fast, 0); assert.equal(w.instance.ep15_1_mcd01, 0); assert.equal(w.players[0].vars.NEED_MD_CHARLESTON_CLEAR, 1); });
test('battle/material/reward/init bodies are byte-identical to HEAD', () => {
  const original = new TextDecoder('euc-kr').decode(cp.execFileSync('git', ['show', 'HEAD:npc/NEED/instances/CharlestonCrisis.txt'], { cwd: root })).replace(/\r\n/g, '\n');
  const allNames = [...source.matchAll(/^function\tscript\t([^\t]+)\t|^[^\r\n]*\tscript\t[^\r\n]*::([^\t]+)\t/gm)].map(m => m[1] || m[2]);
  const modified = new Set(['F_CharlestonCanEnableFast', 'Charleston Fast Start#1', 'Charleston 1#0', 'Dr. Ve#4', 'Shalosh#1']);
  for (const name of allNames.filter(n => !modified.has(n))) assert(original.includes(body(name)), `Unexpected edit in ${name}`);
  const reward = text => text.slice(text.indexOf('if (\'ep15_1_mcd01 == 17)', text.indexOf('case 3:')), text.indexOf('warp "verus04",107,124;') + 'warp "verus04",107,124;'.length);
  assert(reward(body('Shalosh#1')).length > 100); assert(original.includes(reward(body('Shalosh#1'))));
});
console.log(`OFFLINE_SMOKE_RESULT tests=${passed} failures=0; game APIs mocked, no live login/SQL/timers/packets`);
