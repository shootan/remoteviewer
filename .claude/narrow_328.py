import io

# ================================================================== the header states the limits
p = 'apps/native_poc/src/update_handoff.hpp'
d = io.open(p, encoding='utf-8').read()
old = ''' * matters because the interesting death is the one that does not get to run any cleanup.
 *
 * The BOOTSTRAP must not hold it. It exits immediately by design, and a name held by the process
 * whose exit means nothing would say the worker had died every single time.
 */'''
new = ''' * matters because the interesting death is the one that does not get to run any cleanup.
 *
 * The BOOTSTRAP must not hold it. It exits immediately by design, and a name held by the process
 * whose exit means nothing would say the worker had died every single time.
 *
 * WHAT IT DOES NOT COVER, stated because the measured number invites a wider claim than it earns:
 *
 *   * A death BEFORE the mutex is taken. There is no object yet, so there is nothing to be
 *     released, and the wait falls through to the timeout exactly as it did before. The window is
 *     from the working copy starting to its first few instructions -- small, and not zero.
 *   * It follows the THREAD that holds it, not the process. That is the same thing only because
 *     the working copy takes it on its main thread and holds it to exit. If that ever stops being
 *     true -- taken on a worker thread, released early -- this reports a death that has not
 *     happened, which is worse than missing one.
 *
 * So it is a contract, not a property of the operating system: the holder must be the process
 * whose life is being reported, and must take it as early as it can.
 */'''
assert old in d, 'alive doc'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== docs
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
old = '''  - **실측**: worker 가 죽으면 **328ms** 에 알아채고 사유도 "the working copy stopped before it had a verified download" 로 분리된다. **반대 증거**: 추적을 끄면 **20000ms 를 다 기다리고** 사유가 "did not reach a verified download in time" 로 뭉개진다.'''
new = '''  - **실측**: worker 가 죽으면 **328ms** 에 알아채고 사유도 "the working copy stopped before it had a verified download" 로 분리된다. **반대 증거**: 추적을 끄면 **20000ms 를 다 기다리고** 사유가 "did not reach a verified download in time" 로 뭉개진다.
  - ⚠️ **이 328ms 가 "모든 사망" 은 아니다**(#478 에서 좁힘): **뮤텍스를 잡기 전에 죽으면 관측 대상이 없어 timeout 경로**로 간다(창이 작을 뿐 0 이 아니다). 그리고 뮤텍스는 **보유 스레드의 수명**이지 프로세스 핸들과 항상 같은 관측력이 아니다 — **working copy 가 main thread 에서 잡고 종료까지 들고 있다는 계약** 위에서만 같다. **OS 의 성질이 아니라 계약이다.**'''
assert old in d, 'history 328'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'docs/full_code_audit_2026-09-08.md'
d = io.open(p, encoding='utf-8').read()
old = '''| 실측 **328ms** 에 감지, 사유도 분리. 반대 증거: 끄면 **20000ms** 를 다 기다리고 사유가 뭉개진다 |'''
new = '''| 실측 **328ms** 에 감지, 사유도 분리. 반대 증거: 끄면 **20000ms** 를 다 기다린다. ⚠️ **범위**: 뮤텍스 확보 **전** 사망은 timeout 경로이고, 뮤텍스는 **보유 스레드 수명**이라 main-thread 보유 계약 위에서만 프로세스 수명과 같다 |'''
assert old in d, 'ledger 328'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the measured number no longer implies more than it covers')
