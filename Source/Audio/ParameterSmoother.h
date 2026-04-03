#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ParameterSmoother.h  –  One-pole low-pass for click-free parameter changes.
//
// Embedded in Voice structs. Audio thread only.
// ─────────────────────────────────────────────────────────────────────────────

struct ParameterSmoother
{
    float current { 0.0f };
    float coeff   { 0.9995f };

    void reset(float value)
    {
        current = value;
    }

    float next(float target)
    {
        current += (target - current) * (1.0f - coeff);
        return current;
    }
};
