#pragma once
#include <JuceHeader.h>
#include <vector>
#include "MathUtils.h"
#include <optional>
#include "BlockEntry.h"
#include "AudioAnalysis.h"
#include <functional>
#include <memory>

class MuteSchedulePopup;
class KeyframeEditorPopup;

struct AudioItem
{
    juce::String fileName;
    juce::String relativePath;
    juce::String fullPath;
};

class SidebarComponent : public juce::Component
{
public:
    struct AudioItem
    {
        juce::String fileName;
        juce::String relativePath;
        juce::String fullPath;
    };

    SidebarComponent();
    ~SidebarComponent() override;

    void paint(juce::Graphics&) override;
    // Initial state: sidebar is expanded, so show the close (✕) symbol.
    // Text is updated dynamically in onClick via CharPointer_UTF8.
    juce::TextButton toggleButton { juce::CharPointer_UTF8("\xe2\x9c\x95") };
    juce::TextButton AudioListButton { "Blocks" };
    juce::TextButton infoButton { "Info" };
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;
    std::function<void(bool)> onCollapsedChanged;

    void setCollapsed(bool shouldCollapse);
    void setAudioList(const std::vector<AudioItem>& newAudioFiles);
    bool isCollapsed() const { return collapsed; }
    bool isBlockPanelOpen() const { return blockPanelOpen; }
    bool isInfoPanelOpen() const { return infoPanelOpen; }

    void showBlockInfo(const BlockEntry& block, const juce::String& displayName = {});

    /// Called from MainComponent to resolve sample buffers for analysis.
    void setAudioAnalyzer(std::function<AudioAnalysisResult(const BlockEntry&)> fn)
    {
        audioAnalyzer_ = std::move(fn);
    }

    /// Drop the currently shown block info (used when the scene is cleared
    /// or a new scene is loaded — otherwise stale info from the previous
    /// scene keeps showing in the Info panel).
    void clearSelectedBlock();

    /// Clear the info panel only when it is showing this serial (e.g. block
    /// was deleted from the viewport or timeline).
    void clearSelectedBlockIfSerial(int serial);

    /// Copy of the block currently shown in the Info panel (if any).
    std::optional<BlockEntry> getSelectedBlockCopy() const { return selectedBlock_; }

    void setListenerSpatialReadout(const juce::String& line);
    void setBlockDistanceReadout(const juce::String& line);
    void setDistancePickActive(bool picking);

    /// Clears A->B distance readout, resets the Distance button, and cancels
    /// an in-progress pick.  Call when leaving the Info tab or clearing the
    /// scene so the button does not float over the Blocks list.
    void resetSpatialUi();

    /// User clicked Distance — pick block B in the viewport (A = current block).
    std::function<void(int anchorSerial)> onRequestDistancePick;
    /// Fired from resetSpatialUi() so the host can cancel the viewport pick.
    std::function<void()> onCancelDistancePick;

    /// Callback fired when the user clicks Apply.  Carries every editable
    /// field on the Info panel.  `playbackMode` is a uint8_t so the header
    /// doesn't have to pull in BlockEntry.h transitively for downstream
    /// callers that only forward the value.
    std::function<void(
        int                            serial,
        Vec3i                          newPos,
        double                         newStart,
        double                         newDuration,
        bool                           movementEnabled,
        uint8_t                        playbackMode,
        double                         movementDurationSec,
        int                            movementYOffset,
        bool                           isMuted,
        bool                           isHidden,
        double                         loopBufferSec,
        bool                           isLooping,
        double                         loopDurationSec,
        std::vector<MuteWindow>        muteWindows
    )> onApplyBlockInfo;

    /// Fired when the user clicks "Match Duration to Sound" in the Info panel.
    /// Receives the serial of the currently shown block; ViewPortComponent
    /// resolves the sample length and updates the region duration.
    std::function<void(int serial)> onMatchDurationToSound;

    /// Fired when the user clicks Apply inside the Keyframe Editor popup.
    /// The vector replaces the block's `recordedMovement`; an empty / single
    /// entry vector clears the path (engine treats <2 frames as no motion).
    std::function<void(int serial, std::vector<MovementKeyFrame> frames)>
        onApplyKeyframes;
    
private:
    bool collapsed = false;
    bool blockPanelOpen = true;
    bool infoPanelOpen = false;

    std::vector<AudioItem> AudioListUI;
    juce::CriticalSection AudioListMutex;

