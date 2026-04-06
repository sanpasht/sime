#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TransportBarComponent.h
//
// A thin horizontal bar containing Play/Pause, Stop, a time readout, and a
// progress bar that fills from left to right based on current transport time.
//
// Designed to sit at the bottom of the 3D viewport area in MainComponent.
// It does NOT own or know about TransportClock directly — it communicates
// purely through callbacks and a periodic timer update so it stays decoupled.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>

class TransportBarComponent : public juce::Component,
                               private juce::Timer
{
public:
    // ── Callbacks wired by MainComponent ─────────────────────────────────────
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;

    // ── State pushed in from MainComponent each timer tick ────────────────────
    // Call this from a juce::Timer or after each transport update to keep
    // the display in sync.
    void setTransportState(bool playing, bool paused,
                           double currentTimeSec, double totalDurationSec);

    TransportBarComponent();

    void paint  (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeight = 44;   ///< Reserve this many pixels in MainComponent

private:
    void timerCallback() override;       ///< Polls onPollState to refresh UI
    void updateButtonStates();

    // ── Buttons ───────────────────────────────────────────────────────────────
    juce::TextButton playPauseButton { "Play" };
    juce::TextButton stopButton      { "STOP" };

    // ── Internal display state ────────────────────────────────────────────────
    bool   isPlaying_    = false;
    bool   isPaused_     = false;
    double currentTime_  = 0.0;
    double totalDuration_= 0.0;   ///< Used to scale the progress bar; 0 = unknown

    static constexpr int kBtnW  =52;
    static constexpr int kBtnH  = 28;
    static constexpr int kPad   = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBarComponent)
};