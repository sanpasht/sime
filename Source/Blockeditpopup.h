#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEditPopup.h
//
// Floating editor for a placed block.  Surfaces:
//   * type + serial header (color-coded)
//   * start / duration text fields
//   * embedded SoundPickerComponent for library-backed types
//   * Browse… file picker for Custom blocks
//   * Loop toggle + loop-duration field
//
// Uses addToDesktop() so the popup gets its own native OS window and appears
// on top of the OpenGL context, which owns a native child window that normal
// JUCE components cannot paint over.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockType.h"
#include "SoundPickerComponent.h"

class SoundLibrary;

class BlockEditPopup : public juce::Component
{
public:
    /// Commit returns: serial, start, duration, soundId, customFilePath,
    ///                 isLooping, loopDurationSec
    /// soundId is left at -1 when the user picked from the library; the
    /// (absolute) path in customFilePath uniquely identifies the sound and
    /// ViewPortComponent resolves it to a runtime soundId via SoundLibrary.
    std::function<void(int, double, double, int, const juce::String&,
                       bool, double)> onCommit;
    std::function<void()> onCancel;

    BlockEditPopup();
    ~BlockEditPopup() override;

    void setSoundLibrary(SoundLibrary* lib);

    void showAt(int blockSerial, BlockType type,
                double startTime, double duration,
                int soundId, const juce::String& customFile,
                bool isLooping, double loopDurationSec,
                juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void commit();
    void updateLoopFieldEnabled();   ///< Greys out loop duration when toggle is off

    int       editingSerial = -1;
    BlockType editingType   = BlockType::Violin;

    juce::Label      titleLabel;
    juce::Label      typeBadge;          ///< small color-coded "VIOLIN" pill in the header
    juce::Label      startLabel,    durationLabel;
    juce::TextEditor startField,    durationField;

    juce::Label              soundLabel;
    SoundPickerComponent     soundPicker;
    SoundLibrary*            library_ = nullptr;

    juce::Label      fileLabel;
    juce::TextEditor fileField;
    juce::TextButton browseButton { "Browse…" };
    juce::String     customFilePath_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    // ── Loop controls ─────────────────────────────────────────────────────────
    juce::ToggleButton loopButton    { "Loop" };
    juce::Label        loopDurationLabel;
    juce::TextEditor   loopDurationField;

    juce::TextButton applyButton  { "Apply"  };
    juce::TextButton cancelButton { "Cancel" };

    static constexpr int kWidth  = 440;
    static constexpr int kHeight = 560;   // bumped from 520 to fit the loop row
    static constexpr int kPad    = 14;
    static constexpr int kRowH   = 28;
    static constexpr int kLabelW = 80;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockEditPopup)
};
