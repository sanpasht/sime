// ─────────────────────────────────────────────────────────────────────────────
// TransportBarComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "TransportBarComponent.h"
TransportBarComponent::TransportBarComponent()
{
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(speedButton_);
    addAndMakeVisible(timeInput_);
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

    // ── Speed button (popup menu: 0.25x / 0.5x / 0.75x / 1x / 2x / 3x) ───────
    speedButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff333344));
    speedButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    speedButton_.setTooltip("Playback speed (change before or during playback)");
    speedButton_.setButtonText(formatSpeedLabel(kSpeedRates[speedIndex_]));

    speedButton_.onClick = [this] { showSpeedMenu(); };

    // ── Time input ────────────────────────────────────────────────────────────
    // Doubles as a readout (when unfocused) and a "jump to" input (when focused).
    // Accepts plain seconds ("10"), M:SS ("1:23") or H:MM:SS.
    timeInput_.setFont(juce::Font(14.0f, juce::Font::bold));
    timeInput_.setColour(juce::TextEditor::backgroundColourId,      juce::Colour(0xff1a1c28));
    timeInput_.setColour(juce::TextEditor::outlineColourId,         juce::Colour(0xff353952));
    timeInput_.setColour(juce::TextEditor::focusedOutlineColourId,  juce::Colour(0xff3f6fff));
    timeInput_.setColour(juce::TextEditor::textColourId,            juce::Colours::white);
    timeInput_.setJustification(juce::Justification::centred);
    timeInput_.setTooltip("Click to type a time (e.g. 10 or 1:23) and press Enter to jump.");
    timeInput_.setSelectAllWhenFocused(true);

    timeInput_.onReturnKey  = [this] { commitTypedTime(); };
    timeInput_.onFocusLost  = [this] { commitTypedTime(); };
    timeInput_.onEscapeKey  = [this] { syncTimeDisplay(); };

    // ── Timeline callbacks ────────────────────────────────────────────────────
    timeline.onBlockEdited = [this](int serial, int timeIndex, double start, double duration)
    {
        if (onBlockEdited)
            onBlockEdited(serial, timeIndex, start, duration);
    };

    timeline.onPlayheadMoved = [this](double newTimeSec)
    {
        if (onPlayheadMoved)
            onPlayheadMoved(newTimeSec);
    };

    timeline.onRectRegionClicked = [this](int serial)
    {
        if (onTimelineBlockClicked)
            onTimelineBlockClicked(serial);
    };
    timeline.onRegionDuplicated = [this](int serial, double start, double duration)
    {
        if (onRegionDuplicated)
            onRegionDuplicated(serial, start, duration);
    };

    timeline.onRegionEdited = [this](int serial, int timeIndex,
                                    double start, double duration)
    {
        if (onRegionEdited)
            onRegionEdited(serial, timeIndex, start, duration);
    };
    timeline.onDeleteBlockOrRegion = [this](int serial, int timeIndex)
    {
        if (onDeleteBlockOrRegion)
            onDeleteBlockOrRegion(serial, timeIndex);
    };

     // ── Sidebar callbacks ─────────────────────────────────────────────────────

    // ── Collapse button ───────────────────────────────────────────────────────
    // ▲ = \xe2\x96\xb2  (U+25B2)   ▼ = \xe2\x96\xbc  (U+25BC)
    collapseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25283a));
    collapseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    collapseButton.setButtonText(isCollapsed_
        ? juce::CharPointer_UTF8("\xe2\x96\xb2")
        : juce::CharPointer_UTF8("\xe2\x96\xbc"));

    collapseButton.onClick = [this]
    {
        isCollapsed_ = !isCollapsed_;

        collapseButton.setButtonText(isCollapsed_
            ? juce::CharPointer_UTF8("\xe2\x96\xb2")
            : juce::CharPointer_UTF8("\xe2\x96\xbc"));
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

    syncTimeDisplay();
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
    // 70 px gives "0.75x" comfortable headroom; cycling-only versions were 55.
    speedButton_.setBounds(controlStrip.removeFromLeft(70).reduced(6, 5));

    timeInput_.setBounds(controlStrip.removeFromLeft(170).reduced(6, 5));

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

// ─────────────────────────────────────────────────────────────────────────────
// Typed-time input (jump-to)
// ─────────────────────────────────────────────────────────────────────────────

double TransportBarComponent::parseTypedTimeString(const juce::String& raw)
{
    auto s = raw.trim();
    if (s.isEmpty()) return -1.0;

    // Allow trailing "/ MM:SS" so the user can edit in-place over the readout.
    const int slashIdx = s.indexOfChar('/');
    if (slashIdx > 0)
        s = s.substring(0, slashIdx).trim();

    if (s.containsChar(':'))
    {
        auto parts = juce::StringArray::fromTokens(s, ":", "");
        if (parts.size() == 2)
            return parts[0].getDoubleValue() * 60.0 + parts[1].getDoubleValue();
        if (parts.size() >= 3)
            return parts[0].getDoubleValue() * 3600.0
                 + parts[1].getDoubleValue() * 60.0
                 + parts[2].getDoubleValue();
        return -1.0;
    }

    return s.getDoubleValue();
}

void TransportBarComponent::commitTypedTime()
{
    const double t = parseTypedTimeString(timeInput_.getText());

    if (t >= 0.0)
    {
        const double target = juce::jmax(0.0, t);
        if (onPlayheadMoved)
            onPlayheadMoved(target);
        currentTime_ = target;
    }

    // Force-refresh + release focus so the next timer ticks paint the readout.
    const int curMin = (int)(currentTime_ / 60.0);
    const int curSec = (int)std::fmod(currentTime_, 60.0);
    const int totMin = (int)(totalDuration_ / 60.0);
    const int totSec = (int)std::fmod(totalDuration_, 60.0);
    timeInput_.setText(juce::String::formatted("%d:%02d / %d:%02d",
                                               curMin, curSec, totMin, totSec),
                       juce::dontSendNotification);
    juce::Component::unfocusAllComponents();
}

void TransportBarComponent::syncTimeDisplay()
{
    if (timeInput_.hasKeyboardFocus(true))
        return;   // don't overwrite while the user is typing

    const int curMin = (int)(currentTime_ / 60.0);
    const int curSec = (int)std::fmod(currentTime_, 60.0);
    const int totMin = (int)(totalDuration_ / 60.0);
    const int totSec = (int)std::fmod(totalDuration_, 60.0);
    timeInput_.setText(juce::String::formatted("%d:%02d / %d:%02d",
                                               curMin, curSec, totMin, totSec),
                       juce::dontSendNotification);
}

// ─────────────────────────────────────────────────────────────────────────────

juce::String TransportBarComponent::formatSpeedLabel(double rate)
{
    // "0.25x", "0.5x", "0.75x", "1x", "2x", "3x" — drop trailing ".0" for whole
    // numbers so the button stays compact at small widths.
    if (std::abs(rate - std::round(rate)) < 0.001)
        return juce::String((int) std::round(rate)) + "x";

    return juce::String(rate, 2).trimCharactersAtEnd("0") + "x";
}

void TransportBarComponent::showSpeedMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < kSpeedCount; ++i)
    {
        const bool isCurrent = (i == speedIndex_);
        menu.addItem(juce::PopupMenu::Item(formatSpeedLabel(kSpeedRates[i]))
                        .setTicked(isCurrent)
                        .setID(i + 1));   // PopupMenu IDs must be > 0
    }

    juce::PopupMenu::Options opts;
    opts = opts.withTargetComponent(&speedButton_)
               .withMinimumWidth(speedButton_.getWidth());

    menu.showMenuAsync(opts, [this](int result)
    {
        if (result <= 0) return;   // dismissed

        const int idx = juce::jlimit(0, kSpeedCount - 1, result - 1);

        speedIndex_ = idx;
        const double rate = kSpeedRates[idx];

        speedButton_.setButtonText(formatSpeedLabel(rate));

        if (onSpeedChanged)
            onSpeedChanged(rate);
    });
}


void TransportBarComponent::setTimelinePlaying(bool playing)
{
    timeline.setPlaying(playing);
}

void TransportBarComponent::setCollapsible(bool canCollapse)
{
    collapseButton.setVisible(canCollapse);
    if (!canCollapse)
        isCollapsed_ = false;   // a non-collapsible bar always shows the timeline
    resized();
    if (onHeightChanged)
        onHeightChanged();
}