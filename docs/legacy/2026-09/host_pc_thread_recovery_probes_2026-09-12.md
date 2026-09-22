# 2026-09-12 복구 원장 소형 프로브 원문

[조사 결과](host_pc_thread_recovery_audit_2026-09-12.md#9-직접-실행한-소형-검증). 제품 수정용 코드가 아닌 조사 재현용 원문이다. 실행에는 원장 inventory의 고정 소스가 필요하다. NAS·제품 프로세스를 조작하지 않는다.

## compile-probe.cmd

```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /I source\apps\native_poc\src liveness_probe.cpp /Fe:liveness_probe.exe /Fo:liveness_probe.obj
if errorlevel 1 exit /b 1
liveness_probe.exe
```

## liveness_probe.cpp

```cpp
#include <cstdio>
#include "viewer_recv_liveness.hpp"
using namespace remote60::native_poc::viewer;
int main() {
  SessionLivenessConfig cfg;
  SessionLivenessSample s;
  s.nowUs=100000000; s.stage=RecvStage::Recv; s.stageEnterUs=99999000;
  s.controlConnected=true; s.lastDatagramUs=99999000;
  s.lastPublishUs=1000000; s.lastAssembledUs=1000000;
  auto a=evaluate_session_liveness(s,cfg);
  std::printf("control_alive_video_absent_99s stalled=%d silent=%d dead=%d\n",a.recvStalled,a.linkSilent,a.sessionDead);
  s.stage=RecvStage::Decode; s.stageEnterUs=1000000;
  auto b=evaluate_session_liveness(s,cfg);
  std::printf("decode_stalled_99s_control_alive stalled=%d dead=%d\n",b.recvStalled,b.sessionDead);
  s.stage=RecvStage::Recv; s.stageEnterUs=99999000;
  s.controlConnected=false; s.controlGoneSinceUs=0;
  auto c=evaluate_session_liveness(s,cfg);
  std::printf("control_never_observed_connected dead=%d\n",c.sessionDead);
  s.controlGoneSinceUs=90000000;
  auto d=evaluate_session_liveness(s,cfg);
  std::printf("control_gone_10s_video_absent dead=%d\n",d.sessionDead);
  bool ok=!a.sessionDead && !a.linkSilent && b.recvStalled && !b.sessionDead && !c.sessionDead && d.sessionDead;
  std::puts(ok?"PROBE CONFIRMED (policy gaps, not product health PASS)":"PROBE FAILED");
  return ok?0:1;
}
```

## server_probe.js

```javascript
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
```
