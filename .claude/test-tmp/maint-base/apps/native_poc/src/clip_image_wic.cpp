#include "clip_image_wic.hpp"

#include <objidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>

#include "clip_image_core.hpp"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

namespace remote60::native_poc {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWICImagingFactory> factory() {
  ComPtr<IWICImagingFactory> f;
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
  return f;
}

// A read-only IStream over two segments -- a small prefix and a caller-owned buffer -- so a packed
// DIB can be decoded as a .bmp without copying its pixels behind a file header (plan r2 §9 S2).
class TwoPartStream : public IStream {
 public:
  TwoPartStream(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) : a_(a), na_(na), b_(b), nb_(nb) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
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
    if (!r) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
    auto* out = static_cast<uint8_t*>(pv);
    ULONG done = 0;
    while (done < cb && pos_ < na_ + nb_) {
      const bool first = pos_ < na_;
      const uint8_t* src = first ? a_ + pos_ : b_ + (pos_ - na_);
      const uint64_t avail = first ? na_ - pos_ : na_ + nb_ - pos_;
      const ULONG take = static_cast<ULONG>(std::min<uint64_t>(avail, cb - done));
      std::memcpy(out + done, src, take);
      done += take;
      pos_ += take;
    }
    if (pcbRead) *pcbRead = done;
    return done < cb ? S_FALSE : S_OK;
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override {
    const int64_t base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? static_cast<int64_t>(pos_)
                                                                                    : static_cast<int64_t>(na_ + nb_);
    const int64_t np = base + move.QuadPart;
    if (np < 0) return STG_E_INVALIDFUNCTION;
    pos_ = static_cast<uint64_t>(np);
    if (newPos) newPos->QuadPart = pos_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* st, DWORD) override {
    std::memset(st, 0, sizeof(*st));
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = na_ + nb_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

 private:
  std::atomic<ULONG> ref_{1};
  const uint8_t* a_;
  uint64_t na_;
  const uint8_t* b_;
  uint64_t nb_;
  uint64_t pos_ = 0;
};

ComPtr<IWICBitmapFrameDecode> png_frame(IWICImagingFactory* f, const uint8_t* png, size_t bytes) {
  ComPtr<IWICStream> s;
  ComPtr<IWICBitmapDecoder> dec;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(f->CreateStream(&s)) ||
      FAILED(s->InitializeFromMemory(const_cast<BYTE*>(png), static_cast<DWORD>(bytes))) ||
      FAILED(f->CreateDecoderFromStream(s.Get(), &GUID_ContainerFormatPng, WICDecodeMetadataCacheOnDemand, &dec)) ||
      FAILED(dec->GetFrame(0, &frame))) {
    return nullptr;
  }
  return frame;
}

}  // namespace

ClipWicResult clip_png_dimensions(const uint8_t* png, size_t bytes, uint32_t* width, uint32_t* height) {
  auto f = factory();
  if (!f) return ClipWicResult::ComFailed;
  auto frame = png_frame(f.Get(), png, bytes);
  if (!frame) return ClipWicResult::NotAnImage;
  UINT w = 0, h = 0;
  if (FAILED(frame->GetSize(&w, &h))) return ClipWicResult::NotAnImage;
  *width = w;
  *height = h;
  return ClipWicResult::Ok;
}

