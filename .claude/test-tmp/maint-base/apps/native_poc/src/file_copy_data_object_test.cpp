// The clipboard data object against a transport that re-enters it. (file-copy-helper r3 ③)
//
// The data object lives on an STA. Its waits on the host (the descriptor, every Read) pump
// messages, so while one call is inside a wait the shell can start another paste, the owner can
// clear the offer, the clipboard can change hands. What must never happen: a reply for the OLD
// operation confirming the NEW one's descriptor, or bytes for a paste that no longer exists being
// handed to a stream and counted as activity. This drives exactly those re-entries through a fake
// transport whose waits call back into the object -- deterministically, no shell, no pipe.
//
// Build: remote60_file_copy_data_object_test (CMake). Tags: pure-logic (COM object called in
// process; no apartment needed for direct calls).

#include <windows.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "file_copy_data_object.hpp"

using namespace remote60::native_poc::file_copy;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

uint8_t content_byte(uint64_t i, uint32_t k) { return static_cast<uint8_t>((i * 131 + k * 7) & 0xFF); }

class FakeTransport : public PasteTransport {
 public:
  std::vector<RemoteFileItem> confirmed;
  Status descriptorStatus = Status::Ok;
  Status readStatus = Status::Ok;
  uint32_t shortBy = 0;  // answer this many bytes fewer than asked (a truncating host)
  std::function<void()> duringWaitDescriptor;
  std::function<void()> duringRead;
  int begins = 0, descriptorWaits = 0, reads = 0;
  std::vector<std::pair<uint64_t, EndReason>> ends;
  uint64_t next = 1;

  uint64_t NewPasteOp() override { return next++; }
  void PasteBegin(uint64_t, uint64_t) override { ++begins; }
  Status WaitDescriptor(uint64_t, uint64_t, DWORD, std::vector<RemoteFileItem>* out) override {
    ++descriptorWaits;
    if (duringWaitDescriptor) duringWaitDescriptor();
    *out = confirmed;
    return descriptorStatus;
  }
  Status Read(uint64_t, uint64_t, uint32_t fileIndex, uint64_t offset, uint32_t length, DWORD, std::vector<uint8_t>* out) override {
    ++reads;
    if (duringRead) duringRead();
    const uint32_t n = length > shortBy ? length - shortBy : 0;
    out->resize(n);
    for (uint32_t i = 0; i < n; ++i) (*out)[i] = content_byte(offset + i, fileIndex);
    return readStatus;
  }
  void PasteEnd(uint64_t, uint64_t pasteOp, EndReason reason) override { ends.push_back({pasteOp, reason}); }
};

RemoteFileItem item(const char16_t* name, uint64_t size) {
  RemoteFileItem it;
  it.name = name;
  it.size = size;
  it.mtime = 1;
  it.attributes = 0x20;
  return it;
}

