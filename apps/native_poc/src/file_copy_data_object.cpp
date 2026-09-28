// See file_copy_data_object.hpp.

#include "file_copy_data_object.hpp"

#include <shlwapi.h>

#include <algorithm>
#include <cstring>

namespace remote60::native_poc::file_copy {

// The documented format names, spelled out: the CFSTR_* macros follow UNICODE, which this
// target does not define.
UINT cf_file_group_descriptor() {
  static const UINT cf = RegisterClipboardFormatW(L"FileGroupDescriptorW");
  return cf;
}
UINT cf_file_contents() {
  static const UINT cf = RegisterClipboardFormatW(L"FileContents");
  return cf;
}
UINT cf_preferred_drop_effect() {
  static const UINT cf = RegisterClipboardFormatW(L"Preferred DropEffect");
  return cf;
}

namespace {

std::string narrow(const std::u16string& s) {
  std::string out;
  for (char16_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  return out;
}

}  // namespace

// One file of one paste operation. Position semantics are what the shell drives: Seek(0,CUR),
// Seek(0,SET), sequential 256 KiB reads, Seek(0) at the end, Release. Every Read goes to the host
// through the transport; nothing is buffered beyond the chunk in flight.
class RemoteFileStream : public IStream {
 public:
  RemoteFileStream(RemoteFilesDataObject* owner, uint64_t pasteOp, uint32_t index, uint64_t size)
      : owner_(owner), pasteOp_(pasteOp), index_(index), size_(size) {
    owner_->AddRef();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISequentialStream) {
      *ppv = static_cast<IStream*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = --ref_;
    if (r == 0) {
      owner_->log("stream index=" + std::to_string(index_) + " released pos=" + std::to_string(pos_) +
                  " reads=" + std::to_string(reads_));
      owner_->Release();
      delete this;
    }
    return r;
  }

  HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
    if (pcbRead) *pcbRead = 0;
    if (!pv) return STG_E_INVALIDPOINTER;
    ++reads_;
    if (!owner_->op_is(pasteOp_)) {
      owner_->log("stream index=" + std::to_string(index_) + " Read after the operation ended -> E_FAIL");
      return E_FAIL;
    }
    if (pos_ >= size_) return S_FALSE;  // EOF
    const uint64_t avail = size_ - pos_;
    const ULONG want = static_cast<ULONG>(std::min<uint64_t>(cb, avail));
    ULONG done = 0;
    auto* out = static_cast<uint8_t*>(pv);
    while (done < want) {
      const uint32_t chunk = static_cast<uint32_t>(std::min<ULONG>(want - done, kMaxChunkBytes));
      std::vector<uint8_t> data;
      ++owner_->waiting_;
      const Status st = owner_->transport_->Read(owner_->offerId_, pasteOp_, index_, pos_, chunk,
                                                owner_->config_.readTimeoutMs, &data);
      --owner_->waiting_;
      if (!owner_->op_is(pasteOp_)) {
        // The STA pumped while waiting and the operation ended or was replaced: these bytes are
        // for a paste that no longer exists. Nothing is copied, nothing is touched. (r3 ③)
        owner_->log("stream index=" + std::to_string(index_) + " op=" + std::to_string(pasteOp_) +
                    " ended while a Read was waiting -> E_FAIL");
        return E_FAIL;
      }
      if (st != Status::Ok) {
        owner_->log("stream index=" + std::to_string(index_) + " Read offset=" + std::to_string(pos_) +
                    " -> " + status_name(st) + " -> E_FAIL");
        return E_FAIL;
      }
      if (data.size() != chunk) {
        // The host answered short of the confirmed size: that is a truncated file, not an EOF.
        owner_->log("stream index=" + std::to_string(index_) + " Read offset=" + std::to_string(pos_) +
                    " short " + std::to_string(data.size()) + "/" + std::to_string(chunk) + " -> E_FAIL");
        return E_FAIL;
      }
      std::memcpy(out + done, data.data(), data.size());
      done += static_cast<ULONG>(data.size());
      pos_ += data.size();
      owner_->touch();
    }
    if (pcbRead) *pcbRead = done;
    if (reads_ <= 2 || (reads_ % 512) == 0) {
      owner_->log("stream index=" + std::to_string(index_) + " Read cb=" + std::to_string(cb) + " -> " +
                  std::to_string(done) + " pos=" + std::to_string(pos_));
    }
    return done < cb ? S_FALSE : S_OK;
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override {
    int64_t base = 0;
    if (origin == STREAM_SEEK_CUR) base = static_cast<int64_t>(pos_);
    else if (origin == STREAM_SEEK_END) base = static_cast<int64_t>(size_);
    else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
    const int64_t np = base + move.QuadPart;
    if (np < 0) return STG_E_INVALIDFUNCTION;
    pos_ = static_cast<uint64_t>(np);
    if (newPos) newPos->QuadPart = pos_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* st, DWORD) override {
    if (!st) return STG_E_INVALIDPOINTER;
    ZeroMemory(st, sizeof(*st));
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = size_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

 private:
  std::atomic<ULONG> ref_{1};
  RemoteFilesDataObject* const owner_;
  const uint64_t pasteOp_;
  const uint32_t index_;
  const uint64_t size_;
  uint64_t pos_ = 0;
  uint64_t reads_ = 0;
};

RemoteFilesDataObject::RemoteFilesDataObject(uint64_t offerId, std::vector<RemoteFileItem> items,
                                             PasteTransport* transport, DataObjectConfig config, LogFn log)
    : offerId_(offerId), items_(std::move(items)), transport_(transport), config_(config), log_(std::move(log)) {}

HRESULT RemoteFilesDataObject::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IDataObject) {
    *ppv = static_cast<IDataObject*>(this);
    AddRef();
    return S_OK;
  }
  if (riid == __uuidof(IDataObjectAsyncCapability)) {
    *ppv = static_cast<IDataObjectAsyncCapability*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}
ULONG RemoteFilesDataObject::AddRef() { return ++ref_; }
ULONG RemoteFilesDataObject::Release() {
  const ULONG r = --ref_;
  if (r == 0) {
    log("dataobject offer=" + std::to_string(offerId_) + " released");
    delete this;
  }
  return r;
}

void RemoteFilesDataObject::touch() { op_.lastActivityMs = GetTickCount64(); }

void RemoteFilesDataObject::end_operation(EndReason reason) {
  if (!op_.active) return;
  log("paste op=" + std::to_string(op_.pasteOp) + " end reason=" + end_reason_name(reason));
  const uint64_t op = op_.pasteOp;
  op_ = Operation{};
  transport_->PasteEnd(offerId_, op, reason);
}

void RemoteFilesDataObject::AbortOperation(EndReason reason) { end_operation(reason); }

HRESULT RemoteFilesDataObject::descriptor(STGMEDIUM* sm) {
  if (!op_.active) {
    // No StartOperation: a synchronous consumer. Nothing is served to it (header).
    log("descriptor refused: no async operation (synchronous consumer)");
    return E_FAIL;
  }
  if (!op_.confirmed) {
    // The operation this wait belongs to, fixed BEFORE the wait: the STA pumps while waiting, and
    // a new StartOperation (a second paste, a cleared offer) can replace op_ underneath. A reply
    // for the old operation must not become the new one's descriptor. (r3 ③)
    const uint64_t op = op_.pasteOp;
    std::vector<RemoteFileItem> confirmed;
    ++waiting_;
    const Status st = transport_->WaitDescriptor(offerId_, op, config_.descriptorTimeoutMs, &confirmed);
    --waiting_;
    if (!op_is(op)) {
      log("descriptor: op=" + std::to_string(op) + " changed while waiting (now " +
          (op_.active ? std::to_string(op_.pasteOp) : std::string("none")) + ") -> E_FAIL");
      return E_FAIL;
    }
    if (st != Status::Ok) {
      log("descriptor: host answered " + std::string(status_name(st)) + " -> E_FAIL");
      return E_FAIL;
    }
    if (confirmed.size() != items_.size()) {
      log("descriptor: confirmed count " + std::to_string(confirmed.size()) + " != offer " +
          std::to_string(items_.size()) + " -> E_FAIL");
      return E_FAIL;
    }
    for (const auto& it : confirmed) {
      if (!validate_remote_name(it.name)) {
        log("descriptor: confirmed name refused -> E_FAIL");
        return E_FAIL;
      }
    }
    op_.items = std::move(confirmed);
    op_.confirmed = true;
    touch();
  }
  const size_t count = op_.items.size();
  const size_t bytes = sizeof(FILEGROUPDESCRIPTORW) + sizeof(FILEDESCRIPTORW) * (count == 0 ? 0 : count - 1);
  HGLOBAL h = GlobalAlloc(GHND, bytes);
  if (!h) return E_OUTOFMEMORY;
  auto* g = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(h));
  g->cItems = static_cast<UINT>(count);
  for (size_t k = 0; k < count; ++k) {
    const RemoteFileItem& it = op_.items[k];
    FILEDESCRIPTORW& d = g->fgd[k];
    d.dwFlags = FD_FILESIZE | FD_ATTRIBUTES | FD_WRITESTIME | FD_PROGRESSUI;
    DWORD attrs = it.attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                                                      FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_SYSTEM);
    if (attrs == 0) attrs = FILE_ATTRIBUTE_NORMAL;
    d.dwFileAttributes = attrs;
    d.nFileSizeHigh = static_cast<DWORD>(it.size >> 32);
    d.nFileSizeLow = static_cast<DWORD>(it.size);
    d.ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(it.mtime);
    d.ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(it.mtime >> 32);
    const size_t n = std::min<size_t>(it.name.size(), MAX_PATH - 1);
    for (size_t i = 0; i < n; ++i) d.cFileName[i] = static_cast<wchar_t>(it.name[i]);
    d.cFileName[n] = 0;
  }
  GlobalUnlock(h);
  sm->tymed = TYMED_HGLOBAL;
  sm->hGlobal = h;
  sm->pUnkForRelease = nullptr;
  log("descriptor served op=" + std::to_string(op_.pasteOp) + " items=" + std::to_string(count));
  return S_OK;
}

