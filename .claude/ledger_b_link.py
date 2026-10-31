import io

p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()

old = '''### (c) `do_install()` 은 설치 후 제품을 재실행하지 않는다 → "재실행"은 전부 업데이터의 신규 책임'''
new = '''#### (b) 에 대해 실제로 한 일 (2026-09-08 구현, history #455·#456)

Codex 확정대로 **`/T` 폐기**를 축으로 풀었고, 그 결과 설치기 쪽에서는 **아무것도 더하지 않는 것**이
답이었다. 세 가지가 함께 성립해야 (b) 가 닫힌다.

1. **업데이터를 `stop_running_product()` 의 `kImages` 에 넣지 않았다.** 넣으면
   `taskkill /F /T /IM GNLinkUpdater.exe` 가 업데이터와 **그 자식으로 방금 재실행된 제품까지** 죽인다
   — (b) 를 고치러 와서 (b) 를 새로 만드는 것이다. 그리고 넣을 이유도 없다: 돌고 있는 업데이터는
   `GNLink.update` 의 **복사본**이므로 설치기가 필요로 하는 `installDir` 의 파일을 하나도 잠그지
   않는다. **넣을 이유가 없고 넣으면 해로울 때는 넣지 않는다.**
2. **업데이터가 자기 복사본으로 재실행한다**(W2a). 원본은 `installDir` 안에 있어 교체 가능하고,
   실행 중인 복사본은 밖에 있어 교체 대상이 아니다.
3. **`CREATE_BREAKAWAY_FROM_JOB`.** 호스트가 job object 에 속해 있으면 그 자식인 업데이터도 호스트
   정지와 함께 죽는데, **정지가 바로 다음 단계다.** 실패하면 플래그 없이 재시도하되 그 사실을
   로그에 남긴다 — 조용히 job 안에서 도는 것이 가장 나쁘다.

`(a)` 쪽 대칭도 같은 방식으로 지켰다: 언인스톨이 `installDir` **밖**의 작업 복사본을 지울 때, 열려
있으면 설치기가 자기 이미지에 이미 쓰고 있던 **`MOVEFILE_DELAY_UNTIL_REBOOT` 폴백을 그대로** 쓴다.
새 메커니즘을 만들지 않았다.

### (c) `do_install()` 은 설치 후 제품을 재실행하지 않는다 → "재실행"은 전부 업데이터의 신규 책임'''
assert old in d, 'defect c anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ledger (b) linked to what was done')
