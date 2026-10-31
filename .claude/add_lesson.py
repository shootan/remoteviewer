import io

p = 'docs/업데이트_기능_설계.md'
d = io.open(p, encoding='utf-8').read()

anchor = "- **환경 제약**: 이 회귀들은 RDP 여부와 무관하다"
assert anchor in d, 'harness section anchor'
lesson = '''> **통과하는 테스트가 옳음의 증거는 아니다** (2026-09-08, 실제 사례)
>
> 업데이터의 백업 파일을 등록 성공 직후 지우는 버그가 있었다. 상태기계는 `Register → Relaunch
> → Health` 순이고 Health 실패는 롤백인데, 그때 복원할 백업이 이미 없었다 — **예상된 실패 경로가
> 복구 불가능한 상태로 이어졌고, 안전장치가 필요한 바로 그 순간에 없었다.**
>
> 그 버그를 지키고 있던 것은 통과하던 단정이었다: `"backups are dropped only after
> registration"`. 그 문장은 **참이었다.** 참이었기 때문에 테스트가 통과했고, 테스트가 통과했기
> 때문에 버그가 안전해졌다. 그 단정은 옳음을 확인한 것이 아니라 **버그가 있는 동작을 사양으로
> 기록**하고 있었다.
>
> 교훈: 통과하는 테스트는 **무엇을 옳다고 적어 뒀는지의 증거**일 뿐이다. 단정을 쓸 때는 "지금
> 코드가 이렇게 한다" 가 아니라 **"왜 이래야 하는가"** 를 근거로 써야 하고, 그 근거를 댈 수 없는
> 단정은 사양이 아니라 스냅샷이다. 이 사례에서 옳은 단정은 "등록 뒤에 백업이 사라진다" 가 아니라
> **"롤백이 가능한 동안에는 백업이 살아 있어야 한다"** 였다.

'''
d = d.replace(anchor, lesson + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('lesson recorded in the design doc')
