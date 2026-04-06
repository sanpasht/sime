#pragma once
#include <JuceHeader.h>
#include <vector>
#include "MathUtils.h"

class SidebarComponent : public juce::Component
{
public:
    struct BlockEntry
    {
        int serial;
        Vec3i pos;
    };

    SidebarComponent();

    void paint(juce::Graphics&) override;
    juce::TextButton toggleButton { "X" };
    void resized() override;
    std::function<void(bool)> onCollapsedChanged;

    void setCollapsed(bool shouldCollapse);
    void setBlocks(const std::vector<BlockEntry>& newBlocks);
    bool isCollapsed() const { return collapsed; }

private:
    bool collapsed = false;

    std::vector<BlockEntry> blockListUI;
    juce::CriticalSection blockListMutex;

    bool blockListOpen = true;
    int blockListScroll = 0;

    static constexpr int kRowH = 20;
    static constexpr int kHeaderH = 26;
    static constexpr int kPanelTopY = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)
};