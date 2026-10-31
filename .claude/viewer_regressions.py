import io

p = 'apps/native_poc/src/directory_session_client_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''    // ---- the advertised host, which the server does NOT validate'''
new = '''    // ---- the scheme decision, which used to be made in three places
    //
    // The parser compared the scheme case-sensitively while two callers lowercased first, so
    // `HTTPS://host` was "unsupported scheme" to one and "secure" to the others. That answer picks
    // the transport, so the disagreement was between encrypting and not.
    using directory::directory_url_is_secure;
    check("https is secure", directory_url_is_secure("https://rem.shotan.net"));
    check("http is not", !directory_url_is_secure("http://rem.shotan.net"));
    check("the scheme is case-insensitive", directory_url_is_secure("HTTPS://rem.shotan.net"));
    check("mixed case too", directory_url_is_secure("HtTpS://rem.shotan.net"));
    check("leading whitespace does not hide it",
          directory_url_is_secure("   https://rem.shotan.net"));
    check("HTTP in any case is still not secure", !directory_url_is_secure("HTTP://x"));
    check("a bare host is not secure", !directory_url_is_secure("rem.shotan.net"));
    check("a name that merely starts with https is not a scheme",
          !directory_url_is_secure("httpsx://x"));
    check("empty is not secure", !directory_url_is_secure(""));

    // ---- the advertised host, which the server does NOT validate'''
assert old in d, 'scheme anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scheme regressions')
