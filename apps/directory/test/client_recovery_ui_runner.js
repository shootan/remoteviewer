// Run the production-shell UI fixture against a private instance of the production directory.
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');
const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'client-recovery-'));
assert(!path.relative(root, scratch).startsWith('..'));
const profile = path.join(scratch, 'profile'); fs.mkdirSync(profile); fs.writeFileSync(path.join(scratch, '.fixture'), 'owned test profile');
const serverPath = path.resolve(__dirname, '../server.js');
const delayFlag = path.join(scratch, 'delay-hosts'), preload = path.join(scratch, 'delay.cjs');
fs.writeFileSync(preload, `const http=require('http'),fs=require('fs'); const create=http.createServer;
http.createServer=function(listener){return create.call(http,(req,res)=>{
if(req.url==='/api/hosts'&&fs.existsSync(process.env.TEST_HOSTS_DELAY)){
fs.unlinkSync(process.env.TEST_HOSTS_DELAY);fs.writeFileSync(process.env.TEST_HOSTS_DELAY+'.seen','1');
setTimeout(()=>listener(req,res),1200);}else listener(req,res);});};`);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function port(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise(r=>tcp?socket.listen(0,'127.0.0.1',r):socket.bind(0,'127.0.0.1',r));
  const value=socket.address().port; await new Promise(r=>socket.close(r)); return value;
}
let server, ui, decoy;
(async()=>{
  const httpPort=await port(true),udpPort=await port(false);
  // fixed-server: the address the shell's client.txt names when it starts, and that a login
  // message from the page names later. Nothing may arrive here. It answers like a directory
  // would, so a shell that did come here would carry on and be counted rather than fail early
  // and look like a network error.
  const decoyHits=[];
  decoy=require('http').createServer((req,res)=>{
    decoyHits.push(req.method+' '+req.url);
    res.writeHead(200,{'content-type':'application/json'});res.end('{"ok":true}');
  });
  await new Promise(r=>decoy.listen(0,'127.0.0.1',r));
  const decoyUrl=`http://127.0.0.1:${decoy.address().port}`;
  // The counter is only evidence if it counts. One request of our own, seen, then forgotten.
  await fetch(decoyUrl+'/self-check');
  assert.deepEqual(decoyHits,['GET /self-check'],'the decoy counts what reaches it');
  decoyHits.length=0;
  const env={...process.env,TEMP:profile,TMP:profile,LOCALAPPDATA:path.join(scratch,'appdata'),
    GNLINK_RECOVERY_TEST_ROOT:scratch,TEST_HOSTS_DELAY:delayFlag,
    REMOTE60_DIR_DATA:path.join(scratch,'store.json'),REMOTE60_DIR_PORT:String(httpPort),
    REMOTE60_DIR_UDP_PORT:String(udpPort),REMOTE60_RELAY_ENABLED:'0',
    REMOTE60_DIR_TLS_KEY:'',REMOTE60_DIR_TLS_CERT:'',REMOTE60_UPDATE_MANIFEST_URL:'',
    REMOTE60_LOG_DIR:path.join(scratch,'logs')};
  fs.mkdirSync(env.LOCALAPPDATA);
  for(const id of ['account-a','account-b'])
    assert.equal(spawnSync(process.execPath,[serverPath,'--add-account',id,'fixture-password'],{env,stdio:'ignore'}).status,0);
  server=spawn(process.execPath,['--require',preload,serverPath],{env,stdio:'ignore'});
  const url=`http://127.0.0.1:${httpPort}`;
  let ready=false;
  for(let i=0;i<100&&!ready;++i){try{ready=(await fetch(url+'/healthz')).ok;}catch{}if(!ready)await sleep(50);}
  assert(ready);
  const executable=process.argv[2]||path.join(root,'build-local/apps/native_poc/Release/remote60_client_recovery_ui_test.exe');
  const screenshot=path.join(root,'.claude','client-recovery-ui.png');
  ui=spawn(executable,[url,delayFlag,screenshot,decoyUrl],{env,stdio:'inherit'});
  const result=await new Promise((resolve,reject)=>{ui.once('error',reject);ui.once('exit',resolve);});
  assert.equal(result,0,'production UI recovery fixture');
  console.log('PASS UI screenshot '+screenshot);
  console.log('PASS sign-in screenshot '+screenshot+'.login.png');
  // client.txt named the decoy for the whole run and one login message named it too.
  console.log('      (requests the decoy received: '+decoyHits.length+(decoyHits.length?' -- '+decoyHits.join(', '):'')+')');
  assert.equal(decoyHits.length,0,'the stored address and the page-supplied address received no request');
  console.log('PASS [fixed-server] the address in client.txt and the one a page named received no request');
})().catch(error=>{console.error('FAIL',error.message);process.exitCode=1;})
.finally(async()=>{
  if(ui&&ui.exitCode===null){const done=new Promise(r=>ui.once('exit',r));ui.kill();await done;}
  if(server&&server.exitCode===null){const done=new Promise(r=>server.once('exit',r));server.kill();await done;}
  if(decoy){decoy.closeAllConnections?.();await new Promise(r=>decoy.close(r));}
  // Keep private profile evidence out of git; WebView descendants can still be closing handles.
  console.log('Private fixture retained at '+scratch);
});