FORMATETC fe_descriptor() { return {static_cast<CLIPFORMAT>(cf_file_group_descriptor()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }
FORMATETC fe_contents(LONG index) { return {static_cast<CLIPFORMAT>(cf_file_contents()), nullptr, DVASPECT_CONTENT, index, TYMED_ISTREAM}; }

struct Fixture {
  FakeTransport fake;
  RemoteFilesDataObject* obj = nullptr;
  Fixture() {
    fake.confirmed = {item(u"a.bin", 300000), item(u"b.txt", 5)};
    DataObjectConfig cfg;
    obj = new RemoteFilesDataObject(7, fake.confirmed, &fake, cfg, [](const std::string& line) { std::printf("      | %s\n", line.c_str()); });
  }
  ~Fixture() { obj->Release(); }
  HRESULT descriptor(UINT* items) {
    FORMATETC fe = fe_descriptor();
    STGMEDIUM sm{};
    const HRESULT hr = obj->GetData(&fe, &sm);
    if (SUCCEEDED(hr)) {
      auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(sm.hGlobal));
      if (items) *items = g->cItems;
      GlobalUnlock(sm.hGlobal);
      ReleaseStgMedium(&sm);
    }
    return hr;
  }
  IStream* stream(LONG index, HRESULT* hr) {
    FORMATETC fe = fe_contents(index);
    STGMEDIUM sm{};
    *hr = obj->GetData(&fe, &sm);
    return SUCCEEDED(*hr) ? sm.pstm : nullptr;
  }
};

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  {
    std::printf("--- baseline: one async paste through the object ---\n");
    Fixture f;
    check("StartOperation begins a paste", f.obj->StartOperation(nullptr) == S_OK && f.fake.begins == 1 && f.obj->in_operation());
    UINT items = 0;
    check("the descriptor is the confirmed one", f.descriptor(&items) == S_OK && items == 2 && f.fake.descriptorWaits == 1);
    HRESULT hr = E_FAIL;
    IStream* s = f.stream(0, &hr);
    check("a content stream", SUCCEEDED(hr) && s != nullptr);
    std::vector<uint8_t> buf(256 * 1024);
    ULONG got = 0;
    const HRESULT r1 = s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got);
    bool ok = r1 == S_OK && got == buf.size();
    for (size_t i = 0; ok && i < buf.size(); ++i) ok = buf[i] == content_byte(i, 0);
    ULONG got2 = 0;
    const HRESULT r2 = s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got2);
    check("reads return the host's bytes; the last one is short and S_FALSE", ok && r2 == S_FALSE && got2 == 300000 - 262144, std::to_string(got2));
    ULONG got3 = 0;
    check("at EOF: 0 bytes, S_FALSE", s->Read(buf.data(), 16, &got3) == S_FALSE && got3 == 0);
    s->Release();
    check("EndOperation -> PasteEnd(ended)", f.obj->EndOperation(S_OK, nullptr, DROPEFFECT_COPY) == S_OK && f.fake.ends.size() == 1 &&
                                                 f.fake.ends[0].second == EndReason::Ended && !f.obj->in_operation());
  }
  {
    std::printf("\n--- ③ a second paste starts while the descriptor wait is pumping ---\n");
    Fixture f;
    f.obj->StartOperation(nullptr);
    const uint64_t first = f.obj->paste_op();
    HRESULT second = S_OK;
    f.fake.duringWaitDescriptor = [&] { second = f.obj->StartOperation(nullptr); };  // another paste, re-entrant
    UINT items = 0;
    const HRESULT hr = f.descriptor(&items);
    check("THE SECOND PASTE IS REFUSED (busy), NOT SWAPPED IN", second == HRESULT_FROM_WIN32(ERROR_BUSY),
          std::to_string(static_cast<unsigned long>(second)));
    check("...the running one is untouched: no end, same op", f.fake.ends.empty() && f.obj->in_operation() && f.obj->paste_op() == first);
    check("...and its own wait still confirms its descriptor", hr == S_OK && items == 2 && f.fake.descriptorWaits == 1,
          std::to_string(static_cast<unsigned long>(hr)));
    f.fake.duringWaitDescriptor = nullptr;
    HRESULT shr = E_FAIL;
    IStream* s = f.stream(0, &shr);
    check("...and serves its streams", SUCCEEDED(shr) && s);
    if (s) s->Release();
  }
  {
    std::printf("\n--- ③ a new paste starts while a Read is waiting on the host ---\n");
    Fixture f;
    f.obj->StartOperation(nullptr);
    UINT items = 0;
    f.descriptor(&items);
    HRESULT hr = E_FAIL;
    IStream* s = f.stream(0, &hr);
    std::vector<uint8_t> buf(4096, 0xEE);
    HRESULT second = S_OK;
    f.fake.duringRead = [&] { second = f.obj->StartOperation(nullptr); };
    ULONG got = 7;
    const HRESULT r = s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got);
    check("A SECOND PASTE DURING A READ IS REFUSED (busy)", second == HRESULT_FROM_WIN32(ERROR_BUSY));
    check("...and the running paste's Read completes with its bytes", SUCCEEDED(r) && got > 0 && buf[0] == content_byte(0, 0) &&
                                                                     f.fake.ends.empty() && f.fake.reads == 1);
    f.fake.duringRead = nullptr;
    s->Release();
  }
  {
    std::printf("\n--- ③ the offer is cleared / the clipboard changes hands while a Read is waiting ---\n");
    Fixture f;
    f.obj->StartOperation(nullptr);
    UINT items = 0;
    f.descriptor(&items);
    HRESULT hr = E_FAIL;
    IStream* s = f.stream(0, &hr);
    std::vector<uint8_t> buf(4096, 0xEE);
    f.fake.duringRead = [&] { f.obj->AbortOperation(EndReason::Cleared); };
    ULONG got = 7;
    const HRESULT r = s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got);
    check("a Read whose operation was cleared under it fails without bytes", FAILED(r) && got == 0 && buf[0] == 0xEE);
    check("...PasteEnd(cleared) once", f.fake.ends.size() == 1 && f.fake.ends[0].second == EndReason::Cleared && !f.obj->in_operation());
    s->Release();
  }
  {
    std::printf("\n--- the descriptor's own refusals ---\n");
    Fixture f;
    UINT items = 0;
    check("no StartOperation (synchronous consumer): E_FAIL, the host is never asked", FAILED(f.descriptor(&items)) && f.fake.descriptorWaits == 0);
    HRESULT hr = S_OK;
    check("...nor contents", f.stream(0, &hr) == nullptr && FAILED(hr));
    f.obj->StartOperation(nullptr);
    f.fake.confirmed = {item(u"only-one.bin", 1)};
    check("a confirmed count that differs from the offer: E_FAIL", FAILED(f.descriptor(&items)));
    f.fake.confirmed = {item(u"a.bin", 300000), item(u"..\\evil", 5)};
    check("a confirmed name the descriptor may not carry: E_FAIL", FAILED(f.descriptor(&items)));
    f.fake.confirmed = {item(u"a.bin", 300000), item(u"b.txt", 5)};
    f.fake.descriptorStatus = Status::Replaced;
    check("the host refusing (Replaced): E_FAIL", FAILED(f.descriptor(&items)));
    f.fake.descriptorStatus = Status::Ok;
    check("...and once the host confirms, S_OK", f.descriptor(&items) == S_OK);
  }
  {
    std::printf("\n--- a truncating host ---\n");
    Fixture f;
    f.obj->StartOperation(nullptr);
    UINT items = 0;
    f.descriptor(&items);
    HRESULT hr = E_FAIL;
    IStream* s = f.stream(0, &hr);
    f.fake.shortBy = 10;
    std::vector<uint8_t> buf(65536);
    ULONG got = 0;
    check("fewer bytes than asked before EOF is an error, not an EOF", FAILED(s->Read(buf.data(), static_cast<ULONG>(buf.size()), &got)) && got == 0);
    s->Release();
  }
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
