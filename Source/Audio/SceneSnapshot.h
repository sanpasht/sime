#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SceneSnapshot.h  –  Flat, POD-safe scene description for the audio thread.
//
// Built once per GL frame by SoundScene::buildSnapshot().
// Transferred to audio thread via AtomicSnapshotBuffer.
// No heap-owning types. No strings. No shared_ptr.
// ─────────────────────────────────────────────────────────────────────────────

#include "../MathUtils.h"
#include "AudioConfig.h"
#include <cstdint>

struct SnapshotBlockEntry
{
    uint64_t    id               = 0;
    Vec3f       position;
    float       startTime        = 0.0f;
    float       duration         = 0.0f;
    const float* samplesL        = nullptr;
    const float* samplesR        = nullptr;
    int         totalSamples     = 0;
    int         nativeSampleRate = 0;
    float       gainLinear       = 1.0f;
    float       attenuationRadius = AudioConfig::kDefaultAttenRadius;
    float       spread           = 0.0f;
    bool        looping          = false;
};

struct SceneSnapshot
{
    Vec3f       listenerPos;
    Vec3f       listenerForward;
    Vec3f       listenerRight;
    double      transportTimeSec = 0.0;
    bool        isPlaying        = false;
    int         numBlocks        = 0;

    SnapshotBlockEntry blocks[AudioConfig::kMaxBlocks] {};
};
