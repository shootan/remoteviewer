import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = '''  - 설치기 안에 **0.2.105 가 3건, 0.2.104 는 0건**(UTF-16 스캔). payload 이름 **`GNLinkUpdater.exe`·`GNLinkHost.exe`·`ui\\shell.html`·`macro.html`** 존재 — `ui\\shell.html` 이 **깨지지 않은 형태**로 들어 있다(#468 의 escape 결함 회귀 확인).'''
new = '''  - 설치기 안에 **0.2.105 가 3건, 0.2.104 는 0건**(UTF-16 스캔). payload 이름 문자열도 있고, `ui\\shell.html` 이 **깨지지 않은 형태**다 — **#468 escape 결함의 회귀 확인으로는 이걸로 충분하다.**
  - ⚠️ **다만 이름이 있다는 것은 payload 가 그 바이트라는 증거가 아니다.** 내가 처음 낸 근거가 그것뿐이었고, **약했다.** 실제 근거는 **PE 리소스 디렉터리를 파싱해 RT_RCDATA 를 꺼내 staging 9파일과 대조한 것**이다: id 200~208 ↔ Host 601,600 / Stream 974,848 / InputService 226,304 / Capture 127,488 / Client 635,904 / Viewer 848,896 / shell.html 12,648 / macro.html 10,996 / **Updater 482,816** — **크기·SHA256 9/9 일치, 예상 외 RCDATA id 없음.** (검증용 측정. 설치 실행·재빌드 없음.)
  - **재빌드 대조는 별개 사실이다**(provenance 근거가 아니라 부수 확인): `9ff1716` 재빌드본과 `dist` 산출물은 **크기 동일**하고 **다른 바이트가 40 개**뿐이며, 그중 **36 바이트가 `.rsrc` 안** — 항목별로 Host 4 / Stream 6 / InputService 6 / Capture 6 / Client 4 / Viewer 6 / Updater 4 로, **전부 내장 PE 각각의 COFF·Debug TimeDateStamp** 다. html 두 개는 **바이트 동일**, **미분류 0 바이트**, **CodeView(PDB) GUID 는 한 바이트도 다르지 않다.** 즉 **차이가 코드·데이터가 아니라 타임스탬프에 한정된다는 측정**이지, "재현 빌드가 원래 안 된다" 는 일반론이 아니다.'''
assert old in d, 'utf16 claim'
d = d.replace(old, new, 1)

old = '''  - **검증용의 산출물 검사에서 걸렸다.** 내 릴리스 기록에는 빠져 있었다 — 산출물이 무엇을 바꾸는지 적을 때 **버전과 해시만 적고 권한을 안 봤다.**'''
new = '''  - **검증용의 산출물 검사에서 걸렸다.** 내 릴리스 기록에는 빠져 있었다 — 산출물이 무엇을 바꾸는지 적을 때 **버전과 해시만 적고 권한을 안 봤다.**
  - ⚠️ **권한은 `app/build/intermediates/**` 가 아니라 APK 자체에서 확인할 것.** intermediates 의 release 쪽 `AndroidManifest.xml` 에는 이 권한이 **없다**(낡은 산출물이다). 그걸 근거로 "권한 안 늘었다" 고 읽을 수 있다. 확인은 `aapt2 dump badging` 또는 APK 안 manifest 문자열 풀로 한다 — **두 APK 를 그렇게 대조해서 추가 1 · 제거 0** 을 확인했다.'''
assert old in d, 'permission note'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('provenance basis corrected, and the intermediates trap recorded')
