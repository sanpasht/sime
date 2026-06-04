// ─────────────────────────────────────────────────────────────────────────────
// KeyframeEditorPopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "KeyframeEditorPopup.h"
#include <algorithm>

namespace
{
    constexpr juce::uint32 kBgColor      = 0xf012141cu;
    constexpr juce::uint32 kBorderColor  = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor  = 0xff5b7ce6u;
    constexpr juce::uint32 kFieldBgColor = 0xff181a24u;
    constexpr juce::uint32 kFieldBdColor = 0xff2f3447u;
    constexpr juce::uint32 kRowBgColor   = 0xff161a26u;
    constexpr juce::uint32 kRowBdColor   = 0xff262d44u;

    // Column geometry (must match between resized() / layoutRows() / paint()).
    constexpr int kColTimeW = 90;
    constexpr int kColXYZW  = 70;
    constexpr int kColGap   = 8;
    constexpr int kDelW     = 28;
}

KeyframeEditorPopup::KeyframeEditorPopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Position Keyframes", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(15.f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xfff0f2fa));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setFont(juce::Font(11.5f));
    subtitleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    subtitleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitleLabel_);

    auto styleHeader = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(10.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, juce::Colour(0xff6b7494));
        l.setJustificationType(juce::Justification::centredLeft);
    };
    styleHeader(headerTimeLabel_, "TIME (S)");
    styleHeader(headerXLabel_,    "X");
    styleHeader(headerYLabel_,    "Y");
    styleHeader(headerZLabel_,    "Z");
    addAndMakeVisible(headerTimeLabel_);
    addAndMakeVisible(headerXLabel_);
    addAndMakeVisible(headerYLabel_);
    addAndMakeVisible(headerZLabel_);

    hintLabel_.setText("Times are seconds from the block's start. The list is "
                       "sorted on Apply; the earliest keyframe becomes the "
                       "block's anchor position.",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(11.f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    hintLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(hintLabel_);

    emptyHintLabel_.setText("No keyframes yet. Click \"+ Add Keyframe\" to "
                            "place the block at a specific time, or use Alt-drag "
                            "in the viewport to record a path.",
                            juce::dontSendNotification);
    emptyHintLabel_.setFont(juce::Font(12.f, juce::Font::italic));
    emptyHintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7a83a3));
    emptyHintLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyHintLabel_);

    addButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    addButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    addButton_.onClick = [this]
    {
        pullValuesFromRows();

        // Default time: last row's time + 0.5s (or 0 if empty).
        double t = 0.0;
        Vec3i  p = anchorPos_;
        if (!draft_.empty())
        {
            t = draft_.back().timeSec + 0.5;
            p = draft_.back().position;
        }
        draft_.push_back({ t, p });
        rebuildRows();
        scrollToBottom();
        resized();
        repaint();
    };
    addAndMakeVisible(addButton_);

    clearButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    clearButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    clearButton_.onClick = [this]
    {
        draft_.clear();
        scrollY_ = 0;
        rebuildRows();
        resized();
        repaint();
    };
    addAndMakeVisible(clearButton_);

    applyButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kAccentColor));
    applyButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    applyButton_.onClick = [this]
    {
        pullValuesFromRows();

        // Sort by time and shift so the earliest keyframe sits at t = 0.
        // The engine treats the first keyframe as the block's anchor
        // position, so anything else would feel like a jump at start.
        std::vector<MovementKeyFrame> cleaned = draft_;
        std::sort(cleaned.begin(), cleaned.end(),
                  [](const MovementKeyFrame& a, const MovementKeyFrame& b)
                  { return a.timeSec < b.timeSec; });

        if (!cleaned.empty() && cleaned.front().timeSec != 0.0)
        {
            const double off = cleaned.front().timeSec;
            for (auto& k : cleaned)
                k.timeSec -= off;
        }

        if (onApply)
            onApply(editingSerial_, cleaned);

        hide();
    };
    addAndMakeVisible(applyButton_);

    cancelButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    cancelButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    cancelButton_.onClick = [this]
    {
        hide();
        if (onDismiss) onDismiss();
    };
    addAndMakeVisible(cancelButton_);

    // Time-snap selector — rounds keyframe times to a multiple of the chosen
    // interval.  Off by default to avoid surprising existing scenes; pick
    // 1 s for a "show every second" view that's much easier to read.
    snapLabel_.setText("Snap times:", juce::dontSendNotification);
    snapLabel_.setFont(juce::Font(11.f, juce::Font::bold));
    snapLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    snapLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(snapLabel_);

    snapBox_.addItem("Off (exact)", 1);
    snapBox_.addItem("0.5 s",       2);
    snapBox_.addItem("1 s",         3);
    snapBox_.addItem("2 s",         4);
    snapBox_.addItem("5 s",         5);
    snapBox_.setSelectedId(1, juce::dontSendNotification);
    snapBox_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(kFieldBgColor));
    snapBox_.setColour(juce::ComboBox::textColourId,       juce::Colour(0xfff0f2fa));
    snapBox_.setColour(juce::ComboBox::outlineColourId,    juce::Colour(kFieldBdColor));
    snapBox_.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff8b94ad));
    snapBox_.setTooltip("Round every keyframe's time to a multiple of this interval.  "
                        "Off keeps the exact original times.");
    snapBox_.onChange = [this]
    {
        pullValuesFromRows();
        applySnapToDraft();
        rebuildRows();
        resized();
        repaint();
    };
    addAndMakeVisible(snapBox_);

    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowHasDropShadow);

    setVisible(false);
}

