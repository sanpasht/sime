
#include "BlockEditPopup.h"

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
    styleLabel(startLabel,    "Start (s)");
    styleLabel(durationLabel, "Duration (s)");
    styleLabel(soundLabel,    "Sound ID");
    addAndMakeVisible(startLabel);
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(soundLabel);

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
    styleField(soundField,    "-0123456789");
    addAndMakeVisible(startField);
    addAndMakeVisible(durationField);
    addAndMakeVisible(soundField);

    // ── Buttons ───────────────────────────────────────────────────────────────
    applyButton .setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff3355cc));
    applyButton .setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancelButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff333344));
    cancelButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    applyButton .onClick = [this] { commit(); };
    cancelButton.onClick = [this] { hide();   if (onCancel) onCancel(); };

    startField   .onReturnKey = [this] { commit(); };
    durationField.onReturnKey = [this] { commit(); };
    soundField   .onReturnKey = [this] { commit(); };

    addAndMakeVisible(applyButton);
    addAndMakeVisible(cancelButton);

    // ── Register as a desktop component ──────────────────────────────────────
    // componentIsOpaque = false so we can draw a rounded rect with transparency.
    // windowIsTemporary = true so it doesn't appear in the taskbar.
    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowHasDropShadow);

    setVisible(false);
}

BlockEditPopup::~BlockEditPopup()
{
    // removeFromDesktop is called automatically by ~Component, but be explicit
    removeFromDesktop();
}

// ─────────────────────────────────────────────────────────────────────────────

void BlockEditPopup::showAt(int blockSerial, double startTime, double duration,
                             int soundId, juce::Point<int> screenPos)
{
    editingSerial = blockSerial;
    titleLabel.setText("Edit Block " + juce::String(blockSerial), juce::dontSendNotification);
    startField   .setText(juce::String(startTime, 3), false);
    durationField.setText(juce::String(duration,  3), false);
    soundField   .setText(juce::String(soundId),      false);

    // Position so the popup doesn't fall off screen edges
    auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    juce::Rectangle<int> screen = display ? display->userArea
                                          : juce::Rectangle<int>(0, 0, 1920, 1080);

    int px = screenPos.x + 16;
    int py = screenPos.y - kHeight / 2;
    px = juce::jlimit(screen.getX(), screen.getRight()  - kWidth,  px);
    py = juce::jlimit(screen.getY(), screen.getBottom() - kHeight, py);

    // For a desktop component, setBounds uses screen coordinates
    setBounds(px, py, kWidth, kHeight);
    setVisible(true);
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

    const double newStart    = startField   .getText().getDoubleValue();
    const double newDuration = std::max(0.01, durationField.getText().getDoubleValue());
    const int    newSoundId  = soundField   .getText().getIntValue();

    if (onCommit)
        onCommit(editingSerial, newStart, newDuration, newSoundId);

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

    auto row = [&](juce::Label& lbl, juce::TextEditor& fld)
    {
        lbl.setBounds(kPad,                   y, kLabelW,                        kRowH - 4);
        fld.setBounds(kPad + kLabelW + 6,     y, kWidth - kPad*2 - kLabelW - 6, kRowH - 4);
        y += kRowH;
    };

    row(startLabel,    startField);
    row(durationLabel, durationField);
    row(soundLabel,    soundField);

    y += 6;
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