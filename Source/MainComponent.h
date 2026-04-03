#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.h
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "MathUtils.h"
#include "VoxelGrid.h"
#include "Camera.h"
#include "Raycaster.h"
#include "Renderer.h"
#include "Audio/AudioEngine.h"
#include "Audio/SoundScene.h"
#include <atomic>
#include <vector>

class MainComponent final
    : public juce::Component
    , public juce::OpenGLRenderer
{
public:
    MainComponent();
    ~MainComponent() override;

    // ── juce::OpenGLRenderer ─────────────────────────────────────────────────
    void newOpenGLContextCreated() override;
    void renderOpenGL()            override;
    void openGLContextClosing()    override;

    // ── juce::Component ──────────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized ()                override;

    void mouseDown    (const juce::MouseEvent&)                         override;
    void mouseUp      (const juce::MouseEvent&)                         override;
    void mouseDrag    (const juce::MouseEvent&)                         override;
    void mouseMove    (const juce::MouseEvent&)                         override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&)                 override;

    bool keyPressed  (const juce::KeyPress&) override;
    void focusGained (FocusChangeType)       override;

private:
    void processKeyboardMovement(float dt);
    void doRaycast(float mx, float my);
    bool isPanelHit(float x, float y) const;

    // ── OpenGL context ────────────────────────────────────────────────────────
    juce::OpenGLContext openGLContext;

    // ── Core subsystems ───────────────────────────────────────────────────────
    VoxelGrid   voxelGrid;
    Camera      camera;
    Renderer    renderer;
    AudioEngine audioEngine;
    SoundScene  soundScene;

    // ─────────────────────────────────────────────────────────────────────────
    // Pending voxel ops — used for Delete/Backspace (message → GL thread)
    // ─────────────────────────────────────────────────────────────────────────
    struct VoxelOp { enum Type { ADD, REMOVE } type; Vec3i pos; };
    std::vector<VoxelOp>  pendingOps;
    juce::CriticalSection opsMutex;

    // ─────────────────────────────────────────────────────────────────────────
    // Click requests — LMB place and RMB remove are queued so the GL thread
    // handles them with a consistent camera state (fixes missed placements).
    // ─────────────────────────────────────────────────────────────────────────
    struct ClickRequest
    {
        bool  active = false;
        float x = 0.f, y = 0.f;
        bool  shift = false;
    };
    ClickRequest          pendingPlace;
    ClickRequest          pendingRemove;
    juce::CriticalSection clickMutex;

    std::atomic<bool>     pendingClear { false };

    // ─────────────────────────────────────────────────────────────────────────
    // Mouse state  (written on message thread, read on GL thread)
    // ─────────────────────────────────────────────────────────────────────────
    struct MouseState
    {
        float curX = 0.f, curY = 0.f;
        float dX   = 0.f, dY   = 0.f;
        float rightDragDist = 0.f;
        bool  rightDown     = false;
        juce::Point<float> rightDownPos;
    };
    MouseState            mouse;
    juce::CriticalSection mouseMutex;

    // ─────────────────────────────────────────────────────────────────────────
    // Raycast  (GL thread only)
    // ─────────────────────────────────────────────────────────────────────────
    RaycastResult currentHit;
    bool          hasHit = false;
    Vec3f         currentRayDir;

    // ─────────────────────────────────────────────────────────────────────────
    // Shift-plane  –  hold Shift to place on a fixed Y level in mid-air.
    // Scroll wheel while Shift held raises / lowers the plane.
    // ─────────────────────────────────────────────────────────────────────────
    int   shiftPlaneY      = 0;
    Vec3i shiftPreviewPos  { 0, 0, 0 };
    bool  shiftPreviewValid = false;
    std::atomic<int> shiftScrollDelta { 0 };

    // X,Z column locked when Shift is first pressed — scroll only moves Y
    bool  shiftAnchorSet   = false;
    int   shiftAnchorX     = 0;
    int   shiftAnchorZ     = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Misc placement state
    // ─────────────────────────────────────────────────────────────────────────
    Vec3i lastPlacedPos { 0, 0, 0 };

    // ─────────────────────────────────────────────────────────────────────────
    // Audio: active clip for placement (set by 'L' key to load WAV)
    // ─────────────────────────────────────────────────────────────────────────
    juce::String activeClipId;
    std::unique_ptr<juce::FileChooser> fileChooser;
    void createSoundBlockAt(const Vec3f& pos);
    void removeSoundBlockAt(const Vec3i& voxelPos);

    // ─────────────────────────────────────────────────────────────────────────
    // Frame timing
    // ─────────────────────────────────────────────────────────────────────────
    double lastRenderTime = 0.0;

    // ─────────────────────────────────────────────────────────────────────────
    // HUD text  (built on GL thread, painted on message thread)
    // ─────────────────────────────────────────────────────────────────────────
    struct Hud { juce::String text; juce::CriticalSection lock; } hud;

    // ─────────────────────────────────────────────────────────────────────────
    // Block list  –  ordered by placement; displayed in the left-side panel
    // ─────────────────────────────────────────────────────────────────────────
    struct BlockEntry { int serial; Vec3i pos; };

    std::vector<BlockEntry> blockList;       ///< GL thread only
    int                     nextSerial = 1;

    std::vector<BlockEntry> blockListUI;     ///< Snapshot for paint()
    juce::CriticalSection   blockListMutex;

    // Panel UI state (message thread only — no mutex needed)
    bool blockListOpen   = true;
    int  blockListScroll = 0;

    static constexpr int kPanelW    = 200;
    static constexpr int kRowH      = 20;
    static constexpr int kHeaderH   = 26;
    static constexpr int kPanelTopY = 22;   ///< Starts below HUD bar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
