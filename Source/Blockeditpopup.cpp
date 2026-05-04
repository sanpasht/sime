// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "BlockEditPopup.h"
#include "SoundLibrary.h"

namespace
{
    constexpr juce::uint32 kBgColor       = 0xf012141cu;
    constexpr juce::uint32 kBorderColor   = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor   = 0xff5b7ce6u;
    constexpr juce::uint32 kFieldBgColor  = 0xff181a24u;
    constexpr juce::uint32 kFieldBdColor  = 0xff2f3447u;
}

BlockEditPopup::BlockEditPopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    // ── Title + type badge ────────────────────────────────────────────────
    titleLabel.setText("Edit Block", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(14.f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0f2fa));
    addAndMakeVisible(titleLabel);

    typeBadge.setFont(juce::Font(10.f, juce::Font::bold));
    typeBadge.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(typeBadge);

    // ── Row labels ────────────────────────────────────────────────────────
    auto styleLabel = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(12.f));
        l.setColour(juce::Label::textColourId, juce::Colour(0xff8b94ad));
        l.setJustificationType(juce::Justification::centredRight);
    };
    styleLabel(startLabel,    "Start (s)");
    styleLabel(durationLabel, "Duration (s)");
    styleLabel(soundLabel,    "Sound");
    styleLabel(fileLabel,     "File");

    addAndMakeVisible(startLabel);
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(soundLabel);
    addAndMakeVisible(fileLabel);

    // ── Text editors ──────────────────────────────────────────────────────
    auto styleField = [](juce::TextEditor& f, const juce::String& allowed)
    {
        f.setFont(juce::Font(13.f));
        f.setColour(juce::TextEditor::backgroundColourId,        juce::Colour(kFieldBgColor));
        f.setColour(juce::TextEditor::textColourId,              juce::Colour(0xfff0f2fa));
        f.setColour(juce::TextEditor::outlineColourId,           juce::Colour(kFieldBdColor));
        f.setColour(juce::TextEditor::focusedOutlineColourId,    juce::Colour(kAccentColor));
        f.setColour(juce::TextEditor::highlightColourId,         juce::Colour(0xff3a5fbf));
        f.setSelectAllWhenFocused(true);
        f.setIndents(8, 4);
        f.setInputRestrictions(16, allowed);
    };
    styleField(startField,    "0123456789.");
    styleField(durationField, "0123456789.");
    addAndMakeVisible(startField);
    addAndMakeVisible(durationField);

    // ── Sound picker ──────────────────────────────────────────────────────
    addAndMakeVisible(soundPicker);
    soundPicker.onDoubleClick = [this](int) { commit(); };

    // ── File path field + Browse (for Custom blocks) ──────────────────────
    fileField.setFont(juce::Font(11.f));
    fileField.setColour(juce::TextEditor::backgroundColourId,    juce::Colour(kFieldBgColor));
    fileField.setColour(juce::TextEditor::textColourId,          juce::Colour(0xffaabbcc));
    fileField.setColour(juce::TextEditor::outlineColourId,       juce::Colour(kFieldBdColor));
    fileField.setIndents(8, 4);
    fileField.setReadOnly(true);
    addAndMakeVisible(fileField);

    browseButton.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff242a3c));
    browseButton.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffe2e6f2));
    browseButton.onClick = [this]
    {
        auto startDir = customFilePath_.isNotEmpty()
            ? juce::File(customFilePath_).getParentDirectory()
            : juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Select WAV file", startDir, "*.wav");

        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto results = fc.getResults();
                if (results.size() > 0 && results[0].existsAsFile())
                {
                    customFilePath_ = results[0].getFullPathName();
                    fileField.setText(customFilePath_, false);
                }
            });
    };
    addAndMakeVisible(browseButton);

    // ── Buttons ───────────────────────────────────────────────────────────
    applyButton .setColour(juce::TextButton::buttonColourId,    juce::Colour(kAccentColor));
    applyButton .setColour(juce::TextButton::textColourOffId,   juce::Colours::white);
    cancelButton.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff242a3c));
    cancelButton.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffe2e6f2));

    applyButton .onClick = [this] { commit(); };
    cancelButton.onClick = [this] { hide(); if (onCancel) onCancel(); };

    startField   .onReturnKey = [this] { commit(); };
    durationField.onReturnKey = [this] { commit(); };

    addAndMakeVisible(applyButton);
    addAndMakeVisible(cancelButton);

    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowHasDropShadow);

    setVisible(false);
}

