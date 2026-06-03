#include "SidebarComponent.h"
#include "BlockEntry.h"
#include "MuteSchedulePopup.h"
#include "KeyframeEditorPopup.h"

SidebarComponent::SidebarComponent()
{
    addAndMakeVisible(toggleButton);
    addAndMakeVisible(xEditor);
    addAndMakeVisible(yEditor);
    addAndMakeVisible(zEditor);
    addAndMakeVisible(startEditor);
    addAndMakeVisible(durationEditor);
    addAndMakeVisible(movementEnabledToggle);
    addAndMakeVisible(modeCombo_);
    addAndMakeVisible(movementDurationEditor_);
    addAndMakeVisible(pathYOffsetEditor_);
    addAndMakeVisible(keyframesBtn_);
    addAndMakeVisible(loopToggle_);
    addAndMakeVisible(loopDurationEditor_);
    addAndMakeVisible(matchLoopDurBtn_);
    addAndMakeVisible(loopBufferEditor_);
    addAndMakeVisible(muteToggle_);
    addAndMakeVisible(hideToggle_);
    addAndMakeVisible(muteScheduleBtn_);
    addAndMakeVisible(matchSoundDurBtn_);
    addAndMakeVisible(distanceBtn_);
    addAndMakeVisible(applyButton);
    addAndMakeVisible(resetDefaultsBtn_);

    xEditor.setInputRestrictions(0, "-0123456789");
    yEditor.setInputRestrictions(0, "-0123456789");
    zEditor.setInputRestrictions(0, "-0123456789");

    startEditor.setInputRestrictions(0, "0123456789.");
    durationEditor.setInputRestrictions(0, "0123456789.");
    movementDurationEditor_.setInputRestrictions(0, "0123456789.");
    pathYOffsetEditor_.setInputRestrictions(0, "-0123456789");
    loopBufferEditor_.setInputRestrictions(0, "0123456789.");
    loopDurationEditor_.setInputRestrictions(0, "0123456789.");

    muteScheduleBtn_.setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff242a3c));
    muteScheduleBtn_.setColour(juce::TextButton::textColourOffId,
                               juce::Colour(0xffe2e6f2));
    muteScheduleBtn_.setTooltip(
        "Open the advanced mute scheduler to silence this block "
        "for one or more time windows.");
    muteScheduleBtn_.onClick = [this]
    {
        if (!selectedBlock_) return;

        if (!muteSchedulePopup_)
        {
            muteSchedulePopup_ = std::make_unique<MuteSchedulePopup>();
            muteSchedulePopup_->onApply =
                [this](int serial, std::vector<MuteWindow> windows)
            {
                if (!selectedBlock_ || selectedBlock_->serial != serial)
                    return;
                muteWindowsDraft_ = std::move(windows);
                // Pushing through Apply gives the user a single canonical
                // commit path; otherwise the draft would silently override
                // whatever's already on the block.
                if (applyButton.onClick)
                    applyButton.onClick();
            };
            muteSchedulePopup_->onDismiss = [] {};
        }

        const juce::String name = selectedDisplayName_.isNotEmpty()
            ? selectedDisplayName_
            : juce::String("Block ") + juce::String(selectedBlock_->serial);

        muteSchedulePopup_->setSchedule(selectedBlock_->serial,
                                        name,
                                        muteWindowsDraft_);

        const auto screenPos = muteScheduleBtn_.localPointToGlobal(
            juce::Point<int>(muteScheduleBtn_.getWidth(),
                             muteScheduleBtn_.getHeight() / 2));
        muteSchedulePopup_->showAt(screenPos);
    };

    matchSoundDurBtn_.onClick = [this]
    {
        if (selectedBlock_ && onMatchDurationToSound)
            onMatchDurationToSound(selectedBlock_->serial);
    };

    distanceBtn_.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0xff242a3c));
    distanceBtn_.setColour(juce::TextButton::textColourOffId,
                           juce::Colour(0xffe2e6f2));
    distanceBtn_.setTooltip(
        "Measure distance from this block (A) to another block (B). "
        "Click Distance, then click block B in the viewport.");
    distanceBtn_.onClick = [this]
    {
        if (!selectedBlock_ || !onRequestDistancePick)
            return;
        onRequestDistancePick(selectedBlock_->serial);
    };

    keyframesBtn_.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(0xff242a3c));
    keyframesBtn_.setColour(juce::TextButton::textColourOffId,
                            juce::Colour(0xffe2e6f2));
    keyframesBtn_.setTooltip(
        "Open the keyframe editor to add or fine-tune the block's "
        "position keyframes (alternative to Alt-drag recording).");
    keyframesBtn_.onClick = [this]
    {
        if (!selectedBlock_) return;

        if (!keyframeEditorPopup_)
        {
            keyframeEditorPopup_ = std::make_unique<KeyframeEditorPopup>();
            keyframeEditorPopup_->onApply =
                [this](int serial, std::vector<MovementKeyFrame> frames)
            {
                if (!selectedBlock_ || selectedBlock_->serial != serial)
                    return;
                keyframesDraft_ = frames;
                if (onApplyKeyframes)
                    onApplyKeyframes(serial, std::move(frames));
            };
            keyframeEditorPopup_->onDismiss = [] {};
        }

        const juce::String name = selectedDisplayName_.isNotEmpty()
            ? selectedDisplayName_
            : juce::String("Block ") + juce::String(selectedBlock_->serial);

        keyframeEditorPopup_->setKeyframes(selectedBlock_->serial,
                                           name,
                                           selectedBlock_->pos,
                                           keyframesDraft_);

        const auto screenPos = keyframesBtn_.localPointToGlobal(
            juce::Point<int>(keyframesBtn_.getWidth(),
                             keyframesBtn_.getHeight() / 2));
        keyframeEditorPopup_->showAt(screenPos);
    };

    // Flat, inset style so the control reads as part of the loop-length row
    // rather than a raised chip that sticks past the sidebar edge.
    matchLoopDurBtn_.setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff1e2436));
    matchLoopDurBtn_.setColour(juce::TextButton::buttonOnColourId,
                               juce::Colour(0xff28324a));
    matchLoopDurBtn_.setColour(juce::TextButton::textColourOffId,
                               juce::Colour(0xffb8c7e6));
    matchLoopDurBtn_.setConnectedEdges(juce::Button::ConnectedOnLeft);
    matchLoopDurBtn_.setTooltip(
        "Set the loop length to match the block's full region duration "
        "(quick way to say \"loop fills the whole region\").");
    matchLoopDurBtn_.onClick = [this]
    {
        // Mirror the current block-duration value into the loop-duration
        // field — quick way to say "loop fills the whole region".
        loopDurationEditor_.setText(durationEditor.getText(),
                                    juce::dontSendNotification);
    };

    modeCombo_.addItem("Natural",        1 + (int) BlockPlaybackMode::Natural);
    modeCombo_.addItem("Loop",           1 + (int) BlockPlaybackMode::Loop);
    modeCombo_.addItem("Stretch (slow)", 1 + (int) BlockPlaybackMode::Stretch);
    modeCombo_.addItem("Speed (fast)",   1 + (int) BlockPlaybackMode::Speed);
    modeCombo_.setSelectedId(1 + (int) BlockPlaybackMode::Natural,
                             juce::dontSendNotification);

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

        double newStart    = startEditor.getText().getDoubleValue();
        double newDuration = durationEditor.getText().getDoubleValue();
        bool   movementEn  = movementEnabledToggle.getToggleState();

        const int comboId = modeCombo_.getSelectedId();
        const uint8_t mode = comboId > 0
            ? (uint8_t)(comboId - 1)
            : (uint8_t) BlockPlaybackMode::Natural;

        const double movDur   = movementDurationEditor_.getText().getDoubleValue();
        const int    yOff     = pathYOffsetEditor_.getText().getIntValue();
        const double loopBuf  = loopBufferEditor_.getText().getDoubleValue();
        const bool   isMuted  = muteToggle_.getToggleState();
        const bool   isHidden = hideToggle_.getToggleState();
        const bool   isLoop   = loopToggle_.getToggleState();
        const double loopDur  = loopDurationEditor_.getText().getDoubleValue();

        // The Loop toggle is the canonical source of truth for the playback
        // mode now that the popup loop button is gone.  Force the combo to
        // agree so Apply always pushes a consistent (mode, isLooping) pair.
        uint8_t effectiveMode = mode;
        if (isLoop)
            effectiveMode = (uint8_t) BlockPlaybackMode::Loop;
        else if ((BlockPlaybackMode) mode == BlockPlaybackMode::Loop)
            effectiveMode = (uint8_t) BlockPlaybackMode::Natural;

        onApplyBlockInfo(serial, newPos, newStart, newDuration, movementEn,
                         effectiveMode, movDur, yOff,
                         isMuted, isHidden, loopBuf,
                         isLoop, loopDur, muteWindowsDraft_);
    };

    resetDefaultsBtn_.onClick = [this]
    {
        if (!selectedBlock_ || !onApplyBlockInfo)
            return;

        const int serial = selectedBlock_->serial;

        modeCombo_.setSelectedId(1 + (int) BlockPlaybackMode::Natural,
                                 juce::dontSendNotification);
        movementDurationEditor_.setText("0", juce::dontSendNotification);
        pathYOffsetEditor_.setText("0", juce::dontSendNotification);
        loopBufferEditor_.setText("0", juce::dontSendNotification);
        loopDurationEditor_.setText("0", juce::dontSendNotification);
        loopToggle_.setToggleState(false, juce::dontSendNotification);
        muteToggle_.setToggleState(false, juce::dontSendNotification);
        hideToggle_.setToggleState(false, juce::dontSendNotification);
        muteWindowsDraft_.clear();

        Vec3i pos {
            xEditor.getText().getIntValue(),
            yEditor.getText().getIntValue(),
            zEditor.getText().getIntValue()
        };
        const double start = startEditor.getText().getDoubleValue();
        const double dur   = durationEditor.getText().getDoubleValue();
        const bool   movEn = movementEnabledToggle.getToggleState();

        onApplyBlockInfo(serial, pos, start, dur, movEn,
                         (uint8_t) BlockPlaybackMode::Natural,
                         0.0, 0,
                         false, false, 0.0,
                         false, 0.0,
                         std::vector<MuteWindow>{});
    };

    toggleButton.onClick = [this]()
    {
        // ☰ = \xe2\x98\xb0  (U+2630 TRIGRAM FOR HEAVEN / hamburger)
        // ✕ = \xe2\x9c\x95  (U+2715 MULTIPLICATION X)
        //
        // The icon shows the action the click will take *next*, so:
        //   * sidebar open    → show ✕  ("click to close")
        //   * sidebar closed  → show ☰  ("click to open")
        // We have to flip the state first and *then* set the icon — using
        // the pre-flip state was the source of the swapped-icon bug.
        setCollapsed(!isCollapsed());
        toggleButton.setButtonText(isCollapsed()
            ? juce::CharPointer_UTF8("\xe2\x98\xb0")
            : juce::CharPointer_UTF8("\xe2\x9c\x95"));
    };
    addAndMakeVisible(AudioListButton);
    AudioListButton.onClick = [this]()
    {
        blockPanelOpen = true;
        infoPanelOpen = false;
        resetSpatialUi();
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
    AudioListButton.setButtonText("");
    infoButton.setButtonText("");

    AudioListButton.setLookAndFeel(&tabLookAndFeel_);
    infoButton.setLookAndFeel(&tabLookAndFeel_);

    AudioListButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    infoButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setOpaque(true);
}