ClipWicResult clip_dib_to_png(const uint8_t* dib, size_t bytes, std::vector<uint8_t>* png, uint32_t* width,
                              uint32_t* height) {
  const DibInfo info = validate_dib(dib, bytes);
  if (!info.ok()) return ClipWicResult::BadDib;
  const ClipImageGate gate = clip_image_gate(info.width, info.height, 0, 0);
  if (!gate.ok()) return ClipWicResult::GateRefused;
  auto f = factory();
  if (!f) return ClipWicResult::ComFailed;
  // A BITMAPFILEHEADER in front of the untouched DIB: WIC's BMP decoder then handles every bit
  // depth, palette and mask the header validation let through.
  BITMAPFILEHEADER fh{};
  fh.bfType = 0x4D42;  // 'BM'
  fh.bfSize = static_cast<DWORD>(sizeof(fh) + info.pixelOffset + info.pixelBytes);
  fh.bfOffBits = static_cast<DWORD>(sizeof(fh) + info.pixelOffset);
  auto* stream = new TwoPartStream(reinterpret_cast<const uint8_t*>(&fh), sizeof(fh), dib, bytes);
  ComPtr<IStream> in;
  in.Attach(stream);
  ComPtr<IWICBitmapDecoder> dec;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(f->CreateDecoderFromStream(in.Get(), &GUID_ContainerFormatBmp, WICDecodeMetadataCacheOnDemand, &dec)) ||
      FAILED(dec->GetFrame(0, &frame))) {
    return ClipWicResult::NotAnImage;
  }
  UINT w = 0, h = 0;
  frame->GetSize(&w, &h);
  ComPtr<IStream> out;
  out.Attach(SHCreateMemStream(nullptr, 0));
  ComPtr<IWICBitmapEncoder> enc;
  ComPtr<IWICBitmapFrameEncode> fe;
  ComPtr<IPropertyBag2> props;
  if (!out || FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
      FAILED(enc->Initialize(out.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&fe, &props)) ||
      FAILED(fe->Initialize(props.Get())) || FAILED(fe->SetSize(w, h))) {
    return ClipWicResult::ComFailed;
  }
  WICPixelFormatGUID fmt{};
  frame->GetPixelFormat(&fmt);
  WICPixelFormatGUID want = fmt;
  fe->SetPixelFormat(&want);
  // WriteSource pulls rows from the decoder, converting as it goes: no whole-canvas copy here.
  if (FAILED(fe->WriteSource(frame.Get(), nullptr)) || FAILED(fe->Commit()) || FAILED(enc->Commit())) {
    return ClipWicResult::ComFailed;
  }
  STATSTG st{};
  out->Stat(&st, STATFLAG_NONAME);
  if (st.cbSize.QuadPart > kClipImageMaxPngBytes) return ClipWicResult::GateRefused;
  png->resize(static_cast<size_t>(st.cbSize.QuadPart));
  LARGE_INTEGER zero{};
  out->Seek(zero, STREAM_SEEK_SET, nullptr);
  ULONG got = 0;
  out->Read(png->data(), static_cast<ULONG>(png->size()), &got);
  png->resize(got);
  *width = w;
  *height = h;
  return ClipWicResult::Ok;
}

ClipWicResult clip_png_to_dibv5(const uint8_t* png, size_t bytes, uint32_t expectWidth, uint32_t expectHeight,
                                HGLOBAL* out) {
  *out = nullptr;
  auto f = factory();
  if (!f) return ClipWicResult::ComFailed;
  auto frame = png_frame(f.Get(), png, bytes);
  if (!frame) return ClipWicResult::NotAnImage;
  UINT w = 0, h = 0;
  if (FAILED(frame->GetSize(&w, &h))) return ClipWicResult::NotAnImage;
  if (w != expectWidth || h != expectHeight) return ClipWicResult::SizeMismatch;
  const ClipImageGate gate = clip_image_gate(w, h, bytes, 0);
  if (!gate.ok()) return ClipWicResult::GateRefused;
  ComPtr<IWICFormatConverter> conv;
  if (FAILED(f->CreateFormatConverter(&conv)) ||
      FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom))) {
    return ClipWicResult::ComFailed;
  }
  const uint64_t stride = static_cast<uint64_t>(w) * 4;
  const uint64_t pixels = stride * h;
  const size_t total = sizeof(BITMAPV5HEADER) + static_cast<size_t>(pixels);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, total);
  if (!mem) return ClipWicResult::NoMemory;
  auto* base = static_cast<uint8_t*>(GlobalLock(mem));
  BITMAPV5HEADER hdr{};
  hdr.bV5Size = sizeof(hdr);
  hdr.bV5Width = static_cast<LONG>(w);
  hdr.bV5Height = -static_cast<LONG>(h);  // top-down: rows in the order WIC writes them
  hdr.bV5Planes = 1;
  hdr.bV5BitCount = 32;
  hdr.bV5Compression = BI_BITFIELDS;
  hdr.bV5SizeImage = static_cast<DWORD>(pixels);
  hdr.bV5RedMask = 0x00FF0000;
  hdr.bV5GreenMask = 0x0000FF00;
  hdr.bV5BlueMask = 0x000000FF;
  hdr.bV5AlphaMask = 0xFF000000;
  hdr.bV5CSType = LCS_sRGB;
  hdr.bV5Intent = LCS_GM_IMAGES;
  std::memcpy(base, &hdr, sizeof(hdr));
  // Straight into the memory the clipboard will own (plan r2 §9 R3): no separate BGRA buffer.
  const HRESULT hr = conv->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(pixels),
                                      base + sizeof(hdr));
  GlobalUnlock(mem);
  if (FAILED(hr)) {
    GlobalFree(mem);
    return ClipWicResult::ComFailed;
  }
  *out = mem;
  return ClipWicResult::Ok;
}

}  // namespace remote60::native_poc
