#pragma once

// The helper's clipboard object for files that live on the other side of the connection.
// (file-copy-helper r1, plan §1 / §2 P->R, contract ⑥ ⑧)
//
// Role:    an IDataObject offering CFSTR_FILEDESCRIPTORW + CFSTR_FILECONTENTS (one IStream per
//          file) and IDataObjectAsyncCapability. Explorer's paste negotiates async, calls
//          StartOperation inside the drop, then -- from its own background thread -- asks for the
//          descriptor and reads each file 256 KiB at a time. Nothing is served synchronously: a
//          consumer that never calls StartOperation gets E_FAIL for the descriptor, because a
//          synchronous consumer that then fails a Read hangs inside a modal dialog nobody can see
//          (probe T3b), and because a synchronous Read would make a network wait on somebody's
//          UI thread.
// Data:    the descriptor is NOT the offer's metadata. StartOperation sends PasteBegin; the first
//          descriptor request waits (bounded) for PasteDescriptor, the sizes / times the host
//          confirmed after pinning the originals. If they cannot be confirmed, the paste fails
//          before a single byte, never with quietly different content (3차 합의 ③).
// Thread:  the helper's STA thread. COM marshals every call here; the transport's waits pump
//          (CoWaitForMultipleHandles) so the STA stays responsive and re-entrant calls are served.
// Ends:    EndOperation -> PasteEnd(ended / error). The owner ends an operation that nobody ends:
//          idle (no stream activity for the bound), the offer cleared, the object no longer on
//          the clipboard, the pipe gone. A stream whose operation has ended fails its next Read.

#include <objidl.h>
#include <shldisp.h>
#include <shlobj.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "file_copy_pipe.hpp"

namespace remote60::native_poc::file_copy {

/**
 * The data object's way out to the host, implemented over the pipe by the helper (and by a fake
 * in tests). PasteBegin / PasteEnd never block; WaitDescriptor / Read block the calling STA
 * thread up to `timeoutMs` while pumping. All calls arrive on the STA thread.
 */
class PasteTransport {
 public:
  virtual ~PasteTransport() = default;
  virtual uint64_t NewPasteOp() = 0;
  virtual void PasteBegin(uint64_t offerId, uint64_t pasteOp) = 0;
  virtual Status WaitDescriptor(uint64_t offerId, uint64_t pasteOp, DWORD timeoutMs,
                                std::vector<RemoteFileItem>* confirmed) = 0;
  virtual Status Read(uint64_t offerId, uint64_t pasteOp, uint32_t fileIndex, uint64_t offset, uint32_t length,
                      DWORD timeoutMs, std::vector<uint8_t>* out) = 0;
  virtual void PasteEnd(uint64_t offerId, uint64_t pasteOp, EndReason reason) = 0;
};

struct DataObjectConfig {
  DWORD descriptorTimeoutMs = 10000;
  DWORD readTimeoutMs = 30000;
};

using LogFn = std::function<void(const std::string&)>;

UINT cf_file_group_descriptor();
UINT cf_file_contents();
UINT cf_preferred_drop_effect();

class RemoteFilesDataObject : public IDataObject, public IDataObjectAsyncCapability {
 public:
  RemoteFilesDataObject(uint64_t offerId, std::vector<RemoteFileItem> items, PasteTransport* transport,
                        DataObjectConfig config, LogFn log);

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  // IDataObject
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fe, STGMEDIUM* sm) override;
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override;
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fe) override;
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override;
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC* fe, STGMEDIUM* sm, BOOL release) override;
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppenum) override;
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override;
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override;
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override;
  // IDataObjectAsyncCapability
  HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL fDoOpAsync) override;
  HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* pfIsOpAsync) override;
  HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx* pbcReserved) override;
  HRESULT STDMETHODCALLTYPE InOperation(BOOL* pfInAsyncOp) override;
  HRESULT STDMETHODCALLTYPE EndOperation(HRESULT hResult, IBindCtx* pbcReserved, DWORD dwEffects) override;

  // Owner's view (STA thread).
  uint64_t offer_id() const { return offerId_; }
  bool in_operation() const { return op_.active; }
  uint64_t paste_op() const { return op_.pasteOp; }
  uint64_t last_activity_ms() const { return op_.lastActivityMs; }
  /** True while a descriptor / read wait on the host is in progress (not idle, whatever the clock says). */
  bool waiting() const { return waiting_ > 0; }
  uint32_t contents_calls() const { return contentsCalls_; }
  /** Ends the current operation on the owner's behalf (idle, cleared, released, disconnected). */
  void AbortOperation(EndReason reason);

 private:
  friend class RemoteFileStream;
  struct Operation {
    bool active = false;
    uint64_t pasteOp = 0;
    bool confirmed = false;
    std::vector<RemoteFileItem> items;  // the confirmed descriptor, once it arrived
    uint64_t lastActivityMs = 0;
  };
  bool op_is(uint64_t pasteOp) const { return op_.active && op_.pasteOp == pasteOp; }
  void touch();
  void end_operation(EndReason reason);
  HRESULT descriptor(STGMEDIUM* sm);
  HRESULT contents(LONG lindex, STGMEDIUM* sm);
  void log(const std::string& line) { if (log_) log_(line); }

  std::atomic<ULONG> ref_{1};
  const uint64_t offerId_;
  const std::vector<RemoteFileItem> items_;  // the offer as published (names validated)
  PasteTransport* const transport_;
  const DataObjectConfig config_;
  LogFn log_;
  BOOL asyncMode_ = TRUE;
  Operation op_;
  uint32_t contentsCalls_ = 0;
  int waiting_ = 0;  // nested transport waits in progress on the STA (re-entrant calls can stack)
};

}  // namespace remote60::native_poc::file_copy
