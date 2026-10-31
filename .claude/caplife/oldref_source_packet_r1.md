# rel-0164 old-apk-lane — 구·신 참조 대조 SOURCE 패킷 r1 (2026-10-30, task_old_apk_ref_r1 / 정본 codex_old_apk_lane_peer_lost_decision_r1.md)

## 1. 고정
- **구 tree** `C:\work\gnlink-wt\old-ref-163`, branch `test/old-ref-163`. 커밋 순서는 다음과 같습니다.
  - fae0e1f(게시 0.2.163 source)
  - **5572856** 안전 port만
  - **ff98f19** 진단만
  - 별도 worktree이며 삭제 후보에 기록했습니다. `third_party\webview2`는 junction입니다.
- **신 tree** `C:\work\gnlink-wt\old-apk-lane`, c45fb9b(검증용 r7j) 위의 **7f6c7fe**입니다. 진단, 두 ref의 relay trace, to-3w scope, build 폴더 조립이 들어 있습니다.
- c45fb9b verify tree는 바꾸지 않았습니다. 실제 Host와 AVD 실행은 0입니다.
- diff (`caplife/`):

| 파일 | 범위 | sha256 앞자리 |
|---|---|---|
| `oldref_safety.diff` | fae0e1f..5572856 | `deeedd4b…` |
| `oldref_diag_old.diff` | 5572856..ff98f19 | `bd7c10f4…` |
| `oldref_diag_new.diff` | c45fb9b..7f6c7fe `apps/` | `dcd27499…` |
| `oldref_lane_tools.diff` | 같은 범위 `automation/` | `8484af61…` |
| `oldref_SHA256SUMS.txt` | 바뀐 23파일 blob | `dd7f6781…` |

- 채널과 단위시험의 진단 diff 줄은 구·신이 **동일**합니다. 결과 파일 hash는 `31dcfffd` / `9eb7520c`로 양쪽이 같습니다.

## 2. 읽기 전용 대조 (source mapping)
| 항목 | 결과 |
|---|---|
| **게시 directory** (NAS `/opt/gnlink/remote60-directory`, gnlink key로 읽기만 함) | `server.js`(`fdc9cbe8…`, LF, 10-11 배포)와 의존 7파일 모두 **fae0e1f = a0f991a와 바이트 동일**합니다. 게시 상태와 source에 차이가 없습니다. |
| 구·신 directory 차이 | 바뀐 것은 `server.js` 하나입니다. 의존 7파일은 53fced4와 같습니다. |
| fixture env ↔ 구 server | 구 server에는 `REMOTE60_RELAY_HOST_FACING_IP`/`_BIND`가 **없습니다**. 신 server에서 이 둘은 **standby 예약 소켓 전용**입니다. ordinary relay(S의 비교 구간)는 양쪽 모두 host의 관측 tuple로 보내므로 영향이 없습니다. 구 server는 이 env를 무시합니다. standby(case 7)는 구 조합 범위 밖입니다. |
| 구 server의 relay 제공 규칙 | NP의 early relay는 구 server에 없습니다. 구 규칙(`relayEligibleFor`) 그대로이며, 이것도 비교 대상의 일부입니다. |
| control transport | `udp_control_channel.cpp/hpp`는 fae0e1f와 53fced4에서 **같은 blob**입니다(Codex 확인과 일치). |
| PinnedSelectTest seam(target 격리) | seam 블록이 있는 11파일의 `#ifdef` 블록이 구·신 동일합니다. |
| harness가 읽는 host 줄 | capture-window, window-list, window-select, `[keyframe]` size, waiting client bindPort, `[input-trace]`: 출력 문장이 구·신 **동일**합니다(54줄 diff 0). |
| harness가 넘기는 switch | `--bind-address` `--bitrate` `--capture-window-pid/title` `--control-port` `--directory-*` `--fps` `--host-name` `--input-injection-mode` `--input-log-every` `--seconds` 모두 구 host가 parse합니다. `--host-power-management`만 없었으며 안전 port로 추가했습니다. |
| harness의 host staging | host exe 하나만 staging 폴더에 `GNLinkStream.exe`로 복사합니다. 동반 exe(GNLinkCapture 등)는 구·신 모두 없습니다. |

## 3. 안전 port (구 host만, 5572856, 5파일 +83/−6)
- **문제**: fae0e1f는 `HostPowerKeepalive`를 `parse_args`보다 **먼저** 만들었습니다.
  - 생성자가 인자를 읽기 전에 `ES_SYSTEM_REQUIRED`를 요청했습니다.
  - stream 시작 wake가 실제 마우스 ±1과 `SC_MONITORPOWER`를 보냈습니다.
  - power switch 자체가 없었습니다.
