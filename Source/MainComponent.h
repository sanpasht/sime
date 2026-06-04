#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "SoundSchedulePopup.h"
#include "TransportBarComponent.h"
#include "BlockType.h"
#include "MovementConfirmPopup.h"
#include "StartupMenuComponent.h"
#include "SceneAudioExporter.h"
#include "CameraPathPopup.h"
#include "HelpPopup.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    void saveScene(const juce::String& path = {});
    void openScene();
    void newScene();
    void autoSave();
    void loadSceneFromFile(const juce::String& path);

private:
    // ── Startup menu ──────────────────────────────────────────────────────────
    StartupMenuComponent startupMenu_;
    bool                 showingStartup_ = true;
    void                 dismissStartupMenu();

    // ── Main app components ───────────────────────────────────────────────────
    ViewPortComponent     view;
    SidebarComponent      sidebar;
    BlockEditPopup        editPopup;
    std::unique_ptr<SoundSchedulePopup> soundSchedulePopup_;
    TransportBarComponent transportBar;
    std::unique_ptr<MovementConfirmPopup> movementPopup;
    bool isSidebarCollapsed = false;

    // ── User-resizable sidebar ────────────────────────────────────────────────
    int  sidebarWidth_ = 220;               ///< current expanded width (px)
    static constexpr int kSidebarMinW = 200;
    static constexpr int kSidebarMaxW = 520;

    /// Thin vertical strip on the sidebar's right edge.  Dragging it resizes the
    /// sidebar (Cursor-style).  Reports the desired width back to MainComponent.
    class SidebarResizer : public juce::Component
    {
    public:
        std::function<void(int deltaX)> onDrag;   ///< pixels moved since drag start
        std::function<void()>           onDragStart;
        SidebarResizer() { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }
        void mouseDown(const juce::MouseEvent&) override { if (onDragStart) onDragStart(); }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (onDrag) onDrag(e.getDistanceFromDragStartX());
        }
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff202434));
            g.setColour(juce::Colour(0x40ffffff));
            const float cx = getWidth() * 0.5f;
            g.fillRect(cx - 0.5f, getHeight() * 0.5f - 9.f, 1.0f, 18.f);
        }
    } sidebarResizer_;
    int sidebarWidthAtDragStart_ = 220;

    // ── Block type toolbar ────────────────────────────────────────────────────
    // Single grouped ComboBox listing all 23 BlockTypes, plus a color "pill"
    // that visually echoes the active selection.
    juce::ComboBox blockTypeCombo;

    /// Compact color-swatch + active type name shown left of the combo.
    class TypePill : public juce::Component
    {
    public:
        void setActive(BlockType t) { type_ = t; repaint(); }
        void paint(juce::Graphics& g) override;
    private:
        BlockType type_ = BlockType::Violin;
    } typePill_;

    BlockType activeType_ = BlockType::Violin;
    void setActiveBlockType(BlockType t);
    void rebuildBlockTypeCombo();
    void syncComboToActive();

    // ── View toggle buttons (top toolbar) ────────────────────────────────────
    // Floor = XZ plane (y=0).  WallX = YZ plane (x=0).  WallZ = XY plane (z=0).
    // Plane buttons are labelled by the two axes that lie INSIDE the plane,
    // matching common 3D-software convention:
    //   * Floor   = XZ plane (y = 0)         – horizontal ground
    //   * YZ Wall = vertical wall at x = 0   – contains the Y and Z axes
    //   * XY Wall = vertical wall at z = 0   – contains the X and Y axes
    juce::TextButton layersMenuBtn_ { juce::String("Layers ") + juce::String::fromUTF8("\xe2\x96\xbe") };
    juce::TextButton dopplerBtn_     { "Doppler" };
    juce::TextButton anchorBtn_      { "Anchor" };
    juce::TextButton pathEditBtn_    { "Path..." };
    juce::TextButton freeCamBtn_     { "Free Cam" };
    juce::TextButton freezeMovBtn_   { "Freeze Move" };
    juce::TextButton helpBtn_        { "Help" };

    // ── Selected-block audition (enabled only when a block is selected) ───────
    juce::TextButton auditionBtn_       { "Play" };    ///< one-shot preview of the selected block
    juce::TextButton auditionInTimeBtn_ { "@Time" };   ///< seek to block start + solo-play in context
    juce::Slider     spatialSensSlider_;

    std::unique_ptr<CameraPathPopup> cameraPathPopup_;

    void configureToggleButton(juce::TextButton& b);
    void refreshSpatialSidebarReadout();
    void showCameraPathPopup();
    void showHelpPopup();
    void showLayersMenu();

    // ── File / View / Mute menus (DAW-style) ────────────────────────────────
    juce::TextButton fileMenuBtn_;
    juce::TextButton viewMenuBtn_;
    juce::TextButton muteMenuBtn_;

    void showViewMenu();
    void showMuteMenu();

    juce::String currentFilePath_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    void showFileMenu();
    void handleFileMenu(int result);
    void showExportAudioDialog();
    void launchExportSaveChooser(SceneAudioExporter::Format format);

    static constexpr int kToolbarH = 34;
    void showMovementConfirmPopup(int serial, double duration,
                                  const std::vector<MovementKeyFrame>& keyframes,
                                  juce::Point<int> position);

    void setPlaybackUiState(bool playing, bool paused, double currentTime);
    void stopPlaybackAndResetUi();
    void playSelectedBlockInTime();   ///< "Play @Time": seek to block start + solo-play
    void timerCallback() override;

    // ── Title bar + dirty tracking ────────────────────────────────────────────
    bool hasUnsavedChanges_  = false;
    bool suppressNextDirty_  = false;   ///< Set before load/new to ignore the
                                         ///  onBlockListChanged that follows them.
    void updateWindowTitle();
    void markDirty();                    ///< Set dirty + refresh title bar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
