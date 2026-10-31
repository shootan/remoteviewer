p = 'C:/work/gnlink-wt/ka-pc/apps/native_poc/src/ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
i = s.index('constexpr wchar_t kWebView2Policies[]')
j = s.index("// The folder the product's environment REALLY uses")
s = s[:i] + ('// The preflight itself (variables, policies, the product\'s own path computation) is shared with the managed test:\n'
             '// ka_pc_e2e_webview.hpp. Here: the shell\'s UDF for this process, then -- once the shell\'s environment is confirmed\n'
             '// -- the macro\'s UDF for every viewer the shell starts after it (they inherit this environment).\n'
             'std::string webview_preflight(const std::wstring& expectedUdf) {\n'
             '  return ka_pc_e2e::webview_preflight(gRoot, ka_pc_e2e::kShellUdfName, expectedUdf);\n'
             '}\n\n') + s[j:]
a = '#include "ka_pc_e2e_child.hpp"\n'
assert s.count(a) == 1
s = s.replace(a, a + '#include "ka_pc_e2e_webview.hpp"\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
