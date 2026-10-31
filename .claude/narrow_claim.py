import io
p='apps/native_poc/src/update_relaunch.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  // not hypothetical -- a test deleted its own fixtures after firing one of these, and the dialog
  // arrived on a real desktop afterwards. What prevents it is the file still being there when the
  // request lands, which is a question about who may delete it and when, not about checking
  // first. Here that holds because the installation directory is not removed, and because the
  // optional images are started only after the commit, so no rollback follows to replace them.'''
new='''  // not hypothetical -- a test deleted its own fixtures after firing one of these, and the dialog
  // arrived on a real desktop afterwards. What prevents it is the file still being there when the
  // request lands, which is a question about who may delete it and when, not about checking
  // first.
  //
  // FOR THE REST OF THIS ATTEMPT that holds: the optional images start only after the commit, so
  // no rollback follows to move them, and nothing in this attempt removes the installation
  // directory. It is NOT a claim about the file's existence in general. A later update, or an
  // uninstall, may replace or remove it, and a shell request still in flight from this attempt
  // would then find whatever they left. That window is small and nothing here can close it --
  // saying so is better than implying a guarantee this does not have.'''
assert old in d,'comment'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='docs/history.md'
d=io.open(p,encoding='utf-8').read()
old='''- **생산 경로 검토**(지시 5): 설치 디렉터리는 삭제되지 않고, **선택 이미지는 commit 뒤에만 기동**되므로(#473) 뒤이어 롤백이 파일을 바꿔치지 않는다. 늦게 도착한 요청은 **제자리에 있는 새 클라이언트**를 띄운다. 근거를 `launch_via_shell` 주석에 적었다.'''
new='''- **생산 경로 검토**(지시 5): **이번 시도 수명 안에서는** 설치 디렉터리가 삭제되지 않고, **선택 이미지는 commit 뒤에만 기동**되므로(#473) 뒤이어 롤백이 파일을 바꿔치지 않는다. 늦게 도착한 요청은 제자리에 있는 새 클라이언트를 띄운다. ⚠️ **정정(#477)**: 이것은 **파일이 항상 존재한다는 보장이 아니다.** 이후의 다른 업데이트나 제거는 그 파일을 바꾸거나 없앨 수 있고, 그때까지 남아 있던 셸 요청은 그들이 남긴 것을 만난다. **닫을 수 없는 창이며, 보장인 척하지 않는 편이 낫다.**'''
assert old in d,'history claim'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the claim is scoped to this attempt')
