#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.h  (revised)
//
// Uses addToDesktop() so the popup gets its own native OS window and appears
// on top of the OpenGL context, which owns a native child window that normal
// JUCE components cannot paint over.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>

class BlockEditPopup : public juce::Component
{
public:
    std::function<void(int, double, double, int)> onCommit;
    std::function<void()>                         onCancel;

    BlockEditPopup();
    ~BlockEditPopup() override;

    // screenPos is in SCREEN coordinates (not component-local).
    // Call getScreenPosition() or localPointToGlobal() before passing in.
    void showAt(int blockSerial, double startTime, double duration,
                int soundId, juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void commit();

    int editingSerial = -1;

    juce::Label      titleLabel;
    juce::Label      startLabel,     durationLabel,     soundLabel;
    juce::TextEditor startField,     durationField,     soundField;
    juce::TextButton applyButton  { "Apply"  };
    juce::TextButton cancelButton { "Cancel" };

    static constexpr int kWidth  = 220;
    static constexpr int kHeight = 190;
    static constexpr int kPad    = 12;
    static constexpr int kRowH   = 28;
    static constexpr int kLabelW = 76;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockEditPopup)
};