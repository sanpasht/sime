#include "SidebarComponent.h"
#include "BlockEntry.h"

SidebarComponent::SidebarComponent()
{
    addAndMakeVisible(toggleButton);
    addAndMakeVisible(xEditor);
    addAndMakeVisible(yEditor);
    addAndMakeVisible(zEditor);
    addAndMakeVisible(startEditor);
    addAndMakeVisible(durationEditor);
    addAndMakeVisible(movementEnabledToggle);
    addAndMakeVisible(applyButton);

    xEditor.setInputRestrictions(0, "-0123456789");
    yEditor.setInputRestrictions(0, "-0123456789");
    zEditor.setInputRestrictions(0, "-0123456789");

    startEditor.setInputRestrictions(0, "0123456789.");
    durationEditor.setInputRestrictions(0, "0123456789.");

    applyButton.onClick = [this]
    {
        if (!selectedBlock_ || !onApplyBlockInfo)
            return;

        const int serial = selectedBlock_->serial;

        Vec3i newPos {
            xEditor.getText().getIntValue(),
            yEditor.getText().getIntValue(),
            zEditor.getText().getIntValue()
        };

        double newStart = startEditor.getText().getDoubleValue();
        double newDuration = durationEditor.getText().getDoubleValue();

        bool movementEnabled = movementEnabledToggle.getToggleState();

        onApplyBlockInfo(serial, newPos, newStart, newDuration, movementEnabled);
    };
    toggleButton.onClick = [this]()
    {
        toggleButton.setButtonText(isCollapsed() ? "X" : "☰");
        setCollapsed(!isCollapsed());
    };
    addAndMakeVisible(blockListButton);
    blockListButton.onClick = [this]()
    {
        blockPanelOpen = true;
        infoPanelOpen = false;
        repaint();
        resized();
    };
    addAndMakeVisible(infoButton);
    infoButton.onClick = [this]()
    {
        blockPanelOpen = false;
        infoPanelOpen = true;
        repaint();
        resized();
    };
    blockListButton.setButtonText("");
    infoButton.setButtonText("");

    blockListButton.setLookAndFeel(&tabLookAndFeel_);
    infoButton.setLookAndFeel(&tabLookAndFeel_);

    blockListButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    infoButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setOpaque(true);
}

