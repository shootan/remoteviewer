// See viewer_paste_gate.hpp.

#include "viewer_paste_gate.hpp"

#include <algorithm>
#include <cstddef>

namespace remote60::native_poc::viewer {

namespace {
constexpr uint32_t kVkInsert = 0x2D;   // VK_INSERT
constexpr uint16_t kVkShift = 0x10;    // VK_SHIFT
constexpr uint16_t kVkControl = 0x11;  // VK_CONTROL
constexpr uint16_t kScanCtrl = 0x1D;   // left Ctrl make code
constexpr uint16_t kScanShift = 0x2A;  // left Shift make code
constexpr uint16_t kScanV = 0x2F;      // V on a US layout
constexpr uint16_t kScanInsert = 0x52; // Insert (E0-extended on the navigation block)
}  // namespace

PasteKey classify_paste_key(uint32_t vk, const PasteMods& m) {
  if (m.alt || m.win) return PasteKey::None;
  if (vk == 'V' && m.ctrl) return m.shift ? PasteKey::CtrlShiftV : PasteKey::CtrlV;
  if (vk == kVkInsert && m.shift && !m.ctrl) return PasteKey::ShiftInsert;
  return PasteKey::None;
}

bool is_copy_key(uint32_t vk, const PasteMods& m) {
  if (m.alt || m.win || !m.ctrl) return false;
  return vk == 'C' || vk == 'X' || vk == kVkInsert;
}

PasteRoute route_paste(bool featureOn, bool hostConfirms, bool localIsLatest) {
  if (!featureOn) return PasteRoute::Forward;
  if (!localIsLatest) return PasteRoute::PassThrough;
  if (!hostConfirms) return PasteRoute::UpdateNeeded;
  return PasteRoute::Send;
}

std::vector<PasteStep> paste_chord(PasteKey key, uint16_t keyScan, bool keyExt) {
  std::vector<PasteStep> steps;
  const PasteStep ctrl{true, kVkControl, kScanCtrl, false};
  const PasteStep shift{true, kVkShift, kScanShift, false};
  std::vector<PasteStep> mods;
  PasteStep k{};
  switch (key) {
    case PasteKey::CtrlV:
      mods = {ctrl};
      k = PasteStep{true, 'V', keyScan ? keyScan : kScanV, keyScan ? keyExt : false};
      break;
    case PasteKey::CtrlShiftV:
      mods = {ctrl, shift};
      k = PasteStep{true, 'V', keyScan ? keyScan : kScanV, keyScan ? keyExt : false};
      break;
    case PasteKey::ShiftInsert:
      mods = {shift};
      k = PasteStep{true, static_cast<uint16_t>(kVkInsert), keyScan ? keyScan : kScanInsert, keyScan ? keyExt : true};
      break;
    default:
      return steps;
  }
  for (const PasteStep& m : mods) steps.push_back(m);
  steps.push_back(k);
  PasteStep up = k;
  up.down = false;
  steps.push_back(up);
  for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
    PasteStep m = *it;
    m.down = false;
    steps.push_back(m);
  }
  return steps;
}

PasteGate::Admitted PasteGate::Admit(const Ticket& t, uint64_t nowUs) {
  if (pending_.id == 0) {
    pending_ = t;
    pending_.startedUs = nowUs;
    pending_.deadlineUs = t.budgetUs ? nowUs + t.budgetUs : 0;
    return Admitted::Started;
  }
  if (waiting_.id == 0) {
    waiting_ = t;
    waitingAt_ = held_.size();  // the keys typed so far come before it
    return Admitted::Waiting;
  }
  return Admitted::Dropped;
}

PasteGate::Outcome PasteGate::OnAnswer(uint64_t id, bool applied, uint64_t connGen, uint64_t targetGen, Ticket* done) {
  if (pending_.id == 0 || id != pending_.id) return Outcome::Ignored;
  *done = pending_;
  pending_ = Ticket{};
  // The answer is for this paste, but the session or the target it was asked for is gone: the key
  // would land somewhere the user did not paste into.
  if (!applied || connGen != done->connGen || targetGen != done->targetGen) {
    DropHeldBeforeWaiting();
    return Outcome::Fail;
  }
  return Outcome::Inject;
}

bool PasteGate::OnTick(uint64_t nowUs, Ticket* expired) {
  if (pending_.id == 0 || pending_.deadlineUs == 0 || nowUs < pending_.deadlineUs) return false;
  *expired = pending_;
  pending_ = Ticket{};
  DropHeldBeforeWaiting();
  return true;
}

