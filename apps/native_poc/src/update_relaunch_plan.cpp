#include "update_relaunch_plan.hpp"

#include <algorithm>

namespace remote60::native_poc::update {
namespace {

/**
 * The product's processes and how each one comes back.
 *
 * A table rather than a chain of comparisons because the interesting content is the reasons, and
 * they are worth reading together. Everything not listed is unknown, and unknown means not
 * started -- see relaunch_plan.
 */
const std::vector<KnownImage> kKnownImages = {
    {L"GNLinkHost.exe", L"gnlinkhost.exe", RelaunchKind::ElevatedProcess,
     "the host is requireAdministrator and was elevated before the update, so a child of the "
     "updater inherits the same token and no prompt appears"},
    {L"GNLinkClient.exe", L"gnlinkclient.exe", RelaunchKind::UserProcess,
     "the client is not an administrator program; starting it from the elevated updater would "
     "leave the whole session running as administrator"},
    {L"GNLinkInputService.exe", L"gnlinkinputservice.exe", RelaunchKind::Service,
     "a Windows service comes back through the SCM; creating the process directly would not make "
     "it one"},
    {L"GNLinkStream.exe", L"gnlinkstream.exe", RelaunchKind::SupervisedByAnother,
     "the host starts and supervises the streaming child; starting it here would leave one "
     "instance nobody supervises and a second when the host starts its own"},
    {L"GNLinkCapture.exe", L"gnlinkcapture.exe", RelaunchKind::SupervisedByAnother,
     "a capture worker is started by the streaming host for the duration of a session"},
    {L"GNLinkViewer.exe", L"gnlinkviewer.exe", RelaunchKind::SupervisedByAnother,
     "the client shell starts the viewer when a connection is made, not at start-up"},
};

const KnownImage* find_known(const std::wstring& leafLower,
                             const std::vector<KnownImage>& table) {
  for (const KnownImage& image : table) {
    if (leafLower == image.lower) return &image;
  }
  return nullptr;
}

}  // namespace

const char* relaunch_kind_name(RelaunchKind kind) {
  switch (kind) {
    case RelaunchKind::ElevatedProcess: return "elevated-process";
    case RelaunchKind::UserProcess: return "user-process";
    case RelaunchKind::Service: return "service";
    case RelaunchKind::SupervisedByAnother: return "supervised-by-another";
  }
  return "unknown";
}

std::wstring image_leaf_lower(const std::wstring& path) {
  size_t start = 0;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == L'\\' || path[i] == L'/') start = i + 1;
  }
  std::wstring leaf = path.substr(start);
  std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                 [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? (c - L'A' + L'a') : c; });
  return leaf;
}

const std::vector<KnownImage>& product_images() { return kKnownImages; }

std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped) {
  return relaunch_plan(stopped, product_images());
}

std::vector<RelaunchEntry> relaunch_plan(const std::vector<ProcessTarget>& stopped,
                                         const std::vector<KnownImage>& table) {
  std::vector<RelaunchEntry> plan;
  std::vector<std::wstring> seen;

  for (const ProcessTarget& target : stopped) {
    const std::wstring leaf = image_leaf_lower(target.imagePath);
    if (leaf.empty()) continue;
    // One entry per image. The product runs one of each, and two would be worse than none.
    if (std::find(seen.begin(), seen.end(), leaf) != seen.end()) continue;

    const KnownImage* known = find_known(leaf, table);
    // Not one of ours. Dropped rather than started: an image name that arrived from outside is
    // data, and data does not get to nominate an executable for this process to run.
    if (!known) continue;

    seen.push_back(leaf);
    RelaunchEntry entry;
    entry.imageName = known->name;
    entry.kind = known->kind;
    entry.reason = known->reason;
    plan.push_back(entry);
  }
  return plan;
}

std::vector<RelaunchEntry> entries_to_start(const std::vector<RelaunchEntry>& plan) {
  std::vector<RelaunchEntry> out;
  for (const RelaunchEntry& entry : plan) {
    if (entry.kind == RelaunchKind::SupervisedByAnother) continue;
    out.push_back(entry);
  }
  return out;
}

}  // namespace remote60::native_poc::update
