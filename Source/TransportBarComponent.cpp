// ─────────────────────────────────────────────────────────────────────────────
// TransportBarComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "TransportBarComponent.h"
TransportBarComponent::TransportBarComponent()
{
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(timeLabel);
    addAndMakeVisible(timeline);
    addAndMakeVisible(collapseButton);

    addAndMakeVisible(bpmLabel_);
    addAndMakeVisible(bpmInput_);
    addAndMakeVisible(tapTempoButton_);

    // ── Play/Pause button ─────────────────────────────────────────────────────
    playPauseButton.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6"));  // ▶
    playPauseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a5298));
    playPauseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    playPauseButton.onClick = [this]
    {
        if (isPlaying_)
        {
            if (onPause)
                onPause();
        }
        else
        {
            if (onPlay)
                onPlay();
        }
    };

    // ── Stop button ───────────────────────────────────────────────────────────
    stopButton.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xa0"));  // ■
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333344));
    stopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    stopButton.onClick = [this]
    {
        if (onStop)
            onStop();
    };

    // ── Time label ────────────────────────────────────────────────────────────
    timeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    timeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    timeLabel.setJustificationType(juce::Justification::centred);

    // ── Timeline callbacks ────────────────────────────────────────────────────
    timeline.onBlockEdited = [this](int serial, double start, double duration)
    {
        if (onBlockEdited)
            onBlockEdited(serial, start, duration);
    };

    timeline.onRectRegionClicked = [this](int serial)
    {
        if (onTimelineBlockClicked)
            onTimelineBlockClicked(serial);
    };

    // ── Collapse button ───────────────────────────────────────────────────────
    collapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25283a));
    collapseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    collapseButton.setButtonText(isCollapsed_ ? "^" : "v");

    collapseButton.onClick = [this]
    {
        isCollapsed_ = !isCollapsed_;

        collapseButton.setButtonText(isCollapsed_ ? "^" : "v");
        timeline.setVisible(!isCollapsed_);

        if (onHeightChanged)
            onHeightChanged();

        resized();
        repaint();
    };

    // ── BPM input ─────────────────────────────────────────────────────────────
    bpmLabel_.setText("BPM", juce::dontSendNotification);
    bpmLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    bpmLabel_.setJustificationType(juce::Justification::centred);

    bpmInput_.setText(juce::String(bpm_, 1), juce::dontSendNotification);
    bpmInput_.setInputRestrictions(5, "0123456789.");
    bpmInput_.setJustification(juce::Justification::centred);

    auto applyBpm = [this](double newBpm)
    {
        bpm_ = juce::jlimit(40.0, 240.0, newBpm);

        bpmInput_.setText(juce::String(bpm_, 1), juce::dontSendNotification);

        timeline.setBpm(bpm_);
        tapTimes_.clear();
    };

    bpmInput_.onReturnKey = [this, applyBpm]
    {
        applyBpm(bpmInput_.getText().getDoubleValue());
    };

    bpmInput_.onFocusLost = bpmInput_.onReturnKey;

    // ── Tap tempo button ──────────────────────────────────────────────────────
    tapTempoButton_.setButtonText("Tap");

    tapTempoButton_.onClick = [this, applyBpm]
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        if (!tapTimes_.empty())
        {
            const double gap = now - tapTimes_.back();

            if (gap > 2.0)
                tapTimes_.clear();
        }

        tapTimes_.push_back(now);

        if (tapTimes_.size() > 6)
            tapTimes_.erase(tapTimes_.begin());

        if (tapTimes_.size() < 2)
            return;

        double totalGap = 0.0;

        for (size_t i = 1; i < tapTimes_.size(); ++i)
            totalGap += tapTimes_[i] - tapTimes_[i - 1];

        const double averageGap =
            totalGap / static_cast<double>(tapTimes_.size() - 1);

        if (averageGap <= 0.0)
            return;

        const double calculatedBpm = 60.0 / averageGap;

        bpm_ = juce::jlimit(40.0, 240.0, calculatedBpm);
        bpmInput_.setText(juce::String(bpm_, 1), juce::dontSendNotification);

        timeline.setBpm(bpm_);
    };

    // Initial sync
    timeline.setBpm(bpm_);

    // Poll at 30 Hz so the time display and progress bar feel live
    startTimerHz(30);
}

// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::setTransportState(bool playing, bool paused,
                                               double currentTimeSec,
                                               double totalDurationSec)
{
    isPlaying_     = playing;
    isPaused_      = paused;
    currentTime_   = currentTimeSec;
    totalDuration_ = totalDurationSec;

    int curMin = (int)(currentTime_ / 60.0);
    int curSec = (int)std::fmod(currentTime_, 60.0);
    int totMin = (int)(totalDuration_ / 60.0);
    int totSec = (int)std::fmod(totalDuration_, 60.0);
    juce::String timeText = juce::String::formatted("%d:%02d / %d:%02d", 
                                                    curMin, curSec, totMin, totSec);
    timeLabel.setText(timeText, juce::dontSendNotification);
    timeline.setCurrentTime(currentTime_);




    updateButtonStates();
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::timerCallback()
{
    // The actual state is pushed in from MainComponent via setTransportState().
    // Timer just triggers a repaint so the time display ticks visually.
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::updateButtonStates()
{
    if (isPlaying_)
    {
        playPauseButton.setButtonText(juce::CharPointer_UTF8("\xe2\x8f\xb8"));  // ⏸
        playPauseButton.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(0xff225588));
    }
    else
    {
        playPauseButton.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb6"));  // ▶
        playPauseButton.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(0xff2a5298));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour(juce::Colour(0xff0e1018));
    g.fillRect(bounds);

    g.setColour(juce::Colour(0xff2a3060));
    g.fillRect(0, 0, bounds.getWidth(), 1);

    if (isCollapsed_ && !miniProgressBounds_.isEmpty())
    {
        float progress = 0.0f;

        if (totalDuration_ > 0.0)
            progress = (float)(currentTime_ / totalDuration_);

        progress = juce::jlimit(0.0f, 1.0f, progress);

        auto bar = miniProgressBounds_.toFloat();

        // background track
        g.setColour(juce::Colour(0xff202436));
        g.fillRoundedRectangle(bar, 4.0f);

        // filled progress
        auto filled = bar;
        filled.setWidth(bar.getWidth() * progress);

        g.setColour(juce::Colour(0xff3f6fff));
        g.fillRoundedRectangle(filled, 4.0f);

        // small playhead dot
        const float headX = bar.getX() + bar.getWidth() * progress;
        const float headY = bar.getCentreY();

        g.setColour(juce::Colours::white);
        g.fillEllipse(headX - 4.0f, headY - 4.0f, 8.0f, 8.0f);
    }

    if (!isCollapsed_)
    {
        g.setColour(juce::Colour(0xff202436));
        g.fillRect(0, kControlHeight - 1, bounds.getWidth(), 1);
    }
}
// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::resized()
{
    auto bounds = getLocalBounds();
    auto controlStrip = bounds.removeFromTop(kControlHeight);

    playPauseButton.setBounds(controlStrip.removeFromLeft(65).reduced(6, 5));
    stopButton.setBounds(controlStrip.removeFromLeft(65).reduced(6, 5));

    timeLabel.setBounds(controlStrip.removeFromLeft(170).reduced(6, 5));

    // Reserve collapse button area FIRST
    auto rightButtonArea = controlStrip.removeFromRight(45);
    collapseButton.setBounds(rightButtonArea.reduced(6, 5));
    // collapseButton.setBounds(controlStrip.reduced(5));

    tapTempoButton_.setBounds(controlStrip.removeFromRight(55).reduced(4));
    auto bpmBounds = controlStrip.removeFromRight(60);
    bpmInput_.setBounds(bpmBounds.reduced(8, 10));
    bpmLabel_.setBounds(controlStrip.removeFromRight(40).reduced(4));

    if (isCollapsed_)
    {
        // Now this is only the safe middle space
        miniProgressBounds_ = controlStrip.reduced(12, 15);
    }
    else
    {
        miniProgressBounds_ = {};
    }

    if (isCollapsed_)
    {
        timeline.setVisible(false);
        timeline.setBounds(0, 0, 0, 0);
    }
    else
    {
        timeline.setVisible(true);
        timeline.setBounds(bounds);
    }
}

void TransportBarComponent::setBlocks(const std::vector<BlockEntry>& blocks)
{
    timeline.setBlocks(blocks);
}


void TransportBarComponent::setTimelinePlaying(bool playing)
{
    timeline.setPlaying(playing);
}