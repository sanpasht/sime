// ─────────────────────────────────────────────────────────────────────────────
// TransportBarComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "TransportBarComponent.h"

TransportBarComponent::TransportBarComponent()
{
    // ── Play/Pause button ─────────────────────────────────────────────────────
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(timeLabel);
    addAndMakeVisible(timeline);
    addAndMakeVisible(collapseButton);

    collapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25283a));
    collapseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    collapseButton.setButtonText(isCollapsed_ ? "^" : "v");

    playPauseButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5298));
    playPauseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    playPauseButton.onClick = [this]
    {
        if (isPlaying_)
        {
            if (onPause) onPause();
        }
        else
        {
            if (onPlay) onPlay();
        }
    };
    addAndMakeVisible(playPauseButton);

    // ── Stop button ───────────────────────────────────────────────────────────
    stopButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff333344));
    stopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopButton.onClick = [this] { if (onStop) onStop(); };

    timeLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    timeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    timeLabel.setJustificationType(juce::Justification::centred);

    timeline.onBlockEdited = [this](int serial, double start, double duration)
    {
        if (onBlockEdited)  // forward to MainComponent
            onBlockEdited(serial, start, duration);
    };

    timeline.onRectRegionClicked = [this](int serial)
    {
        if (onTimelineBlockClicked)
            onTimelineBlockClicked(serial);
    };

    collapseButton.onClick = [this]
    {
        isCollapsed_ = !isCollapsed_;

        collapseButton.setButtonText(isCollapsed_ ? "A" : "V");
        timeline.setVisible(!isCollapsed_);

        if (onHeightChanged)
            onHeightChanged();

        resized();
        repaint();
    };

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
        playPauseButton.setButtonText("Pause");
        playPauseButton.setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(0xff225588));
    }
    else
    {
        playPauseButton.setButtonText("Play");
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