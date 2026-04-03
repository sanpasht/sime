#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Spatializer.h  –  Stereo panning + distance attenuation for headphones.
//
// Stateless. Audio thread only.
// ─────────────────────────────────────────────────────────────────────────────

#include "../MathUtils.h"
#include "AudioConfig.h"

class Spatializer
{
public:
    static void computeGains(const Vec3f& sourcePos,
                             const Vec3f& listenerPos,
                             const Vec3f& listenerRight,
                             float attenuationRadius,
                             float spread,
                             float& outGainL,
                             float& outGainR);
};
