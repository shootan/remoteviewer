p = 'D:/remote/remote/.claude/report_keepalive_r2_host_spike.md'
s = open(p, encoding='utf-8').read()
start = s.index('- **과거 실행 위치와 삭제 사실** (r4 정정')
end = s.index('  - **확인된 것은', start)
good = ('- **과거 실행 위치와 삭제 사실** (r4 정정, Codex `codex_ka_punch_thread_src_r3.md`):\n'
        '  - r2 시험의 9×3 실행과 변이 실행(변이 3 + 복원 1)은 모두 '
        '`C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/remote60_host_agent_punch_test.exe` 에서 돌렸다. 7회다.\n'
        '  - 시험은 매 실행의 시작과 끝에 '
        '`C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/host_agent_punch_test.cache` 를 DeleteFileA 했다. '
        '저장소 밖 경로다.\n')
s = s[:start] + good + s[end:]
assert not any(ord(c) < 32 and c not in '\n\t' for c in s), 'control chars remain'
open(p, 'w', encoding='utf-8').write(s)
print('ok')
