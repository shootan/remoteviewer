#pragma once

// What the directory says about where address observations go.
//
// Its own header, with nothing from <windows.h> behind it, because the viewer's headers need this
// contract and pulling directory_client.hpp in for it dragged winsock along -- and with it the
// min/max macros, which break <algorithm> in whatever included it next.

#include <cstdint>
#include <string>

namespace remote60::native_poc::directory {

/**
 * Whether a directory URL asks for TLS.
 *
 * One implementation, because there were three and they did not agree. The parser tested the
 * scheme case-sensitively while two callers lowercased first, so `HTTPS://host` was "unsupported
 * scheme" to one and "secure" to the others. That disagreement decides whether the transport uses
 * TLS, so the two answers are "encrypt this" and "do not" -- not a cosmetic difference, and not
 * one that announces itself.
 *
 * Leading whitespace is ignored and the scheme is compared without case, which is what a URL
 * scheme actually is.
 */
bool directory_url_is_secure(const std::string& url);

/**
 * Where the server says address observations should be sent.
 *
 * The clients used to work this out themselves as httpPort + 1, which held only while the
 * directory was reached directly on its own two ports. Behind a TLS terminator on 443 that
 * becomes 444 -- nothing listens there, observation fails, and a host whose observation fails
 * skips its heartbeat and so never appears in the list at all. The relay cannot stand in for it
 * either: the relay address arrives in the /api/connect response, and the viewer gives up before
 * it ever calls connect.
 *
 * So the server tells us. `known` false means it did not, and the caller decides what that means
 * for the scheme it is using -- see observe_port_for().
 */
struct ObserveEndpoint {
  bool known = false;
  uint16_t port = 0;
  /** Empty means the directory host itself, which is the ordinary case. */
  std::string host;
  /**
   * True when a host WAS advertised and was thrown away for being unusable.
   *
   * Kept because the fallback is silent otherwise. The server does not validate this value -- it
   * trims it and sends it -- so a configuration slip arrives here looking like an address, and
   * dialling it produces a timeout that reads as a network fault. Falling back is right; falling
   * back without saying so leaves nobody able to find the actual mistake.
   */
  bool hostRejected = false;
};

/**
 * Whether an advertised observe host is safe to dial.
 *
 * A hostname or an IPv4 literal, and nothing else: no scheme, no port, no path, no spaces, no
 * control characters. Anything with those in it is a configuration mistake rather than an
 * address, and resolving it would either fail slowly or -- worse -- succeed against something
 * unintended.
 */
bool observe_host_is_usable(const std::string& host);

/**
 * Reads the optional `observe` metadata out of a login or register response.
 *
 * Returns false when the server said nothing, or said something unusable. A port outside
 * 1..65535 is treated as absent rather than clamped: a wrong port is worse than no port, because
 * no port leaves the caller on a documented fallback while a wrong one sends it somewhere that
 * will never answer and looks like a network fault.
 */
bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out);

/**
 * The port to send observations to, or 0 when there is no safe answer.
 *
 * `secure` is whether the directory URL is https. The rules differ by scheme on purpose:
 *
 *   * The server said so -> use it, whatever the scheme.
 *   * Plain http and no metadata -> httpPort + 1, which is what every existing deployment does
 *     and what an unchanged server still expects.
 *   * https and no metadata -> 0. NOT 443 + 1. That address is not a fallback, it is a guess
 *     that cannot be right, and dialling it turns "this server needs configuring" into a silent
 *     timeout. The caller reports it instead.
 */
uint16_t observe_port_for(const ObserveEndpoint& advertised, uint16_t httpPort, bool secure);

/**
 * Where the observe socket is aimed, and on whose word. directory-retry-fix r1 (Codex contract): a
 * failed observation is diagnosed from these explicit values, never guessed from a bool or the text
 * of an earlier status.
 */
enum class ObserveAim {
  NotAimed,       // nowhere to send it (https with no advertisement, or a host that did not resolve)
  Pinned,         // the configured observe port -- the operator's decision
  Advertised,     // what the directory said (registration answer or health route)
  LegacyDefault,  // plain http, nothing said: httpPort + 1, the documented legacy rule
};

/** What the health route last said about the observe endpoint (kept through its cooldown cycles). */
enum class ObserveHealth {
  NotAsked,         // never asked: a pin, or an advertisement that came with registration
  Advertised,       // it answered with an endpoint
  NoAdvertisement,  // it answered, and said nothing about observations
  Unreachable,      // it could not be asked (the error is kept beside it)
};

/**
 * The status for an observation that got no answer from an aimed socket.
 *
 * Both facts are kept when the aim was the legacy default: the directory gave no address, AND the
 * default address did not answer. Neither is dropped -- the first is not proof the server is
 * misconfigured (the default may be right), and the second is a real timeout, not to be hidden.
 * A pinned or advertised aim that does not answer reads exactly as before. NotAimed is never asked
 * here: the caller keeps the reason that left the socket unaimed.
 */
std::string observe_failure_status(ObserveAim aim, ObserveHealth health, const std::string& healthError,
                                   uint16_t port);

}  // namespace remote60::native_poc::directory
