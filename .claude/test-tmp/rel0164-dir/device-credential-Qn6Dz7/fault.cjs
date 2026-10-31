const fs=require('fs'); const write=fs.writeFileSync;
fs.writeFileSync=function(p,...args){if(String(p)===process.env.REMOTE60_DIR_DATA+'.tmp'&&fs.existsSync(process.env.TEST_FAULT_FLAG))
throw Object.assign(new Error('injected disk full'),{code:'ENOSPC'});return write.call(this,p,...args)};