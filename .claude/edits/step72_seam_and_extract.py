import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

# ---- (A1) the transform seam -------------------------------------------------------------------
edit(S + r'\mf_h264_codec.hpp', [
("  bool provenance_invalid() const { return pendingInputs_.provenance_invalid(); }\n  void shutdown();",
 "  bool provenance_invalid() const { return pendingInputs_.provenance_invalid(); }\n"
 "  /**\n"
 "   * Test seam (A06 regression only): initialize() adopts this transform instead of enumerating\n"
 "   * one, so a test can drive the accepted-input FIFO and the drain path deterministically --\n"
 "   * an overflow followed by a failing drain cannot be produced through a real encoder.\n"
 "   *\n"
 "   * Product code never calls this and its behaviour is unchanged when it is unset: the\n"
 "   * enumeration, the backend choice and every configuration step stay exactly as they are.\n"
 "   * Ownership: this encoder holds its own reference (ComPtr) from here until shutdown() releases\n"
 "   * it, like any enumerated transform; the caller keeps its own reference. Passing nullptr\n"
 "   * restores the enumeration path. If initialize() fails after adopting it, shutdown() (which\n"
 "   * initialize() calls on entry and on failure) releases it as usual.\n"
 "   */\n"
 "  void set_transform_for_test(IMFTransform* transform) { testTransform_ = transform; }\n"
 "  void shutdown();"),
("  Microsoft::WRL::ComPtr<IMFTransform> enc_;",
 "  Microsoft::WRL::ComPtr<IMFTransform> enc_;\n  Microsoft::WRL::ComPtr<IMFTransform> testTransform_;  // see set_transform_for_test"),
])
edit(S + r'\mf_h264_codec.cpp', [
("  IMFTransform* encRaw = nullptr;\n"
 "  codec_debug_log(\"encoder initialize: create transform\");\n"
 "  if (!create_h264_encoder_transform(&encRaw, &usingHardware_, &backendName_) || !encRaw) return false;\n"
 "  codec_debug_log(\"encoder initialize: transform created\");\n"
 "  enc_.Attach(encRaw);",
 "  if (testTransform_) {\n"
 "    // Test seam only (set_transform_for_test): adopt the caller's transform and skip the\n"
 "    // enumeration. Everything after this point runs exactly as it does for an enumerated one.\n"
 "    enc_ = testTransform_;\n"
 "    usingHardware_ = false;\n"
 "    backendName_ = \"test_transform\";\n"
 "    codec_debug_log(\"encoder initialize: using the injected test transform\");\n"
 "  } else {\n"
 "    IMFTransform* encRaw = nullptr;\n"
 "    codec_debug_log(\"encoder initialize: create transform\");\n"
 "    if (!create_h264_encoder_transform(&encRaw, &usingHardware_, &backendName_) || !encRaw) return false;\n"
 "    codec_debug_log(\"encoder initialize: transform created\");\n"
 "    enc_.Attach(encRaw);\n"
 "  }"),
])

# ---- (A2) behaviour-preserving extraction: one owner for encode + provenance --------------------
edit(S + r'\host_encoder_manager.hpp', [
("  bool NoteProvenance(bool codecProvenanceInvalid) {\n"
 "    if (!codecProvenanceInvalid || epochGate.provenanceInvalid) return false;\n"
 "    epochGate.provenanceInvalid = true;\n"
 "    provenanceResyncPending = true;\n"
 "    return true;\n"
 "  }",
 "  bool NoteProvenance(bool codecProvenanceInvalid) {\n"
 "    if (!codecProvenanceInvalid || epochGate.provenanceInvalid) return false;\n"
 "    epochGate.provenanceInvalid = true;\n"
 "    provenanceResyncPending = true;\n"
 "    return true;\n"
 "  }\n"
 "\n"
 "  /** What one encode call did: its own answer, and whether it was the call that lost provenance. */\n"
 "  struct EncodeCallResult {\n"
 "    bool ok = false;                 // what encode_frame_* returned\n"
 "    bool provenanceLatched = false;  // NoteProvenance latched here (first time only: log once)\n"
 "  };\n"
 "\n"
 "  /**\n"
 "   * One encode call and the provenance handling that must follow it. This is a\n"
 "   * BEHAVIOUR-PRESERVING EXTRACTION of what the encode stage did inline, not a pure function:\n"
 "   * it drives the encoder and moves this object's state. It exists so that no exit from an\n"
 "   * encode call can skip the provenance check -- an encode that fails AFTER the MFT accepted the\n"
 "   * input may be the very call that overflowed the accepted-input FIFO, and the stage returns\n"
 "   * early on failure, so a check placed after the failure branch was never reached (A06). Every\n"
 "   * encode path -- surface, BGRA, and the test's -- goes through here, so the order\n"
 "   * `encode -> provenance -> the caller's failure handling` is a property of the code, not of\n"
 "   * remembering to repeat it.\n"
 "   */\n"
 "  template <typename EncodeFn>\n"
 "  EncodeCallResult RunEncodeCall(EncodeFn&& encodeCall) {\n"
 "    EncodeCallResult result{};\n"
 "    result.ok = encodeCall();\n"
 "    result.provenanceLatched = NoteProvenance(codec.provenance_invalid());\n"
 "    return result;\n"
 "  }\n"
 "\n"
 "  /** The stage's BGRA path, with the provenance handling it must not skip. */\n"
 "  EncodeCallResult EncodeBgraWithProvenance(const uint8_t* bgra, uint32_t width, uint32_t height,\n"
 "                                            uint32_t stride, bool forceKeyFrame,\n"
 "                                            int64_t inputSampleTimeHns,\n"
 "                                            std::vector<H264AccessUnit>* outUnits,\n"
 "                                            H264EncodeFrameStats* encodeStats) {\n"
 "    return RunEncodeCall([&] {\n"
 "      return codec.encode_frame_bgra(bgra, width, height, stride, forceKeyFrame,\n"
 "                                     inputSampleTimeHns, outUnits, encodeStats);\n"
 "    });\n"
 "  }\n"
 "\n"
 "  /** The stage's zero-copy path, same contract. */\n"
 "  EncodeCallResult EncodeSurfaceWithProvenance(ID3D11Texture2D* texture, bool forceKeyFrame,\n"
 "                                               int64_t inputSampleTimeHns,\n"
 "                                               std::vector<H264AccessUnit>* outUnits,\n"
 "                                               H264EncodeFrameStats* encodeStats) {\n"
 "    return RunEncodeCall([&] {\n"
 "      return codec.encode_frame_surface(texture, forceKeyFrame, inputSampleTimeHns, outUnits,\n"
 "                                        encodeStats);\n"
 "    });\n"
 "  }"),
])
print('seam + extraction helpers ok')
