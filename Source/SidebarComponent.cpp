#include "SidebarComponent.h"

SidebarComponent::SidebarComponent()
{
    addAndMakeVisible(toggleButton);
    toggleButton.onClick = [this]()
    {
        toggleButton.setButtonText(isCollapsed() ? "X" : "☰");
        setCollapsed(!isCollapsed());
    };
    setOpaque(true);
}

void SidebarComponent::setCollapsed(bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;

    collapsed = shouldCollapse;

    if (onCollapsedChanged)
        onCollapsedChanged(collapsed);

    resized();
    repaint();
}

void SidebarComponent::setBlocks(const std::vector<BlockEntry>& newBlocks)
{
    {
        juce::ScopedLock lock(blockListMutex);
        blockListUI = newBlocks;
    }

    repaint();
}

void SidebarComponent::resized()
{
    if (collapsed)
        toggleButton.setBounds(10, 10, 30, 30);
    else
        toggleButton.setBounds(getWidth() - 40, 10, 30, 30);
}

void SidebarComponent::paint(juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d1120));
    if (collapsed)
    {
        return;
    }else{
        std::vector<BlockEntry> snapshot;
        {
            juce::ScopedLock lock(blockListMutex);
            snapshot = blockListUI;
        }

        const int itemCount = (int) snapshot.size();
        const int panelY = kPanelTopY;
        const int panelW = getWidth();
        const int panelH = getHeight();

        // g.setColour(juce::Colours::cyan.withAlpha(1.0f));
        // g.drawRect(getLocalBounds(), 1);

        g.setFont(juce::Font(16.5f).boldened());
        g.setColour(juce::Colour(0xff88aacc));

        juce::String header = "Blocks (" + juce::String(itemCount) + ")";

        g.drawText(header, 8, panelY + 5, panelW - 16, kHeaderH - 10,
                juce::Justification::centredLeft, true);

        if (!blockListOpen || itemCount == 0)
            return;

        const int visibleContentH = panelH - (panelY + kHeaderH);
        if (visibleContentH <= 0)
            return;

        const int totalContentH = itemCount * kRowH + 6;
        const int maxScroll = std::max(0, totalContentH - visibleContentH);
        blockListScroll = std::clamp(blockListScroll, 0, maxScroll);

        g.saveState();
        g.reduceClipRegion(0, panelY + kHeaderH, panelW, visibleContentH);

        g.setFont(juce::Font(11.f));

        for (int i = 0; i < itemCount; ++i)
        {
            int rowY = panelY + kHeaderH + 3 + i * kRowH - blockListScroll;

            if (rowY + kRowH < panelY + kHeaderH) continue;
            if (rowY > panelH) break;

            if (i % 2 == 0)
            {
                g.setColour(juce::Colour(0x15ffffff));
                g.fillRect(1, rowY, panelW - 2, kRowH);
            }

            const auto& e = snapshot[i];
            juce::String row = "Block " + juce::String(e.serial)
                            + "  (" + juce::String(e.pos.x)
                            + ", "  + juce::String(e.pos.y)
                            + ", "  + juce::String(e.pos.z) + ")";

            g.setColour(juce::Colour(0xffaac8e8));
            g.drawText(row, 8, rowY + 3, panelW - 16, kRowH - 6,
                    juce::Justification::centredLeft, true);
        }

        g.restoreState();
    }
}