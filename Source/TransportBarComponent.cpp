// ─────────────────────────────────────────────────────────────────────────────
// TransportBarComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "TransportBarComponent.h"

TransportBarComponent::TransportBarComponent()
{
    // ── Play/Pause button ─────────────────────────────────────────────────────
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
    addAndMakeVisible(stopButton);

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

    // ── Background ────────────────────────────────────────────────────────────
    g.setColour(juce::Colour(0xff0e1018));
    g.fillRect(bounds);

    // Top separator line
    g.setColour(juce::Colour(0xff2a3060));
    g.fillRect(0, 0, bounds.getWidth(), 1);

    // ── Progress bar area ─────────────────────────────────────────────────────
    // Sits to the right of the buttons and time label
    const int leftReserve = kPad + kBtnW + 4 + kBtnW + kPad + 72 + kPad;
    const int barX  = leftReserve;
    const int barY  = (bounds.getHeight() - 8) / 2;
    const int barW  = bounds.getWidth() - barX - kPad;
    const int barH  = 8;

    if (barW > 0)
    {
        // Track
        g.setColour(juce::Colour(0xff1e2235));
        g.fillRoundedRectangle((float)barX, (float)barY,
                               (float)barW, (float)barH, 3.f);

        // Fill — scales by totalDuration_; if 0 (unknown) fill proportionally
        // to a rolling 10-second window so something is always visible
        double displayDuration = (totalDuration_ > 0.0) ? totalDuration_
                                                         : std::max(currentTime_ + 1.0, 10.0);
        float  fillFraction    = (displayDuration > 0.0)
                                   ? (float)std::min(currentTime_ / displayDuration, 1.0)
                                   : 0.f;
        float  fillW           = fillFraction * (float)barW;

        if (fillW > 0.f)
        {
            // Gradient: blue → cyan
            juce::ColourGradient grad(juce::Colour(0xff2255cc), (float)barX, 0.f,
                                      juce::Colour(0xff00ccff), (float)(barX + barW), 0.f,
                                      false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle((float)barX, (float)barY, fillW, (float)barH, 3.f);
        }

        // Playhead tick
        if (fillW > 0.f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRect((int)(barX + fillW) - 1, barY - 2, 2, barH + 4);
        }

        // Track border
        g.setColour(juce::Colour(0xff2a3060));
        g.drawRoundedRectangle((float)barX, (float)barY,
                               (float)barW, (float)barH, 3.f, 1.f);
    }

    // ── Time display ──────────────────────────────────────────────────────────
    const int timeX = kPad + kBtnW + 4 + kBtnW + kPad;
    const int mins  = (int)(currentTime_ / 60.0);
    const int secs  = (int)(currentTime_) % 60;
    const int ms    = (int)((currentTime_ - std::floor(currentTime_)) * 100.0);

    juce::String timeStr = juce::String::formatted("%d:%02d.%02d", mins, secs, ms);

    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.f,
                         juce::Font::plain));
    g.setColour(juce::Colour(0xffaabbdd));
    g.drawText(timeStr,
               timeX, 0, 68, bounds.getHeight(),
               juce::Justification::centredLeft);
}

// ─────────────────────────────────────────────────────────────────────────────

void TransportBarComponent::resized()
{
    const int cy = (getHeight() - kBtnH) / 2;
    playPauseButton.setBounds(kPad,              cy, kBtnW, kBtnH);
    stopButton     .setBounds(kPad + kBtnW + 4,  cy, kBtnW, kBtnH);
}