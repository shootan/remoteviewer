#pragma once

// Clipboard image v1, host side of direction A (viewer -> host), plan r1 + r2.
//
// Role:    HostClipImageService -- answers the viewer's Offer / Cancel / Status on the control
//          channel, owns the bulk stream (a second UdpControlChannel on the media socket, stream ids
//          in the bit30 namespace) for the one transfer a session may run, pulls the package chunk
//          by chunk, then verifies (SHA-256, PNG header size), decodes into the CF_DIBV5 memory and
//          publishes through the clipboard hub -- with the clipboard held, and only if nothing was
//          copied on the host since the offer was accepted.
// Thread:  control dispatchers (TCP or UDP) call HandleOffer/Cancel/Status; the UDP reader calls
//          OnDatagram; the service's own worker drives the bulk channel, the pulls, the timeouts
//          and the verify/decode/publish. The receiver state is under mu_.
// Callers: host_control_session.cpp (control messages, the Pong bit), host_startup_control.cpp
//          (demux, send path, session boundaries), native_video_host_main.cpp (lifetime).
//
// The bulk path is plaintext, like the rest of the UDP media socket (poc_protocol.hpp). Logs carry
// sizes, the first 8 hex of the digest and outcomes -- never content.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "clip_image_clipboard.hpp"
#include "clip_image_transfer.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

class HostClipboardHub;

/**
 * Where a received image lands: the host clipboard in the product (HubClipImagePublisher), an
 * injected fake at the OS boundary in the two-sided tests -- one logon session has one clipboard,
 * so both ends of a test cannot each own a real one (clip_image_winsta_test).
 */
class HostClipImagePublisher {
 public:
  virtual ~HostClipImagePublisher() = default;
  virtual bool Enabled() const = 0;
  /** The clipboard's sequence number now (recorded at accept; publishing requires it unchanged). */
  virtual uint64_t Sequence() const = 0;
  /** Takes ownership of both HGLOBALs in every outcome. */
  virtual ClipPublishResult Publish(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                    const std::u16string& text) = 0;
};

/** The product's publisher: the host clipboard, through the clipboard hub's monitor thread. */
class HubClipImagePublisher : public HostClipImagePublisher {
 public:
  explicit HubClipImagePublisher(HostClipboardHub* hub) : hub_(hub) {}
  bool Enabled() const override;
  uint64_t Sequence() const override { return GetClipboardSequenceNumber(); }
  ClipPublishResult Publish(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                            const std::u16string& text) override;

 private:
  HostClipboardHub* hub_;
};

class HostClipImageService {
 public:
  using SendFn = UdpControlChannel::SendFn;

  /** `publisher` null: the feature is off (never advertised, every offer Disabled). */
  explicit HostClipImageService(HostClipImagePublisher* publisher);
  ~HostClipImageService() { Stop(); }

  /** Starts the worker. `send` puts one datagram on the media socket towards the current peer. */
  bool Start(SendFn send, uint32_t mtuBytes);
  void Stop();

  /** Whether this host advertises kCaptureFlagClipboardImageV1 (hub running, not disabled). */
  bool Enabled() const;

  /** The current session negotiated kUdpFeatureBulkChannel (set at Hello accept). */
  void SetBulkNegotiated(bool negotiated) { bulkNegotiated_.store(negotiated, std::memory_order_release); }

  /** A session ended, rolled over or was repaired: nothing in flight survives it. */
  void OnSessionEnd(uint64_t newEpoch);

  // Control messages (requests from the viewer; each has exactly one answer).
  ControlClipImageOfferReplyMessage HandleOffer(const ControlClipImageOfferMessage& m, uint64_t sessionEpoch);
  ControlClipImageStatusReplyMessage HandleCancel(const ControlClipImageCancelMessage& m);
  ControlClipImageStatusReplyMessage HandleStatus(const ControlClipImageStatusMessage& m);

  /**
   * A datagram off the media socket. True when it was a bulk-stream datagram (bit30 stream id) --
   * consumed whether or not a transfer is open, so it can never reach the control channel.
   */
  bool OnDatagram(const void* data, size_t len);

  struct Counters {
    uint64_t offers = 0, accepted = 0, published = 0, superseded = 0, failed = 0, cancelled = 0;
    uint64_t chunksAccepted = 0, chunksDropped = 0, datagramsDroppedNoTransfer = 0;
  };
  Counters GetCounters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return counters_;
  }

 private:
  void Run();
  void PumpPulls();                 // caller holds mu_
  void CloseBulk();                 // caller holds mu_
  void VerifyAndPublish();          // worker, without mu_ while decoding / publishing
  void LogEnd(const char* where);   // caller holds mu_

  HostClipImagePublisher* publisher_;
  SendFn send_;
  uint32_t mtu_ = 1200;
  std::atomic<bool> bulkNegotiated_{false};
  std::atomic<bool> running_{false};
  std::thread worker_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  ClipImageReceiver receiver_;
  UdpControlChannel bulk_;
  bool bulkOpen_ = false;
  uint32_t bulkRx_ = 0;             // the stream id chunks arrive on, while open
  uint64_t lastChunkUs_ = 0;        // chunk sizing: when the previous chunk arrived
  uint64_t rxRateBps_ = 0;          // chunk sizing: smoothed arrival rate
  bool adaptiveChunks_ = true;
  Counters counters_;
};

}  // namespace remote60::native_poc