- **53fced4에서 가져온 것은 이것뿐입니다**:
  - `host_power_policy.hpp`를 바이트 그대로 가져왔습니다(guard가 그 줄 전체를 비교함).
  - `Args::hostPower` / `--host-power-management on|off`: 다음 토큰 하나를 받고, 다른 값이면 거부합니다.
  - `HostPowerKeepalive(enabled)`: OFF이면 요청·해제·wake가 모두 0입니다. 이 host의 모든 power 효과는 이 class(ctor/dtor/SetStreaming)만 지납니다.
  - main 순서: parse 먼저 → 정책 줄 → 잘못된 값이면 exit 65 → keepalive.
- **input은 port가 필요 없습니다.** fae0e1f의 `--input-injection-mode none`은 후보와 같은 분기에서 같은 줄 전체(`input injection disabled (unsupported mode) mode=none`)를 냅니다.
- UDP/TCP control, Hello/Pong, ACK/retry/timing, directory auth, capture/codec은 fae0e1f 그대로입니다(backport 0).
- 이 build를 "게시 binary 실행"이라고 부르지 않습니다.

## 4. 진단 (구·신 동일, test-only, header-only)
- **켜지는 곳**: `REMOTE60_CONTROL_CLOSE_DIAG`일 때만입니다. PinnedSelectTest host와 단위시험에만 켜고, 제품 GNLinkStream에는 없습니다.
- **기록 위치**: `udp control session ended … reason=…` 줄 끝에 한 줄을 붙입니다. 내용은 다음과 같습니다.
  - `diag{ack seen / wrongStream / accepted / stale / probe / nack / afterClose / lastSeenSeq / lastAcceptedAgoMs / lastAckedTxSeq}`
  - `head{seq type attempts frags queue firstSendAgoMs lastSendAgoMs}`(닫을 때 ACK를 기다리던 메시지)
  - `lost{…}`(재전송이 소진된 순간의 head)
  - `rx{deliveredSeq pending ready}`
- **type**은 payload `MessageHeader`의 type 필드 하나만 읽습니다. 그 뒤 payload는 기록하지 않습니다.
- **accepted**는 `HandleAck`가 head를 pop한 것만 셉니다. socket 송신이나 ACK 도착(seen)과 구분합니다.
- **단위시험** `udp_control_close_diag_test` 6/6(구·신 각각, 실제 채널 2개를 메모리로 연결):
  - ACK가 없으면 peer-lost, `lost{seq=1 type=37 attempts=4}`, accepted=0, body 문자열 0
  - 늦게 온 ACK → afterClose=2, accepted=0
  - 정상 교환 → accepted=3, head{none}, lost{none}

**relay** (두 ref 공통, `oa_relay_trace_server.py --ref new|old`):

| ref | base | trace diff | 결과 |
|---|---|---|---|
| new 53fced4 | `2742143d` | `9883b4db` | `18f6b6d2` (r7f의 `52918c04` 대체) |
| old fae0e1f | `fdc9cbe8` | `e7d993eb` | `196405dd` |

- old diff: 7a79b09에서 적용되는 hunk에 c→h forward를 손으로 다시 잘라 넣었습니다. standby hunk 4개는 붙일 대상이 없습니다.
- 두 ref 모두 **정기 snap**에 직전 snap 이후 **완료된 1 s bucket**(방향·header kind)과 `bucketsThrough`를 남깁니다. fixture가 Node를 handle로 끝내면 close 줄이 남지 않기 때문입니다.
- "sent"는 socket의 응답일 뿐입니다. ACK 수락은 host 진단으로만 봅니다.

## 5. 대조
| 대조 | 결과 |
|---|---|
| relay check new / old (`.claude/relay_check_ref_*.txt`) | 각각 다음과 같습니다:<br>• POSITIVE: 7a79b09 relay_trace_test ALL PASS, snap test ALL PASS(Node kill 전 snap에 c2h/h2c bucket)<br>• NEGATIVE: base에 env만 → "the trace says it is on" FAIL(relay는 정상) |
| snap 대조 | r7f 사본 52918c04 → snaps=2, with buckets=0 → FAIL |
| build gate (실제 바이트 문자열) | 구·신 모두:<br>• GNLinkStream: diag 0, seam 0, power OFF 줄 있음(구는 이번 port)<br>• PinnedSelectTest: diag 1, seam 1, power OFF 1<br>• harness: diag 0 |
| oa_pick_test 76/76 | + to-3w: exit 0, picture tap 0, 4–7 NOT RUN(scope)<br>대조: scope 없으면 같은 흐름이 4로 감 |
| oa_orchestration_test 26/26 | — |
| guard 거부 대조 | port 안 된 host는 power 줄을 내지 않아 guard가 거부합니다. 후보의 `e2e_injection_guard_test`가 덮는 경우이며 이번에 새로 실행하지는 않았습니다. 구 host의 실제 수락은 실기에서 처음 보입니다. |

