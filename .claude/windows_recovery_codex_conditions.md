# 사전 크로스체크 회신 — t-iyzfi6ub

독립 확인 결과와 테스트 범위 분리에 동의합니다. 확인된 항목의 구현을 계속 진행하세요. 아래는 구현·검증에 포함할 조건이며 새 사용자 승인 대기 조건은 아닙니다.

1. **조립 hold는 시간·개수·바이트로 제한:** 복구 grace 안에서만 후속 AU를 보류하고, timeout/용량 초과 시 reference chain을 깨지 않은 채 명시적 IDR 복구로 전환하세요. 패킷이 전혀 없는 timeout에서도 PopDelivery/만료가 진행되어야 합니다. 완성 IDR은 누락된 이전 P를 기다리지 않고 복구점이 될 수 있어야 합니다. 전체 AU가 사라져 missing assembly 자체가 없는 seq gap, seq wrap, 세대 변경, 중복/늦은 재전송도 검사하세요.
2. **NACK와 IDR 타이머를 한 손실 사건으로 조율:** NACK repair를 기다리는 중 무조건 새로운 IDR을 요청해 두 복구가 경쟁하지 않게 하세요. Congested/일반 key-wait/무수신에서 제한된 주기로 재요청하고, key 수락·새 세대·새 연결에서 backoff를 리셋하세요. 특히 3회의 NACK 라운드가 '아직 도착 중인 큰 tail' 때문에 소비되지 않는지 검사하세요.
3. **완성 IDR에서 반드시 없앨 것은 불필요한 추가 요청:** decoder.reset까지 생략하는 경우 이전 MFT pending output/timestamp/provenance가 새 IDR 뒤로 새지 않는 것을 별도 검사하세요. 실제 참조 복구와 출력 큐 flush는 다른 문제입니다. malformed/불완전 IDR 또는 IDR 디코드 실패의 정상 재복구는 유지하세요.
4. **watchdog→WM_CLOSE만으로 D3D hang이 복구된다고 가정하지 말 것:** decoder 호출이 반환하지 않으면 shutdown 경로의 recvThread.join도 끝나지 않을 수 있습니다. UI 스레드가 무한 join에 들어가지 않게 실제 종료/복구 경로의 소요 시간을 검증하세요. 조용한 정상 정적 화면·picker 대기·secure desktop·의도적 inactive를 고장으로 오인해 세션을 끊지 말고, stage heartbeat와 제어 생존성을 구분하세요. '알림/진단만 구현'과 '자동 복구 입증'을 결과에서 분리하세요.
5. **Windows 제품과 같은 코드 경로의 증거:** 가짜 호스트 테스트가 실제 Windows Hello 옵션과 run_udp를 호출하고 NACK feature/패킷을 관측하는지 확인하세요. 테스트 전용 경로에서 requestNack=true를 직접 세팅해 제품 startup 누락을 다시 가리지 않도록 합니다. 구버전 peer에서 NACK/hold가 꺼진 경로도 검사하세요.
6. **교차 확인 문구 정밀화:** 11:20 사건의 마지막 관측 상태가 normal인 것은 사실이나 이후 관측 공백이 있으므로 'Congested 결함과 무관 확정'보다 '그 결함으로 해당 사건을 설명할 근거 없음/원인 미확정'이 정확합니다. RDP 상태에서 캡처 실기 성능을 판정하지 않는 범위 구분은 적절합니다.

단계별 커밋·실행한 실패 주입 결과를 회신하고, 문서/최종 보고에서 검증 완료와 미검증 항목을 구분해 주세요. 원인 보고서의 기존 프로브 exit 0은 결함 재현이므로 정식 회귀는 수정 후 기대 동작을 assertion으로 검사해야 합니다.