void KeyframeEditorPopup::applySnapToDraft()
{
    const double grid = [this]
    {
        switch (snapBox_.getSelectedId())
        {
            case 2: return 0.5;
            case 3: return 1.0;
            case 4: return 2.0;
            case 5: return 5.0;
            default: return 0.0;   // Off
        }
    }();

    // "Off" restores the pristine path exactly.
    if (grid <= 0.0)
    {
        draft_ = originalFrames_;
        return;
    }

    // Resample the ORIGINAL path at a fixed interval.  We sample the position
    // *along* the path at each grid time (linear interpolation between the two
    // surrounding original keyframes, rounded to the nearest grid cell since
    // block positions are integers).  This preserves the trajectory and spaces
    // keyframes evenly — unlike the old "round each time" which collapsed many
    // keyframes onto the same timestamp and made playback jump straight to the
    // end.
    if (originalFrames_.size() < 2)
    {
        draft_ = originalFrames_;
        return;
    }

    std::vector<MovementKeyFrame> src = originalFrames_;
    std::sort(src.begin(), src.end(),
              [](const MovementKeyFrame& a, const MovementKeyFrame& b)
              { return a.timeSec < b.timeSec; });

    const double t0   = src.front().timeSec;
    const double tEnd = src.back().timeSec;

    auto posAt = [&src](double t) -> Vec3i
    {
        if (t <= src.front().timeSec) return src.front().position;
        if (t >= src.back().timeSec)  return src.back().position;
        for (size_t i = 0; i + 1 < src.size(); ++i)
        {
            const auto& a = src[i];
            const auto& b = src[i + 1];
            if (t >= a.timeSec && t < b.timeSec)
            {
                const double dur = b.timeSec - a.timeSec;
                const double u   = dur > 1e-9 ? (t - a.timeSec) / dur : 0.0;
                return Vec3i{
                    (int) std::lround(a.position.x + (b.position.x - a.position.x) * u),
                    (int) std::lround(a.position.y + (b.position.y - a.position.y) * u),
                    (int) std::lround(a.position.z + (b.position.z - a.position.z) * u)
                };
            }
        }
        return src.back().position;
    };

    std::vector<MovementKeyFrame> out;
    for (double t = t0; t <= tEnd + 1e-6; t += grid)
    {
        MovementKeyFrame kf;
        kf.timeSec  = t;
        kf.position = posAt(t);
        out.push_back(kf);
    }
    // Always pin the exact final pose so the path ends where it should even if
    // tEnd isn't a clean multiple of the grid.
    if (out.empty() || std::abs(out.back().timeSec - tEnd) > 1e-6)
    {
        MovementKeyFrame kf;
        kf.timeSec  = tEnd;
        kf.position = src.back().position;
        out.push_back(kf);
    }

    draft_ = std::move(out);
}

