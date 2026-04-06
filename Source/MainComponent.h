#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "TransportBarComponent.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();

    void resized() override;

private:
    ViewPortComponent view;
    SidebarComponent  sidebar;
    BlockEditPopup    editPopup;
    TransportBarComponent transportBar;
    bool isSidebarCollapsed = false;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};