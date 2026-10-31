import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

edit(r'D:\remote\remote\apps\native_poc\src\product_version.hpp', [
('constexpr wchar_t kProductVersion[] = L"0.2.99";', 'constexpr wchar_t kProductVersion[] = L"0.2.100";'),
])
