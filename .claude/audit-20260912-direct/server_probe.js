const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('source/apps/directory/server.js','utf8');
// Execute verbatim named functions against a failure-injected filesystem, never the NAS.
function functionText(name){
 const start=source.indexOf('function '+name+'(');
 const next=source.indexOf('\n}',start);
 assert(start>=0&&next>start);return source.slice(start,next+2);
}
let successStatus=0;
const c={store:{accounts:{},hosts:{}},DATA_PATH:'/fixture/store.json',hostTokens:new Map(),saveTimer:null,
 fs:{readFileSync(){throw Object.assign(new Error('broken JSON'),{code:'EIO'})},writeFileSync(){throw new Error('disk full')},renameSync(){throw new Error('not reached')}},
 console:{error(){}},clearTimeout(){}};
vm.createContext(c);
vm.runInContext(functionText('indexHostTokens')+'\n'+functionText('loadStore')+'\n'+functionText('saveStoreNow'),c);
c.store={accounts:{existing:{}},hosts:{}}; c.loadStore();
assert.equal(Object.keys(c.store.accounts).length,0);
console.log('loadStore read EIO -> empty accounts (confirmed)');
assert.equal(c.saveStoreNow(),undefined);
console.log('saveStoreNow write failure -> normal undefined return (confirmed; route 200 is static call-site evidence)');
console.log('PROBE CONFIRMED: 2 isolated production-function cases; no live server mutation');
