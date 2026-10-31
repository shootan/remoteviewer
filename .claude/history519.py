# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 519) 2026-09-11 `0.2.116` 게시 — **누를 수 있는 첫 릴리스**, 그리고 http 예시 한 줄
`0.2.115` 는 고아 수정이 실린 첫 릴리스였지만 **Client 에 버튼이 없었다**(#518). 이것이 알림과 행동이
둘 다 있는 첫 릴리스다.

**같이 들어간 한 줄** (검증용이 #518 스크린샷에서 발견)
서버 주소 입력칸 placeholder 가 `http://주소:8080` 이었다. **업데이트 경로는 http 를 두 층에서 거부**하므로
이 예시는 사용자를 **업데이트가 조용히 꺼지는 설정으로 유도**한다 — 이 PC 의 저장값
`http://223.130.132.180:8080` 과 같은 형태이고, 그 값 때문에 이 PC 의 확인은 전부 `not configured` 였다.
`https://주소` 로 바꿨다. ⚠️ **그 이상은 하지 않았다** — 주소 검증도, 강제 승격도 범위 밖이다.

**산출물 대조** (0.2.115 → 0.2.116): 바뀐 것 **5개**.
Host · Client `711168`→**712192** · Setup · Updater · **`ui\\shell.html` `12648`→`15483`**(배너·버튼·핸들러).
나머지 5개 바이트 동일.

**게시 전후** — `0.2.115` 와 같은 순서, 같은 게이트
- payload-set 게이트 통과 · `remote60_verify_release` **실문서 Ok + 1바이트 변조 `SignatureInvalid`**
- manifest `07ec0ad2…` 1687B · sig `435cf93d…`
- 서버 10/10 · **공개 URL 10/10** 익명 재확인 · **서버가 내주는 문서를 되받아 제품 검증기로 재검증**(서명 바이트와 동일)
- `0.2.115` pair 백업 · **`0.2.115` 아티팩트 4종 해시 서버에서 직접 대조 → 불변 확인**
- `dist/GNLinkSetup-0.2.116.exe` — **공개 URL 이 내주는 바이트와 `cmp` 로 동일 확인**(스테이징본 대조로 끝내지 않음)

**⚠️ 이 릴리스에 대해 말할 수 없는 것**
`client_update_ui_test` 17 PASS 는 **메시지가 도착했을 때 버튼이 있고 누르면 나간다**는 것까지다.
**실서버 확인 → 팝업**은 여전히 미검증이고, 계정·네트워크가 필요하다. 전달은 **(b) 수동 설치**로 한다 —
(a) UI 파일만 보내는 것은 **순환**이다: 버튼을 보내려면 버튼이 필요하고, 남은 경로인 Host 트레이는
0.2.114 Host 에 고아 수정이 없어 09-11 에 두 번 기기를 불능으로 만든 경합을 탄다.

**부기 — 관측 기준선을 잃었다**
14:37 이후 이 PC 의 Host·Stream 이 사라졌다. **원인 미상**이다. 제가 종료한 것은 관측용으로 띄웠던
임시 Client(pid 15640) 하나뿐이고, `host_app.log` 는 14:37:17 에서 끊기며 종료 줄이 없다.
**포트를 쥔 고아는 없다**(43000·3478 소유자 0) — 정상 종료와 모순되지 않는 모양이다. 단정하지 않고
기록만 한다. ④ 재시험은 어차피 `qwinsta` 가 **RDP Active** 라 판정 불가 상태였다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history ok')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'0.0.13 [~] **Client 에 업데이트 버튼이 없었다'
new = (u'0.0.14 [x] **`0.2.116` 게시 — 알림과 행동이 둘 다 있는 첫 릴리스** (history #519) — manifest '
       u'`07ec0ad2…`, 공개 URL 10/10, 서버 문서 제품 검증기 재검증 통과, `dist/GNLinkSetup-0.2.116.exe` '
       u'= 공개 URL 바이트와 동일. http placeholder 한 줄 동반 수정. **0.2.115 불변 확인.** 전달은 '
       u'**(b) 수동 설치** — UAC 1회는 사용자 고지·동의 후\n'
       u'0.0.13 [~] **Client 에 업데이트 버튼이 없었다')
assert t.count(old) == 1, 'tasklist anchor'
io.open(p2, 'w', encoding='utf-8', newline='').write(t.replace(old, new, 1))
print('tasklist ok')
