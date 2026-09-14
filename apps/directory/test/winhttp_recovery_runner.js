const http=require('http'),path=require('path'),assert=require('assert');
const {spawn}=require('child_process');
const root=path.resolve(__dirname,'../../..');
const server=http.createServer((req,res)=>{
  if(req.url==='/ok'){res.writeHead(200,{'content-length':11});res.end('{"ok":true}');}
  else if(req.url==='/partial'){
    res.writeHead(200,{'content-length':100});res.flushHeaders();res.write('{}');setTimeout(()=>res.destroy(),30);
  }else{
    res.writeHead(200,{'content-length':128});res.flushHeaders();
    const timer=setInterval(()=>res.write('x'),100);res.on('close',()=>clearInterval(timer));
  }
});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const exe=process.argv[2]||path.join(root,'build-local/apps/native_poc/Release/remote60_winhttp_recovery_test.exe');
  const child=spawn(exe,[String(server.address().port)],{stdio:'inherit'});
  const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
  assert.equal(code,0);
})().catch(error=>{console.error('FAIL',error.message);process.exitCode=1;})
.finally(()=>{server.closeAllConnections();server.close();});
