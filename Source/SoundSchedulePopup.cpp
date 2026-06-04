// ─────────────────────────────────────────────────────────────────────────────
// SoundSchedulePopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "SoundSchedulePopup.h"
#include "SoundLibrary.h"

namespace
{
    constexpr juce::uint32 kBgColor      = 0xf012141cu;
    constexpr juce::uint32 kBorderColor  = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor  = 0xff4caf7du;   // green accent (sound, not mute)
    constexpr juce::uint32 kFieldBgColor = 0xff181a24u;
    constexpr juce::uint32 kFieldBdColor = 0xff2f3447u;
    constexpr juce::uint32 kRowBgColor   = 0xff161a26u;
    constexpr juce::uint32 kRowBdColor   = 0xff262d44u;
    constexpr juce::uint32 kRowSelColor  = 0xff1f3a2cu;
}

SoundSchedulePopup::SoundSchedulePopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Sound Schedule", juce::dontSendNotification);
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
    styleHeader(headerStartLabel_, "START (S)");
    styleHeader(headerDurLabel_,   "DUR (S)");
    styleHeader(headerLoopLabel_,  "LOOP");
    styleHeader(headerGapLabel_,   "GAP (S)");
    styleHeader(headerSoundLabel_, "SOUND");
    addAndMakeVisible(headerStartLabel_);
    addAndMakeVisible(headerDurLabel_);
    addAndMakeVisible(headerLoopLabel_);
    addAndMakeVisible(headerGapLabel_);
    addAndMakeVisible(headerSoundLabel_);

    hintLabel_.setText("Each entry plays its sound at the start time for DUR seconds. Toggle "
                       "LOOP to repeat it (GAP s between repeats) across that window. Pick a "
                       "row, then choose a sound on the right (same instrument type).",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(11.f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    hintLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(hintLabel_);

    emptyHintLabel_.setText("No scheduled sounds yet. Click \"+ Add Sound\".",
                            juce::dontSendNotification);
    emptyHintLabel_.setFont(juce::Font(12.f, juce::Font::italic));
    emptyHintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7a83a3));
    emptyHintLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyHintLabel_);

    pickerLabel_.setText("Sound for selected row", juce::dontSendNotification);
    pickerLabel_.setFont(juce::Font(10.5f, juce::Font::bold));
    pickerLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff6b7494));
    pickerLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pickerLabel_);

    addAndMakeVisible(picker_);
    picker_.onSelectionChanged = [this](int entryIdx)
    {
        assignSoundToSelectedRow(entryIdx);
    };
    picker_.onDoubleClick = [this](int entryIdx)
    {
        assignSoundToSelectedRow(entryIdx);
    };

    addButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    addButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    addButton_.onClick = [this]
    {
        pullValuesFromRows();
        SoundEvent ne;
        ne.startSec = 0.0; ne.durationSec = 1.0; ne.soundId = -1;
        draft_.push_back(ne);
        selectedRow_ = (int) draft_.size() - 1;
        rebuildRows();
        const int listH     = kHeight - kHeaderH - kFooterH - 4;
        const int rowsTotal = (int) rows_.size() * (kRowH + kRowGap);
        scrollY_ = std::max(0, rowsTotal - listH);
        resized();
        repaint();
    };
    addAndMakeVisible(addButton_);

    clearButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff242a3c));
    clearButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    clearButton_.onClick = [this]
    {
        draft_.clear();
        selectedRow_ = -1;
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

        std::vector<SoundEvent> cleaned;
        cleaned.reserve(draft_.size());
        for (const auto& se : draft_)
            if (se.soundId >= 0 && se.durationSec > 0.0)
            {
                SoundEvent out;
                out.startSec     = std::max(0.0, se.startSec);
                out.durationSec  = se.durationSec;
                out.soundId      = se.soundId;
                out.relativePath = se.relativePath;
                out.loop         = se.loop;
                out.loopGapSec   = std::max(0.0, se.loopGapSec);
                cleaned.push_back(std::move(out));
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

    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowHasDropShadow);

    setVisible(false);
}

