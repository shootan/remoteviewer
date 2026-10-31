# NP 직접 연결/중계 문서 r1 — Codex 전문 검토

2026-10-22 · task=np-direct-connect-doc / rev=r1 / kind=doc-review-result
reply_to 검증용 `remote#wwml7h91` · main c04eb47 위 문서4파일

**NEEDS_CHANGES.** NP 조사/후속 후보 등재는 OK지만 현재 집계의 범위·성공 의미와 인과 표현은 그대로 커밋할 수 없다. 아래 지적을 신규 문서·NP 행·history에 한 묶음으로 맞춘다. 제품 수정·새 NAT 구현/외부 서비스·공유기 설정 변경은 요구하지 않는다. LK/DW와 독립 진행한다.

후보 SHA256 앞12 직접 일치 확인: 신규 `직접연결_중계_분석_2026-10-22.md` a12ce02c9ea0, 작업목록4e6ecefb80fa, 구현계획db8640691294, history8cc93f71fc68.

## N1. 분모/합계와 bound의 의미

- 표 합은 **282**다: 집PC179 + 집PC2 27 + 회사PC76. ‘293 접속’과 **11건 차이**가 난다. 빠진 그룹/제외 이유를 적거나 집계 총수를 수정하고 합계 행을 둔다. 해당13일 전체 GNLink 접속 수인지 **relay eligible로 관찰된 고유 connectId 시도**인지도 구분한다. `eligible`는 server의 relayEligible 분기 안에서만 기록된다. 접속 성공·실기 횟수로 바꾸지 않는다.
- `relayBindSession`은 유효 Hello를 받아 **state=provisional, c2h/h2c=0**인 session을 만들고 `bound`를 쓴다(server.js 약1759~1789). 따라서 bound는 **중계 바인딩/시도 관찰**이지 HelloAck·영상 성공 증명이 아니다. **active**는 별도의 인증 HelloAck 뒤에 기록된다(약1911~1918). 현재 수치를 유지하려면 열을 ‘bound 있음/없음’으로 명확히 바꾸고 성공률이 아님을 명시한다. 성공 수를 주장하려면 active/클라이언트 인증·첫 프레임 근거를 별도 결합한다.
- bound없음은 이미 적힌 대로 직접 성공/실패/취소/로그 누락 등을 구분하지 못한다. **‘이동통신→회사33/33 중계없음’→‘100% 직접’(history)**은 삭제한다. ‘집PC2 27/27’도 **이 표본의 모든 시도에 bound관찰**일 뿐 모두 중계 영상 성공으로 보고하지 않는다. 63%/70%는 해당 분모의 bound관찰 비율로만 쓴다. 누적 heartbeat 빈도와 접속 성공 비율의 직접 인과도 주장하지 않는다.

## N2. 재현 가능한 집계 출처와 망 분류

- 시작/끝을 `01:3x` 대신 timezone 포함 정확 시각 또는 journal cursor로 고정하고, 추출 명령/필터·**eligible 고유id 중복 제거 및 bound 결합 방식**을 남긴다. 파생 집계표/스크립트/필요한 최소 비밀 제거 발췌의 위치와 hash를 두면 된다. 원문 전체·토큰·개인 payload를 문서에 복사할 필요 없다. 현재 `.claude/punch_relay_analysis_2026-10-22.md`는 후보와 같은 집계 서술이고, 원본 재집계 입력/명령은 없어 내가293건을 독립 재현한 근거는 아니다.
- eligible의 `client=`는 실제 wire 그대로가 아니다. 코드상 **clientIp/clientPort(광고 관찰값)**이며, 같은 LAN이면 `observedAddressFor`가 public IP를 대입할 수 있다. `wire=`가 별도 원본 주소다. 출처 정의의 ‘클라이언트가 서버에 보인 공인주소’를 수정한다. host-hb의 wire/public 구분과 맞춘다.
- 175.207.45.x/211.218.222.x만으로 모든 /24를 집/회사로 확정하거나 118.235/106.240/175.209를 이동통신으로 확정하지 않는다. **분류 규칙에 따른 추정 그룹**임을 표/요약에도 유지하고, 확인된기기+해당시각 접속 망과 대역추정을 구분한다. 서로 다른 기간의 viewer/APK 보조표는 서버 표와 동일 모집단·접속 성공률인 것처럼 합산하지 않는다. ‘직접 성공 median360ms’도 punch 응답시각이면 **직접 후보 punch 응답 median**으로 적는다(전체 인증/첫화면 시간이 아니다).

## N3. port분포는 잘못된 광고의 충분조건이 아니다

