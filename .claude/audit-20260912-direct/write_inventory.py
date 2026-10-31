from pathlib import Path
import json,hashlib,subprocess
root=Path('D:/remote/remote'); out=root/'.claude/audit-20260912-direct'
m=json.loads((out/'source-manifest.json').read_text(encoding='utf-8'))
detailed=set('''client_shell_main.cpp client_shell_bridge.cpp client_update_gate.cpp client_update_gate.hpp
host_app_main.cpp host_main_loop.hpp host_session.hpp host_watchdog.hpp host_startup.hpp
native_video_host_main.cpp host_shutdown.cpp host_startup_control.cpp host_startup_capture.cpp
host_startup_graphics.cpp host_startup_connect.cpp host_stage_time_limit.cpp host_stage_stats.cpp
host_stage_watchdogs.cpp host_stage_stream_active.cpp host_stage_geometry.cpp host_stage_backend.cpp
host_stage_selection.cpp host_stage_pop_frame.cpp host_stage_encode_send_h264.cpp host_stage_encode_send_h264_au.cpp
host_loop_helpers.cpp host_encoder_manager.cpp host_encoder_manager.hpp host_capture_session.cpp
host_capture_session.hpp host_capture_device.cpp host_encoded_sender.cpp host_control_session.cpp
directory_client.cpp directory_client.hpp udp_control_channel.cpp native_video_client_shared_core.cpp
native_video_client_shared_core.hpp d3d_capture_readback.cpp secure_input_broker.cpp
viewer_shutdown.cpp viewer_thread_join.hpp viewer_session_watchdog.cpp viewer_recv_liveness.hpp
viewer_liveness_test.cpp viewer_session_state.hpp viewer_control_client.cpp viewer_startup.cpp
viewer_video_receiver.cpp viewer_video_receiver_frame.cpp viewer_present.cpp viewer_nv12_renderer.hpp
viewer_window_proc.cpp viewer_layout.cpp viewer_frame_gate.cpp viewer_constants.hpp
log_upload.cpp log_upload.hpp update_check.cpp update_credential_channel.cpp
update_state_machine.cpp update_effects.cpp winhttp_transport.cpp capture_backend_dxgi.cpp
shell.html server.js'''.split())
def category(p):
 if '/test/' in p or Path(p).stem.endswith('_test'): return '테스트'
 if '/tools/' in p or p.endswith('_probe.cpp'): return '도구'
 if Path(p).name in ('CMakeLists.txt','package.json'): return '빌드'
 return '제품/공유'
rows=m['files']; deep=sum(Path(r['path']).name in detailed for r in rows)
content=['# Host / PC 클라이언트 조사 파일별 목록 (2026-09-12)','',
 '[발견 원장](host_pc_thread_recovery_audit_2026-09-12.md)과 함께 읽는다. 아래 해시는 조사 시점 사본의 SHA-256이다.', '',
 f'- 기준 HEAD: `{m["head"]}` + 워킹트리. {len(rows)}파일, {sum(r["lines"] for r in rows):,}줄.',
 f'- `분기 추적`: {deep}파일. 해당 파일 전체의 안전성 인증이 아니라 본 원장에 연결되는 함수/호출·실패 분기를 직접 읽고 대조했다.',
 f'- `검색`: {len(rows)-deep}파일. 목록·해시·전역 위험 패턴 검색만 수행했으며 전체 함수 정독/동적 검증을 주장하지 않는다.',
 '- 패턴: T=thread 생성/detach/join, W=blocking I/O/wait, S=mutex/atomic/UI message, R=restart/retry/watchdog, E=실패/예외/강제 종료. 주석·문자열도 매치되므로 개수는 결함 수가 아니다.',
 '- 검색 대상: rg가 열거하는 apps/native_poc, apps/directory, libs의 .cpp/.hpp/.h/.js/.html, CMakeLists.txt, package.json. Android 전용 및 third_party 제외. 심층 추적이 필요한 잔여 영역은 원장 미검증에 남긴다.', '',
 '## 스레드 생성·종료 패턴이 있는 파일', '',
 '| 파일 | 구분 | 매치 줄 |', '|---|---|---|']
for r in rows:
 if r['hits']['thread']:
  content.append(f'| `{r["path"]}` | {category(r["path"])} | '+', '.join(map(str,r['hits']['thread']))+' |')
content+=['','## 전체 파일', '', '| 파일 | 구분 / 조사 | 줄 | 패턴 수 T/W/S/R/E | SHA-256 |','|---|---|---:|---|---|']
for r in rows:
 p=r['path']; level='분기 추적' if Path(p).name in detailed else '검색'
 counts='/'.join(str(len(r['hits'][k])) for k in ('thread','wait','sync','recovery','failure'))
 content.append(f'| `{p}` | {category(p)} / {level} | {r["lines"]} | {counts} | `{r["sha256"]}` |')
changed=[r['path'] for r in rows if not (root/r['path']).exists() or hashlib.sha256((root/r['path']).read_bytes()).hexdigest()!=r['sha256']]
content+=['','## 문서 작성 중 기준본과 달라진 파일', '',
 '검사 시 HEAD: `'+subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()+'`. 아래는 **별도 작업의 변경**이며 조사자가 수정한 제품 파일이 아니다. 원장 줄 번호는 고정 사본 기준이다.', '']
content += ['- `'+x+'`' for x in changed] or ['- 없음']
(root/'docs/host_pc_thread_recovery_inventory_2026-09-12.md').write_text('\n'.join(content)+'\n',encoding='utf-8')
probe=['# 2026-09-12 복구 원장 소형 프로브 원문','',
 '[조사 결과](host_pc_thread_recovery_audit_2026-09-12.md#9-직접-실행한-소형-검증). 제품 수정용 코드가 아닌 조사 재현용 원문이다. 실행에는 원장 inventory의 고정 소스가 필요하다. NAS·제품 프로세스를 조작하지 않는다.','']
for name,lang in [('compile-probe.cmd','bat'),('liveness_probe.cpp','cpp'),('server_probe.js','javascript')]:
 probe+=['## '+name,'','```'+lang,(out/name).read_text(encoding='utf-8').rstrip(),'```','']
(root/'docs/host_pc_thread_recovery_probes_2026-09-12.md').write_text('\n'.join(probe),encoding='utf-8')
print(json.dumps({'files':len(rows),'branch_traced':deep,'search_only':len(rows)-deep,'changed_since_snapshot':changed},ensure_ascii=False))