    bool AudioListOpen = true;
    int AudioListScroll = 0;

    /// Scroll offset for the Info panel content (rows / graphs between the
    /// tab header and the bottom Apply / Reset buttons).
    int infoScrollY_         = 0;
    int infoContentBottomY_  = 0;   ///< Updated by resized()
    int infoScrollAreaTop_   = 0;
    int infoScrollAreaBot_   = 0;

    static constexpr int kRowH = 20;
    static constexpr int kHeaderH = 26;
    static constexpr int kPanelTopY = 30;

    std::optional<BlockEntry> selectedBlock_;
    std::optional<BlockEntry> originalBlock_;
    juce::String              selectedDisplayName_;

    juce::TextEditor xEditor;
    juce::TextEditor yEditor;
    juce::TextEditor zEditor;
    juce::TextEditor startEditor;
    juce::TextEditor durationEditor;

    juce::ToggleButton movementEnabledToggle { "Enable Recorded Movement" };

    // Opens the floating Keyframe Editor so the user can author / edit
    // position keyframes directly (alternative to Alt-drag recording).
    juce::TextButton keyframesBtn_ { "Keyframes..." };

    /// Working copy of the position keyframes for the block currently shown
    /// in the Info panel.  Edited by KeyframeEditorPopup and pushed via
    /// onApplyKeyframes when the user clicks Apply inside the popup.
    std::vector<MovementKeyFrame> keyframesDraft_;

    /// Lazily-created popup so we don't pay the cost for users who never
    /// open the Info panel.
    std::unique_ptr<KeyframeEditorPopup> keyframeEditorPopup_;

    // Phase 1 movement controls
    juce::ComboBox    modeCombo_;                            ///< Playback mode
    juce::TextEditor  movementDurationEditor_;               ///< 0 = use region duration
    juce::TextEditor  pathYOffsetEditor_;                    ///< Lift the recorded path

    // Loop controls (moved from BlockEditPopup so they live next to the gap)
    juce::ToggleButton loopToggle_       { "Loop sound" };
    juce::TextEditor   loopDurationEditor_;                  ///< 0 = full region
    // Short label so the button never truncates inside the sidebar's narrow
    // editor column.  Tooltip still describes the action in full.
    juce::TextButton   matchLoopDurBtn_  { "= Block" };
    juce::TextEditor   loopBufferEditor_;                    ///< Silence between repeats (s)

    // Per-block flags
    juce::ToggleButton muteToggle_   { "Mute (no audio, forever)" };
    juce::ToggleButton hideToggle_   { "Hide block in viewport" };

    // Opens the floating MuteSchedulePopup so the user can add / edit /
    // remove timed mute windows for the selected block.
    juce::TextButton  muteScheduleBtn_ { "Mute Schedule..." };

    /// Working copy of the scheduled mute windows for the block currently
    /// shown in the Info panel.  Edited live by the MuteSchedulePopup and
    /// pushed downstream when the user clicks Apply.
    std::vector<MuteWindow> muteWindowsDraft_;

    /// Lazily-created popup window so we don't construct it for users who
    /// never open the Info panel.
    std::unique_ptr<MuteSchedulePopup> muteSchedulePopup_;

    // One-click: match the block's region duration to the loaded sample length
    juce::TextButton matchSoundDurBtn_ { "Match Duration to Sound" };

    juce::TextButton distanceBtn_ { "Distance..." };

    juce::String spatialListenerLine_;
    juce::String spatialDistanceLine_;
    bool         distancePickActive_ = false;

    juce::TextButton applyButton      { "Apply" };
    juce::TextButton resetDefaultsBtn_{ "Reset to Default" };
    void movementGraph(juce::Graphics& g, const BlockEntry& selectedBlock, juce::Rectangle<int> graphArea);
    void audioWaveformGraph(juce::Graphics& g, juce::Rectangle<int> area) const;

    std::function<AudioAnalysisResult(const BlockEntry&)> audioAnalyzer_;
    AudioAnalysisResult audioAnalysis_;

    class TabLookAndFeel : public juce::LookAndFeel_V4
    {
        public:
        void drawButtonBackground(juce::Graphics&,
            juce::Button&,
            const juce::Colour&,
            bool,
            bool) override
            {
                // Prevent JUCE from drawing default button background/border
            }
    };
        
    TabLookAndFeel tabLookAndFeel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)

};