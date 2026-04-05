// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(view);
    addAndMakeVisible(sidebar);

    // ── Wire sidebar collapse ─────────────────────────────────────────────────
    view.setSidebarComponent(&sidebar);
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed)
    {
        isSidebarCollapsed = isNowCollapsed;
        resized();
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
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    auto area = getLocalBounds();
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));
    view.setBounds(area);
}
 