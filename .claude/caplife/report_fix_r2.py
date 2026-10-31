p = 'D:/remote/remote/.claude/caplife/report_e2_relay_investigation.md'
s = open(p, encoding='utf-8').read()


def cut(start, end, new):
    """Replace from the line starting with `start` up to (not including) the line starting with `end`."""
    global s
    i = s.index(start)
    j = s.index(end, i)
    s = s[:i] + new + s[j:]


cut('## 핵심 5줄', '## 구간별', '''## 현재 결론 (r2 정리: 아래 "보충"·"ke_r21 정정" 절과 같은 확정 수준으로 맞춘 하나의 결론)
> 이 절이 이 보고의 결론이다. 아래 본문의 옛 표현 중 이보다 강한 것(relay·fixture "원인 아님", "회귀 아님", "control 정지 원인")은 이 절의 수준으로 읽는다.
1. **relay**: "relay 가 영상 전체를 막았다"는 가설은 AU/delta 가 sink 까지 도달한 사실과 맞지 않아 **기각**한다. 그러나 단순 forward source(server.js:2120–2150)와 세션 합계만으로 **모든 IDR/chunk 가 제때 전달됐다거나 relay·UDP·fixture 의 기여가 없었다고는 확정하지 않는다**. 수신 loop 정지 때 socket backlog/drop 이 있었을 가능성도 구간 결과에 포함한다. `client silent` 닫힘은 60 s 무통신 뒤의 결과다(server.js:537, :1759–1765). 통과 세션도 같은 줄로 닫혔다.
2. **영상 0 의 직접 단계**: 약 19.6–29.6(10 s) 동안 surface/codec 이 준비되지 않았다. 그 사이 도착한 gen-2 IDR 은 `KeyLost` 됐다(현재 정책이며, 그것만으로 새 결함이라고 하지 않는다). codec 이 준비된 뒤에는 delta 만 와서 실제 queue 는 0 이었다. 자세한 내용은 보충 절의 ③·④ 다.
3. **control 이 끊긴 이유는 확정하지 않았다.** 유력 가설은 source 로 확인된 제품 구조(codec 작업과 control 진행이 같은 lock·loop 에 묶임)이고, 26.4–29.6 구간을 설명할 수 있다. 그 구간의 직접 RX wait 계측은 없다. 설명되지 않는 구간이 둘 있다:
   - ≈25.0–26.4 의 무응답
   - 30.65 재개 뒤 `keyframe-request seq=7` 이 Host 에 보이지 않은 것. 이것은 그 관측 지점에서 안 보였다는 사실일 뿐이고, wire 에서 유실됐다고 단정하지 않는다.
4. **회귀 여부**: decoder·server 파일이 ke_r16 이전 그대로라는 것은 그 파일들이 같다는 근거일 뿐이다. 호출 순서·build·fixture·부하 전체에서 회귀가 없었다는 증명은 아니다. 참고로 ke_r18 은 같은 sharpen fallback 경로에서 codec 재생성이 0.6 s, ke-r23 은 6.0 s 였다.
5. **ke_r21**: A 의 E7g peer_lost 는 의도된 탈취 종료다. B 의 후속 Host 종료(08:32:27 peer-lost, 영상 decode 중)와 하네스의 메뉴 중단은 **미분류**다. ke-r23 과 원인이 같다고도 다르다고도 하지 않는다.
- 유발 조건(관측): 에뮬레이터 과부하(EGL 4.2 s, slow dispatch 2.6 s). 하네스는 그 구간에 UI 조작이 없었다. 기여 근거는 없지만 배제를 증명한 것도 아니다.
- 제품 대응: Codex r2 결정에 따라 (b) RX/control 의 codec 동기 의존 제거를 구현했다(27d5966). (a) 준비 뒤 IDR 회복은 계측·대조로 확인한다(report_decoder_liveness_r1.md).

''')
s = s.replace('- 결론: **둘 다 끊겼다. 다만 원인은 네트워크가 아니라 앱 수신·제어 스레드의 정지**다. relay 는 끝까지 양방향을 전달했다.',
              '- 결론: **둘 다 끊겼다.** Host 송신 기록과 relay 합계로 보면 영상은 끝까지 보내졌다(전달·수신의 증거는 아니다). control 이 끊긴 원인은 위 "현재 결론" 3번 수준(유력 가설, 미설명 2구간)이다.')
cut('## 분류', '## 2단계 제안', '''## 분류
- 위 "현재 결론"으로 대신한다(옛 표현인 "fixture/relay 원인 아님", "제품 원인"은 철회하고 그 수준으로 낮췄다).

''')
s = s.replace('B 는 standby activation 으로 열린 세션이다. control 과 video 가 같은 socket·같은 loop 를 지나는지는 아직 확인하지 않았다. **별도 미결**로 둔다.',
              'B 는 standby activation 으로 열린 세션이다. control 과 video 가 같은 socket·같은 loop 를 지나는지는 아직 확인하지 않았다. B 의 후속 종료는 **미분류**로 둔다.')
open(p, 'w', encoding='utf-8').write(s)
print('ok')
