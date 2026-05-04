// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "BlockEditPopup.h"
#include "SoundLibrary.h"

BlockEditPopup::BlockEditPopup()
{
    setSize(kWidth, kHeight);
    setWantsKeyboardFocus(true);

    // ── Title ─────────────────────────────────────────────────────────────────
    titleLabel.setText("Edit Block", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(13.f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffccddff));
    addAndMakeVisible(titleLabel);

    // ── Row labels ────────────────────────────────────────────────────────────
    auto styleLabel = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(12.f));
        l.setColour(juce::Label::textColourId, juce::Colour(0xffaaaacc));
        l.setJustificationType(juce::Justification::centredRight);
    };
    styleLabel(typeLabel,     "Type");
    styleLabel(startLabel,    "Start (s)");
    styleLabel(durationLabel, "Duration (s)");
    styleLabel(soundLabel,    "Sound");
    styleLabel(fileLabel,     "File");

    addAndMakeVisible(typeLabel);
    addAndMakeVisible(startLabel);
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(soundLabel);
    addAndMakeVisible(fileLabel);

    // ── Type value (read-only) ────────────────────────────────────────────────
    typeValueLabel.setFont(juce::Font(12.f, juce::Font::bold));
    typeValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xffeeeeff));
    addAndMakeVisible(typeValueLabel);

    // ── Text editors ──────────────────────────────────────────────────────────
    auto styleField = [](juce::TextEditor& f, const juce::String& allowed)
    {
        f.setFont(juce::Font(12.f));
        f.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(0xff1e2030));
        f.setColour(juce::TextEditor::textColourId,           juce::Colour(0xffeeeeff));
        f.setColour(juce::TextEditor::outlineColourId,        juce::Colour(0xff3344aa));
        f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff6677ff));
        f.setSelectAllWhenFocused(true);
        f.setInputRestrictions(16, allowed);
    };
    styleField(startField,    "0123456789.");
    styleField(durationField, "0123456789.");
    addAndMakeVisible(startField);
    addAndMakeVisible(durationField);

    // ── Sound picker (library-backed) ─────────────────────────────────────────
    addAndMakeVisible(soundPicker);

    // ── File path field + Browse (for Custom blocks) ──────────────────────────
    fileField.setFont(juce::Font(11.f));
    fileField.setColour(juce::TextEditor::backgroundColourId,     juce::Colour(0xff1e2030));
    fileField.setColour(juce::TextEditor::textColourId,           juce::Colour(0xffaabbcc));
    fileField.setColour(juce::TextEditor::outlineColourId,        juce::Colour(0xff3344aa));
    fileField.setReadOnly(true);
    addAndMakeVisible(fileField);

    browseButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a3355));
    browseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
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

    // ── Buttons ───────────────────────────────────────────────────────────────
    applyButton .setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3355cc));
    applyButton .setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancelButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff333344));
    cancelButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

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

    titleLabel.setText("Edit " + juce::String(blockTypeDisplayName(type))
                       + " Block " + juce::String(blockSerial),
                       juce::dontSendNotification);
    typeValueLabel.setText(blockTypeDisplayName(type), juce::dontSendNotification);

    startField   .setText(juce::String(startTime, 3), false);
    durationField.setText(juce::String(duration,  3), false);

    // Show/hide library picker vs. custom file picker.
    const bool isCustom   = (type == BlockType::Custom);
    const bool isListener = (type == BlockType::Listener);

    soundLabel  .setVisible(!isCustom && !isListener);
    soundPicker .setVisible(!isCustom && !isListener);
    fileLabel   .setVisible(isCustom);
    fileField   .setVisible(isCustom);
    browseButton.setVisible(isCustom);

    if (isCustom)
    {
        customFilePath_ = customFile;
        fileField.setText(customFile, false);
    }
    else if (!isListener)
    {
        soundPicker.setBlockType(type);
        // If a library entry was previously assigned, customFile holds the
        // relative path — try to highlight it.
        int prevIdx = -1;
        if (library_ && customFile.isNotEmpty())
            prevIdx = library_->findByRelativePath(customFile);
        soundPicker.setSelectedEntry(prevIdx);
    }

    // Position on screen
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
    else if (editingType != BlockType::Listener)
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
    g.setColour(juce::Colour(0xf013151f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);

    g.setColour(juce::Colour(0xff3344aa));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.f, 1.f);

    g.setColour(juce::Colour(0xff3344aa));
    g.fillRect(kPad, 28, kWidth - kPad * 2, 1);
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::resized()
{
    int y = kPad;
    titleLabel.setBounds(kPad, y, kWidth - kPad * 2, 18);
    y += 22;

    const int fieldW = kWidth - kPad * 2 - kLabelW - 6;

    auto row = [&](juce::Label& lbl, juce::Component& fld)
    {
        lbl.setBounds(kPad,                y, kLabelW, kRowH - 4);
        fld.setBounds(kPad + kLabelW + 6,  y, fieldW,  kRowH - 4);
        y += kRowH;
    };

    row(typeLabel,     typeValueLabel);
    row(startLabel,    startField);
    row(durationLabel, durationField);

    // Library picker (instrument blocks) or File browser (custom blocks)
    if (soundPicker.isVisible())
    {
        // Picker spans the full width, growing to fill remaining space.
        soundLabel.setBounds(kPad, y, kLabelW, kRowH - 4);
        y += kRowH - 4;
        const int pickerH = kHeight - y - 36 - kPad - 6;
        soundPicker.setBounds(kPad, y, kWidth - kPad * 2, std::max(120, pickerH));
        y += soundPicker.getHeight() + 8;
    }
    else if (fileField.isVisible())
    {
        fileLabel.setBounds(kPad, y, kLabelW, kRowH - 4);
        const int browseW = 70;
        fileField.setBounds(kPad + kLabelW + 6, y, fieldW - browseW - 4, kRowH - 4);
        browseButton.setBounds(kPad + kLabelW + 6 + fieldW - browseW, y, browseW, kRowH - 4);
        y += kRowH;
    }

    y = kHeight - kPad - 26;
    const int btnW = (kWidth - kPad * 2 - 6) / 2;
    applyButton .setBounds(kPad,            y, btnW, 26);
    cancelButton.setBounds(kPad + btnW + 6, y, btnW, 26);
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
