#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ViewPortComponent.h  (updated to include audio playback architecture)
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "MathUtils.h"          
#include "BlockEntry.h"
#include "VoxelGrid.h"
#include "Camera.h"
#include "Raycaster.h"
#include "Renderer.h"
#include "TransportClock.h"
#include "SequencerEngine.h"
#include "AudioEngine.h"
#include "sidebarComponent.h"
#include "BlockEditPopup.h"

#include <atomic>
#include <vector>

class ViewPortComponent final
    : public juce::Component
    , public juce::OpenGLRenderer
{
public:
    ViewPortComponent();
    ~ViewPortComponent() override;

    // ── juce::OpenGLRenderer ─────────────────────────────────────────────────
    void newOpenGLContextCreated() override;
    void renderOpenGL()            override;   // ← transport.update() lives here
    void openGLContextClosing()    override;

    // ── juce::Component ──────────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized ()                override;

    std::function<void(bool)> onCollapsedChanged;

    void mouseDown    (const juce::MouseEvent&)                         override;
    void mouseUp      (const juce::MouseEvent&)                         override;
    void mouseDrag    (const juce::MouseEvent&)                         override;
    void mouseMove    (const juce::MouseEvent&)                         override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&)                 override;

    bool keyPressed  (const juce::KeyPress&) override;
    void focusGained (FocusChangeType)       override;

    void setSidebarComponent(SidebarComponent* s) { sidebar = s; }

    // ── Transport controls  (bind to toolbar buttons) ─────────────────────────
    void transportPlay()  { transport.start(); }
    void transportPause() { transport.pause(); }
    void transportStop()
    {
        transport.stop();
        SequencerEngine::resetAllBlocks(blockList);
    }


    // ── Edit mode API (called by MainComponent) ───────────────────────────────
 
    /// Fired when user clicks a block in edit mode.
    /// Args: serial, startTimeSec, durationSec, soundId, posInViewLocalCoords
    std::function<void(int, double, double, int, juce::Point<int>)> onRequestBlockEdit;


    /// Apply edited values back to the block (called from MainComponent via popup callback)
    void applyBlockEdit(int serial, double startTime, double duration, int soundId)
    {
        for (auto& b : blockList)
        {
            if (b.serial == serial)
            {
                b.startTimeSec = startTime;
                b.durationSec  = duration;
                b.soundId      = soundId;
                b.resetPlaybackState();
                break;
            }
        }
        selectedSerial = -1;
    }
 
    /// Clear the selected block highlight (called when popup is cancelled)
    void clearSelectedBlock()
    {
        selectedSerial = -1;
    }




private:
    // ── Private helpers ───────────────────────────────────────────────────────
    void processKeyboardMovement(float dt);
    void doRaycast(float mx, float my);
    bool isPanelHit(float x, float y) const;

    // =========================================================================
    // OpenGL context
    // =========================================================================
    juce::OpenGLContext openGLContext;

    // =========================================================================
    // Core rendering subsystems
    // =========================================================================
    VoxelGrid voxelGrid;
    Camera    camera;
    Renderer  renderer;

    // =========================================================================
    // Audio playback subsystems
    //
    // These three objects are the only additions required.  They are owned here
    // so they live as long as the viewport.  AudioEngine manages its own
    // juce::AudioDeviceManager internally (prototype approach).
    // =========================================================================
    TransportClock  transport;
    SequencerEngine sequencer;
    AudioEngine     audioEngine;

    /// Transport time from the previous frame — used to detect loop wraps.
    double prevTransportTime = 0.0;

    // =========================================================================
    // Pending voxel ops  (message → GL thread)
    // =========================================================================
    struct VoxelOp { enum Type { ADD, REMOVE } type; Vec3i pos; };
    std::vector<VoxelOp>  pendingOps;
    juce::CriticalSection opsMutex;

    // =========================================================================
    // Click requests  (message → GL thread)
    // =========================================================================
    struct ClickRequest
    {
        bool  active = false;
        float x = 0.f, y = 0.f;
        bool  shift = false;
    };
    ClickRequest          pendingPlace;
    ClickRequest          pendingRemove;
    juce::CriticalSection clickMutex;

    std::atomic<bool> pendingClear { false };

    // =========================================================================
    // Mouse state  (message thread writes, GL thread reads)
    // =========================================================================
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

    // =========================================================================
    // Raycast  (GL thread only)
    // =========================================================================
    RaycastResult currentHit;
    bool          hasHit = false;
    Vec3f         currentRayDir;

    // =========================================================================
    // Shift-plane
    // =========================================================================
    int   shiftPlaneY       = 0;
    Vec3i shiftPreviewPos   { 0, 0, 0 };
    bool  shiftPreviewValid = false;
    std::atomic<int> shiftScrollDelta { 0 };

    bool  shiftAnchorSet = false;
    int   shiftAnchorX   = 0;
    int   shiftAnchorZ   = 0;

    // =========================================================================
    // Placement state
    // =========================================================================
    Vec3i lastPlacedPos { 0, 0, 0 };

    // =========================================================================
    // Frame timing
    // =========================================================================
    double lastRenderTime = 0.0;   ///< juce::Time::getMillisecondCounterHiRes() at last frame

    // =========================================================================
    // HUD
    // =========================================================================
    struct Hud
    {
        juce::String     text;
        juce::Point<int> pos { 8, 3 };
        juce::CriticalSection lock;
    } hud;

    // =========================================================================
    // Block list
    //
    // Owned and mutated exclusively on the GL thread.
    // =========================================================================
    std::vector<BlockEntry> blockList;
    int                     nextSerial = 1;

    // =========================================================================
    // Sidebar / toggle
    // =========================================================================
    juce::TextButton  toggleButton { "☰" };
    bool              isSidebarCollapsed = false;
    SidebarComponent* sidebar = nullptr;


    // =========================================================================
    // Edit popup
    // =========================================================================
    bool             editMode = false;     ///< Toggled by E key
    int              selectedSerial = -1; ///< Serial of the block being edited

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewPortComponent)
};