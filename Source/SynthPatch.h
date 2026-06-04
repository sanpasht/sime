#pragma once

#include <JuceHeader.h>

// Subtractive synth patch parameters (offline render + UI state).
struct SynthPatch
{
    enum class Waveform { Sine = 0, Square, Saw, Triangle };

    Waveform waveform     = Waveform::Saw;
    int      midiNote       = 60;      ///< Middle C
    double   durationSec    = 2.0;     ///< Held note length before release

    float attackSec         = 0.01f;
    float decaySec          = 0.15f;
    float sustainLevel      = 0.65f;   ///< 0..1
    float releaseSec        = 0.35f;

    float filterCutoffHz    = 4200.0f;
    float filterResonance   = 0.35f;   ///< 0..1 (maps to Q internally)
    float masterGain        = 0.75f;

    static float midiToHz(int midi) noexcept
    {
        return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
    }
};