## 6. 산출물 (각 1회 실행용 build 폴더, `oa_ref_builddir.ps1`, `old-apk-lane\.claude\oa_ref_build\`)
| exe | ref-old-r1 | ref-new-r1 |
|---|---|---|
| remote60_mobile_abr_session_e2e_test.exe | `caf933cd…09b6` | 같음 |
| e2e_job_run.exe | `248d9829…f727` | 같음 |
| remote60_ka_standby_holder_tool.exe | `e9ae9415…4084` | 같음 |
| GNLinkViewerKaTest.exe | `6806dd41…2a5d` | 같음 |
| **GNLinkStreamPinnedSelectTest.exe** | **`305e870c…ae07`** (old-ref-163 ff98f19) | **`7d1cd935…5bd9`** (old-apk-lane 7f6c7fe) |

- 도구 4개는 같은 파일이고 host만 다릅니다. MANIFEST.txt에 host 문자열 수를 함께 적었습니다.
- 제품 GNLinkStream(gate용): 구 `f4c06f59…`, 신 `c81217c0…`.

## 7. 실행 계획 (구 → 신, 각 1회, arm S, 같은 APK 50080b2b·SDK/AVD boot·SH-off·harness 기본 fps/bitrate·target·배경)
1. relay 사본을 만듭니다. `python -I automation\old_apk_lane\oa_relay_trace_server.py build <wt>\.claude\oa_relay_server\<새 이름> --ref old|new`로 만들거나, 이미 만든 `ref-old-t1`(196405dd), `ref-new-t1`(18f6b6d2)을 씁니다.
2. 구: env `KE_RELAY_TRACE=1`, `oa_slot.ps1` 인자는 다음과 같습니다.

   ```
   -Arm S -Apk <S 50080b2b>
   -BuildDir <wt>\.claude\oa_ref_build\ref-old-r1
   -ServerJs <ref-old 사본>\apps\directory\server.js
   -ServerSha256 196405dd60a82c46bab2e57a563b19822517710a92e87d17f62591d3801088d5
   -Scope to-3w
   -AdbSha256 <기존 pin>
   ```

   - 범위: login → ordinary/relay → 선택/first frame → 30 s 유지.
3. 신: 같은 인자에서 세 가지만 다릅니다.

   ```
   -BuildDir …\ref-new-r1
   -ServerJs <ref-new 사본>
   -ServerSha256 18f6b6d211f0337d9f38db1b689ea20e74261225ab0827f5a0132708205e879e
   -Scope full
   ```

   - 같은 구간이고, 성공하면 ④–⑥까지 이어 갑니다.
4. 그대로 유지하는 것: slot/fixture/job/EOF/guard, SDK pin, 자기 slot, 첫 nonzero 중단.
5. 판정에 쓸 원문은 다음 셋입니다:
   - host `udp control session ended … diag{…} head{…} lost{…}`
   - relay `[relay-trace] … snap … buckets=[…]`
   - app `[status]`/`[scene]`

## 8. 한계·미검증
- **구 host 실행은 0입니다.** guard가 구 host를 수락하는지(두 OFF 줄)와 구 host와 새 harness의 실제 흐름은 첫 실행에서 확인됩니다. guard가 거부하면 INVALID(client 0)입니다.
- **구 server의 standby 경로**(host-facing IP 없음)는 비교 범위 밖입니다. 구 조합에서 standby·P를 돌리지 않습니다.
- **진단의 한계**:
  - host 쪽 head·ACK만 기록합니다. APK 쪽 head나 lastError는 여전히 없습니다(APK 수정 0).
  - relay는 header kind 계수까지입니다. control seq 대응은 host 진단과 시각으로만 맞춥니다.
- **안전 port의 순서 이동**: parse_args가 `startup_process_setup()`보다 앞서 실행됩니다(후보와 같은 순서). parse_args는 인자와 `--config`만 읽습니다.
