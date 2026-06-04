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
    std::function<void(int, int, double, double)> onBlockEdited;

    // ── State pushed in from MainComponent each timer tick ────────────────────
    // Call this from a juce::Timer or after each transport update to keep
    // the display in sync.
    void setTransportState(bool playing, bool paused, double currentTimeSec, double totalDurationSec);                 
    void setBlocks(const std::vector<BlockEntry>& blocks);
    
    TransportBarComponent();

    void paint  (juce::Graphics&) override;
    void resized() override;
    void setTimelinePlaying(bool playing);

    /// When false, the collapse button is hidden and the bar is forced to its
    /// expanded (full timeline) state.  Used by the dedicated Timeline tab,
    /// which is always a full-screen timeline.
    void setCollapsible(bool canCollapse);

    int getPreferredHeight() const
    {
        return isCollapsed_ ? kControlHeight : kExpandedHeight;
    };

    std::function<void()> onHeightChanged;  

    std::function<void(int serial)> onTimelineBlockClicked;
    std::function<void(double)> onBpmChanged;
    std::function<void(double newTimeSec)> onPlayheadMoved;
    std::function<void(double rate)> onSpeedChanged;

    std::function<void(int serial, double start, double duration)> onRegionDuplicated;

    std::function<void(int serial, int timeIndex,
                    double start, double duration)> onRegionEdited;

    std::function<void(int serial, int timeIndex)> onDeleteBlockOrRegion;
    

private:
    void timerCallback() override;       ///< Polls onPollState to refresh UI
    void updateButtonStates();

    // ── Buttons ───────────────────────────────────────────────────────────────
    juce::TextButton playPauseButton;
    juce::TextButton stopButton;
    juce::TextButton speedButton_  { "1x" };
    juce::TextButton collapseButton { "⌄" };

    /// All speeds the user can select from the popup menu.  Order shown in
    /// the dropdown matches this array.  1.0 is the default.
    static constexpr int    kSpeedCount    = 6;
    static constexpr double kSpeedRates[kSpeedCount]
        = { 0.25, 0.5, 0.75, 1.0, 2.0, 3.0 };
    int                     speedIndex_    = 3;    ///< Index of 1.0x (default)

    /// Format a rate as the short label shown on the button ("0.25x", "1x", "2x").
    static juce::String formatSpeedLabel(double rate);

    /// Open the speed popup menu anchored under the button.
    void showSpeedMenu();
    juce::TextEditor   timeInput_;
    TimelineComponent timeline;
    static double parseTypedTimeString(const juce::String& s);
    void commitTypedTime();
    void syncTimeDisplay();
    juce::Rectangle<int> miniProgressBounds_;


    juce::Label bpmLabel_;
    juce::TextEditor bpmInput_;
    juce::TextButton tapTempoButton_ { "Tap" };

    double bpm_ = 120.0;

    std::vector<double> tapTimes_;
    
    // ── Internal display state ────────────────────────────────────────────────
    static constexpr int kExpandedHeight  = 300;
    static constexpr int kControlHeight   = 40;

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