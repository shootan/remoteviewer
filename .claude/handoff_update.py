# -*- coding: utf-8 -*-
import io

p = 'docs/nas_release_handoff_2026-09-10.md'
d = io.open(p, encoding='utf-8').read()

pairs = []

# ---------------------------------------------------------------- the wrong size, twice
pairs.append((
u'''
| `dist/GNLink-0.2.16.apk` | 11,518,126 | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |''',
u'''
| `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |'''))

pairs.append((
u'''| `/updates/0.2.16/GNLink-0.2.16.apk` | `dist/GNLink-0.2.16.apk` | 11,518,126 | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |''',
u'''| `/updates/0.2.16/GNLink-0.2.16.apk` | `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |'''))

# ---------------------------------------------------------------- what "only the version differs" means
pairs.append((
u'''| 두 빌드의 차이 | ⚠️ **버전 값뿐이다.** 코드 차이는 없다 — 인앱 업데이트를 시험하려면 "낮은 것" 과 "높은 것" 이 둘 다 필요해서 만든 쌍이다 |''',
u'''| 두 빌드의 차이 | ⚠️ **버전 값뿐이다.** 인앱 업데이트를 시험하려면 "낮은 것" 과 "높은 것" 이 둘 다 필요해서 만든 쌍이다 |

⚠️ **"버전 값뿐" 을 실제로 확인했다.** 두 APK 를 항목 단위로 비교하면 **내용이 다른 항목은 두 개**뿐이다 —
`AndroidManifest.xml`(versionCode·versionName)과 `classes3.dex`(`BuildConfig` 상수). 나머지 118개 항목은 **CRC 까지 같다.**
- 다만 **APK 파일 크기는 다르다**(11,518,126 vs 11,846,438). 압축 방식은 같고 **모든 항목의 원본 크기도 같으므로**,
  차이는 **내용이 아니라 아카이브 압축·배치**에서 온다. **크기가 같아야 한다고 기대하지 말 것.**''' ))

# ---------------------------------------------------------------- NAS actual configuration
pairs.append((
u'''## 4. 서버앱 갱신''',
u'''## 3.5 🔴 이 배포의 실제 구성 (확정본 — 바꾸지 말 것)

앞선 안내 중 *"외부 443 → upstream `http://…:29180`, 프록시가 종료하니 서버 TLS 는 비워 둔다"* 는 **이 서버에 해당하지 않는다.**
구성 선택지 중 하나였고, **이 서버는 다른 쪽을 이미 골랐다**:

| 항목 | 확정값 |
|---|---|
| nginx | `443` → **`https://127.0.0.1:29180`** (`proxy_ssl_verify off`) |
| 내부 29180 | **HTTPS 전용**, cert `/opt/gnlink/certs`, dropin `10-tls.conf` |
| 앱 경로 | `/opt/gnlink/remote60-directory` |
| 서비스 | **user-systemd `remote60-directory`** (linger) |
| UDP | **29181 observe · 29190 relay** — **443 뒤로 가지 않는다** |
| 인증서 | **wildcard `*.shotan.net` 이미 발급** — **DNS/TLS 변경 불필요·금지** |
| nginx body | **512M**(우리 상한 512KB 는 여유롭게 통과) |
| relay IP env | `175.207.45.151` 고정 |

- ⚠️ **서버 TLS 를 끄거나 upstream 을 http 로 바꾸지 말 것.** 이 배포는 **서버가 TLS 를 하고 프록시가 HTTPS 로 전달**한다.
- **loopback upstream 의 `proxy_ssl_verify off` 는 기존 구성으로 기록만 한다.** 이번 배포에서 **검증 우회 옵션을 추가·확대하지 않는다.**
  공인 HTTPS 클라이언트(제품)의 인증서 검증은 **정상 유지**다.
- **TLS env·dropin·다른 vhost 는 건드리지 않는다.** 이번에 추가하는 env 는 **4.2 의 네 개뿐**이다.

## 3.6 정적 아티팩트 경로 — **결정 사항**

`server.js` 에 아티팩트 라우트가 없으므로(3.2), **어디서 서빙할지는 NAS 가 정한다.** 조건만 적는다:

- **총 용량 ≈ 15.3 MiB** — Windows payload 9개 **3.94 MiB** + APK **11.30 MiB** + manifest·서명 **2,115 B**.
- **버전별 불변 경로**: `…/updates/0.2.108/…` · `…/updates/0.2.16/…`. **덮어쓰지 않는다** — manifest 가 가리키는 URL 이
  나중에 **다른 바이트**가 되면 서명은 그대로인데 내용이 달라진다. 새 릴리스는 **새 경로**다.
- **공개 URL 은 manifest 안의 값과 정확히 일치**해야 한다(3.2 표). 다르게 두려면 **manifest 재생성·재서명**이 필요하다.
- **익명 GET**, **HTTPS**, 읽기 전용.
- ⚠️ nginx 가 `/` 를 디렉터리 서버로 프록시하고 있다면, **정적 `location` 이 그 catch-all 보다 먼저** 잡혀야 한다.

## 4. 서버앱 갱신'''))

