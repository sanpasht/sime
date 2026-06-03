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
#include "SceneAudioExporter.h"
#include "SoundLibrary.h"
#include "AudioAnalysis.h"
#include "CameraPath.h"
#include <atomic>
#include <array>
#include <unordered_set>
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
    void applySidebarBlockInfo(int serial,
                               Vec3i pos,
                               double start,
                               double duration,
                               bool movementEnabled,
                               uint8_t playbackMode,
                               double movementDurationSec,
                               int movementYOffset,
                               bool isMuted,
                               bool isHidden,
                               double loopBufferSec,
                               bool isLooping,
                               double loopDurationSec,
                               std::vector<MuteWindow> muteWindows);

    /// Resize @p serial's region duration to its loaded sample's natural
    /// length, preserving any recorded movement span.  Safe to call from the
    /// message thread.
    void matchBlockDurationToSound(int serial);

    /// Fired when user clicks a block in edit mode.
    /// Args: serial, blockType, start, dur, soundId, customFilePath,
    ///       isLooping, loopDurationSec, viewLocalPos
    std::function<void(int, BlockType, double, double, int,
                       const juce::String&, bool, double,
                       juce::Point<int>)> onRequestBlockEdit;

    std::function<void(int serial)> onBlockSelected;

    /// Fired on the message thread whenever blocks are added, removed, loaded,
    /// or cleared.  MainComponent uses this to update the transport bar without
    /// waiting for the next 30 Hz timer tick.
    std::function<void()> onBlockListChanged;

    /// Fired on the message thread after sidebar edits or movement confirm
    /// are applied on the GL thread (blockList snapshot is up to date next frame).
    std::function<void(int serial)> onBlockPropertiesChanged;

    void updateBlockTiming(int serial, int timeIndex, double start, double duration);

    /// Queue an undo of the last block placement. Safe to call from the message thread.
    void requestUndo() { pendingUndo_.store(true); }

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

    /// Called by MainComponent when movement recording is confirmed.
    /// Queues the update for the GL thread — safe to call from message thread.
    void confirmMovementRecording(int serial, double duration)
    {
        juce::ScopedLock lock(movementOpMutex_);
        pendingMovementOp_ = { PendingMovementOp::Confirm, serial, duration };
    }

    /// Called when movement recording is cancelled.
    /// Queues the update for the GL thread — safe to call from message thread.
    void cancelMovementRecording(int serial)
    {
        juce::ScopedLock lock(movementOpMutex_);
        pendingMovementOp_ = { PendingMovementOp::Cancel, serial, 0.0 };
    }
 
    /// Clear the selected block highlight (called when popup is cancelled)
    void clearSelectedBlock()
    {
        selectedSerial = -1;
    }

    void highlightBlock(int serial);

    // ── Plane / arrow visibility toggles (message thread writes, GL reads) ────
    void setShowFloorPlane (bool v) { showFloorPlane_ .store(v); repaint(); }
    void setShowWallXPlane (bool v) { showWallXPlane_ .store(v); repaint(); }
    void setShowWallZPlane (bool v) { showWallZPlane_ .store(v); repaint(); }
    void setShowArrows     (bool v) { showArrows_      .store(v); repaint(); }

    bool getShowFloorPlane () const noexcept { return showFloorPlane_ .load(); }
    bool getShowWallXPlane () const noexcept { return showWallXPlane_ .load(); }
    bool getShowWallZPlane () const noexcept { return showWallZPlane_ .load(); }
    bool getShowArrows     () const noexcept { return showArrows_      .load(); }

    /// Doppler effect toggle — forwards to AudioEngine and the exporter.
    void setDopplerEnabled(bool v) { audioEngine.setDopplerEnabled(v); }
    bool getDopplerEnabled() const noexcept { return audioEngine.isDopplerEnabled(); }

    void setSpatialSensitivity(float s) { audioEngine.setSpatialSensitivity(s); }
    float getSpatialSensitivity() const noexcept { return audioEngine.getSpatialSensitivity(); }

    /// Freeze the audio listener at the current camera pose.  Toggle off
    /// restores the camera to that same pose.  Processed on the GL thread.
    void setAudioAnchorEnabled(bool enabled);
    bool isAudioAnchorActive() const noexcept { return audioAnchorActive_; }

    AudioEngine::SpatialReadout measureSpatialAt(float x, float y, float z) const noexcept
    {
        return audioEngine.measureSourceAt(x, y, z);
    }

    /// Snapshot of the listener pose the exporter will use.  If the user has
    /// hit Anchor, that frozen pose is returned; otherwise the live camera.
    struct ExportListenerInfo
    {
        bool   anchored      = false;
        Vec3f  pos;
        Vec3f  forward;
        Vec3f  right;
        float  sensitivity   = 1.f;
    };

    ExportListenerInfo getExportListenerInfo() const;

    // ── Camera (listener) path ─────────────────────────────────────────────
    //
    // A user-authored sequence of keyframes that drives the camera (and
    // therefore the audio listener) during transport playback and during
    // export.  Two keyframe modes: Hold (instant cut to the next pose) and
    // Lerp (smooth interpolation).  See CameraPath.h for details.
    //
    // The popup edits a working draft on the message thread; calling
    // applyCameraPath() queues the replacement for the GL thread to swap
    // in atomically.  Live following can be toggled on/off without
    // throwing the path away.
    //
    // R-key recording: while the transport is playing, R toggles a
    // capture session that emits Lerp keyframes from the live camera pose
    // every ~50 ms.  The captured keyframes splice into the existing
    // path (replacing anything whose time falls inside the recording
    // window).

    std::vector<CameraKeyframe> getCameraPathCopy() const;
    void applyCameraPath(std::vector<CameraKeyframe> path);
    void clearCameraPath();

    void setCameraPathFollowEnabled(bool enabled);
    bool isCameraPathFollowEnabled() const noexcept { return cameraPathFollowEnabled_.load(); }

    /// When true, the user keeps full manual control of the camera even if
    /// a path is present — the path is still used for the audio listener
    /// pose during export, but live playback no longer drives the camera.
    void setFreeCameraOverride(bool freeCam) noexcept { freeCameraOverride_.store(freeCam); }
    bool isFreeCameraOverride() const noexcept { return freeCameraOverride_.load(); }

    void toggleCameraPathRecording();
    bool isCameraPathRecording() const noexcept { return cameraPathRecording_.load(); }

    /// User-facing capture granularity for live R-recording.  Default 1.0s
    /// — the popup dropdown picks 0.1 / 0.25 / 0.5 / 1 / 2 / 5 etc.
    void   setCameraRecordIntervalSec(double s) noexcept
    { cameraRecordIntervalSec_.store(juce::jlimit(0.02, 30.0, s)); }
    double getCameraRecordIntervalSec() const noexcept
    { return cameraRecordIntervalSec_.load(); }

    /// Captures the current camera pose at the current playhead time and
    /// inserts it into the path as a Hold keyframe.  Safe from message
    /// thread.  Returns true if queued (false when no scene loaded).
    void addCameraHoldFromCurrent(double atTimeSec);

    /// True iff at least one camera keyframe is set.
    bool hasCameraPath() const noexcept { return cameraPathHasAny_.load(); }

    /// Live camera pose snapshot — pose-only (position + yaw/pitch).  Used
    /// by the CameraPathPopup to add Hold keyframes locally without round-
    /// tripping through the GL thread.
    CameraPose getCurrentCameraPose() const
    {
        CameraPose p;
        p.pos      = camera.getPosition();
        p.yawRad   = camera.getYaw();
        p.pitchRad = camera.getPitch();
        return p;
    }

    /// Pick block B in the viewport after calling this with block A's serial.
    void beginDistancePick(int anchorSerial);
    void cancelDistancePick() noexcept { distancePickActive_.store(false); }
    bool isDistancePickActive() const noexcept { return distancePickActive_.load(); }

    std::function<void(int anchorSerial, int targetSerial,
                       float dx, float dy, float dz,
                       float distanceMetres, float approxDb)> onDistanceMeasured;

    // ── Type filter (show / hide whole categories of blocks) ────────────────
    void setBlockTypeVisible(BlockType t, bool v)
    {
        const auto idx = static_cast<size_t>(t);
        if (idx < blockTypeVisible_.size())
        {
            blockTypeVisible_[idx].store(v);
            repaint();
        }
    }

    bool isBlockTypeVisible(BlockType t) const noexcept
    {
        const auto idx = static_cast<size_t>(t);
        return idx >= blockTypeVisible_.size()
               || blockTypeVisible_[idx].load();
    }

    std::vector<SidebarComponent::AudioItem> scanWorkspaceAudios() const;
    void refreshWorkspaceAudioPanel();
    juce::File getWorkspaceAudioDir() const;

    // ── Per-type indefinite mute (toolbar Mute menu) ───────────────────────
    void setBlockTypeMuted(BlockType t, bool muted)
    {
        const auto idx = static_cast<size_t>(t);
        if (idx < blockTypeMuted_.size())
            blockTypeMuted_[idx].store(muted);
    }

    bool isBlockTypeMuted(BlockType t) const noexcept
    {
        const auto idx = static_cast<size_t>(t);
        return idx < blockTypeMuted_.size()
               && blockTypeMuted_[idx].load();
    }

    // ── Transport state queries (called by MainComponent to update transport bar) ─────

    bool   isTransportPlaying() const noexcept { return transportClock.isPlaying(); }
    bool   isTransportPaused()  const noexcept { return transportClock.isPaused();  }
    double getTransportTime()   const noexcept { return transportClock.currentTimeSec(); }

    double getTransportDuration() const noexcept
    {
        juce::ScopedLock lock(blockListSnapshotMutex_);
        double maxEnd = 0.0;
        for (const auto& b : blockListSnapshot_){
            maxEnd = std::max(maxEnd, b.endTimeSec());
            for (const auto& t : b.timesList){
                maxEnd = std::max(maxEnd, t.endTimeSec());
            }
        }
        return maxEnd;
    }

    void transportPlay()
    {
        // Coming back from Pause: just unfreeze the audio thread.  Voices
        // resume from where they were when the user paused.
        transportClock.start();
        audioEngine.setAudioPaused(false);
    }

    void transportPause()
    {
        // Freeze the clock AND the audio engine instantly — voices stop
        // ringing out immediately and resume on Play with no drift.
        transportClock.pause();
        audioEngine.setAudioPaused(true);
    }

    void transportStop()
    {
        transportClock.stop();                              // clock back to 0
        audioEngine.setAudioPaused(false);                  // not paused, just silent
        audioEngine.killAllVoices();                        // kill every in-flight voice
        pendingStop_ = true;                                // GL thread resets block state
        SequencerEngine::resetAllBlocks(blockList);

        // Snap every block with recorded movement back to its starting keyframe.
        // (The Pause button preserves position; Stop must rewind visuals too.)
        if (SequencerEngine::snapBlockPositionsToTime(blockList, 0.0))
        {
            voxelGrid.clear();
            for (const auto& b : blockList)
                voxelGrid.add(b.pos);
            renderer.meshDirty = true;
            pushBlockListToUi();
        }
    }

    /// Fast-forward / playback speed. 1.0 = real time. Routes to both the
    /// transport clock (so the sequencer schedules events faster) and the
    /// audio engine (so in-flight voices and new voices play faster).
    void setPlaybackRate(double rate)
    {
        transportClock.setPlaybackRate(rate);
        audioEngine.setPlaybackRate(rate);
    }

    double getPlaybackRate() const noexcept { return transportClock.playbackRate(); }
    std::function<void(int serial, double duration, 
                    const std::vector<MovementKeyFrame>& keyframes,
                    juce::Point<int>)> onRequestMovementConfirm;

    // ── Scene persistence ─────────────────────────────────────────────────────

    /// Snapshot current blocks for saving (called from message thread).
    /// Reads from blockListSnapshot_ which the GL thread refreshes each frame.
    std::vector<BlockEntry> getBlockListCopy() const
    {
        juce::ScopedLock lock(blockListSnapshotMutex_);
        return blockListSnapshot_;
    }

    /// Replace the entire scene with loaded blocks (called from message thread).
    void loadScene(std::vector<BlockEntry> newBlocks);

    /// Clear the scene (delegates to existing clear path).  Also cancels any
    /// pending scene load so the user gets a truly empty scene, even if an
    /// autoload was queued for the next render frame.
    void clearScene()
    {
        {
            juce::ScopedLock lock(loadMutex_);
            pendingLoadBlocks_.clear();
        }
        pendingLoad_  = false;
        pendingClear  = true;
    }

    /// Display name for a serial in the current scene, e.g. "Violin 2".
    /// Returns empty if the serial is not found.  Safe to call from the
    /// message thread (reads the GL-thread-owned blockList by snapshot lock).
    juce::String displayNameForSerial(int serial) const
    {
        auto blocks = getBlockListCopy();
        for (const auto& b : blocks)
            if (b.serial == serial)
                return BlockEntry::displayName(b, blocks);
        return {};
    }

    /// Public access to the loaded sound index (used by BlockEditPopup).
    SoundLibrary& soundLibrary() noexcept { return library_; }

    /// Offline mix of the full timeline to disk (call from the message thread).
    bool exportSceneAudioToFile(const juce::File& outputFile,
                                 SceneAudioExporter::Format format,
                                 juce::String& errorOut);

    void seekTransportClock(double newTimeSec);
    void addTimeRangeToBlock(int serial, double start, double duration);

    void updateBlockTimeRange(int serial,
                            int timeIndex,
                            double start,
                            double duration);

    const std::vector<BlockEntry>& getBlocks() const{
        return blockList;
    }

    bool deleteBlockOrRegion(int serial, int timeIndex);

    /// Offline pitch + waveform analysis for the selected block's WAV.
    /// Safe on the message thread (read-only sample library access).
    AudioAnalysisResult analyzeBlockAudio(const BlockEntry& block) const;

    // =========================================================================
    // Clipboard / multi-selection API (called from MainComponent on the
    // message thread, drained on the GL thread inside renderOpenGL).
    // =========================================================================

    /// Copy every currently-selected block (`selectedSerial` plus anything
    /// in the multi-selection set) into the in-memory clipboard.  No-op
    /// when there is no selection.
    void requestCopySelection();

    /// Paste the clipboard contents.  Each pasted block gets a fresh
    /// serial; positions are translated by (+1, 0, 0) and pushed further
    /// out if the target cell already holds a (visible) block.  No-op
    /// when the clipboard is empty.
    void requestPasteSelection();

    /// Replace `selectedSerial` + `multiSelection_` with every block in
    /// the current scene.  Used by Ctrl+A.
    void requestSelectAll();

    /// Drop the multi-selection set, keep `selectedSerial`.  Used by
    /// Escape and by single-clicks on a block / empty space.
    void requestClearMultiSelection();

    /// Snapshot of `multiSelection_` refreshed each GL frame — safe on the
    /// message thread (sidebar bulk Apply, status display, etc.).
    std::vector<int> getMultiSelectionCopy() const;

    /// Replace @p serial's recorded movement path with @p frames.  Sorted,
    /// time-normalized (first keyframe at t=0), and snapped to integer
    /// grid coordinates on the GL thread.  Used by the Keyframe Editor
    /// popup as an alternative to Alt-drag recording.
    void applyMovementKeyframes(int serial,
                                std::vector<MovementKeyFrame> frames);

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
    // recordKeyHeld is written by the message thread (mouseDown/mouseUp/keyPressed)
    // and read by the GL thread (renderOpenGL keyframe loop).  std::atomic ensures
    // both threads see consistent values without a lock.
    std::atomic<bool> recordKeyHeld { false };
    int  recordingBlockSerial = -1;
    Vec3i dragStartPos;
    

    // =========================================================================
    // HUD
    // =========================================================================
    struct Hud
    {
        juce::String     text;
        juce::Point<int> pos { 8, 3 };
        bool             isRecording = false;
        bool             isEditMode  = false;
        bool             isShiftMode = false;
        int              shiftY      = 0;
        int              voxelCount  = 0;
        Vec3i            cursorPos   { 0, 0, 0 };
        juce::CriticalSection lock;
    } hud;

    // =========================================================================
    // Block list
    //
    // Owned and mutated exclusively on the GL thread.
    // =========================================================================
    std::vector<BlockEntry> blockList;
    int                     nextSerial = 1;
    int highlightedBlockSerial_ = -1;  ///< Selected via timeline / viewport click
    int hoveredBlockSerial_   = -1;  ///< Block under cursor (viewport hover)

    /// Rebuild sidebar block list on the message thread.  When @p removedSerial
    /// is >= 0, clear the info panel if it was showing that block.
    void pushBlockListToUi(int removedSerial = -1);

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
    bool             editMode = false;     ///< Toggled by Tab key
    int              selectedSerial = -1; ///< Serial of the block being edited

    // =========================================================================
    // Multi-selection + clipboard  (GL-thread owned)
    //
    // `multiSelection_` holds every serial included in a bulk operation.
    // `selectedSerial` is the *primary* — it's the one the sidebar Info
    // panel shows.  Bulk ops (copy, future bulk mute/hide) read the
    // union of the two.
    //
    // `clipboardBlocks_` stores deep copies (movement keyframes, mute
    // windows, etc.) of whatever was on the multi-selection at the time
    // of the last Copy.  Lives until the next Copy or until SIME exits.
    // =========================================================================
    std::unordered_set<int>  multiSelection_;
    std::vector<BlockEntry>  clipboardBlocks_;

    /// Pending clipboard / selection op queued from the message thread.
    /// Drained at the top of every renderOpenGL() pass so blockList /
    /// voxelGrid mutations stay on the GL thread.
    struct PendingClipboardOp
    {
        enum Type { None, Copy, Paste, SelectAll, ClearMulti } type = None;
    };
    PendingClipboardOp     pendingClipboardOp_;
    juce::CriticalSection  clipboardOpMutex_;

    // =========================================================================
    // Pending sidebar block-info edit  (message → GL thread)
    // Replaces the old direct blockList mutation in applySidebarBlockInfo().
    // =========================================================================
    struct PendingSidebarEdit
    {
        int     serial              = -1;
        Vec3i   pos;
        double  start               = 0.0;
        double  duration            = 1.0;
        bool    movementEnabled     = false;
        bool    active              = false;

        // Phase 1 movement controls
        uint8_t playbackMode        = 0;
        double  movementDurationSec = 0.0;
        int     movementYOffset     = 0;

        // v7 per-block UI flags
        bool    isMuted             = false;
        bool    isHidden            = false;
        double  loopBufferSec       = 0.0;

        // v8 / loop-section additions
        bool    isLooping           = false;
        double  loopDurationSec     = 0.0;
        // v9: scheduled mute windows (replaces single muteStartSec/muteEndSec)
        std::vector<MuteWindow> muteWindows;
    };
    std::vector<PendingSidebarEdit> pendingSidebarEdits_;
    juce::CriticalSection           sidebarEditMutex_;

    // =========================================================================
    // Pending position-keyframe edit  (message → GL thread)
    //
    // The Keyframe Editor popup runs on the message thread; the actual
    // BlockEntry / voxelGrid mutation has to happen on the GL thread so
    // we queue here and drain inside renderOpenGL().
    // =========================================================================
    struct PendingKeyframeEdit
    {
        int                            serial = -1;
        bool                           active = false;
        std::vector<MovementKeyFrame>  frames;
    };
    std::vector<PendingKeyframeEdit> pendingKeyframeEdits_;
    juce::CriticalSection            keyframeEditMutex_;

    // =========================================================================
    // Pending timing-only update  (message → GL thread)
    // Used by timeline drag (updateBlockTiming).
    // =========================================================================
    struct PendingTimingUpdate
    {
        int    serial   = -1;
        double start    = 0.0;
        double duration = 1.0;
        bool   active   = false;
        int   timeIndex = -1;
    };
    PendingTimingUpdate   pendingTimingUpdate_;
    juce::CriticalSection timingMutex_;

    // =========================================================================
    // Pending movement confirm / cancel  (message → GL thread)
    // =========================================================================
    struct PendingMovementOp
    {
        enum Type { None, Confirm, Cancel } type = None;
        int    serial   = -1;
        double duration = 0.0;   ///< Used only for Confirm
    };
    PendingMovementOp     pendingMovementOp_;
    juce::CriticalSection movementOpMutex_;

    // =========================================================================
    // Pending transport stop  (message → GL thread)
    // transportClock.stop() is called directly (pre-existing); the blockList
    // reset (SequencerEngine::resetAllBlocks) is deferred here so it runs
    // safely on the GL thread.
    // =========================================================================
    std::atomic<bool> pendingStop_ { false };

    // =========================================================================
    // Pending edit-mode click  (message → GL thread)
    // All edit-mode raycasts that need to read camera/voxelGrid/blockList are
    // queued here and executed on the GL thread in renderOpenGL().
    // =========================================================================
    struct EditClickRequest
    {
        enum Type { None, EditRMB, SelectLMB, AltRecordLMB } type = None;
        float x = 0.f, y = 0.f;
        bool  active = false;
        bool  shift  = false;
    };
    EditClickRequest      pendingEditClick_;
    juce::CriticalSection editClickMutex_;

    // =========================================================================
    // Marquee (rubber-band) multi-select  (message thread drag, GL finalize)
    // =========================================================================
    struct MarqueeDragState
    {
        bool              pending = false;
        bool              active  = false;
        bool              shiftAdds = false;
        juce::Point<float> start;
        juce::Point<float> current;
    };
    MarqueeDragState      marqueeDrag_;
    juce::CriticalSection marqueeDragMutex_;

    struct PendingMarqueeSelect
    {
        bool  active = false;
        float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
        bool  addToSelection = false;
    };
    PendingMarqueeSelect  pendingMarquee_;
    juce::CriticalSection marqueeSelectMutex_;

    mutable std::vector<int>  multiSelectionSnapshot_;
    mutable juce::CriticalSection multiSelectionSnapshotMutex_;

    // =========================================================================
    // Pending recording stop  (message → GL thread)
    //
    // mouseUp queues this instead of touching blockList directly.
    // The GL thread reads keyframes, stops audio, and fires the confirm
    // callback — all safely on the GL thread.
    // =========================================================================
    struct PendingRecordingStop
    {
        bool             active   = false;
        juce::Point<int> mousePos;   ///< Captured on message thread for popup placement
    };
    PendingRecordingStop  pendingRecordingStop_;
    juce::CriticalSection recordingStopMutex_;

    // =========================================================================
    // blockList snapshot  (GL thread writes, message thread reads)
    //
    // The GL thread copies blockList into this snapshot at the end of each
    // renderOpenGL() frame.  All message-thread reads (getBlockListCopy,
    // getTransportDuration, getBlockBySerial) use this snapshot, eliminating
    // direct cross-thread access to blockList.
    // =========================================================================
    mutable std::vector<BlockEntry>  blockListSnapshot_;
    mutable juce::CriticalSection    blockListSnapshotMutex_;

    static constexpr int  kMaxUndoDepth = 20;
    std::vector<int>      undoStack_;
    std::atomic<bool>     pendingUndo_ { false };

    // =========================================================================
    // Plane / arrow visibility (user toggles in MainComponent)
    // =========================================================================
    std::atomic<bool> showFloorPlane_  { true };
    std::atomic<bool> showWallXPlane_  { false };  // YZ plane (x = 0)  – off by default
    std::atomic<bool> showWallZPlane_  { false };  // XY plane (z = 0)  – off by default
    std::atomic<bool> showArrows_       { false }; //                  – off by default

    // Audio anchor (frozen listener pose; camera restores on toggle off)
    bool  audioAnchorActive_ = false;
    Vec3f audioAnchorPos_;
    Vec3f audioAnchorForward_;
    Vec3f audioAnchorRight_;
    Vec3f savedCameraPos_;
    Vec3f savedCameraLookAt_;
    std::atomic<bool> pendingAnchorOp_     { false };
    std::atomic<bool> pendingAnchorEnable_ { false };

    // Camera path (GL thread owned; snapshotted under cameraPathMutex_)
    std::vector<CameraKeyframe>   cameraPath_;
    mutable juce::CriticalSection cameraPathMutex_;
    std::atomic<bool>             cameraPathHasAny_         { false };
    std::atomic<bool>             cameraPathFollowEnabled_  { false };
    std::atomic<bool>             cameraPathRecording_      { false };

    // Pending camera-path ops (message thread → GL thread)
    struct PendingPathOp
    {
        enum Type { None, Replace, Clear, AddHold,
                    StartRecord, StopRecord, FollowSet };
        Type   type = None;
        bool   enable = false;
        double timeSec = 0.0;
        std::vector<CameraKeyframe> payload;
    };
    std::vector<PendingPathOp>    pendingPathOps_;
    juce::CriticalSection         pendingPathOpMutex_;

    // Live recording state (GL thread).  Works whether transport is playing
    // or not — captures are spaced by wall-clock interval; keyframe times
    // are derived from playhead-at-start + wall-clock elapsed.
    bool   recordingActive_         = false;
    double recordingStartSec_       = 0.0;   ///< Playhead time at record start
    double recordingStartWallSec_   = 0.0;   ///< Wall-clock time at record start
    double recordingLastCaptureWall_= 0.0;
    std::vector<CameraKeyframe>  recordingBuffer_;

    /// User-controlled capture rate.  Defaults to 1 s so a live R-take
    /// produces a manageable list of keyframes; can be lowered down to
    /// 0.02 s for finer hand-flown shots from the popup dropdown.
    std::atomic<double> cameraRecordIntervalSec_{ 1.0 };

    /// When true the live camera is NOT driven by the path during playback
    /// — the user retains free-fly control.  The path still affects the
    /// audio listener pose during offline export.
    std::atomic<bool> freeCameraOverride_{ false };

    std::atomic<bool> distancePickActive_        { false };
    std::atomic<int>  distancePickAnchorSerial_  { -1 };

    // Per-block-type visibility filter.  Indexed by static_cast<size_t>(BlockType).
    // All types start visible; the View menu in MainComponent flips entries.
    // (std::atomic<bool> is non-copyable, so the array is default-initialized
    //  and the constructor below stores `true` into every slot.)
    static constexpr size_t kNumBlockTypes = static_cast<size_t>(BlockType::_Count);
    std::array<std::atomic<bool>, kNumBlockTypes> blockTypeVisible_ {};
    /// Per-type indefinite mute (mirrors blockTypeVisible_).  All audible
    /// (false) at startup; the Mute menu in MainComponent toggles entries.
    std::array<std::atomic<bool>, kNumBlockTypes> blockTypeMuted_   {};

    // =========================================================================
    // 3D arrow gizmo state  (GL thread owned, message thread peeks via atomic)
    // =========================================================================
    // Axis index convention used everywhere:  0 = X, 1 = Y, 2 = Z, -1 = none.
    std::atomic<int> gizmoHoveredAxis_ { -1 };

    struct GizmoDragRequest
    {
        enum Type { None, Start, Move, End } type = None;
        float x = 0.f, y = 0.f;
        int   axis = -1;       ///< Captured on the message thread for Start
    };
    GizmoDragRequest      pendingGizmoDrag_;
    juce::CriticalSection gizmoMutex_;

    /// Drag state.  `gizmoActiveAxis_` is also peeked by the message thread
    /// (mouseDrag / mouseUp) to know whether a drag is in progress.
    std::atomic<int> gizmoActiveAxis_  { -1 };  ///< -1 when not dragging
    int   gizmoDragSerial_     = -1;
    Vec3i gizmoDragOrigPos_;
    float gizmoDragStartCoord_ = 0.f;    ///< Axis projection at drag start

    /// Cast the current mouse ray against the move arrows of the selected
    /// block.  Returns the closest axis index (0/1/2) or -1 if none.
    int  pickGizmoAxis(float mx, float my) const;

    /// Project the given screen ray onto the world-space line that passes
    /// through @p axisOrigin in direction of @p axis (0=X,1=Y,2=Z).  Returns
    /// the scalar coordinate along that axis (in world units).
    float projectRayOntoAxis(float mx, float my,
                             const Vec3f& axisOrigin, int axis) const;

    /// World-space center of the selected block's arrow gizmo (block center).
    /// Returns false if there is no selected block.
    bool  getSelectedGizmoOrigin(Vec3f& outOrigin, int& outSerial) const;
    juce::File copyAudioToWorkspace(const juce::File& sourceFile);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewPortComponent)
};