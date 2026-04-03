#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioConfig.h  –  Compile-time constants for the SIME audio engine.
// ─────────────────────────────────────────────────────────────────────────────

namespace AudioConfig
{
    inline constexpr int   kPreferredSampleRate = 44100;
    inline constexpr int   kPreferredBufferSize = 512;
    inline constexpr int   kMaxVoices           = 128;
    inline constexpr int   kMaxBlocks           = 128;
    inline constexpr float kSmoothingCoeff      = 0.9995f;
    inline constexpr float kDefaultAttenRadius  = 50.0f;
    inline constexpr float kAttenuationMinDist  = 1.0f;
    inline constexpr float kOutputLimiterDrive  = 1.5f;
}