bool PasteGate::Promote(uint64_t nowUs, Ticket* started) {
  if (pending_.id != 0 || waiting_.id == 0) return false;
  pending_ = waiting_;
  waiting_ = Ticket{};
  waitingAt_ = 0;  // what is held now was typed after the paste that has just become pending
  pending_.startedUs = nowUs;
  pending_.deadlineUs = pending_.budgetUs ? nowUs + pending_.budgetUs : 0;
  *started = pending_;
  return true;
}

std::vector<PasteGate::Ticket> PasteGate::CancelAll() {
  std::vector<Ticket> ended;
  if (pending_.id != 0) ended.push_back(pending_);
  if (waiting_.id != 0) ended.push_back(waiting_);
  pending_ = Ticket{};
  waiting_ = Ticket{};
  dropped_.insert(dropped_.end(), held_.begin(), held_.end());
  held_.clear();
  waitingAt_ = 0;
  return ended;
}

bool PasteGate::Hold(const HeldKey& k) {
  if (held_.size() >= kPasteHeldKeyLimit) return false;
  held_.push_back(k);
  return true;
}

bool PasteGate::HoldsDown(uint64_t wp) const {
  // The latest edge for this key decides: a down not yet followed by its up.
  for (auto it = held_.rbegin(); it != held_.rend(); ++it) {
    if (it->wp != wp) continue;
    return it->msg == 0x0100 /*WM_KEYDOWN*/ || it->msg == 0x0104 /*WM_SYSKEYDOWN*/;
  }
  return false;
}

std::vector<HeldKey> PasteGate::TakeHeld() {
  std::vector<HeldKey> out(held_.begin(), held_.end());
  held_.clear();
  waitingAt_ = 0;
  return out;
}

std::vector<HeldKey> PasteGate::TakeHeldBeforeWaiting() {
  if (waiting_.id == 0) return TakeHeld();
  const size_t n = (std::min)(waitingAt_, held_.size());
  std::vector<HeldKey> out(held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(n));
  held_.erase(held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(n));
  waitingAt_ = 0;
  return out;
}

void PasteGate::DropHeldBeforeWaiting() {
  if (waiting_.id == 0) {
    dropped_.insert(dropped_.end(), held_.begin(), held_.end());
    held_.clear();
    waitingAt_ = 0;
    return;
  }
  const size_t n = (std::min)(waitingAt_, held_.size());
  dropped_.insert(dropped_.end(), held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(n));
  held_.erase(held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(n));
  waitingAt_ = 0;
}

std::vector<HeldKey> PasteGate::TakeDropped() {
  std::vector<HeldKey> out;
  out.swap(dropped_);
  return out;
}

std::wstring paste_failure_text(PasteFailure f) {
  const std::wstring head = L"전송하지 못해 붙여넣기를 실행하지 않았습니다";
  switch (f) {
    case PasteFailure::Timeout:
      return head + L" (응답 시간 초과)";
    case PasteFailure::HostWrite:
      return head + L" (원격 PC 클립보드에 쓰지 못함)";
    case PasteFailure::Refused:
      return head + L" (원격 PC가 받지 않음)";
    case PasteFailure::HelperUnavailable:
      return head + L" (원격 PC의 파일 복사 도우미를 시작하지 못함)";
    case PasteFailure::Link:
      return head + L" (연결이 끊김)";
    case PasteFailure::Busy:
      return L"붙여넣기가 이미 진행 중입니다";
    case PasteFailure::Empty:
      return L"붙여넣을 수 있는 내용이 아닙니다 — 붙여넣기를 실행하지 않았습니다";
    case PasteFailure::TooLarge:
      return L"복사한 내용이 너무 커서 붙여넣기를 실행하지 않았습니다";
    case PasteFailure::ReadFailed:
      return L"이 PC 클립보드를 읽지 못해 붙여넣기를 실행하지 않았습니다";
    case PasteFailure::UpdateNeeded:
      return L"원격 PC의 GNLink 를 업데이트해야 이 PC에서 복사한 내용을 붙여넣을 수 있습니다";
    case PasteFailure::Cancelled:
      return L"붙여넣기를 취소했습니다";
    default:
      return std::wstring();
  }
}

}  // namespace remote60::native_poc::viewer
