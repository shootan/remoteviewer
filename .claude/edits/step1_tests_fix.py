import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
edit(r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core_test.cpp', [
("""using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264FrameAssembler;
""",
"""using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264AssemblyStepResult;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::kEncodedFrameFlagKeyFrame;
"""),
])
