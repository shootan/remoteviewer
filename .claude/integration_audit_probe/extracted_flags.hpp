// Exact function extracted from current installer source for read-only verification.
bool has_flag(const std::wstring& commandLine, const wchar_t* flag) {
  std::wstring haystack = commandLine;
  for (wchar_t& c : haystack) c = static_cast<wchar_t>(towlower(c));
  std::wstring needle = flag;
  for (wchar_t& c : needle) c = static_cast<wchar_t>(towlower(c));
  return haystack.find(needle) != std::wstring::npos;
}
