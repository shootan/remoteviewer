const fs=require('fs'); const write=fs.writeFileSync;
fs.writeFileSync=function(p,...args){if(String(p)===process.env.REMOTE60_DIR_DATA+'.tmp'&&fs.existsSync(process.env.TEST_FAULT_FLAG))
throw Object.assign(new Error('injected disk full'),{code:'ENOSPC'});return write.call(this,p,...args)};
// The next password hash to start while TEST_HOLD_KDF exists is finished but not answered until
// the file is gone -- so a case can do something through the admin API in between. One hash only
// (a .seen file marks it taken), so the admin API's own hashing is never held.
const crypto=require('crypto'); const scrypt=crypto.scrypt;
crypto.scrypt=function(pw,salt,len,...rest){const cb=rest[rest.length-1];const f=process.env.TEST_HOLD_KDF;
if(f&&fs.existsSync(f)&&!fs.existsSync(f+'.seen')){fs.writeFileSync(f+'.seen','1');
return scrypt.call(this,pw,salt,len,(e,v)=>{const wait=()=>fs.existsSync(f)?setTimeout(wait,20):cb(e,v);wait();});}
return scrypt.call(this,pw,salt,len,...rest)};