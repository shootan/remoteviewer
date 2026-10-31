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

/**
 * Serialises what the connect screen should start with.
 *
 * Escaped like everything else crossing the boundary: the server address and account come off
 * disk, where a text editor may have left a byte order mark or a stray quote, and a message the
 * page cannot parse produces a blank screen with nothing to explain it.
 */
std::string shell_restore_json(const std::string& server, const std::string& accountId,
                               const ShellRuntimeSettings& settings);

/** Reads a connect request out of what the page posted. Returns false when it is not one. */
bool shell_parse_connect(const std::string& json, ShellConnectRequest* out);

/** Reads a settings change out of what the page posted. Returns false when it is not one. */
bool shell_parse_settings(const std::string& json, ShellRuntimeSettings* out);

/** The "type" field, or empty when the message is not something we recognise. */
std::string shell_message_type(const std::string& json);

/** What a start-up update check should say to the user, if anything. */
struct ShellUpdateNotice {
  /**
   * False for every outcome except "there is a newer version".
   *
   * The client checks at start-up, and a start-up check that cannot reach the server is the
   * ordinary case for a laptop opened on a train. Telling the user about it teaches them to
   * dismiss a dialog, and the next time it says something that matters they will dismiss that
   * too. Failures go to the log, where someone diagnosing a problem will look for them.
   *
   * "Up to date" is silent for the same reason: it is the answer the user already assumed.
   */
  bool show = false;
  std::string text;
  /** Always present, for the log, including the outcomes that say nothing to the user. */
  std::string logLine;
};

/**
 * Decides what a check outcome means for the user.
 *
 * Takes the outcome as a string (from update_check's check_outcome_name) rather than the enum, so
 * the UI boundary does not drag the whole update stack into everything that includes this header.
 * An outcome this build does not recognise is logged and shown to nobody -- a name that is not
 * one of the five is not a reason to interrupt someone.
 */
ShellUpdateNotice shell_update_notice(const std::string& outcome,
                                      const std::string& availableVersion,
                                      const std::string& detail);

/**
 * Tells the page there is a version to install, and which one.
 *
 * Separate from the status line on purpose. The status line is prose, and a page that had to read
 * "새 버전 0.2.115 이 있습니다." to learn the version would be parsing a sentence written for a
 * human -- which changes whenever the wording does. The version travels as a field.
 *
 * This message is what puts a button on the screen. Before it existed the client could say a new
 * version was available and offer no way to install it: the native side already handled a
 * {"type":"update"} message, but nothing in the page ever sent one.
 */
std::string shell_update_available_json(const std::string& version, const std::string& text);

/**
 * Takes the offer back down.
 *
 * Sent when signing out, because the answer described the session that asked, and when a check
 * finds nothing to install. Not sent on a failed check -- see ShellUpdateNotice::show.
 */
std::string shell_update_cleared_json();

/**
 * Whether the page's install button should be usable.
 *
 * False while an update is being started, so a second click cannot launch a second updater; true
 * again if starting failed, because a failure the user can do nothing about is a dead end.
 */
std::string shell_update_busy_json(bool busy);

}  // namespace remote60::native_poc
