#pragma once
#include <JuceHeader.h>
#include <vector>
#include "MathUtils.h"
#include <optional>
#include "BlockEntry.h"

class SidebarComponent : public juce::Component
{
public:
    struct Block
    {
        int serial;
        Vec3i pos;
    };

    SidebarComponent();
    ~SidebarComponent() override;

    void paint(juce::Graphics&) override;
    juce::TextButton toggleButton { "X" };
    juce::TextButton blockListButton { "Blocks" };
    juce::TextButton infoButton { "Info" };
    void resized() override;
    std::function<void(bool)> onCollapsedChanged;

    void setCollapsed(bool shouldCollapse);
    void setBlocks(const std::vector<Block>& newBlocks);
    bool isCollapsed() const { return collapsed; }
    bool isBlockPanelOpen() const { return blockPanelOpen; }
    bool isInfoPanelOpen() const { return infoPanelOpen; }

    void showBlockInfo(const BlockEntry& block);
    std::function<void(
        int serial,
        Vec3i newPos,
        double newStart,
        double newDuration,
        bool movementEnabled
    )> onApplyBlockInfo;
    
private:
    bool collapsed = false;
    bool blockPanelOpen = true;
    bool infoPanelOpen = false;

    std::vector<Block> blockListUI;
    juce::CriticalSection blockListMutex;

    bool blockListOpen = true;
    int blockListScroll = 0;

    static constexpr int kRowH = 20;
    static constexpr int kHeaderH = 26;
    static constexpr int kPanelTopY = 30;

    std::optional<BlockEntry> selectedBlock_;
    std::optional<BlockEntry> originalBlock_;

    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    juce::TextEditor zEditor;
    juce::TextEditor startEditor;
    juce::TextEditor durationEditor;

    juce::ToggleButton movementEnabledToggle { "Enable Recorded Movement" };
    juce::TextButton applyButton { "Apply" };
    void movementGraph(juce::Graphics& g, const BlockEntry& selectedBlock, juce::Rectangle<int> graphArea);

    class TabLookAndFeel : public juce::LookAndFeel_V4
    {
        public:
        void drawButtonBackground(juce::Graphics&,
            juce::Button&,
            const juce::Colour&,
            bool,
            bool) override
            {
                // Prevent JUCE from drawing default button background/border
            }
    };
        
    TabLookAndFeel tabLookAndFeel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)

};