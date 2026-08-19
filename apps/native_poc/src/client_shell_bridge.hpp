#pragma once

// The contract between the interface and the client.
//
// The interface is HTML in a WebView2 and the client is C++, so everything that crosses between
// them is a JSON string. Keeping that contract in one place matters more than it looks: the two
// sides are written in different languages and compiled at different times, and a field renamed
// on one side fails silently on the other.
//
// The video never crosses this boundary. It arrives on a UDP socket, decodes through Media
// Foundation and is drawn by D3D11, exactly as before -- the shell only decides which host to
// open and what settings to open it with.
//
// Messages from the page carry a "type"; replies are pushed back as JSON the page listens for.

#include <string>
#include <vector>

#include "directory_session_client.hpp"

namespace remote60::native_poc {

/** What the user chose on the connect screen, once they press connect. */
struct ShellConnectRequest {
  std::string hostId;
  std::string hostName;
  uint32_t bitrateKbps = 0;   // 0 = leave the current setting alone
  uint32_t fps = 0;
};

/** Settings the page can change while connected. */
struct ShellRuntimeSettings {
  uint32_t bitrateKbps = 8000;
  uint32_t fps = 30;
  uint32_t monitorId = 0;
};

/**
 * Serialises a host list for the page.
 *
 * Online hosts first, because an offline one cannot be connected to and burying the useful ones
 * under it is the sort of thing that makes an app feel careless.
 */
std::string shell_hosts_json(const std::vector<DirectoryHostEntry>& hosts);

/** Serialises a single status line, which is how every long operation reports progress. */
std::string shell_status_json(const std::string& state, const std::string& detail);

/** Reads a connect request out of what the page posted. Returns false when it is not one. */
bool shell_parse_connect(const std::string& json, ShellConnectRequest* out);

/** Reads a settings change out of what the page posted. Returns false when it is not one. */
bool shell_parse_settings(const std::string& json, ShellRuntimeSettings* out);

/** The "type" field, or empty when the message is not something we recognise. */
std::string shell_message_type(const std::string& json);

}  // namespace remote60::native_poc