BlockEditPopup::~BlockEditPopup()
{
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::setSoundLibrary(SoundLibrary* lib)
{
    library_ = lib;
    soundPicker.setLibrary(lib);
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::showAt(int blockSerial, BlockType type,
                             double startTime, double duration,
                             int /*soundId*/, const juce::String& customFile,
                             juce::Point<int> screenPos)
{
    editingSerial = blockSerial;
    editingType   = type;

    titleLabel.setText("Block #" + juce::String(blockSerial),
                       juce::dontSendNotification);

    auto badgeColor = blockTypeColor(type);
    typeBadge.setText(juce::String(blockTypeDisplayName(type)).toUpperCase(),
                      juce::dontSendNotification);
    typeBadge.setColour(juce::Label::backgroundColourId, badgeColor);
    typeBadge.setColour(juce::Label::textColourId,
                        badgeColor.getPerceivedBrightness() > 0.55f
                            ? juce::Colour(0xff1a1d26) : juce::Colours::white);

    startField   .setText(juce::String(startTime, 3), false);
    durationField.setText(juce::String(duration,  3), false);

    const bool isCustom = (type == BlockType::Custom);

    soundLabel  .setVisible(!isCustom);
    soundPicker .setVisible(!isCustom);
    fileLabel   .setVisible(isCustom);
    fileField   .setVisible(isCustom);
    browseButton.setVisible(isCustom);

    if (isCustom)
    {
        customFilePath_ = customFile;
        fileField.setText(customFile, false);
    }
    else
    {
        soundPicker.setBlockType(type);
        // If a library entry was previously assigned, customFile holds the
        // relative path — try to highlight it.
        int prevIdx = -1;
        if (library_ && customFile.isNotEmpty())
            prevIdx = library_->findByRelativePath(customFile);
        soundPicker.setSelectedEntry(prevIdx);
    }

    auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    juce::Rectangle<int> screen = display ? display->userArea
                                          : juce::Rectangle<int>(0, 0, 1920, 1080);

    int px = screenPos.x + 16;
    int py = screenPos.y - kHeight / 2;
    px = juce::jlimit(screen.getX(), screen.getRight()  - kWidth,  px);
    py = juce::jlimit(screen.getY(), screen.getBottom() - kHeight, py);

    setVisible(true);
    setBounds(px, py, kWidth, kHeight);
    resized();
    toFront(true);
    startField.grabKeyboardFocus();
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::hide()
{
    setVisible(false);
    editingSerial = -1;
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::commit()
{
    if (editingSerial < 0) return;

    const double newStart    = startField.getText().getDoubleValue();
    const double newDuration = std::max(0.01, durationField.getText().getDoubleValue());

    int          newSoundId  = -1;
    juce::String newCustomFile;

    if (editingType == BlockType::Custom)
    {
        newCustomFile = customFilePath_;
    }
    else
    {
        int idx = soundPicker.getSelectedEntry();
        if (idx >= 0 && library_ != nullptr)
        {
            // Pass the absolute path; ViewPortComponent::applyBlockEdit detects
            // it lives under Sounds/ and routes it through the library cache.
            newCustomFile = library_->at(idx).fullPath.getFullPathName();
        }
    }

    if (onCommit)
        onCommit(editingSerial, newStart, newDuration, newSoundId, newCustomFile);

    hide();
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Card background
    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 8.f);

    // Outer accent stroke
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.f, 1.f);

    // Top accent line in the active block-type color
    auto stripe = blockTypeColor(editingType);
    g.setColour(stripe);
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    // Header divider
    g.setColour(juce::Colour(0xff242a3c));
    g.fillRect(kPad, 50, kWidth - kPad * 2, 1);
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::resized()
{
    int y = kPad + 4;

    // Header: title on the left, color-coded type badge on the right
    titleLabel.setBounds(kPad, y, kWidth / 2, 22);

    int badgeW = juce::Font(10.f, juce::Font::bold)
                     .getStringWidth(typeBadge.getText()) + 18;
    badgeW = juce::jlimit(64, kWidth / 2 - kPad, badgeW);
    typeBadge.setBounds(kWidth - kPad - badgeW, y + 2, badgeW, 18);

    y += 36;

    const int fieldW = kWidth - kPad * 2 - kLabelW - 6;

    // start + duration share one row
    const int halfW = (fieldW - 6) / 2;
    startLabel   .setBounds(kPad, y, kLabelW, kRowH - 4);
    startField   .setBounds(kPad + kLabelW + 6, y, halfW, kRowH - 4);
    durationLabel.setBounds(kPad + kLabelW + 6 + halfW + 6, y - 18, halfW, 14);
    durationField.setBounds(kPad + kLabelW + 6 + halfW + 6, y, halfW, kRowH - 4);
    y += kRowH;

    if (soundPicker.isVisible())
    {
        soundLabel.setBounds(kPad, y, kLabelW, kRowH - 4);
        y += kRowH - 4;

        const int pickerH = kHeight - y - 36 - kPad - 8;
        soundPicker.setBounds(kPad, y, kWidth - kPad * 2, std::max(150, pickerH));
        y += soundPicker.getHeight() + 10;
    }
    else if (fileField.isVisible())
    {
        fileLabel.setBounds(kPad, y, kLabelW, kRowH - 4);
        const int browseW = 80;
        fileField   .setBounds(kPad + kLabelW + 6, y,
                               fieldW - browseW - 4, kRowH - 4);
        browseButton.setBounds(kPad + kLabelW + 6 + fieldW - browseW, y,
                               browseW, kRowH - 4);
        y += kRowH;
    }

    y = kHeight - kPad - 28;
    const int btnW = (kWidth - kPad * 2 - 8) / 2;
    cancelButton.setBounds(kPad,            y, btnW, 28);
    applyButton .setBounds(kPad + btnW + 8, y, btnW, 28);
}

// ─────────────────────────────────────────────────────────────────────────────

bool BlockEditPopup::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        hide();
        if (onCancel) onCancel();
        return true;
    }
    return false;
}
