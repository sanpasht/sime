// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(view);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(transportBar);

    // ── Wire sidebar collapse ─────────────────────────────────────────────────
    view.setSidebarComponent(&sidebar);
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed)
    {
        isSidebarCollapsed = isNowCollapsed;
        resized();
    };

    // ── Transport bar ─────────────────────────────────────────────────────────
    transportBar.onPlay  = [this] { view.transportPlay(); };
    transportBar.onPause = [this] { view.transportPause(); };
    transportBar.onStop  = [this]
    {
        view.transportStop();
        // Snap display to zero immediately on stop
        transportBar.setTransportState(false, false, 0.0,
                                       view.getTransportDuration());
    };

    // ── Wire edit popup ───────────────────────────────────────────────────────
    view.onRequestBlockEdit = [this](int serial, double start, double dur,
                                     int soundId, juce::Point<int> posInView)
    {
        juce::Point<int> screenPos = view.localPointToGlobal(posInView);
        editPopup.showAt(serial, start, dur, soundId, screenPos);
    };

    // Apply edits back into ViewPortComponent's blockList
    editPopup.onCommit = [this](int serial, double start, double dur, int sid)
    {
        view.applyBlockEdit(serial, start, dur, sid);
    };

    editPopup.onCancel = [this]()
    {
        view.clearSelectedBlock();
    };
    // ── Poll transport state at 30 Hz → keeps bar display live ───────────────
    startTimerHz(30);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    const double currentTime = view.getTransportTime();
    const double duration    = view.getTransportDuration();

    // Auto-stop when transport reaches the end of all blocks
    if (view.isTransportPlaying() && duration > 0.0 && currentTime >= duration)
    {
        view.transportStop();
        transportBar.setTransportState(false, false, 0.0, duration);
        return;
    }

    transportBar.setTransportState(
        view.isTransportPlaying(),
        view.isTransportPaused(),
        currentTime,
        duration);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    auto area = getLocalBounds();
 
    // Sidebar — full height, left side
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));
 
    // Transport bar — bottom of viewport area only (not behind sidebar)
    transportBar.setBounds(area.removeFromBottom(TransportBarComponent::kHeight));
 
    // 3D viewport — whatever remains
    view.setBounds(area);
}
 