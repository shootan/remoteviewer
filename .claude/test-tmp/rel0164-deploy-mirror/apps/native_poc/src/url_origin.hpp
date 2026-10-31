#pragma once

// "scheme://host:port" -- the one answer to "are these two urls the same server?".
//
// It lives on its own, with no Windows headers and no other dependency, because two trees need it
// and they are deliberately kept apart: the product links the directory client, and the updater
// deliberately does not (it has its own transport so that reworking the product's cannot change
// how an administrator-privileged artifact is fetched). Without a shared file the answer would be
// written twice, and the two copies would decide differently about a trailing slash or a
// written-out default port -- which is the same class of split this repository has already been
// bitten by, in the scheme test that said `HTTPS://` was secure in one place and unsupported in
// another.

#include <string>

namespace remote60::native_poc {

/**
 * Canonical origin for comparison: lowercased host, the port always written out, no path.
 *
 * `http://h/`, `http://h`, `http://h:80` and `http://H/api` are one origin. `https://h` is not the
 * same origin as `http://h` -- the scheme stays in the key, because a credential issued to one is
 * not for the other, and folding them together is how a token ends up somewhere it was never
 * meant to go.
 *
 * A url this cannot parse is returned trimmed and lowercased instead: it still compares equal to
 * itself, which keeps two different unusable values from looking like one server.
 */
std::string url_origin_key(const std::string& url);

}  // namespace remote60::native_poc
