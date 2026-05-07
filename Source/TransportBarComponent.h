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
#include "TimelineComponent.h"
#include "MathUtils.h"
#include "BlockEntry.h"

class TransportBarComponent : public juce::Component,
                               private juce::Timer
{
public:
    // ── Callbacks wired by MainComponent ─────────────────────────────────────
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void(int, double, double)> onBlockEdited;

    // ── State pushed in from MainComponent each timer tick ────────────────────
    // Call this from a juce::Timer or after each transport update to keep
    // the display in sync.
    void setTransportState(bool playing, bool paused, double currentTimeSec, double totalDurationSec);                 
    void setBlocks(const std::vector<BlockEntry>& blocks);
    
    TransportBarComponent();

    void paint  (juce::Graphics&) override;
    void resized() override;
    void setTimelinePlaying(bool playing);

    int getCurrentHeight() const
    {
        return isCollapsed_ ? kCollapsedHeight : kExpandedHeight;
    }

    std::function<void(int serial)> onTimelineBlockClicked;

    

private:
    void timerCallback() override;       ///< Polls onPollState to refresh UI
    void updateButtonStates();

    // ── Buttons ───────────────────────────────────────────────────────────────
    juce::TextButton playPauseButton { "Play" };
    juce::TextButton stopButton      { "STOP" };
    juce::TextButton collapseButton { "⌄" };
    juce::Label timeLabel;
    TimelineComponent timeline;
    
    // ── Internal display state ────────────────────────────────────────────────
    static constexpr int kExpandedHeight  = 300;
    static constexpr int kCollapsedHeight = 40;
    static constexpr int kControlHeight   = 40;


    static constexpr int kHeight = kExpandedHeight;


    bool isCollapsed_ = false; 
    bool   isPlaying_    = false;
    bool   isPaused_     = false;
    double currentTime_  = 0.0;
    double totalDuration_= 0.0;   ///< Used to scale the progress bar; 0 = unknown

    static constexpr int kBtnW  =52;
    static constexpr int kBtnH  = 28;
    static constexpr int kPad   = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBarComponent)
};