"""Codex 정정 지시 1건 (주석 전용, 코드 동작 변경 0).

`NoteKeyAccepted` 는 decode 성공이 아니라 조립/게이트 수락 경계다.
"decoder's new reference" 문구를 그 취지로 바꾼다. 이후의 decode 실패는
기존 reason 4/5/7 복구가 담당한다는 점을 명시.
"""
import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'

edit(ROOT + r'\apps\native_poc\src\native_video_client_shared_core.cpp', [
    ("""  // That key is the recovery point: every identity at or before it is behind the decoder's new
  // reference now, whatever generation it was recorded under -- the host's sequence is monotone
  // across the session -- and the ordinary stale guard takes over for their late chunks.""",
     """  // That key is the recovery point at the assembly/gate admission boundary -- the receiver
  // admitted it for decoding, which is not a claim that it decoded (a decode failure afterwards
  // is the existing reason 4/5/7 recovery's job, not this list's). From that boundary on, every
  // identity at or before its seq is behind the point the stream resumed from, whatever
  // generation it was recorded under -- the host's sequence is monotone across the session --
  // and the ordinary stale guard takes over for their late chunks."""),
])

edit(ROOT + r'\apps\native_poc\src\native_video_client_shared_core.hpp', [
    ("""  // The episode ends when the caller reports that one of the delivered candidates was actually
  // accepted for decoding (NoteKeyAccepted): that key is the recovery point, so everything the
  // watermark covers is retired.""",
     """  // The episode ends when the caller reports that one of the delivered candidates passed the
  // admission boundary (NoteKeyAccepted -- assembly/gate admission, not a decode success): that
  // key is the recovery point, so everything the watermark covers is retired."""),
])
print('comment fix ok')
