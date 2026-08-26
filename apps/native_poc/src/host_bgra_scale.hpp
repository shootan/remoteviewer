#pragma once

// CPU-side BGRA frame geometry and scaling helpers used by the host encode path and the picker.
//
// Role:    encode-size selection (aspect fit, bitrate ladder, ABR 720p box), frame change
//          estimation for static-frame gating, box-halve + bilinear downscale, and the GDI
//          PrintWindow thumbnail grab for the window picker.
// Thread:  pure functions over caller-owned buffers. capture_window_thumbnail is called from the
//          control thread (window-list/thumbnail requests); the rest from the main encode loop.
// Input:   BGRA buffers + dimensions, Args (encodeWidth/encodeHeight/bitrate), HWND for thumbnails.
// Output:  chosen dimensions / permille change / resized BGRA in caller-supplied vectors.
// Callers: native_video_host_main.cpp (main loop, control session), host_gpu_scaler (CPU fallback).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-2). Definitions
// live in host_bgra_scale.cpp; behavior is byte-identical.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "host_args.hpp"

namespace remote60::native_poc {

uint32_t clamp_even_dim(uint32_t v, uint32_t minValue, uint32_t maxValue);

// Fit a source frame inside a target box without changing its aspect ratio. Encoding a
// 4:3 window into a 16:9 box (the shipped profiles) otherwise stretches the picture.
void fit_size_preserving_aspect(uint32_t srcW, uint32_t srcH, uint32_t boxW, uint32_t boxH,
                                uint32_t* outW, uint32_t* outH);

void choose_h264_encode_size(const Args& args, uint32_t captureW, uint32_t captureH,
                             uint32_t* outW, uint32_t* outH, bool* outAutoFallback720);

void choose_abr_720_size(uint32_t captureW, uint32_t captureH, uint32_t* outW, uint32_t* outH);

// Fraction of the frame that differs from the previous one, in permille. Returns 0 if and only if
// the two frames are byte-identical (see the definition for why this is a full blockwise compare).
uint32_t estimate_bgra_change_permille(const uint8_t* a, const uint8_t* b, size_t sizeBytes,
                                       uint32_t sampleTarget);

// Average 2x2 blocks into one pixel (used ahead of bilinear for >2x downscales).
void box_halve_bgra(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                    std::vector<uint8_t>* out, uint32_t* outW, uint32_t* outH);

bool resize_bgra_bilinear(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                          uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra);

// Grab a still preview of one window (nullptr = whole virtual desktop) for the target picker.
bool capture_window_thumbnail(HWND hwnd, uint32_t maxW, uint32_t maxH,
                              std::vector<uint8_t>* outBgra, uint32_t* outW, uint32_t* outH);

}  // namespace remote60::native_poc