- 관찰 port≠43000은 정상적인 NAT 외부 port 재매핑에서도 가능하다. **‘한 번도43000 아님→광고주소항상틀림→27/27중계’는 성립하지 않는다.** 43000/3478과 같다는 것도 외부도달 성공을 보장하지 않는다.
- 확인한 것은 `wire=LAN주소:port` 관찰/서버 public-IP대입 코드와 주소·port분포다. **외부 peer용 실제 mapping과 이 LAN/hairpin 관찰 mapping이 다른지**는 외부 동일 socket/시각 측정·peer 펀치 대상/응답 결합이 필요한 가설이다. ‘63%가 이것으로 설명된다’는 ‘관찰과 부합하는 유력 가설, 매핑오광고의 기여율·인과 미확정’으로 바꾼다. history 제목 ‘집쪽매핑을못본다’도 **LAN관찰 기반광고와 외부mapping 불일치 의심** 수준으로 맞춘다.
- NAT mapping은 상대 destination에 따라 다를 수 있으며 외부STUN이 본 한 tuple도 모든 peer의 정답은 아니다([RFC4787 §4.1](https://www.rfc-editor.org/rfc/rfc4787.html#section-4.1)). ⓐ는 **실제 전송 socket에서 외부 관찰 + fresh connectivity check + 기존relay fallback**을 비교하는 후보라고 적는다. STUN만으로 NAT traversal이 보장되지 않는다는 [RFC8489](https://www.rfc-editor.org/rfc/rfc8489.html)의 범위를 유지한다.
- ⓒ의 ‘43000 고정매핑일때만’은 실제 설정/외부 검증된 mapping이 있는 경우의 후보 조건이지, bindport로 공인 port를 추정하는 새 규칙이 아니다. ⓑUPnP/NAT-PMP의 공유기 변경은 이번 문서 승인으로 실행하지 않는다. OSLink의 클라우드 **시그널링 주소 관찰만으로 실제STUN observer/같은 socket·알고리즘을 확인했다고 쓰지 않는다**. 참고 가설로 남긴다.

## N4. 2500ms는 relay응답지연 정책이며 직접실패 판정기가 아니다

server.js `relayScheduleReply`는 relay punch를 받은 뒤 timer로 응답을 보낼 뿐 **직접 경로의 응답/실패를 관측하지 않는다**. 따라서 ‘직접경로가2.5초안에답하지못할때만중계가이긴다’라는 절대 조건을 ‘relay punch 응답을 기본2500ms 지연해 direct 후보를 우선시키는 race 정책’으로 수정한다. 기준 시점은 client 연결시작이 아니라 relay punch 수신/스케줄이며 도착/재전송/취소·타이머 오차가 있다. source의2500은 env로 바뀔 수 있는 **기본값**, 실제 서버값은 당시 listening/answering 로그 등 확인한 경우에만 확정한다. ms≈2502도 본 표본의 punch응답 시간으로 한정한다.

## N5. relay 라벨 결함의 실제 범위

`candidate_kind_name`/enum에 relay가 없다는 발견은 맞다. 다만 코드에는 단순 출력 외에 연결이 있다: `directory_session_client.cpp`는 unknown kind를 Public으로 보존 → `directory_session_bootstrap.cpp`가 enum을 문자열로 다시 만들어 race에 넘김 → `result.relay = chosen.kind == "relay"` → `viewer_startup.cpp`가 toolbar relayPath에 전달한다. **그 directory PC 경로에서는 relay enum정보 손실이 판정flag에도 전파될 수 있는 코드 근거**가 있다. 문서에 이 경계를 짧게 명시하고 실제 UI 재현은 미검증으로 남긴다. 주소·port로 따로 구분한 집계라는 사실도 유지한다. 주소는 당시 relay 설정 snapshot/기간에 귀속해야 하며 영구 하드코딩 진단 규칙으로 만들지 않는다.

NP 행은 현재 ‘로그·relayPath가 … 표시’라고 확정하고 본문은 ‘아마’라서 어긋난다. **코드 경로에서의 판정 정보 손실 / 특정 배포판 실제 UI 미재현**으로 통일한다. 수정 구현은 이번 문서scope 밖이다.

## 다음과 검증 한계

구현계획의 체크박스와 ‘후속후보/설계판정전/착수승인아님’ 경계는 OK. 미래 효과는 목표이며 측정된개선이 아니라고 유지한다. 보완4파일 후보를 다시 고정하면 위 지적 delta만 재검토→검증용Git MCPcommit(push없음)으로 잇는다. history에는 검토결과와 다음단계를 간결히 반영한다.

직접 수행: 후보4개 hash/본문·diff, 로컬 조사메모, server의eligible/bound/active·observedAddressFor·relay timer, PC parser→bootstrap→viewer 라우팅을 읽고 RFC원문을 대조했다. **NAS journal 원본을 새로 조회·293건을 독립 재집계하거나, 외부mapping/직접접속 성공/relay UI를 실측하지 않았다.** CR-at-EOL 허용 diff-check exit0. 공개 문서/제품코드를 직접 수정하지 않았고 이 미추적 판정서만 작성해 별도commit은 없다.
