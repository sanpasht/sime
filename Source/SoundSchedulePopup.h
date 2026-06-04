#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SoundSchedulePopup.h
//
// Floating editor for a block's `soundSchedule` — a list of timed sounds the
// block fires in addition to its main region.  Each entry has a start time, a
// play duration, and a sound (which must belong to the block's instrument
// type, chosen from the right-hand picker).  Mirrors MuteSchedulePopup but adds
// a per-entry sound selection.
//
// Usage
//   popup->setSchedule(serial, "Violin 1", BlockType::Violin,
//                      &library, ensureLoadedFn, schedule);
//   popup->onApply = [](int serial, std::vector<SoundEvent> s) { ... };
//   popup->showAt({ screenX, screenY });
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockEntry.h"
#include "BlockType.h"
#include "SoundPickerComponent.h"
#include <functional>
#include <vector>

class SoundLibrary;

class SoundSchedulePopup : public juce::Component
{
public:
    /// Called on Apply with the edited list (entries without a chosen sound are
    /// dropped).  Each SoundEvent carries soundId + relativePath, ready to play
    /// and to persist.
    std::function<void(int blockSerial, std::vector<SoundEvent>)> onApply;

    /// Called when the popup is dismissed without applying.
    std::function<void()> onDismiss;

    SoundSchedulePopup();
    ~SoundSchedulePopup() override;

    /// Replace the working list.  `ensureLoadedFn` registers a library entry
    /// index and returns its runtime soundId (-1 on failure).
    void setSchedule(int blockSerial,
                     const juce::String& blockName,
                     BlockType blockType,
                     SoundLibrary* library,
                     std::function<int(int entryIdx)> ensureLoadedFn,
                     const std::vector<SoundEvent>& schedule);

    void showAt(juce::Point<int> screenPos);
    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    struct EntryRow
    {
        std::unique_ptr<juce::TextEditor> start;
        std::unique_ptr<juce::TextEditor> duration;
        std::unique_ptr<juce::TextButton> loopBtn;     ///< toggles this sound's loop
        std::unique_ptr<juce::TextEditor> gap;         ///< loop gap (s)
        std::unique_ptr<juce::TextButton> pickBtn;     ///< selects this row for the picker
        std::unique_ptr<juce::TextButton> deleteBtn;
    };

    void rebuildRows();
    void pullValuesFromRows();
    void layoutRows();
    void assignSoundToSelectedRow(int entryIdx);
    juce::String soundNameFor(const SoundEvent& se) const;

    int                       editingSerial_ = -1;
    juce::String              editingName_;
    BlockType                 blockType_     = BlockType::Violin;
    SoundLibrary*             library_       = nullptr;
    std::function<int(int)>   ensureLoadedFn_;

    std::vector<SoundEvent>   draft_;
    std::vector<EntryRow>     rows_;
    int                       selectedRow_ = -1;

    juce::Label               titleLabel_;
    juce::Label               subtitleLabel_;
    juce::Label               headerStartLabel_;
    juce::Label               headerDurLabel_;
    juce::Label               headerLoopLabel_;
    juce::Label               headerGapLabel_;
    juce::Label               headerSoundLabel_;
    juce::Label               hintLabel_;
    juce::Label               emptyHintLabel_;
    juce::Label               pickerLabel_;
    SoundPickerComponent      picker_;
    juce::TextButton          addButton_     { "+ Add Sound" };
    juce::TextButton          clearButton_   { "Clear All" };
    juce::TextButton          applyButton_   { "Apply" };
    juce::TextButton          cancelButton_  { "Cancel" };

    int                       scrollY_           = 0;

    // Two-pane layout: left list + right picker.
    static constexpr int kWidth    = 820;
    static constexpr int kHeight   = 500;
    static constexpr int kPad      = 16;
    static constexpr int kHeaderH  = 90;
    static constexpr int kRowH     = 30;
    static constexpr int kRowGap   = 8;
    static constexpr int kFooterH  = 96;
    static constexpr int kListW    = 470;   ///< width of the left (list) pane

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundSchedulePopup)
};
