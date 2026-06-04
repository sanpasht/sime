#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// KeyframeEditorPopup.h
//
// Floating editor for a block's `recordedMovement` path.  Lets the user
// add, edit, and remove position keyframes by hand instead of (or in
// addition to) Alt-drag recording.  Each keyframe is
//
//   { timeSec : double,   position : Vec3i }
//
// where `timeSec` is relative to the block's start time.  When applied,
// the list replaces `BlockEntry::recordedMovement` and the engine plays
// it back exactly the same way as a recorded path — so this popup is the
// "clean up the recording" tool the engine has always wanted.
//
// Usage
//   popup->setKeyframes(blockSerial, "Violin 1", currentBlockPos, frames);
//   popup->onApply = [](int serial, std::vector<MovementKeyFrame> kfs) { ... };
//   popup->showAt({ screenX, screenY });
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "BlockEntry.h"
#include <functional>
#include <memory>
#include <vector>

class KeyframeEditorPopup : public juce::Component
{
public:
    /// Called when the user clicks Apply.  Receives the sorted, cleaned
    /// list ready for `ViewPortComponent::applyMovementKeyframes`.
    std::function<void(int blockSerial, std::vector<MovementKeyFrame>)> onApply;

    /// Fired when the popup closes without applying.  The sidebar uses
    /// this to drop its working draft if the user cancels.
    std::function<void()> onDismiss;

    KeyframeEditorPopup();
    ~KeyframeEditorPopup() override;

    /// Replace the working list and reset scroll.
    ///   @p anchorPos – the block's current world position; used as the
    ///                  default for new rows so "+ Add Keyframe" doesn't
    ///                  drop blocks at the origin.
    void setKeyframes(int blockSerial,
                      const juce::String& blockName,
                      Vec3i anchorPos,
                      const std::vector<MovementKeyFrame>& frames);

    /// Show the popup near the given screen coordinates.  Clamps to the
    /// primary display so we never open off-screen.
    void showAt(juce::Point<int> screenPos);

    void hide();

    void paint   (juce::Graphics&) override;
    void resized () override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;

private:
    /// One editable keyframe row.
    struct KeyRow
    {
        std::unique_ptr<juce::TextEditor> time;
        std::unique_ptr<juce::TextEditor> x;
        std::unique_ptr<juce::TextEditor> y;
        std::unique_ptr<juce::TextEditor> z;
        std::unique_ptr<juce::TextButton> deleteBtn;
    };

    void rebuildRows();
    void pullValuesFromRows();
    void layoutRows();
    void scrollToBottom();

    int                            editingSerial_ = -1;
    juce::String                   editingName_;
    Vec3i                          anchorPos_;
    std::vector<MovementKeyFrame>  draft_;
    /// Pristine path captured in setKeyframes().  The interval selector always
    /// resamples from this so switching intervals (incl. back to "Off") is
    /// lossless and never compounds rounding.
    std::vector<MovementKeyFrame>  originalFrames_;
    std::vector<KeyRow>            rows_;

    juce::Label      titleLabel_;
    juce::Label      subtitleLabel_;
    juce::Label      headerTimeLabel_;
    juce::Label      headerXLabel_;
    juce::Label      headerYLabel_;
    juce::Label      headerZLabel_;
    juce::Label      hintLabel_;
    juce::Label      emptyHintLabel_;
    juce::TextButton addButton_   { "+ Add Keyframe" };
    juce::TextButton clearButton_ { "Clear All" };
    juce::TextButton applyButton_ { "Apply" };
    juce::TextButton cancelButton_{ "Cancel" };

    // Time granularity selector — rounds row times to a multiple of the
    // chosen interval.  Default is "Off" so existing scenes look unchanged.
    juce::Label    snapLabel_;
    juce::ComboBox snapBox_;
    void           applySnapToDraft();

    int scrollY_ = 0;

    static constexpr int kWidth   = 540;
    static constexpr int kHeight  = 520;
    static constexpr int kPad     = 16;
    // Header band: title (24) + subtitle (18) + spacing (8) + column header (16) + spacing (8)
    static constexpr int kHeaderH = 90;
    static constexpr int kRowH    = 30;
    static constexpr int kRowGap  = 8;
    static constexpr int kFooterH = 124;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyframeEditorPopup)
};
