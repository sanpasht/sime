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

#include "BlockType.h"
#include "SceneFile.h"
#include "SoundLibrary.h"
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

    /// Set the block type used for future placements (called from toolbar).
    void setActiveBlockType(BlockType t) { activeBlockType_.store(static_cast<int>(t)); }

    // ── Edit mode API (called by MainComponent) ───────────────────────────────

    std::optional<BlockEntry> getBlockBySerial(int serial) const;
    void applySidebarBlockInfo(int serial, Vec3i pos, double start, double duration, bool movementEnabled);

    /// Fired when user clicks a block in edit mode.
    /// Args: serial, blockType, start, dur, soundId, customFilePath,
    ///       isLooping, loopDurationSec, viewLocalPos
    std::function<void(int, BlockType, double, double, int,
                       const juce::String&, bool, double,
                       juce::Point<int>)> onRequestBlockEdit;
    
    void updateBlockTiming(int serial, double startTime, double duration);

    /// Apply edited values back to the block.
    /// Safe to call from the message thread — queues the edit for the GL thread.
    ///
    /// customFile may be:
    ///   * empty                            -> use the existing soundId
    ///   * an absolute path under Sounds/   -> resolved via SoundLibrary
    ///                                         (lazy WAV load + cached)
    ///   * any other absolute path           -> loaded as a one-off Custom WAV
    void applyBlockEdit(int serial, double startTime, double duration,
                        int soundId, const juce::String& customFile,
                        bool isLooping, double loopDurationSec)
    {
        int resolvedSoundId = soundId;
        std::string resolvedPath;

        if (customFile.isNotEmpty())
        {
            juce::File wav(customFile);
            // Must match the folder used when SoundLibrary was loaded (see
            // contentRoot_ set in the cpp ctor — not raw current-working-directory).
            juce::File libRoot = contentRoot_.getChildFile("Sounds");

            if (wav.existsAsFile() && wav.isAChildOf(libRoot))
            {
                // Library sound — store relative path, lazy-load through
                // SoundLibrary so subsequent plays are cached.
                auto rel = wav.getRelativePathFrom(libRoot)
                              .replaceCharacter('\\', '/');
                int entryIdx = library_.findByRelativePath(rel);
                if (entryIdx >= 0)
                {
                    int sid = library_.ensureLoaded(entryIdx, audioEngine);
                    if (sid >= 0)
                    {
                        resolvedSoundId = sid;
                        resolvedPath    = rel.toStdString();
                    }
                }
            }
            else if (wav.existsAsFile())
            {
                // User-supplied WAV (Custom block flow)
                int newId = nextCustomSoundId_++;
                if (audioEngine.loadSample(newId, wav))
                {
                    resolvedSoundId = newId;
                    resolvedPath    = customFile.toStdString();
                }
            }
        }

        {
            juce::ScopedLock lock(editMutex_);
            pendingBlockEdit_ = { serial, startTime, duration,
                                   resolvedSoundId, resolvedPath,
                                   isLooping, loopDurationSec, true };
        }
    }

    /// Called by MainComponent when movement recording is confirmed
    void confirmMovementRecording(int serial, double duration)
    {
        for (auto& b : blockList)
        {
            if (b.serial == serial)
            {
                b.durationSec = duration;
                b.durationLocked = true;
                b.hasRecordedMovement = true;
                b.isRecordingMovement = false;
                DBG("Movement recording confirmed for block " << serial 
                    << " with " << b.recordedMovement.size() << " keyframes");
                break;
            }
        }
        recordingBlockSerial = -1;
    }

    /// Called when movement recording is cancelled
    void cancelMovementRecording(int serial)
    {
        for (auto& b : blockList)
        {
            if (b.serial == serial)
            {
                b.recordedMovement.clear();
                b.isRecordingMovement = false;
                break;
            }
        }
        recordingBlockSerial = -1;
    }
 
    /// Clear the selected block highlight (called when popup is cancelled)
    void clearSelectedBlock()
    {
        selectedSerial = -1;
    }

    void highlightBlock(int serial);

    // ── Transport state queries (called by MainComponent to update transport bar) ─────

    bool   isTransportPlaying() const noexcept { return transportClock.isPlaying(); }
    bool   isTransportPaused()  const noexcept { return transportClock.isPaused();  }
    double getTransportTime()   const noexcept { return transportClock.currentTimeSec(); }

    double getTransportDuration() const noexcept
    {
        double maxEnd = 0.0;
        for (const auto& b : blockList)
            maxEnd = std::max(maxEnd, b.endTimeSec());
        return maxEnd;
    }

    void transportPlay()  { transportClock.start(); }
    void transportPause() { transportClock.pause(); }
    void transportStop()
    {
        transportClock.stop();
        SequencerEngine::resetAllBlocks(blockList);
    }
    std::function<void(int serial, double duration, 
                    const std::vector<MovementKeyFrame>& keyframes,
                    juce::Point<int>)> onRequestMovementConfirm;

    // ── Scene persistence ─────────────────────────────────────────────────────

    /// Snapshot current blocks for saving (called from message thread).
    std::vector<BlockEntry> getBlockListCopy() const
    {
        return blockList;   // blockList is GL-thread-owned but we copy safely
    }

    /// Replace the entire scene with loaded blocks (called from message thread).
    void loadScene(std::vector<BlockEntry> newBlocks);

    /// Clear the scene (delegates to existing clear path).
    void clearScene() { pendingClear = true; }

    /// Public access to the loaded sound index (used by BlockEditPopup).
    SoundLibrary& soundLibrary() noexcept { return library_; }

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
    TransportClock  transportClock;
    SequencerEngine sequencer;
    AudioEngine     audioEngine;

    /// Transport time from the previous frame — used to detect loop wraps.
    double prevTransportTime = 0.0;

    // =========================================================================
    // Pending block edit  (message → GL thread, fixes BUG-T1)
    // =========================================================================
    struct PendingBlockEdit
    {
        int         serial          = -1;
        double      startTime       = 0.0;
        double      duration        = 1.0;
        int         soundId         = -1;
        std::string customFile;
        bool        isLooping       = false;
        double      loopDurationSec = 4.0;
        bool        active          = false;
    };
    PendingBlockEdit      pendingBlockEdit_;
    juce::CriticalSection editMutex_;

    // =========================================================================
    // Movement drag axis lock
    // =========================================================================
    int moveDragPlaneY_ = 0;  ///< Y plane locked at the start of a block drag

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

    // Pending scene load (message → GL thread)
    std::vector<BlockEntry>  pendingLoadBlocks_;
    std::atomic<bool>        pendingLoad_ { false };
    juce::CriticalSection    loadMutex_;

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

    // Movement recording state
    bool recordKeyHeld = false;
    int recordingBlockSerial = -1;
    Vec3i dragStartPos;
    

    // =========================================================================
    // HUD
    // =========================================================================
    struct Hud
    {
        juce::String     text;
        juce::Point<int> pos { 8, 3 };
        bool             isRecording = false;  ///< True while movement recording is active
        bool             isEditMode  = false;  ///< True while edit mode is active
        juce::CriticalSection lock;
    } hud;

    // =========================================================================
    // Block list
    //
    // Owned and mutated exclusively on the GL thread.
    // =========================================================================
    std::vector<BlockEntry> blockList;
    int                     nextSerial = 1;
    int highlightedBlockSerial_ = -1;

    // =========================================================================
    // Sidebar / toggle
    // =========================================================================
    juce::TextButton  toggleButton { "☰" };
    bool              isSidebarCollapsed = false;
    SidebarComponent* sidebar = nullptr;


    // =========================================================================
    // Block type selection
    // =========================================================================
    std::atomic<int> activeBlockType_ { static_cast<int>(BlockType::Violin) };
    int              nextCustomSoundId_ = 1000;

    // =========================================================================
    // Sound library (CSV index + lazy WAV cache)
    //
    // Loaded once at GL ctx creation; samples are decoded into AudioEngine
    // on first use only.
    // =========================================================================
    SoundLibrary    library_;
    bool            libraryLoaded_ = false;

    /// Directory that contains both `Sounds/` and `CSV/sound_library.csv`.
    /// Resolved once in the ctor by walking up from CWD and from the .exe folder.
    juce::File      contentRoot_;

    // =========================================================================
    // View gizmo / direction snap
    // =========================================================================
    std::atomic<int> pendingViewSnap_ { -1 };  ///< -1 = none, 0–3 = direction

    struct GizmoAxis { float x, y; };  ///< 2D projected axis endpoint
    struct GizmoState
    {
        GizmoAxis axes[3];          // X, Y, Z projected directions
        juce::CriticalSection lock;
    } gizmo_;

    juce::Rectangle<int> getGizmoButtonRect(int index) const;
    bool isInGizmoArea(float x, float y) const;

    // =========================================================================
    // Edit popup
    // =========================================================================
    bool             editMode = false;     ///< Toggled by E key
    int              selectedSerial = -1; ///< Serial of the block being edited

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewPortComponent)
};