KeyframeEditorPopup::~KeyframeEditorPopup()
{
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

void KeyframeEditorPopup::setKeyframes(int blockSerial,
                                       const juce::String& blockName,
                                       Vec3i anchorPos,
                                       const std::vector<MovementKeyFrame>& frames)
{
    editingSerial_ = blockSerial;
    editingName_   = blockName;
    anchorPos_     = anchorPos;
    draft_         = frames;
    originalFrames_ = frames;   // pristine copy for lossless re-intervalling
    scrollY_       = 0;

    // New selection starts at "Off (exact)" so the times shown are the real
    // recorded times until the user explicitly picks an interval.
    snapBox_.setSelectedId(1, juce::dontSendNotification);

    titleLabel_.setText("Position Keyframes", juce::dontSendNotification);
    subtitleLabel_.setText(blockName.isEmpty() ? juce::String("(no block selected)")
                                               : blockName,
                           juce::dontSendNotification);

    rebuildRows();
}

void KeyframeEditorPopup::showAt(juce::Point<int> screenPos)
{
    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    juce::Rectangle<int> screen = display
        ? display->userArea
        : juce::Rectangle<int>(0, 0, 1920, 1080);

    int px = screenPos.x + 16;
    int py = screenPos.y - kHeight / 2;
    px = juce::jlimit(screen.getX(), screen.getRight()  - kWidth,  px);
    py = juce::jlimit(screen.getY(), screen.getBottom() - kHeight, py);

    setVisible(true);
    setBounds(px, py, kWidth, kHeight);
    resized();
    toFront(true);
    grabKeyboardFocus();
}

void KeyframeEditorPopup::hide()
{
    setVisible(false);
}

// ─────────────────────────────────────────────────────────────────────────────

void KeyframeEditorPopup::rebuildRows()
{
    for (auto& r : rows_)
    {
        if (r.time)      removeChildComponent(r.time.get());
        if (r.x)         removeChildComponent(r.x.get());
        if (r.y)         removeChildComponent(r.y.get());
        if (r.z)         removeChildComponent(r.z.get());
        if (r.deleteBtn) removeChildComponent(r.deleteBtn.get());
    }
    rows_.clear();
    rows_.reserve(draft_.size());

    auto styleField = [](juce::TextEditor& f, bool allowMinus)
    {
        f.setFont(juce::Font(13.f));
        f.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(kFieldBgColor));
        f.setColour(juce::TextEditor::textColourId,           juce::Colour(0xfff0f2fa));
        f.setColour(juce::TextEditor::outlineColourId,        juce::Colour(kFieldBdColor));
        f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentColor));
        f.setColour(juce::TextEditor::highlightColourId,      juce::Colour(0xff3a5fbf));
        f.setIndents(8, 4);
        f.setInputRestrictions(16, allowMinus ? "-0123456789." : "0123456789.");
        f.setSelectAllWhenFocused(true);
    };

    for (size_t i = 0; i < draft_.size(); ++i)
    {
        KeyRow r;
        r.time      = std::make_unique<juce::TextEditor>();
        r.x         = std::make_unique<juce::TextEditor>();
        r.y         = std::make_unique<juce::TextEditor>();
        r.z         = std::make_unique<juce::TextEditor>();
        r.deleteBtn = std::make_unique<juce::TextButton>("X");

        styleField(*r.time, false);
        styleField(*r.x,    true);
        styleField(*r.y,    true);
        styleField(*r.z,    true);

        r.time->setText(juce::String(draft_[i].timeSec, 3),
                        juce::dontSendNotification);
        r.x->setText   (juce::String(draft_[i].position.x), juce::dontSendNotification);
        r.y->setText   (juce::String(draft_[i].position.y), juce::dontSendNotification);
        r.z->setText   (juce::String(draft_[i].position.z), juce::dontSendNotification);

        r.deleteBtn->setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff3a1c24));
        r.deleteBtn->setColour(juce::TextButton::textColourOffId,
                               juce::Colour(0xfff0c8c8));
        r.deleteBtn->setTooltip("Remove this keyframe");

        const int rowIndex = static_cast<int>(i);
        r.deleteBtn->onClick = [this, rowIndex]
        {
            pullValuesFromRows();
            if (rowIndex >= 0 && rowIndex < (int) draft_.size())
            {
                draft_.erase(draft_.begin() + rowIndex);
                rebuildRows();
                resized();
                repaint();
            }
        };

        addAndMakeVisible(*r.time);
        addAndMakeVisible(*r.x);
        addAndMakeVisible(*r.y);
        addAndMakeVisible(*r.z);
        addAndMakeVisible(*r.deleteBtn);

        rows_.push_back(std::move(r));
    }

    layoutRows();
    repaint();
}