SoundSchedulePopup::~SoundSchedulePopup()
{
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

juce::String SoundSchedulePopup::soundNameFor(const SoundEvent& se) const
{
    if (se.soundId < 0)
        return "(no sound - pick one ->)";
    if (library_ != nullptr)
    {
        const int idx = library_->entryForSoundId(se.soundId);
        if (idx >= 0)
            return library_->at(idx).displayName;
    }
    if (!se.relativePath.empty())
        return juce::File(juce::String(se.relativePath)).getFileNameWithoutExtension();
    return "sound #" + juce::String(se.soundId);
}

void SoundSchedulePopup::setSchedule(int blockSerial,
                                     const juce::String& blockName,
                                     BlockType blockType,
                                     SoundLibrary* library,
                                     std::function<int(int)> ensureLoadedFn,
                                     const std::vector<SoundEvent>& schedule)
{
    editingSerial_  = blockSerial;
    editingName_    = blockName;
    blockType_      = blockType;
    library_        = library;
    ensureLoadedFn_ = std::move(ensureLoadedFn);
    draft_          = schedule;
    selectedRow_    = draft_.empty() ? -1 : 0;
    scrollY_        = 0;

    titleLabel_.setText("Sound Schedule", juce::dontSendNotification);
    subtitleLabel_.setText(blockName.isEmpty() ? juce::String("(no block selected)")
                                               : blockName,
                           juce::dontSendNotification);

    picker_.setLibrary(library_);
    picker_.setBlockType(blockType_);

    rebuildRows();
}

void SoundSchedulePopup::showAt(juce::Point<int> screenPos)
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

void SoundSchedulePopup::hide()
{
    setVisible(false);
}

// ─────────────────────────────────────────────────────────────────────────────

void SoundSchedulePopup::assignSoundToSelectedRow(int entryIdx)
{
    if (selectedRow_ < 0 || selectedRow_ >= (int) draft_.size())
        return;
    if (library_ == nullptr || entryIdx < 0)
        return;

    int sid = ensureLoadedFn_ ? ensureLoadedFn_(entryIdx) : -1;
    if (sid < 0) return;

    draft_[selectedRow_].soundId      = sid;
    draft_[selectedRow_].relativePath = library_->at(entryIdx).relativePath.toStdString();
    rebuildRows();
    repaint();
}

void SoundSchedulePopup::rebuildRows()
{
    for (auto& r : rows_)
    {
        if (r.start)     removeChildComponent(r.start.get());
        if (r.duration)  removeChildComponent(r.duration.get());
        if (r.loopBtn)   removeChildComponent(r.loopBtn.get());
        if (r.gap)       removeChildComponent(r.gap.get());
        if (r.pickBtn)   removeChildComponent(r.pickBtn.get());
        if (r.deleteBtn) removeChildComponent(r.deleteBtn.get());
    }
    rows_.clear();
    rows_.reserve(draft_.size());

    for (size_t i = 0; i < draft_.size(); ++i)
    {
        EntryRow r;
        r.start     = std::make_unique<juce::TextEditor>();
        r.duration  = std::make_unique<juce::TextEditor>();
        r.loopBtn   = std::make_unique<juce::TextButton>("Loop");
        r.gap       = std::make_unique<juce::TextEditor>();
        r.pickBtn   = std::make_unique<juce::TextButton>();
        r.deleteBtn = std::make_unique<juce::TextButton>("X");

        auto styleField = [](juce::TextEditor& f)
        {
            f.setFont(juce::Font(13.f));
            f.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(kFieldBgColor));
            f.setColour(juce::TextEditor::textColourId,           juce::Colour(0xfff0f2fa));
            f.setColour(juce::TextEditor::outlineColourId,        juce::Colour(kFieldBdColor));
            f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentColor));
            f.setIndents(8, 4);
            f.setInputRestrictions(16, "0123456789.");
            f.setSelectAllWhenFocused(true);
        };
        styleField(*r.start);
        styleField(*r.duration);
        styleField(*r.gap);

        r.start->setText(juce::String(draft_[i].startSec, 3), juce::dontSendNotification);
        r.duration->setText(juce::String(draft_[i].durationSec, 3), juce::dontSendNotification);
        r.gap->setText(juce::String(draft_[i].loopGapSec, 2), juce::dontSendNotification);

        const int rowIdxL = (int) i;
        const bool looping = draft_[i].loop;
        r.loopBtn->setButtonText(looping ? "Loop: ON" : "Loop");
        r.loopBtn->setColour(juce::TextButton::buttonColourId,
                             juce::Colour(looping ? kAccentColor : 0xff242a3c));
        r.loopBtn->setColour(juce::TextButton::textColourOffId,
                             looping ? juce::Colours::white : juce::Colour(0xffd6f5e0));
        r.loopBtn->setTooltip("Loop this scheduled sound across its play window "
                              "(GAP seconds between repeats).");
        r.loopBtn->onClick = [this, rowIdxL]
        {
            pullValuesFromRows();
            if (rowIdxL >= 0 && rowIdxL < (int) draft_.size())
                draft_[rowIdxL].loop = !draft_[rowIdxL].loop;
            rebuildRows();
            repaint();
        };

        // "Pick" button doubles as the row's sound-name display + row selector.
        r.pickBtn->setButtonText(soundNameFor(draft_[i]));
        r.pickBtn->setColour(juce::TextButton::buttonColourId,
                             juce::Colour(i == (size_t) selectedRow_ ? kRowSelColor : 0xff242a3c));
        r.pickBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd6f5e0));
        r.pickBtn->setTooltip("Select this row, then pick a sound on the right.");

        const int rowIndex = (int) i;
        r.pickBtn->onClick = [this, rowIndex]
        {
            pullValuesFromRows();
            selectedRow_ = rowIndex;
            rebuildRows();
            repaint();
        };

        r.deleteBtn->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3a1c24));
        r.deleteBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0c8c8));
        r.deleteBtn->setTooltip("Remove this scheduled sound");
        r.deleteBtn->onClick = [this, rowIndex]
        {
            pullValuesFromRows();
            if (rowIndex >= 0 && rowIndex < (int) draft_.size())
            {
                draft_.erase(draft_.begin() + rowIndex);
                if (selectedRow_ >= (int) draft_.size())
                    selectedRow_ = (int) draft_.size() - 1;
                rebuildRows();
                resized();
                repaint();
            }
        };

        addAndMakeVisible(*r.start);
        addAndMakeVisible(*r.duration);
        addAndMakeVisible(*r.loopBtn);
        addAndMakeVisible(*r.gap);
        addAndMakeVisible(*r.pickBtn);
        addAndMakeVisible(*r.deleteBtn);

        rows_.push_back(std::move(r));
    }

    // Keep the picker reflecting the selected row's current sound.
    if (selectedRow_ >= 0 && selectedRow_ < (int) draft_.size()
        && library_ != nullptr)
    {
        const int idx = library_->entryForSoundId(draft_[selectedRow_].soundId);
        picker_.setSelectedEntry(idx);
    }

    layoutRows();
    repaint();
}

