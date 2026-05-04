#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.h
//
// Floating editor that surfaces the block's serial, type, timing, and an
// embedded SoundPickerComponent for choosing a sample from the SoundLibrary.
// Falls back to a "Browse..." button for blocks of type Custom (user WAV).
//
// Uses addToDesktop() so the popup gets its own native OS window and appears
// on top of the OpenGL context.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockType.h"
#include "SoundPickerComponent.h"

class SoundLibrary;

class BlockEditPopup : public juce::Component
{
public:
    /// Commit returns: serial, start, duration, soundId, customFilePath
    /// soundId is left at -1 when the user picked from the library (the path
    /// in customFilePath uniquely identifies the sound, and ViewPortComponent
    /// resolves it to a runtime soundId via SoundLibrary).
    std::function<void(int, double, double, int, const juce::String&)> onCommit;
    std::function<void()> onCancel;

    BlockEditPopup();
    ~BlockEditPopup() override;

    void setSoundLibrary(SoundLibrary* lib);

    void showAt(int blockSerial, BlockType type,
                double startTime, double duration,
                int soundId, const juce::String& customFile,
                juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void commit();

    int       editingSerial = -1;
    BlockType editingType   = BlockType::Violin;

    juce::Label      titleLabel;
    juce::Label      typeLabel,     typeValueLabel;
    juce::Label      startLabel,    durationLabel;
    juce::TextEditor startField,    durationField;

    // Library-backed instrument selector (search + scrollable list)
    juce::Label              soundLabel;
    SoundPickerComponent     soundPicker;
    SoundLibrary*            library_ = nullptr;

    // Custom file selector
    juce::Label      fileLabel;
    juce::TextEditor fileField;
    juce::TextButton browseButton { "Browse..." };
    juce::String     customFilePath_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    juce::TextButton applyButton  { "Apply"  };
    juce::TextButton cancelButton { "Cancel" };

    static constexpr int kWidth  = 380;
    static constexpr int kHeight = 460;
    static constexpr int kPad    = 12;
    static constexpr int kRowH   = 28;
    static constexpr int kLabelW = 76;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockEditPopup)
};
