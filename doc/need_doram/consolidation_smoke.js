// Offline source-body smoke. This is NOT the rAthena parser or a live server.
// Execute the real function bodies after translating only command/variable syntax;
// replace engine inventory, dialogue and skill calls with deterministic test doubles.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = path.resolve(__dirname, '../..');
const source = new TextDecoder('euc-kr').decode(fs.readFileSync(path.join(root, 'npc/NEED/need_doram_exchange.txt')));
const functions = {};
const pattern = /function\s+script\s+(\w+)\s*\{/g;
for (const match of source.matchAll(pattern)) {
  const start = match.index + match[0].length;
  let end = start, depth = 1;
  while (depth) {
    if (source[end] === '{') ++depth;
    if (source[end] === '}') --depth;
    ++end;
  }
  let body = source.slice(start, end - 1)
    .replace(/\.@(\w+\$?)/g, 'local.$1')
    .replace(/@(\w+)/g, 'inv.$1')
    .replace(/setarray\s+(local\.\w+)\[0\],([\s\S]*?);/g, '$1 = [$2];')
    .replace(/getinventorylist\s*;/g, 'getinventorylist();')
    .replace(/\b(mes|callfunc|delitem|getitem|skill)\s+([^;]+);/g, '$1($2);')
    .replace(/\bnext\s*;/g, 'next();');
  functions[match[1]] = eval(`(function(...args){ const local={}; const getarg=i=>args[i]; ${body}\n})`);
}
assert.equal(Object.keys(functions).length, 7);

let inv, rows, choices, selected, messages, tokens, output, skills, mutations, failDelete, capacity, onSelect, language;
let Class;
const Job_Summoner = 4218, Job_Baby_Summoner = 4220, SKILL_PERM_GRANT = 4;
const fields = [...new Set([...source.matchAll(/@inventorylist_(\w+)/g)].map(m => m[1]))];
function reset(items = [], coinCount = 0, selectChoices = []) {
  inv = {}; rows = items.map((r, i) => ({idx:i, amount:1, identify:1, ...r}));
  tokens=coinCount; choices=[...selectChoices]; selected=0; messages=[]; output={}; skills={};
  mutations=0; failDelete=false; capacity=true; onSelect=null; Class=Job_Summoner; language='en';
}
function getinventorylist() {
  inv.inventorylist_count=rows.length;
  for(const key of fields) if(key!=='count') inv['inventorylist_'+key]=rows.map(r=>r[key]||0);
}
function countitem(id) {return id===25142?tokens:rows.filter(r=>r.id===id).reduce((n,r)=>n+r.amount,0)+(output[id]||0);}
function delitemidx(idx,amount) {
  const row=rows.find(r=>r.idx===idx);
  if(failDelete || !row || row.amount<amount) return false;
  row.amount-=amount; ++mutations; return true;
}
function delitem(id,amount) {assert.equal(id,25142);assert(tokens>=amount);tokens-=amount;++mutations;}
function getitem(id,amount) {if(id===25142)tokens+=amount;else output[id]=(output[id]||0)+amount;++mutations;}
function checkweight(id,amount) {assert(id>0&&amount>0);return capacity;}
function getarraysize(a) {return a.length;}
function getitemname(id) {return 'Item'+id;}
function needtr(kr,en) {return language==='en'?en:kr;}
function mes(s) {messages.push(s);}
function next() {}
function select(...args) {
  const choice=choices.shift(); assert(Number.isInteger(choice),'missing dialogue choice');
  const count=args.join(':').split(':').length; assert(choice>0&&choice<=count,'invalid choice');
  ++selected;if(onSelect)onSelect(selected);return choice;
}
function callfunc(name,...args) {assert(functions[name],name);return functions[name](...args);}
function getskilllv(name) {return skills[name]||0;}
function skill(name,level,mode) {assert.equal(mode,SKILL_PERM_GRANT);skills[name]=level;++mutations;}
let cases=0;
function test(name,run) {run();++cases;console.log('PASS '+name);}

test('weapon turn-in uses existing 2-token recipe',()=>{
 reset([{id:1691}],0,[2,1]);callfunc('F_NEED_DoramDonate',0);
 assert.equal(tokens,2);assert.equal(rows[0].amount,0);
});
test('level-100 armor keeps existing 3-token value',()=>{
 reset([{id:15126}],0,[13,1]);callfunc('F_NEED_DoramDonate',1);assert.equal(tokens,3);
});
test('all 6 basic/special charm purchase routes, including 28424',()=>{
 const ids=[28413,28416,28419,28422,28423,28424];
 for(let i=0;i<6;++i){reset([],100,[i+1,1]);callfunc('F_NEED_DoramBuyCharm');assert.equal(output[ids[i]],1);assert.equal(tokens,100-(i<3?10:30));}
});
test('all 6 upgrades retain 30/90 tokens and one input',()=>{
 for(let branch=0;branch<3;++branch)for(let tier=0;tier<2;++tier){
  const id=28413+branch*3+tier;reset([{id}],100,[branch+1,1]);callfunc('F_NEED_DoramUpgrade');
  assert.equal(output[id+1],1);assert.equal(rows[0].amount,0);assert.equal(tokens,100-(tier?90:30));
 }
});
test('equipped/carded duplicates protected; only plain index removed',()=>{
 reset([{id:1691,card1:4001},{id:1691},{id:1691,equip:2}],0,[2,1]);callfunc('F_NEED_DoramDonate',0);
 assert.deepEqual(rows.map(r=>r.amount),[1,0,1]);assert.equal(tokens,2);
});
test('refine, cards, options, rental, bound, favorite and grade excluded',()=>{
 for(const field of ['equip','refine','attribute','expire','bound','favorite','enchantgrade','card1','card2','card3','card4','option_id1','option_id2','option_id3','option_id4','option_id5']){
  reset([{id:1691,[field]:1}]);assert.equal(callfunc('F_NEED_DoramPlainIndex',1691),-1);
 }
 reset([{id:1691,identify:0}]);assert.equal(callfunc('F_NEED_DoramPlainIndex',1691),-1);
});
test('plain duplicate amount consumes exactly one item',()=>{
 reset([{id:1691,amount:2}],0,[2,1]);callfunc('F_NEED_DoramDonate',0);assert.equal(rows[0].amount,1);assert.equal(tokens,2);
});
test('cancel root, item selection, purchase confirmation and upgrade confirmation',()=>{
 reset([],100,[5]);callfunc('F_NEED_DoramMenu');assert.equal(mutations,0);
 reset([],100,[7]);callfunc('F_NEED_DoramBuyCharm');assert.equal(mutations,0);
 reset([],100,[6,2]);callfunc('F_NEED_DoramBuyCharm');assert.equal(mutations,0);
 reset([{id:28413}],100,[1,2]);callfunc('F_NEED_DoramUpgrade');assert.equal(mutations,0);
 reset([{id:1691}],0,[9]);callfunc('F_NEED_DoramDonate',0);assert.equal(mutations,0);
});
test('insufficient tokens, missing input and inventory/weight failure do not consume',()=>{
 reset([],9,[1,1]);callfunc('F_NEED_DoramBuyCharm');assert.equal(mutations,0);
 reset([],100,[2,1]);callfunc('F_NEED_DoramDonate',0);assert.equal(mutations,0);
 reset([{id:28413}],100,[1,1]);capacity=false;callfunc('F_NEED_DoramUpgrade');assert.equal(mutations,0);
});
test('delete failure never pays tokens or consumes upgrade coins',()=>{
 reset([{id:28413}],100,[1,1]);failDelete=true;callfunc('F_NEED_DoramUpgrade');assert.equal(mutations,0);
});
test('inventory and tokens rechecked after confirmation',()=>{
 reset([{id:1691}],0,[2,1]);onSelect=n=>{if(n===2)rows[0].equip=2;};callfunc('F_NEED_DoramDonate',0);assert.equal(mutations,0);
 reset([{id:28413}],100,[1,1]);onSelect=n=>{if(n===2)tokens=0;};callfunc('F_NEED_DoramUpgrade');assert.equal(mutations,0);
});
test('advanced charm cannot be upgraded or downgraded',()=>{
 reset([{id:28415}],100,[1]);callfunc('F_NEED_DoramUpgrade');assert.equal(mutations,0);
});
test('root menu calls existing token path in both languages',()=>{
 for(const lang of ['kr','en']){reset([{id:1691}],0,[1,2,1]);language=lang;callfunc('F_NEED_DoramMenu');assert.equal(tokens,2);}
});
test('First Aid learn, cancel, duplicate and class guards; no inventory rewards',()=>{
 reset([],0,[1]);callfunc('F_NEED_DoramFirstAid');assert.equal(skills.NV_FIRSTAID,1);assert.equal(tokens,0);assert.deepEqual(output,{});
 const previous=mutations;callfunc('F_NEED_DoramFirstAid');assert.equal(mutations,previous);
 reset([],0,[2]);callfunc('F_NEED_DoramFirstAid');assert.equal(mutations,0);
 reset();Class=0;callfunc('F_NEED_DoramFirstAid');assert.equal(mutations,0);
});
console.log(`PASS ${cases} offline source-body groups; real NPC parser, purchase packets and persistence NOT TESTED`);
