#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "TransportBarComponent.h"
#include "BlockType.h"
#include "MovementConfirmPopup.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ViewPortComponent view;
    SidebarComponent  sidebar;
    BlockEditPopup    editPopup;
    TransportBarComponent transportBar;
    std::unique_ptr<MovementConfirmPopup> movementPopup;
    bool isSidebarCollapsed = false;

    // ── Block type toolbar ────────────────────────────────────────────────────
    juce::TextButton violinBtn   { "Violin"   };
    juce::TextButton pianoBtn    { "Piano"    };
    juce::TextButton drumBtn     { "Drum"     };
    juce::TextButton customBtn   { "Custom"   };
    juce::TextButton listenerBtn { "Listener" };
    BlockType activeType_ = BlockType::Violin;
    void setActiveBlockType(BlockType t);
    void refreshToolbarColors();

    static constexpr int kToolbarH = 34;
    void showMovementConfirmPopup(int serial, double duration, 
                              const std::vector<MovementKeyFrame>& keyframes,
                              juce::Point<int> position);
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
