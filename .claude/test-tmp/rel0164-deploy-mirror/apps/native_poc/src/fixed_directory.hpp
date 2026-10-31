#pragma once

namespace remote60::native_poc {

// The single place the Windows products' directory server is written.
//
// GNLinkClient and GNLinkHost used to ask the user for this on the sign-in form and remember the
// answer (client.txt line 1, host.json `directoryUrl`). There is one server, so the question had
// one right answer and every other answer was a way to be signed out: a typo, an old address, a
// test's loopback url left in the file. Now nobody is asked, and what is on disk is not consulted
// -- the saved files still carry this value, only so that a build from before this change can
// read them after a rollback.
//
// Everything that reaches the directory from those two programs starts here: sign-in, the host
// list, connect, the viewer and streaming children (handed it as --directory-url), the log
// uploader and the derived update endpoint.
//
// What that does and does not promise. GNLinkClient and GNLinkHost have no input -- no field,
// argument, environment variable or stored value -- that changes where they sign in, list hosts,
// connect or upload logs. Two things are outside that statement and unchanged by it:
//
//   * REMOTE60_UPDATE_MANIFEST_URL (Windows) and BuildConfig.UPDATE_MANIFEST_URL (Android) still
//     name a manifest address of the operator's own. A request to it carries no credential
//     (update_endpoint_for), so it is somewhere an update can be fetched from, not somewhere a
//     sign-in can be sent.
//   * GNLinkStream and GNLinkViewer take their directory as --directory-url from the program
//     that starts them, and GNLinkStream falls back to REMOTE60_DIRECTORY_URL when started
//     without one. The product always passes the argument; the fallback is what scripts that
//     start a streaming host by hand use.
//
// The tests that need a fixture server are separate builds (REMOTE60_SHELL_TEST_SEAM,
// REMOTE60_HOST_TEST_SEAM, REMOTE60_STREAM_TEST_SEAM), and
// automation/gnlink_check_fixed_server.py checks that what ships carries this address and none
// of their switches.
constexpr char kFixedDirectoryUrl[] = "https://gnlink.shotan.net";

// Names this same server was signed in to under, before the address was fixed.
//
// A host token is kept beside the address it was issued at, and is only ever sent back to that
// address. An install made before this build has the old name in host.json; without this list
// its token would be left unused and an unattended PC would come up signed out after updating.
//
// An entry here says one thing: a token stored under this exact origin may be presented to
// kFixedDirectoryUrl, and nowhere else. host.json is rewritten only after the server has
// accepted it. Exact https origins, confirmed to be this service -- a name that merely resolves
// to the same machine does not belong here, and anything not listed is neither sent nor erased.
constexpr const char* kMigratableDirectoryOrigins[] = {
    "https://rem.shotan.net",
};

}  // namespace remote60::native_poc