SidebarComponent::~SidebarComponent()
{
    blockListButton.setLookAndFeel(nullptr);
    infoButton.setLookAndFeel(nullptr);
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

void SidebarComponent::setBlocks(const std::vector<Block>& newBlocks)
{
    {
        juce::ScopedLock lock(blockListMutex);
        blockListUI = newBlocks;
    }

    repaint();
}

void SidebarComponent::resized()
{
    const int tabY = 6;
    const int tabH = 30;
    const int tabX = 10;
    const int closeW = 34;
    const int closeGap = 8;

    if (collapsed)
    {
        toggleButton.setBounds(10, 10, 30, 30);

        blockListButton.setBounds(0, 0, 0, 0);
        infoButton.setBounds(0, 0, 0, 0);

        xEditor.setBounds(0, 0, 0, 0);
        yEditor.setBounds(0, 0, 0, 0);
        zEditor.setBounds(0, 0, 0, 0);
        startEditor.setBounds(0, 0, 0, 0);
        durationEditor.setBounds(0, 0, 0, 0);
        movementEnabledToggle.setBounds(0, 0, 0, 0);
        applyButton.setBounds(0, 0, 0, 0);
        return;
    }

    toggleButton.setBounds(getWidth() - closeW - 8, tabY, closeW, tabH);

    const int tabAreaW = getWidth() - tabX - closeW - closeGap - 10;
    const int tabW = tabAreaW / 2;

    blockListButton.setBounds(tabX, tabY, tabW, tabH);
    infoButton.setBounds(tabX + tabW, tabY, tabW, tabH);

    // Hide all info controls by default
    xEditor.setBounds(0, 0, 0, 0);
    yEditor.setBounds(0, 0, 0, 0);
    zEditor.setBounds(0, 0, 0, 0);
    startEditor.setBounds(0, 0, 0, 0);
    durationEditor.setBounds(0, 0, 0, 0);
    movementEnabledToggle.setBounds(0, 0, 0, 0);
    applyButton.setBounds(0, 0, 0, 0);

    if (!isInfoPanelOpen() || !selectedBlock_)
        return;

    const int margin = 12;
    const int labelW = 82;
    const int editorH = 30;
    const int rowGap = 14;

    const int editorX = margin + labelW + 10;
    const int editorW = getWidth() - editorX - margin;

    int y = 86;

    xEditor.setBounds(editorX, y, editorW, editorH);
    y += editorH + rowGap;

    yEditor.setBounds(editorX, y, editorW, editorH);
    y += editorH + rowGap;

    zEditor.setBounds(editorX, y, editorW, editorH);
    y += editorH + 24;

    startEditor.setBounds(editorX, y, editorW, editorH);
    y += editorH + rowGap;

    durationEditor.setBounds(editorX, y, editorW, editorH);
    y += editorH + 24;

    movementEnabledToggle.setBounds(margin, y, getWidth() - 2 * margin, 28);
    y += 40;

    applyButton.setBounds(margin, getHeight() - 52, getWidth() - 2 * margin, 36);
}

void SidebarComponent::paint(juce::Graphics& g)
{
    const auto bg = juce::Colour(0xff0d1120);
    const auto border = juce::Colour(0xff4f6a96);
    const auto text = juce::Colour(0xffaac8e8);

    g.fillAll(bg);

    if (collapsed)
        return;

    auto drawTab = [&](juce::TextButton& btn, bool active, const juce::String& label)
    {
        auto b = btn.getBounds().toFloat();

        g.setColour(active ? bg : juce::Colour(0xff141b2e));
        g.fillRect(b);

        g.setColour(border);

        g.drawLine(b.getX(), b.getY(), b.getX(), b.getBottom(), 1.0f);
        g.drawLine(b.getX(), b.getY(), b.getRight(), b.getY(), 1.0f);
        g.drawLine(b.getRight(), b.getY(), b.getRight(), b.getBottom(), 1.0f);

        if (!active)
            g.drawLine(b.getX(), b.getBottom(), b.getRight(), b.getBottom(), 1.0f);

        g.setColour(active ? juce::Colours::white : juce::Colour(0xffb8c7e6));
        g.setFont(13.0f);
        g.drawText(label, btn.getBounds(), juce::Justification::centred);
    };

    auto drawDividerSkippingActiveTab = [&](juce::TextButton& activeBtn)
    {
        auto b = activeBtn.getBounds();
        int y = b.getBottom();

        g.setColour(border);
        g.drawLine(0.0f, (float)y, (float)b.getX(), (float)y, 1.0f);
        g.drawLine((float)b.getRight(), (float)y, (float)getWidth(), (float)y, 1.0f);
    };

    drawTab(blockListButton, isBlockPanelOpen(), "Blocks");
    drawTab(infoButton, isInfoPanelOpen(), "Info");

    drawDividerSkippingActiveTab(
        isBlockPanelOpen() ? blockListButton : infoButton
    );

    const int contentTopY = 58;

    if (isBlockPanelOpen())
    {
        std::vector<Block> snapshot;
        {
            juce::ScopedLock lock(blockListMutex);
            snapshot = blockListUI;
        }

        const int itemCount = (int)snapshot.size();

        g.setFont(juce::Font(16.0f).boldened());
        g.setColour(juce::Colour(0xff88aacc));
        g.drawText("Blocks (" + juce::String(itemCount) + ")",
                   12, contentTopY,
                   getWidth() - 24, 28,
                   juce::Justification::centredLeft);

        if (itemCount == 0)
        {
            g.setFont(13.0f);
            g.setColour(text.withAlpha(0.7f));
            g.drawText("No blocks yet",
                       12, contentTopY + 40,
                       getWidth() - 24, 24,
                       juce::Justification::centredLeft);
            return;
        }

        const int listY = contentTopY + 40;
        const int visibleContentH = getHeight() - listY;
        const int totalContentH = itemCount * kRowH + 6;
        const int maxScroll = std::max(0, totalContentH - visibleContentH);

        blockListScroll = std::clamp(blockListScroll, 0, maxScroll);

        g.saveState();
        g.reduceClipRegion(0, listY, getWidth(), visibleContentH);

        g.setFont(11.0f);

        for (int i = 0; i < itemCount; ++i)
        {
            int rowY = listY + 3 + i * kRowH - blockListScroll;

            if (rowY + kRowH < listY)
                continue;

            if (rowY > getHeight())
                break;

            if (i % 2 == 0)
            {
                g.setColour(juce::Colour(0x15ffffff));
                g.fillRect(1, rowY, getWidth() - 2, kRowH);
            }

            const auto& e = snapshot[i];

            juce::String row = "Block " + juce::String(e.serial)
                             + "  (" + juce::String(e.pos.x)
                             + ", "  + juce::String(e.pos.y)
                             + ", "  + juce::String(e.pos.z) + ")";

            g.setColour(text);
            g.drawText(row,
                       8, rowY + 3,
                       getWidth() - 16, kRowH - 6,
                       juce::Justification::centredLeft,
                       true);
        }

        g.restoreState();
        return;
    }

    if (isInfoPanelOpen())
    {
        const int margin = 12;
        const int labelW = 82;
        const int editorH = 30;
        const int rowGap = 14;

        if (!selectedBlock_)
        {
            g.setFont(15.0f);
            g.setColour(text);
            g.drawText("Select a block",
                       margin, contentTopY + 20,
                       getWidth() - 2 * margin, 30,
                       juce::Justification::centredLeft);
            return;
        }

        g.setFont(juce::Font(16.0f).boldened());
        g.setColour(juce::Colour(0xff88aacc));
        g.drawText("Block " + juce::String(selectedBlock_->serial),
                   margin, contentTopY,
                   getWidth() - 2 * margin, 26,
                   juce::Justification::centredLeft);

        int y = 86;

        g.setFont(13.0f);
        g.setColour(text);

        g.drawText("X:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Y:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Z:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + 24;

        g.drawText("Start:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Duration:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + 24;

        y += 40;

        juce::Rectangle<int> graphArea(
            margin,
            y,
            getWidth() - 2 * margin,
            145
        );

        movementGraph(g, *selectedBlock_, graphArea);
        return;
    }
}


void SidebarComponent::showBlockInfo(const BlockEntry& block)
{
    selectedBlock_ = block;
    originalBlock_ = block;

    blockPanelOpen = false;
    infoPanelOpen = true;

    xEditor.setText(juce::String(block.pos.x), juce::dontSendNotification);
    yEditor.setText(juce::String(block.pos.y), juce::dontSendNotification);
    zEditor.setText(juce::String(block.pos.z), juce::dontSendNotification);

    startEditor.setText(juce::String(block.startTimeSec, 2), juce::dontSendNotification);
    durationEditor.setText(juce::String(block.durationSec, 2), juce::dontSendNotification);

    movementEnabledToggle.setToggleState(
        block.hasRecordedMovement,
        juce::dontSendNotification
    );

    resized();
    repaint();
}

void SidebarComponent::movementGraph(juce::Graphics& g,
                                     const BlockEntry& block,
                                     juce::Rectangle<int> graphArea)
{
    g.setColour(juce::Colour(0xff151a2e));
    g.fillRoundedRectangle(graphArea.toFloat(), 6.0f);

    g.setColour(juce::Colour(0xff445577));
    g.drawRoundedRectangle(graphArea.toFloat(), 6.0f, 1.0f);

    if (block.recordedMovement.size() < 2)
    {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(12.0f);
        g.drawText("No recorded movement",
                   graphArea,
                   juce::Justification::centred);
        return;
    }

    auto area = graphArea.reduced(10);

    const auto& keys = block.recordedMovement;

    int minX = keys[0].position.x;
    int maxX = minX;
    int minZ = keys[0].position.z;
    int maxZ = minZ;

    for (const auto& kf : keys)
    {
        minX = std::min(minX, kf.position.x);
        maxX = std::max(maxX, kf.position.x);
        minZ = std::min(minZ, kf.position.z);
        maxZ = std::max(maxZ, kf.position.z);
    }

    int rangeX = std::max(1, maxX - minX + 2);
    int rangeZ = std::max(1, maxZ - minZ + 2);

    float scaleX = area.getWidth() / (float)rangeX;
    float scaleZ = area.getHeight() / (float)rangeZ;
    float scale = std::min(scaleX, scaleZ);

    auto toScreen = [&](const Vec3i& pos) -> juce::Point<float>
    {
        float x = area.getX() + (pos.x - minX + 0.5f) * scale;
        float y = area.getBottom() - (pos.z - minZ + 0.5f) * scale;
        return { x, y };
    };

    // Grid lines
    g.setColour(juce::Colour(0xff2c344f));
    for (int x = minX; x <= maxX + 1; ++x)
    {
        auto p1 = toScreen({ x, 0, minZ });
        auto p2 = toScreen({ x, 0, maxZ + 1 });
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.5f);
    }

    for (int z = minZ; z <= maxZ + 1; ++z)
    {
        auto p1 = toScreen({ minX, 0, z });
        auto p2 = toScreen({ maxX + 1, 0, z });
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 0.5f);
    }

    // Movement path
    juce::Path path;
    auto firstPoint = toScreen(keys[0].position);
    path.startNewSubPath(firstPoint);

    for (size_t i = 1; i < keys.size(); ++i)
    {
        auto point = toScreen(keys[i].position);
        path.lineTo(point);
    }

    g.setColour(juce::Colours::cyan.withAlpha(0.75f));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    // Keyframe dots
    for (size_t i = 0; i < keys.size(); ++i)
    {
        auto point = toScreen(keys[i].position);

        float t = (float)i / (float)(keys.size() - 1);
        auto color = juce::Colour::fromHSV(
            0.33f * (1.0f - t),
            0.8f,
            0.9f,
            1.0f
        );

        g.setColour(color);
        g.fillEllipse(point.x - 3.5f, point.y - 3.5f, 7.0f, 7.0f);

        if (keys[i].position.y > 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(9.0f);
            g.drawText(juce::String(keys[i].position.y),
                       (int)point.x - 8,
                       (int)point.y - 16,
                       16,
                       10,
                       juce::Justification::centred);
        }
    }

    // Start / End labels
    auto startPoint = toScreen(keys.front().position);
    auto endPoint = toScreen(keys.back().position);

    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.setFont(9.0f);

    g.drawText("START",
               (int)startPoint.x - 20,
               (int)startPoint.y + 6,
               40,
               12,
               juce::Justification::centred);

    g.drawText("END",
               (int)endPoint.x - 20,
               (int)endPoint.y + 6,
               40,
               12,
               juce::Justification::centred);

    // Small coordinate range
    g.setColour(juce::Colours::grey);
    g.setFont(9.0f);

    g.drawText("X " + juce::String(minX) + "→" + juce::String(maxX)
             + "   Z " + juce::String(minZ) + "→" + juce::String(maxZ),
               graphArea.reduced(6).removeFromBottom(12),
               juce::Justification::centredLeft);
}
