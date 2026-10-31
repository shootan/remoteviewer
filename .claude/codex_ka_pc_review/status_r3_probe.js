// Review only: actual fixed page status handler; fake DOM and an ordinary exit's real message shape.
const cp = require('child_process');
const source = cp.execFileSync('git', ['-C','C:/work/gnlink-wt/ka-pc','show','5a1fb45:apps/native_poc/ui/shell.html'], {encoding:'utf8'});
const begin=source.indexOf("    if (msg.type === 'status') {");
if(begin<0) throw Error('missing handler');
const end=source.indexOf('\n    }',begin)+6;
const handler=source.slice(begin,end);
function apply(msg) {
  const run = new Function('msg', `
    let connectingId='B';
    const $=id=>({classList:{contains:()=>id==='settingsCard'}});
    const show=()=>{}, message=()=>{};
    const setBusy=on=>{if(!on) connectingId='';};
    ${handler}
    return connectingId;
  `);
  return run(msg);
}
const scoped=apply({type:'status',state:'idle',detail:'A ended',hostId:'A'});
const ordinary=apply({type:'status',state:'idle',detail:'A ended'});
console.log(JSON.stringify({current_card:'B',scoped_A_idle:scoped,ordinary_A_idle:ordinary}));
process.exit(scoped==='B' && ordinary===''?0:1);