SidebarComponent::~SidebarComponent()
{
    AudioListButton.setLookAndFeel(nullptr);
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

void SidebarComponent::setAudioList(const std::vector<AudioItem>& newAudioFiles)
{
    {
        juce::ScopedLock lock(AudioListMutex);
        AudioListUI = newAudioFiles;
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

        AudioListButton.setBounds(0, 0, 0, 0);
        infoButton.setBounds(0, 0, 0, 0);

        xEditor.setBounds(0, 0, 0, 0);
        yEditor.setBounds(0, 0, 0, 0);
        zEditor.setBounds(0, 0, 0, 0);
        startEditor.setBounds(0, 0, 0, 0);
        durationEditor.setBounds(0, 0, 0, 0);
        movementEnabledToggle.setBounds(0, 0, 0, 0);
        keyframesBtn_.setBounds(0, 0, 0, 0);
        modeCombo_.setBounds(0, 0, 0, 0);
        movementDurationEditor_.setBounds(0, 0, 0, 0);
        pathYOffsetEditor_.setBounds(0, 0, 0, 0);
        loopToggle_.setBounds(0, 0, 0, 0);
        loopDurationEditor_.setBounds(0, 0, 0, 0);
        matchLoopDurBtn_.setBounds(0, 0, 0, 0);
        loopBufferEditor_.setBounds(0, 0, 0, 0);
        muteToggle_.setBounds(0, 0, 0, 0);
        hideToggle_.setBounds(0, 0, 0, 0);
        muteScheduleBtn_.setBounds(0, 0, 0, 0);
        matchSoundDurBtn_.setBounds(0, 0, 0, 0);
        distanceBtn_.setBounds(0, 0, 0, 0);
        applyButton.setBounds(0, 0, 0, 0);
        resetDefaultsBtn_.setBounds(0, 0, 0, 0);
        return;
    }

    toggleButton.setBounds(getWidth() - closeW - 8, tabY, closeW, tabH);

    const int tabAreaW = getWidth() - tabX - closeW - closeGap - 10;
    const int tabW = tabAreaW / 2;

    AudioListButton.setBounds(tabX, tabY, tabW, tabH);
    infoButton.setBounds(tabX + tabW, tabY, tabW, tabH);

    // Hide all info controls by default
    xEditor.setBounds(0, 0, 0, 0);
    yEditor.setBounds(0, 0, 0, 0);
    zEditor.setBounds(0, 0, 0, 0);
    startEditor.setBounds(0, 0, 0, 0);
    durationEditor.setBounds(0, 0, 0, 0);
    movementEnabledToggle.setBounds(0, 0, 0, 0);
    keyframesBtn_.setBounds(0, 0, 0, 0);
    modeCombo_.setBounds(0, 0, 0, 0);
    movementDurationEditor_.setBounds(0, 0, 0, 0);
    pathYOffsetEditor_.setBounds(0, 0, 0, 0);
    loopToggle_.setBounds(0, 0, 0, 0);
    loopDurationEditor_.setBounds(0, 0, 0, 0);
    matchLoopDurBtn_.setBounds(0, 0, 0, 0);
    loopBufferEditor_.setBounds(0, 0, 0, 0);
    muteToggle_.setBounds(0, 0, 0, 0);
    hideToggle_.setBounds(0, 0, 0, 0);
    muteScheduleBtn_.setBounds(0, 0, 0, 0);
    matchSoundDurBtn_.setBounds(0, 0, 0, 0);
    distanceBtn_.setBounds(0, 0, 0, 0);
    applyButton.setBounds(0, 0, 0, 0);
    resetDefaultsBtn_.setBounds(0, 0, 0, 0);

    if (!isInfoPanelOpen() || !selectedBlock_)
        return;

    const int margin = 12;
    const int labelW = 82;
    const int editorH = 30;
    const int rowGap = 14;

    const int editorX = margin + labelW + 10;
    const int editorW = getWidth() - editorX - margin;

    // ── Fixed bottom strip for Apply / Reset ─────────────────────────────────
    const int btnH = 32;
    const int btnGap = 8;
    const int bottomY = getHeight() - margin - btnH;

    resetDefaultsBtn_.setBounds(margin, bottomY - btnH - btnGap,
                                getWidth() - 2 * margin, btnH);
    applyButton.setBounds(margin, bottomY, getWidth() - 2 * margin, btnH);

    // ── Scrollable content area ──────────────────────────────────────────────
    // Logical y starts at 86; paint() and setBounds() apply the scroll offset.
    infoScrollAreaTop_ = 86;
    infoScrollAreaBot_ = bottomY - btnH - btnGap - 6;   // gap above Reset btn

    auto placeRow = [&](juce::Component& c, int logicalY,
                        int x, int w, int h)
    {
        const int yScreen = logicalY - infoScrollY_;
        // Hide rows that have scrolled outside the visible content area, so
        // they don't paint over the tab header or the bottom button strip.
        if (yScreen + h <= infoScrollAreaTop_
            || yScreen >= infoScrollAreaBot_)
        {
            c.setBounds(0, 0, 0, 0);
        }
        else
        {
            c.setBounds(x, yScreen, w, h);
        }
    };

    // Two-column variant: places two components side-by-side on the same
    // logical row, with the same scroll-clip behaviour as placeRow.  Used
    // by the loop-length editor + "= Block Dur." button row, which used to
    // bypass placeRow and float over the header on scroll.
    auto placeRowPair = [&](juce::Component& a, juce::Component& b,
                            int logicalY,
                            int xA, int wA, int xB, int wB, int h)
    {
        const int yScreen = logicalY - infoScrollY_;
        const bool offscreen = (yScreen + h <= infoScrollAreaTop_
                                || yScreen >= infoScrollAreaBot_);
        if (offscreen)
        {
            a.setBounds(0, 0, 0, 0);
            b.setBounds(0, 0, 0, 0);
        }
        else
        {
            a.setBounds(xA, yScreen, wA, h);
            b.setBounds(xB, yScreen, wB, h);
        }
    };

    int y = infoScrollAreaTop_;

    // Vertical budget for a "MOVEMENT" / "LOOP" / "MUTE" section header:
    //   * `kSectionGapPre`  — breathing room above the label
    //   * label drawn in paint() at this offset (height 14)
    //   * `kSectionGapPost` — gap between the label and the first row below
    // paint() and resized() both add `kSectionBandH` exactly once per section
    // so labels stay aligned with their controls regardless of scroll.
    constexpr int kSectionGapPre  = 10;
    constexpr int kSectionLabelH  = 14;
    constexpr int kSectionGapPost = 6;
    constexpr int kSectionBandH   = kSectionGapPre + kSectionLabelH + kSectionGapPost;

    placeRow(xEditor, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(yEditor, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(zEditor, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(startEditor, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(durationEditor, y, editorX, editorW, editorH);

    // ── SPATIAL section ────────────────────────────────────────────────────
    y += editorH + kSectionBandH;

    placeRow(distanceBtn_, y, margin, getWidth() - 2 * margin, editorH);
    y += editorH + rowGap;

    y += 36;   // listener readout (paint)
    y += 44;   // A→B distance readout (paint)

    // ── MOVEMENT section ───────────────────────────────────────────────────
    y += kSectionBandH;

    placeRow(movementEnabledToggle, y, margin, getWidth() - 2 * margin, 28);
    y += 32;

    placeRow(keyframesBtn_, y, margin, getWidth() - 2 * margin, editorH);
    y += editorH + rowGap;

    placeRow(modeCombo_, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(movementDurationEditor_, y, editorX, editorW, editorH);
    y += editorH + rowGap;

    placeRow(pathYOffsetEditor_, y, editorX, editorW, editorH);

    // ── LOOP section ───────────────────────────────────────────────────────
    y += editorH + kSectionBandH;

    placeRow(loopToggle_, y, margin, getWidth() - 2 * margin, 26);
    y += 30;

    // Loop-length row: editor + "= Block" share the same column as every
    // other field.  The button is right-aligned inside that column so it
    // never hangs past the sidebar margin (the old std::max(40, …) guard
    // forced a 40-px minimum editor width and pushed a 72-px button 14 px
    // past the edge on the 220-px sidebar).
    constexpr int kLoopRowGap = 0;   // flush join — ConnectedOnLeft on the btn
    const int matchBtnW = 64;
    const int btnRight  = getWidth() - margin;
    const int btnX      = btnRight - matchBtnW;
    const int loopEditorW = juce::jmax(24, btnX - editorX - kLoopRowGap);
    placeRowPair(loopDurationEditor_, matchLoopDurBtn_, y,
                 editorX, loopEditorW,
                 btnX,    matchBtnW,
                 editorH);
    y += editorH + rowGap;

    placeRow(loopBufferEditor_, y, editorX, editorW, editorH);

    // ── MUTE section ───────────────────────────────────────────────────────
    y += editorH + kSectionBandH;

    placeRow(muteToggle_, y, margin, getWidth() - 2 * margin, 26);
    y += 30;

    placeRow(hideToggle_, y, margin, getWidth() - 2 * margin, 26);
    y += 30;

    // "Mute Schedule..." button takes the row that used to host the inline
    // from/to editors.  Advanced mute settings live in the popup now.
    placeRow(muteScheduleBtn_, y, margin, getWidth() - 2 * margin, editorH);
    y += editorH + 12;

    placeRow(matchSoundDurBtn_, y, margin, getWidth() - 2 * margin, 28);
    y += 36;

    // Sound header + stats line (paint-only)
    y += 18;                // "Sound" header
    y += 30;                // stats line
    y += 68 + 10;           // oscillation graph

    // Movement section header + graph (paint-only)
    y += 16;                // "Movement" label
    y += 108;               // movement graph

    infoContentBottomY_ = y;

    // Clamp scroll so we never scroll past the end.
    const int visibleH  = infoScrollAreaBot_ - infoScrollAreaTop_;
    const int totalH    = infoContentBottomY_ - infoScrollAreaTop_;
    const int maxScroll = std::max(0, totalH - visibleH);
    if (infoScrollY_ > maxScroll)
    {
        infoScrollY_ = maxScroll;
        // Re-place rows with the corrected offset on the next paint pass.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SidebarComponent>(this)]
        {
            if (safe != nullptr) safe->resized();
        });
    }
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
        g.setFont(juce::Font("Public Sans", 13.0f, juce::Font::plain));
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

    drawTab(AudioListButton, isBlockPanelOpen(), "Audio");
    drawTab(infoButton, isInfoPanelOpen(), "Info");

    drawDividerSkippingActiveTab(
        isBlockPanelOpen() ? AudioListButton : infoButton
    );

    const int contentTopY = 58;

    if (isBlockPanelOpen())
    {
        std::vector<AudioItem> snapshot;
        {
            juce::ScopedLock lock(AudioListMutex);
            snapshot = AudioListUI;
        }

        const int itemCount = (int)snapshot.size();

        g.setFont(juce::Font("Public Sans", 16.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff88aacc));
        g.drawText("Audio Files (" + juce::String(itemCount) + ")",
                12, contentTopY,
                getWidth() - 24, 28,
                juce::Justification::centredLeft);

        if (itemCount == 0)
        {
            g.setFont(juce::Font("Public Sans", 13.0f, juce::Font::plain));
            g.setColour(text.withAlpha(0.7f));
            g.drawText("No workspace audio files",
                    12, contentTopY + 40,
                    getWidth() - 24, 24,
                    juce::Justification::centredLeft);
            return;
        }

        const int listY = contentTopY + 40;
        const int visibleContentH = getHeight() - listY;
        const int totalContentH = itemCount * kRowH + 6;
        const int maxScroll = std::max(0, totalContentH - visibleContentH);

        AudioListScroll = std::clamp(AudioListScroll, 0, maxScroll);

        g.saveState();
        g.reduceClipRegion(0, listY, getWidth(), visibleContentH);

        g.setFont(juce::Font("Public Sans", 11.0f, juce::Font::plain));

        for (int i = 0; i < itemCount; ++i)
        {
            int rowY = listY + 3 + i * kRowH - AudioListScroll;

            if (rowY + kRowH < listY)
                continue;

            if (rowY > getHeight())
                break;

            if (i % 2 == 0)
            {
                g.setColour(juce::Colour(0x15ffffff));
                g.fillRect(1, rowY, getWidth() - 2, kRowH);
            }

            const auto& item = snapshot[i];

            g.setColour(text);
            g.drawText(item.fileName,
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
            g.setFont(juce::Font("Public Sans", 15.0f, juce::Font::plain));
            g.setColour(text);
            g.drawText("Select a block",
                       margin, contentTopY + 20,
                       getWidth() - 2 * margin, 30,
                       juce::Justification::centredLeft);
            return;
        }

        g.setFont(juce::Font("Public Sans", 16.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff88aacc));

        const juce::String headerName = selectedDisplayName_.isNotEmpty()
            ? selectedDisplayName_
            : juce::String("Block ") + juce::String(selectedBlock_->serial);

        g.drawText(headerName,
                   margin, contentTopY,
                   getWidth() - 2 * margin, 26,
                   juce::Justification::centredLeft);

        // ── Clip + scroll the rest of the info content ───────────────────────
        const int clipTop = infoScrollAreaTop_;
        const int clipBot = juce::jmax(clipTop + 1, infoScrollAreaBot_);
        g.saveState();
        g.reduceClipRegion(0, clipTop, getWidth(), clipBot - clipTop);

        const int scroll = infoScrollY_;
        int y = 86 - scroll;          // start of the scrolling content

        g.setFont(juce::Font("Public Sans", 13.0f, juce::Font::plain));
        g.setColour(text);

        // Section-header band constants — MUST match the values used in
        // resized() exactly, or the labels will drift off their controls
        // when the user scrolls.
        constexpr int kSectionGapPre  = 10;
        constexpr int kSectionLabelH  = 14;
        constexpr int kSectionGapPost = 6;
        constexpr int kSectionBandH   = kSectionGapPre
                                      + kSectionLabelH
                                      + kSectionGapPost;

        auto drawSectionHeader = [&](const juce::String& headerText, int bandTopY)
        {
            const int labelY = bandTopY + kSectionGapPre;
            g.setFont(juce::Font("Public Sans", 11.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xff6a9fd8));
            g.drawText(headerText,
                       margin, labelY,
                       getWidth() - 2 * margin, kSectionLabelH,
                       juce::Justification::centredLeft);

            // Subtle hairline beneath the section label.
            g.setColour(juce::Colour(0xff222a3e));
            g.fillRect(margin, labelY + kSectionLabelH + 2,
                       getWidth() - 2 * margin, 1);

            g.setFont(juce::Font("Public Sans", 13.0f, juce::Font::plain));
            g.setColour(text);
        };

        g.drawText("X:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Y:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Z:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Start:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Duration:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH;

        // ── SPATIAL section ──────────────────────────────────────────────
        drawSectionHeader("SPATIAL (1 unit = 1 m)", y);
        y += kSectionBandH;

        y += editorH + rowGap;   // Distance button row

        g.setFont(juce::Font("Public Sans", 12.0f, juce::Font::plain));
        g.setColour(text.withAlpha(0.92f));
        if (spatialListenerLine_.isNotEmpty())
        {
            g.drawText(spatialListenerLine_,
                       margin, y, getWidth() - 2 * margin, 32,
                       juce::Justification::topLeft);
        }
        y += 36;

        if (spatialDistanceLine_.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffc8e8ff));
            g.drawFittedText(spatialDistanceLine_,
                             margin, y, getWidth() - 2 * margin, 40,
                             juce::Justification::topLeft, 3);
        }
        else if (distancePickActive_)
        {
            g.setColour(juce::Colour(0xffffcc66));
            g.drawText("Click block B in the viewport...",
                       margin, y, getWidth() - 2 * margin, 24,
                       juce::Justification::centredLeft);
        }
        y += 44;

        // ── MOVEMENT section ─────────────────────────────────────────────
        drawSectionHeader("MOVEMENT", y);
        y += kSectionBandH;

        // Movement toggle row label is drawn by the toggle itself.
        y += 32;

        // Keyframes button row — no left-hand label (button is full-width).
        y += editorH + rowGap;

        g.drawText("Mode:",         margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Move dur (s):", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Path Y lift:",  margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH;

        // ── LOOP section ────────────────────────────────────────────────
        drawSectionHeader("LOOP", y);
        y += kSectionBandH;

        // Loop toggle row (no left-side label — the toggle draws its own).
        y += 30;

        g.drawText("Loop length:", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH + rowGap;

        g.drawText("Loop gap (s):", margin, y, labelW, editorH, juce::Justification::centredLeft);
        y += editorH;

        // ── MUTE section ────────────────────────────────────────────────
        drawSectionHeader("MUTE / HIDE", y);
        y += kSectionBandH;

        // Mute / Hide toggle rows.
        y += 30;   // mute toggle
        y += 30;   // hide toggle

        // The "Mute Schedule" button draws its own label (with a count of
        // active windows — see showBlockInfo()).  Just advance y to skip
        // the row resized() reserved for it.
        y += editorH + 12;

        // Match-to-sound button row.
        y += 36;

        // (All Y arithmetic below is in scrolled coordinates.)

        // ── Audio analysis (frequency + oscillation) ─────────────────────────
        g.setFont(juce::Font("Public Sans", 12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff6a9fd8));
        g.drawText("Sound",
                   margin, y, getWidth() - 2 * margin, 16,
                   juce::Justification::centredLeft);
        y += 18;

        g.setFont(juce::Font("Public Sans", 11.0f, juce::Font::plain));
        g.setColour(juce::Colour(0xffaac8e8));

        juce::String statsLine;
        if (audioAnalysis_.valid)
        {
            statsLine = audioAnalysis_.pitchLabel;
            statsLine += "   ·   "
                       + juce::String(audioAnalysis_.durationSec, 2) + " s";
            if (audioAnalysis_.pitchReliable && audioAnalysis_.fundamentalHz > 0.f)
            {
                const float periodMs = 1000.f / audioAnalysis_.fundamentalHz;
                statsLine += "   ·   "
                           + juce::String(periodMs, 2) + " ms / cycle";
            }
        }
        else
        {
            statsLine = selectedBlock_->soundId >= 0
                ? "Sample not loaded"
                : "No sound assigned";
        }

        g.drawText(statsLine,
                   margin, y, getWidth() - 2 * margin, 28,
                   juce::Justification::topLeft);
        y += 30;

        constexpr int kWaveH = 68;
        juce::Rectangle<int> waveArea(margin, y, getWidth() - 2 * margin, kWaveH);
        audioWaveformGraph(g, waveArea);
        y += kWaveH + 10;

        // Reserve the same 16-px label band that resized() allocates above
        // the movement graph.  Without this, the "Movement" label is drawn
        // 16 px above the graph rect — i.e. straight on top of the wave's
        // bottom edge — and looks like it's been clipped.
        constexpr int kMovementLabelH = 16;
        const int movementLabelY = y;
        y += kMovementLabelH;

        juce::Rectangle<int> graphArea(
            margin,
            y,
            getWidth() - 2 * margin,
            108
        );

        g.setFont(juce::Font("Public Sans", 12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff6a9fd8));
        g.drawText("Movement",
                   margin, movementLabelY,
                   getWidth() - 2 * margin, kMovementLabelH,
                   juce::Justification::centredLeft);

        movementGraph(g, *selectedBlock_, graphArea);

        g.restoreState();

        // ── Vertical scrollbar indicator (only when content overflows) ───────
        {
            const int visibleH = infoScrollAreaBot_ - infoScrollAreaTop_;
            const int totalH   = infoContentBottomY_ - infoScrollAreaTop_;
            if (totalH > visibleH)
            {
                const float trackX = (float) (getWidth() - 6);
                const float trackY = (float) infoScrollAreaTop_;
                const float trackH = (float) visibleH;
                g.setColour(juce::Colour(0x33ffffff));
                g.fillRoundedRectangle(trackX, trackY, 3.f, trackH, 1.5f);

                const float thumbH = juce::jmax(20.f,
                                                trackH * (float) visibleH / (float) totalH);
                const float thumbY = trackY
                    + (trackH - thumbH) * (float) infoScrollY_
                                        / (float) juce::jmax(1, totalH - visibleH);
                g.setColour(juce::Colour(0xffaac8e8).withAlpha(0.55f));
                g.fillRoundedRectangle(trackX, thumbY, 3.f, thumbH, 1.5f);
            }
        }
        return;
    }
}


void SidebarComponent::showBlockInfo(const BlockEntry& block,
                                     const juce::String& displayName)
{
    selectedBlock_       = block;
    originalBlock_       = block;
    selectedDisplayName_ = displayName;

    blockPanelOpen = false;
    infoPanelOpen = true;

    xEditor.setText(juce::String(block.pos.x), juce::dontSendNotification);
    yEditor.setText(juce::String(block.pos.y), juce::dontSendNotification);
    zEditor.setText(juce::String(block.pos.z), juce::dontSendNotification);

    startEditor.setText(juce::String(block.startTimeSec, 2), juce::dontSendNotification);
    durationEditor.setText(juce::String(block.durationSec, 2), juce::dontSendNotification);

    const bool hasPath = block.hasRecordedMovement
                      && block.recordedMovement.size() >= 2;

    movementEnabledToggle.setEnabled(hasPath);
    movementEnabledToggle.setToggleState(block.movementEnabled && hasPath,
                                         juce::dontSendNotification);

    modeCombo_.setSelectedId(1 + (int) block.playbackMode,
                             juce::dontSendNotification);

    movementDurationEditor_.setText(juce::String(block.movementDurationSec, 2),
                                    juce::dontSendNotification);
    pathYOffsetEditor_.setText(juce::String(block.movementYOffset),
                               juce::dontSendNotification);
    loopBufferEditor_.setText(juce::String(block.loopBufferSec, 2),
                              juce::dontSendNotification);
    loopDurationEditor_.setText(juce::String(block.loopDurationSec, 2),
                                juce::dontSendNotification);
    muteToggle_.setToggleState(block.isMuted,  juce::dontSendNotification);
    hideToggle_.setToggleState(block.isHidden, juce::dontSendNotification);

    muteWindowsDraft_ = block.muteWindows;
    const int n = (int) muteWindowsDraft_.size();
    muteScheduleBtn_.setButtonText(
        n > 0 ? ("Mute Schedule (" + juce::String(n) + ")...")
              : juce::String("Mute Schedule..."));

    // If the popup was open for a different block, refresh its contents so
    // it doesn't keep editing the previous selection.
    if (muteSchedulePopup_ && muteSchedulePopup_->isVisible())
    {
        const juce::String name = selectedDisplayName_.isNotEmpty()
            ? selectedDisplayName_
            : juce::String("Block ") + juce::String(block.serial);
        muteSchedulePopup_->setSchedule(block.serial, name, muteWindowsDraft_);
    }

    // Position-keyframe draft: pull from the block so the popup edits
    // whatever the engine currently plays back (including paths captured
    // via Alt-drag).
    keyframesDraft_ = block.recordedMovement;
    const int kfCount = (int) keyframesDraft_.size();
    keyframesBtn_.setButtonText(
        kfCount > 0 ? ("Keyframes (" + juce::String(kfCount) + ")...")
                    : juce::String("Keyframes..."));

    if (keyframeEditorPopup_ && keyframeEditorPopup_->isVisible())
    {
        const juce::String name = selectedDisplayName_.isNotEmpty()
            ? selectedDisplayName_
            : juce::String("Block ") + juce::String(block.serial);
        keyframeEditorPopup_->setKeyframes(block.serial, name, block.pos,
                                           keyframesDraft_);
    }

    const bool loopOn = block.isLooping
                     || block.playbackMode == BlockPlaybackMode::Loop;
    loopToggle_.setToggleState(loopOn, juce::dontSendNotification);

    audioAnalysis_ = {};
    if (audioAnalyzer_)
        audioAnalysis_ = audioAnalyzer_(block);

    spatialDistanceLine_.clear();
    distancePickActive_ = false;
    distanceBtn_.setButtonText("Distance...");

    infoScrollY_ = 0;
    resized();
    repaint();
}

void SidebarComponent::setListenerSpatialReadout(const juce::String& line)
{
    if (spatialListenerLine_ != line)
    {
        spatialListenerLine_ = line;
        repaint();
    }
}

void SidebarComponent::setBlockDistanceReadout(const juce::String& line)
{
    spatialDistanceLine_ = line;
    distancePickActive_ = false;
    distanceBtn_.setButtonText("Distance...");
    repaint();
}

void SidebarComponent::setDistancePickActive(bool picking)
{
    distancePickActive_ = picking;
    distanceBtn_.setButtonText(picking ? "Pick B..." : "Distance...");
    repaint();
}

void SidebarComponent::resetSpatialUi()
{
    spatialListenerLine_.clear();
    spatialDistanceLine_.clear();
    distancePickActive_ = false;
    distanceBtn_.setButtonText("Distance...");
    distanceBtn_.setBounds(0, 0, 0, 0);

    if (onCancelDistancePick)
        onCancelDistancePick();
}

void SidebarComponent::mouseWheelMove(const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e);

    if (collapsed || !isInfoPanelOpen() || !selectedBlock_)
        return;

    const int visibleH = infoScrollAreaBot_ - infoScrollAreaTop_;
    const int totalH   = infoContentBottomY_ - infoScrollAreaTop_;
    const int maxScroll = std::max(0, totalH - visibleH);
    if (maxScroll <= 0)
        return;

    // Positive deltaY = wheel up = scroll content up (show earlier content).
    const int step = static_cast<int>(wheel.deltaY * -80.f);
    infoScrollY_ = std::clamp(infoScrollY_ + step, 0, maxScroll);
    resized();
    repaint();
}

void SidebarComponent::clearSelectedBlock()
{
    resetSpatialUi();

    selectedBlock_.reset();
    originalBlock_.reset();
    selectedDisplayName_.clear();

    // Hide the editors so they don't keep showing stale text
    xEditor.setText({}, juce::dontSendNotification);
    yEditor.setText({}, juce::dontSendNotification);
    zEditor.setText({}, juce::dontSendNotification);
    startEditor.setText({}, juce::dontSendNotification);
    durationEditor.setText({}, juce::dontSendNotification);
    movementEnabledToggle.setToggleState(false, juce::dontSendNotification);
    modeCombo_.setSelectedId(1 + (int) BlockPlaybackMode::Natural,
                             juce::dontSendNotification);
    movementDurationEditor_.setText({}, juce::dontSendNotification);
    pathYOffsetEditor_.setText({}, juce::dontSendNotification);
    loopBufferEditor_.setText({}, juce::dontSendNotification);
    loopDurationEditor_.setText({}, juce::dontSendNotification);
    loopToggle_.setToggleState(false,  juce::dontSendNotification);
    muteToggle_.setToggleState(false,  juce::dontSendNotification);
    hideToggle_.setToggleState(false,  juce::dontSendNotification);
    muteWindowsDraft_.clear();
    muteScheduleBtn_.setButtonText("Mute Schedule...");
    if (muteSchedulePopup_)
        muteSchedulePopup_->hide();

    keyframesDraft_.clear();
    keyframesBtn_.setButtonText("Keyframes...");
    if (keyframeEditorPopup_)
        keyframeEditorPopup_->hide();
    audioAnalysis_ = {};
    infoScrollY_ = 0;

    resized();
    repaint();
}

void SidebarComponent::clearSelectedBlockIfSerial(int serial)
{
    if (!selectedBlock_ || selectedBlock_->serial != serial)
        return;

    clearSelectedBlock();
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
        g.setFont(juce::Font("Public Sans", 12.0f, juce::Font::plain));
        g.drawText(block.hasRecordedMovement ? "Record more keyframes (Alt+LMB drag)"
                                             : "No recorded movement",
                   graphArea,
                   juce::Justification::centred);
        return;
    }

    if (!block.movementEnabled)
    {
        g.setColour(juce::Colours::orange.withAlpha(0.85f));
        g.setFont(juce::Font("Public Sans", 11.0f, juce::Font::italic));
        g.drawText("Movement disabled",
                   graphArea.removeFromTop(18),
                   juce::Justification::centred);
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
            g.setFont(juce::Font("Public Sans", 9.0f, juce::Font::plain));
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
    g.setFont(juce::Font("Public Sans", 9.0f, juce::Font::plain));

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
    g.setFont(juce::Font("Public Sans", 9.0f, juce::Font::plain));

    // U+2192 RIGHT ARROW = \xe2\x86\x92
    const juce::String arrow = juce::String::fromUTF8("\xe2\x86\x92");
    g.drawText("X " + juce::String(minX) + arrow + juce::String(maxX)
             + "   Z " + juce::String(minZ) + arrow + juce::String(maxZ),
               graphArea.reduced(6).removeFromBottom(12),
               juce::Justification::centredLeft);
}

void SidebarComponent::audioWaveformGraph(juce::Graphics& g,
                                          juce::Rectangle<int> area) const
{
    g.setColour(juce::Colour(0xff151a2e));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    g.setColour(juce::Colour(0xff445577));
    g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

    if (!audioAnalysis_.valid
        || audioAnalysis_.waveformMin.empty()
        || audioAnalysis_.waveformMax.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.setFont(juce::Font("Public Sans", 11.0f, juce::Font::plain));
        g.drawText("No waveform",
                   area,
                   juce::Justification::centred);
        return;
    }

    auto plot = area.reduced(8);
    const float midY = plot.getCentreY();
    const float halfH = plot.getHeight() * 0.45f;

    // Centre line
    g.setColour(juce::Colour(0xff2c344f));
    g.drawHorizontalLine(static_cast<int>(midY),
                         static_cast<float>(plot.getX()),
                         static_cast<float>(plot.getRight()));

    const int cols = static_cast<int>(audioAnalysis_.waveformMin.size());
    const float colW = plot.getWidth() / static_cast<float>(cols);

    juce::Path fillPath;
    bool started = false;

    for (int c = 0; c < cols; ++c)
    {
        const float x = plot.getX() + (c + 0.5f) * colW;
        const float yMax = midY - audioAnalysis_.waveformMax[static_cast<size_t>(c)] * halfH;
        if (!started)
        {
            fillPath.startNewSubPath(x, yMax);
            started = true;
        }
        else
        {
            fillPath.lineTo(x, yMax);
        }
    }

    for (int c = cols - 1; c >= 0; --c)
    {
        const float x = plot.getX() + (c + 0.5f) * colW;
        const float yMin = midY - audioAnalysis_.waveformMin[static_cast<size_t>(c)] * halfH;
        fillPath.lineTo(x, yMin);
    }

    fillPath.closeSubPath();

    g.setColour(juce::Colour(0xff3f8cff).withAlpha(0.35f));
    g.fillPath(fillPath);

    g.setColour(juce::Colour(0xff7eb8ff).withAlpha(0.9f));
    g.strokePath(fillPath, juce::PathStrokeType(1.2f));

    g.setColour(juce::Colours::grey.withAlpha(0.7f));
    g.setFont(juce::Font("Public Sans", 8.5f, juce::Font::plain));
    g.drawText("Oscillation",
               plot.removeFromBottom(10),
               juce::Justification::centredRight);
}
