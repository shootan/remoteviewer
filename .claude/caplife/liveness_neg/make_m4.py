"""apk-decoder-liveness r3 D1 negative: m4 -- the UI's detach waits WITHOUT a deadline (r1's D1 (a)), copied from the
current sink. Expected to FAIL android_decoder_liveness_test [3] (the bounded detach)."""
import os
import shutil
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/android_direct_client/app/src/main/cpp/'
DST = os.path.join(HERE, 'm4')
os.makedirs(DST, exist_ok=True)
for f in ('android_video_decoder.hpp', 'android_video_decoder.cpp', 'android_video_decoder_sh_seam.hpp'):
    shutil.copyfile(SRC + f, os.path.join(DST, f))
p = os.path.join(DST, 'android_video_decoder.cpp')
s = open(p, encoding='utf-8', newline='').read()
a = 'doneCv_.wait_for(lk, std::chrono::milliseconds(kDetachDeadlineMs),'
assert s.count(a) == 1
s = s.replace(a, 'doneCv_.wait_for(lk, std::chrono::hours(1) /* MUTANT m4: no deadline */,')
a2 = '  if (earlierDetachPending) {'
assert s.count(a2) == 1
s = s.replace(a2, '  if (false && earlierDetachPending) {  // MUTANT m4: waits again')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('m4 made')
