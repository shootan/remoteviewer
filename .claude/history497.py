# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 497) 2026-09-10 🟡 스위트 4개가 빨개졌다 — **원인 미확정, 다만 세션 상태가 그 사이에 바뀌었다**
검증용이 `53d4f66` 을 **구현 OK** 하면서 함께 보고했다: 자기 직전 전수(`76897cf`)에서 rc=0 이던 **`host_bgra_scale` · `host_encode_epoch` · `viewer_udp_recovery`** 가 rc=1 이고, `gdi_capture_process` 를 더해 **4개**. `udp_control_e2e` 는 오히려 **1P/17F → 16P/2F 로 개선**.

**우리 변경 때문인가 — 도달성으로는 아니다 (검증용 확인)**
- `host_bgra_scale`·`host_encode_epoch` — **변경 파일 링크 0건.**
- `viewer_udp_recovery` — `directory_client.cpp` 를 **링크는 하지만**, `HostAgent` 도 신규 함수도 `directory::` 심볼도 **쓰지 않는다.** 우리 변경은 전부 `HostAgent::Run` 과 신규 함수 안이라 **도달하지 않는다.**
- ⚠️ **링크 겹침 0 만으로 무관을 증명하지 않는다**는 기준은 유지한다. 도달성은 *"이 경로로는 아니다"* 까지만 말한다.

**내가 재실행해서 본 것 (전부 사실, 해석 아님)**
| 스위트 | 실패 지점 | 산출 |
|---|---|---|
| `host_bgra_scale` | `capture_window_thumbnail(desktop)` → **`FAIL desktop thumbnail captured`** | PASS 0 / FAIL 0 (단정 전에 끝남) |
| `gdi_capture_process` | — | **`GDI_DELIVERED_FRAMES=0`**, fps 0, copy 통계 전부 0 |
| `host_encode_epoch` | `hold_for(...)` — **`surface(D3D11 NV12)` 하드웨어 MFT 경로** | `A surfaced 0 us after its capture` |
| `viewer_udp_recovery` | epoch 전환 — `transitions +0, requests +0` | 미디어 타이밍 |
| `udp_control_e2e` | — | 16 PASS / 2 FAIL (**개선**) |

**🔴 두 실행 사이에 `qwinsta` 가 달라졌다**
```
녹색이던 때:  >console   shotan   1  활성
지금:          (없음)    shotan   1  Disc        ← 사용자 세션이 콘솔에서 떨어졌다
               console            8  Conn        ← 콘솔에 붙은 사용자가 없다
```
- 테스트 프로세스는 **세션 1** 에서 돈다. 그 세션이 **Disc** 면 **붙어 있는 데스크톱이 없다.**
- 빨간 4개는 **전부 데스크톱 캡처·GPU 인코더 의존**이고, 전달 프레임이 **0** 이다. 느린 게 아니라 **아무것도 안 나온다.**
- 반대로 **그 의존이 없는 `udp_control_e2e` 는 좋아졌다.** 코드가 아니라 **환경이 움직였다**는 쪽을 가리킨다.
- ⚠️ **그래도 "디스플레이 탓" 으로 단정하지 않는다.** 세션 상태 변화는 **관측된 사실**이고, 그것이 **이 4개의 원인이라는 것은 아직 증명되지 않았다.** 다른 terminate·타이밍 요인을 배제하지 않았다.
- ⚠️ **CLAUDE.md 규칙의 이유가 그대로 적용된다**: 세션 상태가 잘못된 채 나온 결과는 **PASS 든 FAIL 든 판정 근거로 쓰지 않는다.** 지금 이 4개는 **판정이 아니라 관측**이다.
- **깨끗한 판정 조건**: 콘솔 세션이 다시 붙은 뒤 **같은 바이너리로 재실행.** 그때도 빨가면 그때부터 코드 문제로 다룬다.

**전수 합계가 서로 다른 것도 기록해 둔다**
검증용 **1780**, 내 실행 **1762**, 차이 **18**. 다투지 않고 항목으로 쪼갠다:
- `udp_control_e2e` 가 그쪽 실행에서 **1P → 16P**(+15) — 차이의 대부분이 여기다.
- 남은 **+3** 은 **아직 설명되지 않았다.** 빨간 스위트가 죽기 전에 내는 PASS 줄 수가 실행마다 다른 것이 후보지만 **확인하지 않았다.**
- ⚠️ **합계가 다를 때 누가 맞느냐를 겨루지 않는다.** 숫자는 **차이를 항목으로 분해할 때만** 증거가 된다.

**하지 않은 것**: 세션 재연결·로그온·프로세스 종료 없음(사용자 것이다). 코드 수정 없음 — **`53d4f66` 산출물은 그대로다.**
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history 497')
