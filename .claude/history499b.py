# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'''→ 즉 **제품은 이미 그 파일이 패키지 멤버라고 전제하고 있었고, manifest 만 그렇지 않았다.** 이것은 결정이 아니라 **누락**이었다.'''

new = u'''→ 즉 **제품은 이미 그 파일이 패키지 멤버라고 전제하고 있었고, manifest 만 그렇지 않았다.** 이것은 결정이 아니라 **누락**이었다.

**🔴 "업로드만 하고 manifest 는 두는" 선택지는 애초에 없었다** (검증용 지적, 코드로 재확인)
내가 *"URL 만 열면 404 는 해소된다"* 를 선택지처럼 적었는데 **틀렸다.**
```cpp
// update_effects.cpp:312  Download -- manifest 의 목록만 받는다
for (const ManifestArtifact& artifact : fields.artifacts) { ... stagedNames_.push_back(...); }
// update_effects.cpp:468  Swap -- payloadNames 전부가 staged 에 있어야 한다
for (const std::wstring& name : config_.payloadNames)
  if (GetFileAttributesW(staged_path_for(...)) == INVALID_FILE_ATTRIBUTES)
    { lastError_ = "the staged release does not contain " + to_utf8(name); return false; }
```
- staging 에는 **Download 가 받아온 것만** 들어가고, Download 는 **`fields.artifacts` 만** 돈다.
- 그래서 `payloadNames` 에 있고 manifest 에 없는 이름은 **URL 이 열려 있든 말든 swap 에서 실패**한다.
- ⚠️ **"파일이 서버에 있다" 와 "업데이트가 그 파일을 얻는다" 는 다른 명제다.** 나는 그 둘을 한 문장으로 묶어 선택지를 만들었다. **manifest 추가는 선택이 아니라 필수였다.**
- 대조를 도구화했다(`.claude/check_release_payloadnames.py`, 목록은 `updater_effects.cpp:293-295` 에서 읽는다): **payloadNames 9/9 가 manifest 에 존재.** `GNLinkUpdater.exe` 만 manifest 에 있고 swap 대상이 아닌데, **업데이터는 자기 자신을 교체하지 않으므로** 정상이다.

**`releaseId` 는 staging 디렉터리 이름이 된다**(`staged_path_for`). 그래서 **경로로 안전한 값이어야 한다** — `r-0.2.109-2` 는 안전하다. 값이 바뀌었으므로 **기존 `r-0.2.109` staged 를 재사용하지 않고 다시 받는다.** 이것도 의도한 결과다.'''

assert d.count(old) == 1, 'anchor 1'
d = d.replace(old, new, 1)

# --- the backup-guard entry: it was mine, and the record should say which commit
old2 = u'''- 수정: 백업 디렉터리가 이미 있으면 **덮지 않고** `0.2.109-<UTC>` 로. **음성 대조**: 가드를 빼면 신규 회귀 2건이 FAIL 하고 백업이 실제로 새 manifest(`743fbb1f…`→`1d5c11b5…`)로 덮인다.'''
new2 = u'''- 수정: 백업 디렉터리가 이미 있으면 **덮지 않고** `0.2.109-<UTC>` 로. **음성 대조**: 가드를 빼면 신규 회귀 2건이 FAIL 하고 백업이 실제로 새 manifest(`743fbb1f…`→`1d5c11b5…`)로 덮인다.
- ⚠️ 검증용은 이 가드가 **`d110fe2` 에 이미 있었다**고 정정을 보냈지만, `git show d110fe2:automation/gnlink_deploy.sh` 에 **그 가드도 그 주석도 없다.** 인용된 주석 문구는 **`cacede2` 에서 내가 쓴 것**이고, 작업 트리를 보고 이전 커밋에 있었다고 읽은 것이다. 가드는 **1개**뿐이며 중복 추가는 없다. — **커밋 내용을 확인하지 않고 작업 트리로 판단하면 "이미 있다" 와 "방금 생겼다" 가 구분되지 않는다.**'''
assert d.count(old2) == 1, 'anchor 2'
d = d.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