void KeyframeEditorPopup::pullValuesFromRows()
{
    for (size_t i = 0; i < rows_.size() && i < draft_.size(); ++i)
    {
        draft_[i].timeSec   = rows_[i].time ? rows_[i].time->getText().getDoubleValue() : 0.0;
        draft_[i].position.x = rows_[i].x   ? rows_[i].x->getText().getIntValue()       : 0;
        draft_[i].position.y = rows_[i].y   ? rows_[i].y->getText().getIntValue()       : 0;
        draft_[i].position.z = rows_[i].z   ? rows_[i].z->getText().getIntValue()       : 0;
    }
}

void KeyframeEditorPopup::scrollToBottom()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;
    const int totalH  = static_cast<int>(rows_.size()) * (kRowH + kRowGap);
    scrollY_ = std::max(0, totalH - listH);
}

void KeyframeEditorPopup::layoutRows()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;

    const int colTimeX = kPad;
    const int colXX    = colTimeX + kColTimeW + kColGap;
    const int colYX    = colXX    + kColXYZW  + kColGap;
    const int colZX    = colYX    + kColXYZW  + kColGap;
    const int delX     = kWidth - kPad - kDelW;

    int y = listTop - scrollY_;
    for (auto& r : rows_)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (offscreen)
        {
            if (r.time)      r.time->setBounds(0, 0, 0, 0);
            if (r.x)         r.x->setBounds(0, 0, 0, 0);
            if (r.y)         r.y->setBounds(0, 0, 0, 0);
            if (r.z)         r.z->setBounds(0, 0, 0, 0);
            if (r.deleteBtn) r.deleteBtn->setBounds(0, 0, 0, 0);
        }
        else
        {
            r.time     ->setBounds(colTimeX, y, kColTimeW, kRowH - 4);
            r.x        ->setBounds(colXX,    y, kColXYZW,  kRowH - 4);
            r.y        ->setBounds(colYX,    y, kColXYZW,  kRowH - 4);
            r.z        ->setBounds(colZX,    y, kColXYZW,  kRowH - 4);
            r.deleteBtn->setBounds(delX,     y, kDelW,     kRowH - 4);
        }
        y += kRowH + kRowGap;
    }

    const int totalH = static_cast<int>(rows_.size()) * (kRowH + kRowGap);
    const int maxScroll = std::max(0, totalH - listH);
    if (scrollY_ > maxScroll)
    {
        scrollY_ = maxScroll;
        layoutRows();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void KeyframeEditorPopup::resized()
{
    const int titleY = kPad + 4;
    titleLabel_   .setBounds(kPad, titleY,      kWidth - 2 * kPad, 22);
    subtitleLabel_.setBounds(kPad, titleY + 22, kWidth - 2 * kPad, 18);

    const int colHeaderY = kHeaderH - 22;
    const int colTimeX = kPad;
    const int colXX    = colTimeX + kColTimeW + kColGap;
    const int colYX    = colXX    + kColXYZW  + kColGap;
    const int colZX    = colYX    + kColXYZW  + kColGap;

    headerTimeLabel_.setBounds(colTimeX, colHeaderY, kColTimeW, 14);
    headerXLabel_   .setBounds(colXX,    colHeaderY, kColXYZW,  14);
    headerYLabel_   .setBounds(colYX,    colHeaderY, kColXYZW,  14);
    headerZLabel_   .setBounds(colZX,    colHeaderY, kColXYZW,  14);

    emptyHintLabel_.setBounds(kPad, kHeaderH + 12,
                              kWidth - 2 * kPad, 48);

    const int footerTop = kHeight - kFooterH + 6;
    hintLabel_.setBounds(kPad, footerTop, kWidth - 2 * kPad, 32);

    // Row 0: Snap-times selector (right-aligned, label + combo).
    const int snapH   = 24;
    const int snapW   = 120;
    const int snapLbW = 90;
    const int snapY   = footerTop + 36;
    snapLabel_.setBounds(kWidth - kPad - snapW - snapLbW - 4, snapY, snapLbW, snapH);
    snapBox_  .setBounds(kWidth - kPad - snapW,                snapY, snapW,  snapH);

    // Row 1: Add Keyframe / Clear All
    const int rowY    = snapY + snapH + 6;
    const int actionH = 26;
    const int halfW   = (kWidth - 2 * kPad - 8) / 2;
    addButton_  .setBounds(kPad,             rowY, halfW, actionH);
    clearButton_.setBounds(kPad + halfW + 8, rowY, halfW, actionH);

    // Row 2: Cancel / Apply
    const int btnY = rowY + actionH + 6;
    cancelButton_.setBounds(kPad,             btnY, halfW, actionH);
    applyButton_ .setBounds(kPad + halfW + 8, btnY, halfW, actionH);

    layoutRows();
}

void KeyframeEditorPopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.f, 1.f);

    g.setColour(juce::Colour(kAccentColor));
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeaderH - 4, kWidth - 2 * kPad, 1);

    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeight - kFooterH - 2, kWidth - 2 * kPad, 1);

    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;

    g.saveState();
    g.reduceClipRegion(0, listTop, kWidth, listBot - listTop);

    int y = listTop - scrollY_;
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (!offscreen)
        {
            juce::Rectangle<float> rowR((float) kPad - 6.f, (float) y - 3.f,
                                        (float) (kWidth - 2 * kPad) + 12.f,
                                        (float) (kRowH - 4) + 6.f);
            g.setColour(juce::Colour(kRowBgColor));
            g.fillRoundedRectangle(rowR, 5.f);
            g.setColour(juce::Colour(kRowBdColor));
            g.drawRoundedRectangle(rowR, 5.f, 1.f);

            g.setColour(juce::Colour(0xff5b6685));
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.drawText("#" + juce::String((int) i + 1),
                       0, y, kPad + 2, kRowH - 4,
                       juce::Justification::centredRight);
        }
        y += kRowH + kRowGap;
    }

    g.restoreState();

    emptyHintLabel_.setVisible(rows_.empty());

    const int listH  = listBot - listTop;
    const int totalH = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    if (totalH > listH)
    {
        const float trackX = (float) (kWidth - 7);
        const float trackY = (float) listTop + 2.f;
        const float trackH = (float) listH - 4.f;
        g.setColour(juce::Colour(0x33ffffff));
        g.fillRoundedRectangle(trackX, trackY, 3.f, trackH, 1.5f);

        const float thumbH = juce::jmax(24.f,
                                        trackH * (float) listH / (float) totalH);
        const float thumbY = trackY
            + (trackH - thumbH) * (float) scrollY_
                                / (float) juce::jmax(1, totalH - listH);
        g.setColour(juce::Colour(0xffaac8e8).withAlpha(0.6f));
        g.fillRoundedRectangle(trackX, thumbY, 3.f, thumbH, 1.5f);
    }
}

bool KeyframeEditorPopup::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        hide();
        if (onDismiss) onDismiss();
        return true;
    }
    if (k == juce::KeyPress::returnKey)
    {
        applyButton_.triggerClick();
        return true;
    }
    return false;
}

void KeyframeEditorPopup::mouseWheelMove(const juce::MouseEvent&,
                                          const juce::MouseWheelDetails& wheel)
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;
    const int totalH  = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    const int maxScroll = std::max(0, totalH - listH);
    if (maxScroll <= 0) return;

    const int step = static_cast<int>(wheel.deltaY * -80.f);
    scrollY_ = std::clamp(scrollY_ + step, 0, maxScroll);
    layoutRows();
    repaint();
}