void SoundSchedulePopup::pullValuesFromRows()
{
    for (size_t i = 0; i < rows_.size() && i < draft_.size(); ++i)
    {
        draft_[i].startSec    = rows_[i].start
            ? rows_[i].start->getText().getDoubleValue() : 0.0;
        draft_[i].durationSec = rows_[i].duration
            ? rows_[i].duration->getText().getDoubleValue() : 0.0;
        draft_[i].loopGapSec  = rows_[i].gap
            ? rows_[i].gap->getText().getDoubleValue() : 0.0;
    }
}

void SoundSchedulePopup::layoutRows()
{
    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;
    const int listH   = listBot - listTop;

    const int colStartX = kPad;
    const int colStartW = 48;
    const int colDurX   = colStartX + colStartW + 6;
    const int colDurW   = 48;
    const int colLoopX  = colDurX + colDurW + 6;
    const int colLoopW  = 56;
    const int colGapX   = colLoopX + colLoopW + 6;
    const int colGapW   = 44;
    const int delW      = 24;
    const int delX      = kListW - kPad - delW;
    const int pickX     = colGapX + colGapW + 6;
    const int pickW     = delX - pickX - 6;

    int y = listTop - scrollY_;
    for (auto& r : rows_)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (offscreen)
        {
            if (r.start)     r.start->setBounds(0, 0, 0, 0);
            if (r.duration)  r.duration->setBounds(0, 0, 0, 0);
            if (r.loopBtn)   r.loopBtn->setBounds(0, 0, 0, 0);
            if (r.gap)       r.gap->setBounds(0, 0, 0, 0);
            if (r.pickBtn)   r.pickBtn->setBounds(0, 0, 0, 0);
            if (r.deleteBtn) r.deleteBtn->setBounds(0, 0, 0, 0);
        }
        else
        {
            r.start    ->setBounds(colStartX, y, colStartW, kRowH - 4);
            r.duration ->setBounds(colDurX,   y, colDurW,   kRowH - 4);
            r.loopBtn  ->setBounds(colLoopX,  y, colLoopW,  kRowH - 4);
            r.gap      ->setBounds(colGapX,   y, colGapW,   kRowH - 4);
            r.pickBtn  ->setBounds(pickX,     y, pickW,     kRowH - 4);
            r.deleteBtn->setBounds(delX,      y, delW,      kRowH - 4);
        }
        y += kRowH + kRowGap;
    }

    const int totalH = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    const int maxScroll = std::max(0, totalH - listH);
    if (scrollY_ > maxScroll)
    {
        scrollY_ = maxScroll;
        layoutRows();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void SoundSchedulePopup::resized()
{
    const int titleY = kPad + 4;
    titleLabel_   .setBounds(kPad, titleY,      kWidth - 2 * kPad, 22);
    subtitleLabel_.setBounds(kPad, titleY + 22, kWidth - 2 * kPad, 18);

    const int colHeaderY = kHeaderH - 22;
    headerStartLabel_.setBounds(kPad,       colHeaderY, 48, 14);
    headerDurLabel_  .setBounds(kPad + 54,  colHeaderY, 48, 14);
    headerLoopLabel_ .setBounds(kPad + 108, colHeaderY, 56, 14);
    headerGapLabel_  .setBounds(kPad + 170, colHeaderY, 44, 14);
    headerSoundLabel_.setBounds(kPad + 220, colHeaderY, 160, 14);

    emptyHintLabel_.setBounds(kPad, kHeaderH + 12, kListW - 2 * kPad, 32);

    // Right picker pane.
    const int pickerX = kListW + 8;
    const int pickerW = kWidth - pickerX - kPad;
    pickerLabel_.setBounds(pickerX, kPad + 6, pickerW, 16);
    picker_.setBounds(pickerX, kPad + 26,
                      pickerW, kHeight - kPad - 26 - kFooterH + 60);

    const int footerTop = kHeight - kFooterH + 6;
    hintLabel_.setBounds(kPad, footerTop, kWidth - 2 * kPad, 32);

    const int rowY    = footerTop + 36;
    const int actionH = 26;
    const int quarterW = (kWidth - 2 * kPad - 24) / 4;
    addButton_   .setBounds(kPad,                            rowY, quarterW, actionH);
    clearButton_ .setBounds(kPad + (quarterW + 8),           rowY, quarterW, actionH);
    cancelButton_.setBounds(kPad + 2 * (quarterW + 8),       rowY, quarterW, actionH);
    applyButton_ .setBounds(kPad + 3 * (quarterW + 8),       rowY, quarterW, actionH);

    layoutRows();
}

void SoundSchedulePopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.f, 1.f);

    g.setColour(juce::Colour(kAccentColor));
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kHeaderH - 4, kListW - 2 * kPad, 1);
    g.fillRect(kPad, kHeight - kFooterH - 2, kWidth - 2 * kPad, 1);

    // Vertical divider between list and picker pane.
    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kListW, kPad, 1, kHeight - kFooterH - kPad);

    const int listTop = kHeaderH;
    const int listBot = kHeight - kFooterH - 4;

    g.saveState();
    g.reduceClipRegion(0, listTop, kListW, listBot - listTop);

    int y = listTop - scrollY_;
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        const bool offscreen = (y + kRowH <= listTop || y >= listBot);
        if (!offscreen)
        {
            juce::Rectangle<float> rowR((float) kPad - 6.f, (float) y - 3.f,
                                        (float) (kListW - 2 * kPad) + 12.f,
                                        (float) (kRowH - 4) + 6.f);
            g.setColour(juce::Colour(i == (size_t) selectedRow_ ? kRowSelColor : kRowBgColor));
            g.fillRoundedRectangle(rowR, 5.f);
            g.setColour(juce::Colour(kRowBdColor));
            g.drawRoundedRectangle(rowR, 5.f, 1.f);
        }
        y += kRowH + kRowGap;
    }

    g.restoreState();

    emptyHintLabel_.setVisible(rows_.empty());

    const int listH = listBot - listTop;
    const int totalH = std::max(0, (int) rows_.size() * (kRowH + kRowGap));
    if (totalH > listH)
    {
        const float trackX = (float) (kListW - 7);
        const float trackY = (float) listTop + 2.f;
        const float trackH = (float) listH - 4.f;
        g.setColour(juce::Colour(0x33ffffff));
        g.fillRoundedRectangle(trackX, trackY, 3.f, trackH, 1.5f);

        const float thumbH = juce::jmax(24.f, trackH * (float) listH / (float) totalH);
        const float thumbY = trackY + (trackH - thumbH) * (float) scrollY_
                                    / (float) juce::jmax(1, totalH - listH);
        g.setColour(juce::Colour(0xffaac8e8).withAlpha(0.6f));
        g.fillRoundedRectangle(trackX, thumbY, 3.f, thumbH, 1.5f);
    }
}

bool SoundSchedulePopup::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        hide();
        if (onDismiss) onDismiss();
        return true;
    }
    return false;
}

void SoundSchedulePopup::mouseWheelMove(const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& wheel)
{
    if (e.x >= kListW) return;   // let the picker handle its own scrolling

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