HRESULT RemoteFilesDataObject::contents(LONG lindex, STGMEDIUM* sm) {
  if (!op_.active || !op_.confirmed) {
    log("contents refused: no confirmed operation");
    return E_FAIL;
  }
  if (lindex < 0 || static_cast<size_t>(lindex) >= op_.items.size()) return DV_E_LINDEX;
  ++contentsCalls_;
  touch();
  sm->tymed = TYMED_ISTREAM;
  sm->pstm = new RemoteFileStream(this, op_.pasteOp, static_cast<uint32_t>(lindex), op_.items[lindex].size);
  sm->pUnkForRelease = nullptr;
  log("contents index=" + std::to_string(lindex) + " (" + narrow(op_.items[lindex].name) + ") size=" +
      std::to_string(op_.items[lindex].size) + " op=" + std::to_string(op_.pasteOp));
  return S_OK;
}

HRESULT RemoteFilesDataObject::GetData(FORMATETC* fe, STGMEDIUM* sm) {
  if (!fe || !sm) return E_POINTER;
  ZeroMemory(sm, sizeof(*sm));
  if (fe->cfFormat == cf_file_group_descriptor() && (fe->tymed & TYMED_HGLOBAL)) return descriptor(sm);
  if (fe->cfFormat == cf_file_contents() && (fe->tymed & TYMED_ISTREAM)) return contents(fe->lindex, sm);
  if (fe->cfFormat == cf_preferred_drop_effect() && (fe->tymed & TYMED_HGLOBAL)) {
    HGLOBAL h = GlobalAlloc(GHND, sizeof(DWORD));
    if (!h) return E_OUTOFMEMORY;
    *static_cast<DWORD*>(GlobalLock(h)) = DROPEFFECT_COPY;
    GlobalUnlock(h);
    sm->tymed = TYMED_HGLOBAL;
    sm->hGlobal = h;
    return S_OK;
  }
  return DV_E_FORMATETC;
}
HRESULT RemoteFilesDataObject::GetDataHere(FORMATETC*, STGMEDIUM*) { return E_NOTIMPL; }
HRESULT RemoteFilesDataObject::QueryGetData(FORMATETC* fe) {
  if (!fe) return E_POINTER;
  const bool ok = fe->cfFormat == cf_file_group_descriptor() || fe->cfFormat == cf_file_contents() ||
                  fe->cfFormat == cf_preferred_drop_effect();
  return ok ? S_OK : DV_E_FORMATETC;
}
HRESULT RemoteFilesDataObject::GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) {
  if (out) out->ptd = nullptr;
  return E_NOTIMPL;
}
HRESULT RemoteFilesDataObject::SetData(FORMATETC*, STGMEDIUM* sm, BOOL release) {
  // The shell reports effects back (Performed DropEffect and friends). Accepted and dropped.
  if (release && sm) ReleaseStgMedium(sm);
  return S_OK;
}
HRESULT RemoteFilesDataObject::EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppenum) {
  if (dir != DATADIR_GET) return E_NOTIMPL;
  FORMATETC fmts[3] = {
      {static_cast<CLIPFORMAT>(cf_file_group_descriptor()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
      {static_cast<CLIPFORMAT>(cf_file_contents()), nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
      {static_cast<CLIPFORMAT>(cf_preferred_drop_effect()), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
  return SHCreateStdEnumFmtEtc(3, fmts, ppenum);
}
HRESULT RemoteFilesDataObject::DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) { return OLE_E_ADVISENOTSUPPORTED; }
HRESULT RemoteFilesDataObject::DUnadvise(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
HRESULT RemoteFilesDataObject::EnumDAdvise(IEnumSTATDATA**) { return OLE_E_ADVISENOTSUPPORTED; }

HRESULT RemoteFilesDataObject::SetAsyncMode(BOOL fDoOpAsync) {
  asyncMode_ = fDoOpAsync;
  return S_OK;
}
HRESULT RemoteFilesDataObject::GetAsyncMode(BOOL* pfIsOpAsync) {
  if (!pfIsOpAsync) return E_POINTER;
  *pfIsOpAsync = asyncMode_;
  return S_OK;
}
HRESULT RemoteFilesDataObject::StartOperation(IBindCtx*) {
  if (op_.active) {
    // Nobody ended the previous paste; the new one supersedes it rather than sharing its handles.
    end_operation(EndReason::Superseded);
  }
  op_ = Operation{};
  op_.active = true;
  op_.pasteOp = transport_->NewPasteOp();
  touch();
  log("paste op=" + std::to_string(op_.pasteOp) + " begin (StartOperation) offer=" + std::to_string(offerId_));
  transport_->PasteBegin(offerId_, op_.pasteOp);
  return S_OK;
}
HRESULT RemoteFilesDataObject::InOperation(BOOL* pfInAsyncOp) {
  if (!pfInAsyncOp) return E_POINTER;
  *pfInAsyncOp = op_.active ? TRUE : FALSE;
  return S_OK;
}
HRESULT RemoteFilesDataObject::EndOperation(HRESULT hResult, IBindCtx*, DWORD dwEffects) {
  log("EndOperation hr=" + std::to_string(static_cast<unsigned long>(hResult)) + " effects=" + std::to_string(dwEffects));
  end_operation(SUCCEEDED(hResult) ? EndReason::Ended : EndReason::Error);
  return S_OK;
}

}  // namespace remote60::native_poc::file_copy