# ---------------------------------------------------------------- code contract
pairs.append((
u'''### 4.3 재시작의 영향''',
u'''### 4.2.1 코드 계약 (서버가 실제로 하는 것)

- 파일은 `REMOTE60_UPDATE_DIR` 아래 **`<platform>.manifest` + `<platform>.sig`** 두 개다(`server.js:84-87`, 핸들러 `:24-25`).
  `<platform>` 은 **`windows` 또는 `android`** — 그 외 query 는 **400**(화이트리스트).
- ⚠️ **파일은 detached 둘, 응답은 envelope 하나**다: 서버가 `{"manifest":…,"signature":…}` 로 **합쳐서** 돌려준다(`:42`).
  파일을 합쳐 두면 안 되고, 응답을 나눠 주어도 안 된다.
- `REMOTE60_UPDATE_PUBLIC_KEY` 가 설정돼 있으면 서버는 **서빙 직전 스스로 검증**하고, 맞지 않으면 **거부**한다(`:434-436`).
  **설정하지 않으면 그 검사는 아예 수행되지 않는다** — 없는 것과 통과한 것은 다르다.
- manifest 라우트는 **세션 또는 `x-host-token`** 을 요구한다. 무인증 요청은 **401** 이 정상이다.

### 4.3 재시작의 영향'''))

# ---------------------------------------------------------------- deployment order: atomic manifest
pairs.append((
u'''4. **manifest·sig 를 마지막에** 놓는다. ⚠️ 순서가 뒤집히면 **아직 없는 파일을 가리키는 manifest** 가 잠깐 유효해지고,
   그 사이에 확인한 클라이언트는 **다운로드에 실패**한다.''',
u'''4. **manifest·sig 를 마지막에**, **원자적으로** 놓는다(임시 이름으로 쓰고 `mv` 로 교체 — 반쯤 쓰인 파일이 보이면 안 된다).
   ⚠️ 순서가 뒤집히면 **아직 없는 파일을 가리키는 manifest** 가 잠깐 유효해지고, 그 사이에 확인한 클라이언트는
   **다운로드에 실패**한다.'''))

# ---------------------------------------------------------------- confirmations owed by the NAS
pairs.append((
u'''## 9. 이 PC 에서 검증된 것''',
u'''## 8.5 NAS 가 확인해서 돌려줘야 하는 것 2건

이 PC 에서는 알 수 없고, **둘 다 "설정했다" 가 아니라 "확인했다" 로** 답해야 한다.

1. **서버 자체 검증이 실제로 켜져 있는가** — `REMOTE60_UPDATE_PUBLIC_KEY` 가 **실행 중인 프로세스의 환경에** 있는지.
   유닛 파일에 적는 것과 프로세스가 갖는 것은 다르다(재시작을 해야 반영된다). 확인법: manifest 를 **한 바이트 고쳐** 두고
   요청하면 **500 + 로그에 `refusing to serve`** 가 나와야 한다. 나오지 않으면 **검사가 꺼져 있는 것**이다.
   ⚠️ 확인 뒤 **원본 manifest 로 되돌릴 것.**
2. **공개키 128자가 세 곳에서 같은가** — 앱에 박힌 값 · 서버 env 값 · `public_xy.hex`.
   앱 쪽 두 곳은 이 PC 에서 대조했다(9절). **서버 env 는 NAS 만 볼 수 있다.**

## 9. 이 PC 에서 검증된 것'''))

for i, (old, new) in enumerate(pairs):
    assert d.count(old) == 1, 'pair %d: %s' % (i, old[:60])
    d = d.replace(old, new, 1)

# key comparison row in section 9
old9 = u'''| 운영키 = 파일값 = Codex 지정값 | `public_xy.hex` 128자, SPKI `f6121bca…c95b49` 일치 |'''
new9 = u'''| 운영키 = 파일값 = Codex 지정값 | `public_xy.hex` 128자, SPKI `f6121bca…c95b49` 일치 |
| 앱에 박힌 값 = 파일값 | **C++ `update_manifest.cpp` MATCH** · **Kotlin `build.gradle.kts` MATCH**(문자 단위 비교). 서버 env 는 **8.5-2 로 NAS 확인** |'''
assert d.count(old9) == 1, 'key row'
d = d.replace(old9, new9, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('handoff updated')

# ---------------------------------------------------------------- history correction
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
old = u'''| `GNLink-0.2.16.apk` | 11,518,126 | `c4077363…2d4f` |'''
new = u'''| `GNLink-0.2.16.apk` | **11,846,438** | `c4077363…2d4f` |'''
assert d.count(old) == 1, 'history size'
d = d.replace(old, new, 1)

old2 = u'''⚠️ **두 빌드의 차이는 버전 값뿐입니다.**'''
if old2 in d:
    d = d.replace(old2, old2, 1)

old3 = u'''⚠️ **두 빌드의 차이는 버전 값뿐이다.** 코드 차이 없음.'''
new3 = (u'''⚠️ **두 빌드의 차이는 버전 값뿐이다** — 그리고 그것을 **확인했다**: 두 APK 를 항목 단위로 비교하면 '''
        u'''**내용이 다른 항목은 `AndroidManifest.xml` 과 `classes3.dex` 둘뿐**이고(둘 다 버전 상수를 담는다) '''
        u'''나머지 118개는 **CRC 까지 같다**. ⚠️ 다만 **APK 파일 크기는 다르다**(11,518,126 vs 11,846,438) — '''
        u'''압축 방식도 항목별 원본 크기도 같으므로 차이는 **내용이 아니라 아카이브 압축·배치**다. '''
        u'''처음에 두 크기를 **같은 값으로 적었는데** 그것은 앞선 빌드의 숫자를 옮긴 것이었다. **산출물 표의 숫자는 파일에서 다시 읽어야 한다.**''')
assert d.count(old3) == 1, 'history claim'
d = d.replace(old3, new3, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history updated')
