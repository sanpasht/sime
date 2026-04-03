// ─────────────────────────────────────────────────────────────────────────────
// Spatializer.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Spatializer.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void Spatializer::computeGains(const Vec3f& sourcePos,
                                const Vec3f& listenerPos,
                                const Vec3f& listenerRight,
                                float attenuationRadius,
                                float spread,
                                float& outGainL,
                                float& outGainR)
{
    Vec3f toSource = sourcePos - listenerPos;
    float dist = toSource.length();

    // Distance attenuation: linear falloff from 1.0 at minDist to 0.0 at attenuationRadius
    float clampedDist = std::max(dist, AudioConfig::kAttenuationMinDist);
    float attenGain = std::clamp(
        1.0f - (clampedDist / std::max(attenuationRadius, 0.01f)),
        0.0f, 1.0f);

    // Panning: equal-power pan law
    float panValue = 0.0f;
    if (dist > 0.001f)
    {
        Vec3f dir = toSource * (1.0f / dist);
        panValue = std::clamp(dir.dot(listenerRight), -1.0f, 1.0f);
    }

    float panAngle = (panValue + 1.0f) * kPi * 0.25f;
    float pannedL = std::cos(panAngle);
    float pannedR = std::sin(panAngle);

    // Spread: blend between panned (0) and centered/mono (1)
    float centerGain = 0.7071f; // 1/sqrt(2)
    float s = std::clamp(spread, 0.0f, 1.0f);
    outGainL = (pannedL * (1.0f - s) + centerGain * s) * attenGain;
    outGainR = (pannedR * (1.0f - s) + centerGain * s) * attenGain;
}
