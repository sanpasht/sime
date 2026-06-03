// ─────────────────────────────────────────────────────────────────────────────
// ViewPortComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "ViewPortComponent.h"
#include <juce_opengl/juce_opengl.h>
using namespace juce::gl;
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Grid bounds  — must match buildGridMesh(40) in Renderer.cpp
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kGridHalf = 40;

namespace
{
    bool hasSoundsAndCsv(const juce::File& dir)
    {
        return dir.getChildFile("Sounds").isDirectory()
            && dir.getChildFile("CSV").getChildFile("sound_library.csv").existsAsFile();
    }

    /// Walk upward from `start` looking for a folder that contains both `Sounds/`
    /// and `CSV/sound_library.csv`. Returns an invalid File if none found.
    juce::File climbToContentRoot(juce::File start)
    {
        for (int depth = 0; depth < 14; ++depth)
        {
            if (hasSoundsAndCsv(start))
                return start;
            auto parent = start.getParentDirectory();
            if (!parent.isDirectory() || parent == start)
                break;
            start = parent;
        }
        return {};
    }

    /// Prefer repo root whether the user launched from a terminal (`cd sime`) or
    /// double-clicked `SIME.exe` under `build/.../Debug/` (wrong CWD).
    juce::File resolveContentRoot()
    {
        const juce::File seeds[] = {
            juce::File::getCurrentWorkingDirectory(),
            juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                .getParentDirectory(),
        };

        for (auto seed : seeds)
        {
            auto found = climbToContentRoot(seed);
            if (found.getFullPathName().isNotEmpty())
                return found;
        }

        return juce::File::getCurrentWorkingDirectory();
    }
}

static bool isInBounds(const Vec3i& pos)
{
    return pos.x >= -kGridHalf && pos.x < kGridHalf
        && pos.z >= -kGridHalf && pos.z < kGridHalf
        && pos.y >= 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

ViewPortComponent::ViewPortComponent()
{
    // Start every block type visible — the View menu toggles entries.
    for (auto& f : blockTypeVisible_)
        f.store(true);
    // Start every block type audible — the Mute menu toggles entries.
    for (auto& f : blockTypeMuted_)
        f.store(false);

    setWantsKeyboardFocus(true);
    openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    openGLContext.setContinuousRepainting(true);
    // Legacy test tones (backward compat for any soundId = 0/1/2 blocks)
    audioEngine.generateTestTone(0, 440.0f, 2.0);
    audioEngine.generateTestTone(1, 660.0f, 2.0);
    audioEngine.generateTestTone(2, 880.0f, 2.0);

    // Violin presets — vibrato + harmonics, sustained
    audioEngine.generateViolinTone(100, 220.0f, 2.0);   // A3
    audioEngine.generateViolinTone(101, 294.0f, 2.0);   // D4
    audioEngine.generateViolinTone(102, 196.0f, 2.0);   // G3

    // Piano presets — sharp attack, decaying harmonics
    audioEngine.generatePianoTone(200, 262.0f, 2.0);    // C4
    audioEngine.generatePianoTone(201, 440.0f, 2.0);    // A4
    audioEngine.generatePianoTone(202, 523.0f, 2.0);    // C5

    // Drum presets — each has distinct character
    audioEngine.generateDrumHit(300, 0, 0.5);    // Kick
    audioEngine.generateDrumHit(301, 1, 0.4);    // Snare
    audioEngine.generateDrumHit(302, 2, 0.2);    // Hi-Hat

    audioEngine.start();

    // ── Sound library (CSV-only indexing; no WAV decoding here) ────────────
    contentRoot_ = resolveContentRoot();
    auto csvFile   = contentRoot_.getChildFile("CSV").getChildFile("sound_library.csv");
    auto soundsDir = contentRoot_.getChildFile("Sounds");

    DBG("SoundLibrary: content root = " << contentRoot_.getFullPathName());

    if (csvFile.existsAsFile() && soundsDir.isDirectory())
    {
        libraryLoaded_ = library_.load(csvFile, soundsDir);
        DBG("SoundLibrary: " << (libraryLoaded_ ? "loaded " : "FAILED to load ")
            << library_.count() << " entries from " << csvFile.getFullPathName());
    }
    else
    {
        DBG("SoundLibrary: CSV or Sounds not found under content root "
            << contentRoot_.getFullPathName()
            << "  (expected CSV/sound_library.csv + Sounds/ — picker will be empty)");
    }
    // refreshWorkspaceAudioPanel();
}

ViewPortComponent::~ViewPortComponent()
{
    openGLContext.detach();
    audioEngine.stop();
}

void ViewPortComponent::loadScene(std::vector<BlockEntry> newBlocks)
{
    // Re-register WAV samples so the audio engine knows them.  Two flavors:
    //   * relative path (no drive letter) -> library sound, looked up via SoundLibrary
    //   * absolute path                    -> user Custom WAV, loaded directly
    for (auto& b : newBlocks)
    {
        if (b.customFilePath.empty()) continue;
        juce::String pathStr(b.customFilePath);

        const bool isAbsolute = juce::File::isAbsolutePath(pathStr);

        if (pathStr.startsWith("workspaceAudios"))
        {
            auto wav =
                contentRoot_
                    .getChildFile("Source")
                    .getChildFile(pathStr);

            if (wav.existsAsFile())
            {
                if (!audioEngine.hasSample(b.soundId))
                    audioEngine.loadSample(b.soundId, wav);
            }
        }
        else if (!isAbsolute)
        {
            int idx = library_.findByRelativePath(pathStr);
            if (idx >= 0)
            {
                int sid = library_.ensureLoaded(idx, audioEngine);
                if (sid >= 0) b.soundId = sid;
            }
        }
        else
        {
            juce::File wav(pathStr);
            if (wav.existsAsFile() && !audioEngine.hasSample(b.soundId))
                audioEngine.loadSample(b.soundId, wav);
        }
    }

    {
        juce::ScopedLock lock(loadMutex_);
        pendingLoadBlocks_ = std::move(newBlocks);
    }
    pendingLoad_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GL callbacks
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::newOpenGLContextCreated()
{
    renderer.init();
    renderer.meshDirty = true;
}

void ViewPortComponent::openGLContextClosing()
{
    renderer.shutdown();
}

void ViewPortComponent::renderOpenGL()
{
    // ── Frame timing ──────────────────────────────────────────────────────────
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float  dt  = (lastRenderTime > 0.0)
               ? static_cast<float>(now - lastRenderTime) : 0.016f;
    dt = std::min(dt, 0.1f);
    lastRenderTime = now;

    transportClock.update(static_cast<double>(dt));
    // ── Camera: mouse look ────────────────────────────────────────────────────
    {
        juce::ScopedLock lock(mouseMutex);
        if (mouse.dX != 0.f || mouse.dY != 0.f)
        {
            camera.rotate(mouse.dX * camera.lookSpeed,
                          mouse.dY * camera.lookSpeed);
            mouse.dX = mouse.dY = 0.f;
        }
    }

    // ── Camera: keyboard ─────────────────────────────────────────────────────
    processKeyboardMovement(dt);

    // ── Shift plane scroll adjustment ─────────────────────────────────────────
    {
        int delta = shiftScrollDelta.exchange(0);
        if (delta != 0)
            shiftPlaneY = std::clamp(shiftPlaneY + delta, 0, kGridHalf - 1);
    }

    // ── Clear all ─────────────────────────────────────────────────────────────
    if (pendingClear.exchange(false))
    {
        voxelGrid.clear();
        blockList.clear();
        nextSerial            = 1;        // start fresh so new blocks begin at 1
        selectedSerial          = -1;
        multiSelection_.clear();
        highlightedBlockSerial_ = -1;
        hoveredBlockSerial_     = -1;
        recordingBlockSerial    = -1;
        renderer.meshDirty      = true;
        distancePickActive_.store(false);
        distancePickAnchorSerial_.store(-1);

        juce::MessageManager::callAsync([this]()
        {
            if (sidebar != nullptr)
            {
                sidebar->setAudioList({});
                sidebar->resetSpatialUi();
                sidebar->clearSelectedBlock();
            }
            if (onBlockListChanged)
                onBlockListChanged();
        });
    }

    // ── Pending transport stop (blockList reset deferred from message thread) ─
    if (pendingStop_.exchange(false))
        SequencerEngine::resetAllBlocks(blockList);

    // ── Drain clipboard / multi-selection op ────────────────────────────────
    {
        PendingClipboardOp op;
        {
            juce::ScopedLock lk(clipboardOpMutex_);
            op = pendingClipboardOp_;
            pendingClipboardOp_.type = PendingClipboardOp::None;
        }

        // Helper: serials we should treat as "currently selected" for a
        // bulk op.  Primary selection counts as part of the set even when
        // multiSelection_ is empty.
        auto effectiveSelection = [this]() -> std::unordered_set<int>
        {
            auto out = multiSelection_;
            if (selectedSerial >= 0)
                out.insert(selectedSerial);
            return out;
        };

        if (op.type == PendingClipboardOp::Copy)
        {
            const auto serials = effectiveSelection();
            clipboardBlocks_.clear();
            clipboardBlocks_.reserve(serials.size());
            for (const auto& b : blockList)
                if (serials.count(b.serial))
                    clipboardBlocks_.push_back(b);
        }
        else if (op.type == PendingClipboardOp::Paste
                 && !clipboardBlocks_.empty())
        {
            // Translate every block by (+1, 0, 0) from its original
            // position; if the target cell is occupied, keep sliding
            // further along +X until we find a free cell or give up.
            // This keeps relative offsets between multi-block pastes.
            const Vec3i baseDelta { 1, 0, 0 };

            std::vector<int> newlyPasted;
            newlyPasted.reserve(clipboardBlocks_.size());

            // Track positions claimed during *this* paste so the second
            // pasted block can't land on the first one we just placed.
            std::unordered_set<long long> claimedThisPaste;
            auto packPos = [](const Vec3i& p) -> long long
            {
                // Pack into 64 bits — grid is small enough that 21 bits
                // per axis is plenty.
                return ( (long long)(p.x + (1 << 20)) << 42 )
                     | ( (long long)(p.y + (1 << 20)) << 21 )
                     |   (long long)(p.z + (1 << 20));
            };

            for (const auto& src : clipboardBlocks_)
            {
                Vec3i newPos = src.pos + baseDelta;

                int safety = 0;
                while ((voxelGrid.contains(newPos)
                        || claimedThisPaste.count(packPos(newPos))
                        || !isInBounds(newPos)
                        || (newPos.x == 0 && newPos.y == 0 && newPos.z == 0))
                       && safety < 256)
                {
                    newPos.x += 1;
                    ++safety;
                }
                if (voxelGrid.contains(newPos)
                    || claimedThisPaste.count(packPos(newPos))
                    || !isInBounds(newPos))
                    continue;   // give up on this entry

                BlockEntry copy = src;
                copy.serial = nextSerial++;
                copy.pos    = newPos;
                copy.resetPlaybackState();
                // Newly placed copy starts unselected; we'll re-select
                // them below as a group so the user can keep pasting /
                // bulk-editing without re-clicking.
                blockList.push_back(copy);
                voxelGrid.add(newPos);
                claimedThisPaste.insert(packPos(newPos));
                newlyPasted.push_back(copy.serial);
            }

            if (!newlyPasted.empty())
            {
                multiSelection_.clear();
                for (int s : newlyPasted)
                    multiSelection_.insert(s);
                // Promote the first pasted block to primary so the
                // sidebar Info panel shows something meaningful.
                selectedSerial          = newlyPasted.front();
                highlightedBlockSerial_ = selectedSerial;

                renderer.meshDirty = true;
                pushBlockListToUi(-1);

                const int ser = selectedSerial;
                juce::MessageManager::callAsync([this, ser]()
                {
                    if (onBlockSelected)
                        onBlockSelected(ser);
                });
            }
        }
        else if (op.type == PendingClipboardOp::SelectAll)
        {
            multiSelection_.clear();
            for (const auto& b : blockList)
                multiSelection_.insert(b.serial);

            // If nothing was primary-selected, promote the first block
            // so single-target sidebar actions still have a target.
            if (selectedSerial < 0 && !blockList.empty())
            {
                selectedSerial          = blockList.front().serial;
                highlightedBlockSerial_ = selectedSerial;
                const int ser = selectedSerial;
                juce::MessageManager::callAsync([this, ser]()
                {
                    if (onBlockSelected)
                        onBlockSelected(ser);
                });
            }
        }
        else if (op.type == PendingClipboardOp::ClearMulti)
        {
            multiSelection_.clear();
        }
    }

    // ── Load scene from file ────────────────────────────────────────────────
    if (pendingLoad_.exchange(false))
    {
        voxelGrid.clear();
        blockList.clear();

        {
            juce::ScopedLock lock(loadMutex_);
            blockList = std::move(pendingLoadBlocks_);
        }

        int maxSerial = 0;
        for (const auto& b : blockList)
        {
            voxelGrid.add(b.pos);
            if (b.serial > maxSerial) maxSerial = b.serial;
        }
        nextSerial = maxSerial + 1;
        selectedSerial = -1;
        multiSelection_.clear();
        renderer.meshDirty = true;
        
        refreshWorkspaceAudioPanel();

        if (onBlockListChanged)
            onBlockListChanged();
   

    }

    // ── Hover raycast ─────────────────────────────────────────────────────────
    {
        float mx, my;
        {
            juce::ScopedLock lock(mouseMutex);
            mx = mouse.curX;
            my = mouse.curY;
        }
        doRaycast(mx, my);
    }

    // ── Drain Delete/Backspace ops ────────────────────────────────────────────
    {
        juce::ScopedLock lock(opsMutex);
        int lastRemovedSerial = -1;
        bool changed = !pendingOps.empty();
        for (auto& op : pendingOps)
        {
            if (op.type == VoxelOp::REMOVE)
            {
                voxelGrid.remove(op.pos);
                auto it = std::find_if(blockList.begin(), blockList.end(),
                    [&](const BlockEntry& e){ return e.pos == op.pos; });
                if (it != blockList.end())
                {
                    lastRemovedSerial = it->serial;
                    if (highlightedBlockSerial_ == lastRemovedSerial)
                        highlightedBlockSerial_ = -1;
                    if (selectedSerial == lastRemovedSerial)
                        selectedSerial = -1;
                    blockList.erase(it);
                }
                renderer.meshDirty = true;
            }
        }
        pendingOps.clear();
        if (changed)
            pushBlockListToUi(lastRemovedSerial);
    }

    // ── Handle pending place / remove clicks ──────────────────────────────────
    ClickRequest placeReq, removeReq;
    {
        juce::ScopedLock lock(clickMutex);
        placeReq  = pendingPlace;   pendingPlace.active  = false;
        removeReq = pendingRemove;  pendingRemove.active = false;
    }

    // ── Apply queued block edit (BUG-T1 fix: was done on message thread) ─────
    {
        PendingBlockEdit edit;
        {
            juce::ScopedLock lock(editMutex_);
            edit = pendingBlockEdit_;
            pendingBlockEdit_.active = false;
        }
        if (edit.active)
        {
            for (auto& b : blockList)
            {
                if (b.serial == edit.serial)
                {
                    const int prevSoundId = b.soundId;

                    b.startTimeSec = edit.startTime;
                    if (!b.durationLocked)
                        b.durationSec = edit.duration;
                    if (!edit.customFile.empty())
                    {
                        juce::File selectedFile(edit.customFile);

                        if (selectedFile.existsAsFile())
                        {
                            juce::File copiedFile = copyAudioToWorkspace(selectedFile);

                            if (copiedFile.existsAsFile())
                            {
                                b.soundId = edit.soundId;

                                // Save portable relative path, not full C:\... path
                                b.customFilePath =
                                    ("workspaceAudios/" + copiedFile.getFileName()).toStdString();

                                if (!audioEngine.hasSample(b.soundId))
                                    audioEngine.loadSample(b.soundId, copiedFile);

                                refreshWorkspaceAudioPanel();
                            }
                        }
                    }
                    else
                    {
                        b.soundId = edit.soundId;
                    }
                    b.isLooping       = edit.isLooping;
                    b.loopDurationSec = edit.loopDurationSec;

                    // ── Auto-adjust block duration to the sound's natural length.
                    //   * If the user has already locked the duration (typically
                    //     via a recorded-movement confirm), respect that.
                    //   * If the block has movement and movementDurationSec is
                    //     still default (0 = "use durationSec"), pin movement to
                    //     the *current* duration before we expand the region so
                    //     the path keeps its original length.
                    //   * Use max(sample length, movement length) so movement
                    //     always plays out fully and audio plays its full length.
                    if (b.soundId >= 0 && b.soundId != prevSoundId
                        && !b.durationLocked)
                    {
                        const auto& lib = audioEngine.getSampleLibrary();
                        auto itLib = lib.find(b.soundId);
                        if (itLib != lib.end() && itLib->second.getNumSamples() > 0)
                        {
                            const double sampleRate = audioEngine.getOutputSampleRate();
                            const double natDur = (sampleRate > 0.0)
                                ? itLib->second.getNumSamples() / sampleRate
                                : 0.0;

                            if (natDur > 0.001)
                            {
                                const bool hasMov = b.hasRecordedMovement
                                                 && b.movementEnabled
                                                 && b.recordedMovement.size() >= 2;
                                const double movDur = hasMov ? b.effectiveMovementDuration() : 0.0;

                                if (hasMov && b.movementDurationSec <= 0.001)
                                {
                                    // Freeze movement at its previous span so it
                                    // doesn't stretch when the region grows.
                                    b.movementDurationSec = movDur;
                                }

                                b.durationSec = std::max(natDur, movDur);
                            }
                        }
                    }

                    b.resetPlaybackState();
                    break;
                }
            }
            selectedSerial = -1;
        }
    }

    // ── Apply queued sidebar block-info edit(s) ───────────────────────────────
    {
        std::vector<PendingSidebarEdit> sidebarBatch;
        {
            juce::ScopedLock lock(sidebarEditMutex_);
            sidebarBatch.swap(pendingSidebarEdits_);
        }

        for (const auto& se : sidebarBatch)
        {
        if (!se.active)
            continue;

            for (auto& b : blockList)
            {
                if (b.serial == se.serial)
                {
                    if (b.pos != se.pos)
                    {
                        if (voxelGrid.move(b.pos, se.pos))
                        {
                            b.pos = se.pos;
                            renderer.meshDirty = true;
                        }
                        else
                        {
                            // Move failed — show alert on the message thread
                            int failSerial = se.serial;
                            juce::MessageManager::callAsync([failSerial]()
                            {
                                auto* dlg = new juce::AlertWindow(
                                    "Unable to Move Block",
                                    "Could not move block " + juce::String(failSerial)
                                        + " — target position is occupied or out of bounds.",
                                    juce::AlertWindow::WarningIcon);
                                dlg->addButton("OK", 1);
                                dlg->enterModalState(
                                    true,
                                    juce::ModalCallbackFunction::create([](int) {}),
                                    true);
                            });
                        }
                    }
                    b.startTimeSec = se.start;
                    b.durationSec  = se.duration;
                    // movementEnabled controls playback; keep keyframes either way.
                    b.movementEnabled = se.movementEnabled;
                    if (!b.recordedMovement.empty())
                        b.hasRecordedMovement = true;

                    // Phase 1 movement controls
                    b.playbackMode        = static_cast<BlockPlaybackMode>(se.playbackMode);
                    b.movementDurationSec = std::max(0.0, se.movementDurationSec);
                    b.movementYOffset     = se.movementYOffset;

                    // ── v7/v8 per-block flags ─────────────────────────────
                    const bool   wasMuted          = b.isMuted;
                    const double prevLoopBufferSec = b.loopBufferSec;
                    const double prevLoopDurSec    = b.loopDurationSec;
                    const bool   prevIsLooping     = b.isLooping
                                                  || b.playbackMode == BlockPlaybackMode::Loop;

                    b.isMuted        = se.isMuted;
                    b.isHidden       = se.isHidden;
                    b.loopBufferSec  = std::max(0.0, se.loopBufferSec);
                    b.loopDurationSec= std::max(0.0, se.loopDurationSec);

                    // Adopt the scheduled mute windows, sanitising negative
                    // starts / non-positive durations so the engine never
                    // sees nonsensical values.
                    b.muteWindows.clear();
                    b.muteWindows.reserve(se.muteWindows.size());
                    for (const auto& w : se.muteWindows)
                    {
                        if (w.durationSec > 0.0)
                            b.muteWindows.push_back(
                                { std::max(0.0, w.startSec), w.durationSec });
                    }

                    // Loop toggle is the source of truth — keep playbackMode
                    // and the legacy isLooping flag in sync with it so the
                    // sidebar's Loop toggle never desyncs from the combo.
                    if (se.isLooping)
                    {
                        b.playbackMode = BlockPlaybackMode::Loop;
                        b.isLooping    = true;
                    }
                    else
                    {
                        if (b.playbackMode == BlockPlaybackMode::Loop)
                            b.playbackMode = BlockPlaybackMode::Natural;
                        b.isLooping = false;
                    }

                    const bool nowIsLooping = b.isLooping
                                           || b.playbackMode == BlockPlaybackMode::Loop;

                    // If the user just muted a playing block, cut its voice
                    // immediately so they hear the change in real time.
                    if (!wasMuted && b.isMuted)
                    {
                        SequencerEvent stopEv;
                        stopEv.type        = SequencerEventType::Stop;
                        stopEv.blockSerial = b.serial;
                        audioEngine.processEvents({ stopEv });
                    }

                    // Any change to loop semantics needs to retrigger the
                    // voice so the new loop gap / duration / on-off state
                    // actually takes effect on the playing block (voice
                    // params are baked at Start time).
                    const bool loopParamsChanged =
                          nowIsLooping        != prevIsLooping
                       || std::abs(b.loopBufferSec - prevLoopBufferSec) > 1e-4
                       || std::abs(b.loopDurationSec - prevLoopDurSec)   > 1e-4;

                    if (loopParamsChanged)
                    {
                        // Kill the live voice; resetPlaybackState() (below)
                        // will let the next sequencer tick fire Start again
                        // with the new params.
                        SequencerEvent stopEv;
                        stopEv.type        = SequencerEventType::Stop;
                        stopEv.blockSerial = b.serial;
                        audioEngine.processEvents({ stopEv });
                    }

                    b.resetPlaybackState();

                    const int editedSerial = se.serial;
                    juce::MessageManager::callAsync([this, editedSerial]()
                    {
                        if (onBlockPropertiesChanged)
                            onBlockPropertiesChanged(editedSerial);
                    });
                    break;
                }
            }
        }
    }

    // ── Apply queued position-keyframe edit(s) ────────────────────────────────
    //
    // Drained after sidebar edits so a back-to-back Apply (sidebar) → Apply
    // (keyframe popup) sees the latest pos / duration on this same frame.
    {
        std::vector<PendingKeyframeEdit> kfBatch;
        {
            juce::ScopedLock lock(keyframeEditMutex_);
            kfBatch.swap(pendingKeyframeEdits_);
        }

        for (auto& edit : kfBatch)
        {
            if (!edit.active) continue;

            for (auto& b : blockList)
            {
                if (b.serial != edit.serial) continue;

                // Sanitize: drop entries with non-finite time, sort by time,
                // shift so first keyframe sits at t = 0.  The popup already
                // does this on Apply, but defending here keeps the engine
                // safe from any future caller that forgets.
                std::vector<MovementKeyFrame> kfs;
                kfs.reserve(edit.frames.size());
                for (const auto& f : edit.frames)
                {
                    if (std::isfinite(f.timeSec))
                        kfs.push_back({ std::max(0.0, f.timeSec), f.position });
                }
                std::sort(kfs.begin(), kfs.end(),
                          [](const MovementKeyFrame& a, const MovementKeyFrame& b)
                          { return a.timeSec < b.timeSec; });
                if (!kfs.empty() && kfs.front().timeSec > 0.0)
                {
                    const double off = kfs.front().timeSec;
                    for (auto& k : kfs) k.timeSec -= off;
                }

                // The first keyframe becomes the block's anchor position so
                // playback / scrub at t = 0 doesn't look like a jump.  If
                // the target cell is occupied by a *different* block, keep
                // the existing pos so we never knock voxelGrid out of sync.
                if (kfs.size() >= 1 && kfs.front().position != b.pos)
                {
                    const Vec3i target = kfs.front().position;
                    bool collide = false;
                    for (const auto& other : blockList)
                        if (other.serial != b.serial && other.pos == target)
                            { collide = true; break; }
                    if (!collide && voxelGrid.move(b.pos, target))
                    {
                        b.pos = target;
                        renderer.meshDirty = true;
                    }
                }

                b.recordedMovement    = std::move(kfs);
                b.hasRecordedMovement = b.recordedMovement.size() >= 2;
                if (b.hasRecordedMovement)
                    b.movementEnabled = true;
                else
                    b.movementEnabled = false;
                b.resetPlaybackState();

                const int editedSerial = edit.serial;
                juce::MessageManager::callAsync([this, editedSerial]()
                {
                    if (onBlockPropertiesChanged)
                        onBlockPropertiesChanged(editedSerial);
                });
                break;
            }
        }
    }

    // ── Apply queued timing-only update (from timeline drag) ──────────────────
    {
        PendingTimingUpdate tu;
        {
            juce::ScopedLock lock(timingMutex_);
            tu = pendingTimingUpdate_;
            pendingTimingUpdate_.active = false;
        }
        if (tu.active)
        {
            for (auto& b : blockList)
            {
                if (b.serial == tu.serial)
                {
                    if(tu.timeIndex >= 0 && tu.timeIndex < (int)b.timesList.size())
                    {
                        b.timesList[tu.timeIndex].startTimeSec = tu.start;
                        b.timesList[tu.timeIndex].durationSec  = tu.duration;
                    }
                    else
                    {
                        b.startTimeSec = tu.start;
                        b.durationSec  = tu.duration;
                    }
                    break;
                }
            }
        }
    }

    // ── Apply queued movement confirm / cancel ────────────────────────────────
    {
        PendingMovementOp mo;
        {
            juce::ScopedLock lock(movementOpMutex_);
            mo = pendingMovementOp_;
            pendingMovementOp_.type = PendingMovementOp::None;
        }
        if (mo.type == PendingMovementOp::Confirm)
        {
            for (auto& b : blockList)
            {
                if (b.serial == mo.serial)
                {
                    b.durationSec         = mo.duration;
                    b.durationLocked      = true;
                    b.hasRecordedMovement = true;
                    b.movementEnabled     = true;
                    b.isRecordingMovement = false;
                    DBG("Movement confirm applied on GL thread: block " << mo.serial
                        << "  keyframes=" << (int)b.recordedMovement.size());

                    const int confirmedSerial = mo.serial;
                    juce::MessageManager::callAsync([this, confirmedSerial]()
                    {
                        if (onBlockPropertiesChanged)
                            onBlockPropertiesChanged(confirmedSerial);
                    });
                    break;
                }
            }
            recordingBlockSerial = -1;
        }
        else if (mo.type == PendingMovementOp::Cancel)
        {
            for (auto& b : blockList)
            {
                if (b.serial == mo.serial)
                {
                    b.recordedMovement.clear();
                    b.isRecordingMovement = false;
                    break;
                }
            }
            recordingBlockSerial = -1;
        }
    }

    // ── Drain marquee (rubber-band) finalize ──────────────────────────────────
    {
        PendingMarqueeSelect mq;
        {
            juce::ScopedLock lk(marqueeSelectMutex_);
            mq = pendingMarquee_;
            pendingMarquee_.active = false;
        }

        if (mq.active)
        {
            const int   ew = getWidth(), eh = getHeight();
            const float aspect = (eh > 0) ? (float) ew / eh : 1.f;
            const Mat4  view   = camera.getViewMatrix();
            const Mat4  proj   = camera.getProjectionMatrix(aspect);

            const float minX = std::min(mq.x0, mq.x1);
            const float maxX = std::max(mq.x0, mq.x1);
            const float minY = std::min(mq.y0, mq.y1);
            const float maxY = std::max(mq.y0, mq.y1);

            auto inRect = [&](float px, float py)
            {
                return px >= minX && px <= maxX && py >= minY && py <= maxY;
            };

            std::vector<int> hitSerials;
            hitSerials.reserve(blockList.size());

            for (const auto& b : blockList)
            {
                const Vec3f center {
                    b.pos.x + 0.5f, b.pos.y + 0.5f, b.pos.z + 0.5f
                };
                float sx = 0.f, sy = 0.f;
                if (!Raycaster::worldToScreen(center, (float) ew, (float) eh,
                                              view, proj, sx, sy))
                    continue;
                if (inRect(sx, sy))
                    hitSerials.push_back(b.serial);
            }

            if (!mq.addToSelection)
                multiSelection_.clear();

            for (int s : hitSerials)
                multiSelection_.insert(s);

            if (!hitSerials.empty())
            {
                selectedSerial          = hitSerials.front();
                highlightedBlockSerial_ = selectedSerial;
                const int ser = selectedSerial;
                juce::MessageManager::callAsync([this, ser]()
                {
                    if (onBlockSelected)
                        onBlockSelected(ser);
                });
            }
            else if (!mq.addToSelection)
            {
                selectedSerial          = -1;
                highlightedBlockSerial_ = -1;
            }
        }
    }

    // ── Drain edit-mode click (Fix 3: edit raycasts now happen on GL thread) ──
    //
    // All three edit-mode mouse cases (RMB open-popup, LMB select, Alt+LMB
    // start-recording) used to call Raycaster::cast() on the message thread,
    // reading camera / voxelGrid state owned by the GL thread.  mouseDown()
    // now queues an EditClickRequest and the raycast runs here instead.
    {
        EditClickRequest ec;
        {
            juce::ScopedLock lock(editClickMutex_);
            ec = pendingEditClick_;
            pendingEditClick_.active = false;
        }
        if (ec.active)
        {
            const int   ew = getWidth(), eh = getHeight();
            const float eAspect = (eh > 0) ? (float)ew / eh : 1.f;
            const Mat4  eMat  = camera.getViewMatrix();
            const Mat4  eProj = camera.getProjectionMatrix(eAspect);
            Vec3f eRay = Raycaster::screenToRay(ec.x, ec.y,
                                                (float)ew, (float)eh, eMat, eProj);
            RaycastResult eHit = Raycaster::cast(camera.getPosition(), eRay, voxelGrid);

            if (ec.type == EditClickRequest::EditRMB)
            {
                if (eHit.hit)
                {
                    // Cancel the tentative camera drag we started on the message thread
                    {
                        juce::ScopedLock lk(mouseMutex);
                        mouse.rightDown     = false;
                        mouse.rightDragDist = 0.f;
                    }
                    for (const auto& b : blockList)
                    {
                        if (b.pos == eHit.voxelPos)
                        {
                            selectedSerial = b.serial;
                            // Capture all block data needed for the popup callback
                            struct ED { int serial; BlockType bt; double start, dur;
                                        int sid; juce::String cfp; bool loop;
                                        double loopDur; juce::Point<int> pos; };
                            ED ed { b.serial, b.blockType, b.startTimeSec, b.durationSec,
                                    b.soundId, juce::String(b.customFilePath),
                                    b.isLooping, b.loopDurationSec,
                                    { (int)ec.x, (int)ec.y } };
                            if (onRequestBlockEdit)
                            {
                                juce::MessageManager::callAsync([this, ed]()
                                {
                                    if (onRequestBlockEdit)
                                        onRequestBlockEdit(ed.serial, ed.bt,
                                                           ed.start, ed.dur,
                                                           ed.sid, ed.cfp,
                                                           ed.loop, ed.loopDur,
                                                           ed.pos);
                                });
                            }
                            break;
                        }
                    }
                }
                else
                {
                    // Missed — deselect; camera drag was already started
                    selectedSerial = -1;
                }
            }
            else if (ec.type == EditClickRequest::SelectLMB)
            {
                int hitSerial = -1;
                if (eHit.hit)
                {
                    for (const auto& b : blockList)
                    {
                        if (b.pos == eHit.voxelPos)
                        {
                            hitSerial    = b.serial;
                            dragStartPos = b.pos;
                            break;
                        }
                    }
                }

                if (hitSerial >= 0)
                {
                    bool skipSelect = false;

                    if (distancePickActive_.load())
                    {
                        skipSelect = true;
                        const int anchor = distancePickAnchorSerial_.load();
                        if (anchor >= 0 && hitSerial != anchor)
                        {
                            Vec3f aPos {}, bPos {};
                            bool haveA = false, haveB = false;
                            for (const auto& b : blockList)
                            {
                                if (b.serial == anchor)
                                {
                                    aPos  = Vec3f((float) b.pos.x,
                                                  (float) b.pos.y,
                                                  (float) b.pos.z);
                                    haveA = true;
                                }
                                if (b.serial == hitSerial)
                                {
                                    bPos  = Vec3f((float) b.pos.x,
                                                  (float) b.pos.y,
                                                  (float) b.pos.z);
                                    haveB = true;
                                }
                            }

                            if (haveA && haveB)
                            {
                                const float dx = bPos.x - aPos.x;
                                const float dy = bPos.y - aPos.y;
                                const float dz = bPos.z - aPos.z;
                                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                                const auto atB = audioEngine.measureSourceAt(
                                    bPos.x, bPos.y, bPos.z);

                                distancePickActive_.store(false);

                                const int aSer = anchor;
                                const int bSer = hitSerial;
                                juce::MessageManager::callAsync(
                                    [this, aSer, bSer, dx, dy, dz, dist, atB]()
                                    {
                                        if (onDistanceMeasured)
                                            onDistanceMeasured(aSer, bSer,
                                                               dx, dy, dz,
                                                               dist, atB.approxDb);
                                    });
                            }
                        }
                    }

                    if (!skipSelect)
                    {
                    selectedSerial          = hitSerial;
                    highlightedBlockSerial_ = hitSerial;

                    if (ec.shift)
                    {
                        if (multiSelection_.count(hitSerial))
                            multiSelection_.erase(hitSerial);
                        else
                            multiSelection_.insert(hitSerial);
                    }
                    else
                    {
                        multiSelection_.clear();
                        multiSelection_.insert(hitSerial);
                    }

                    const int ser = hitSerial;
                    juce::MessageManager::callAsync([this, ser]()
                    {
                        if (onBlockSelected)
                            onBlockSelected(ser);
                    });
                    }   // !skipSelect
                }
                else if (!ec.shift)
                {
                    selectedSerial          = -1;
                    highlightedBlockSerial_ = -1;
                    multiSelection_.clear();
                }
            }
            else if (ec.type == EditClickRequest::AltRecordLMB)
            {
                if (eHit.hit)
                {
                    for (auto& b : blockList)
                    {
                        if (b.pos == eHit.voxelPos)
                        {
                            selectedSerial  = b.serial;
                            dragStartPos    = b.pos;
                            // recordKeyHeld is set by mouseDown (message thread) before
                            // this click is queued, so it is already true here.
                            moveDragPlaneY_ = b.pos.y;

                            if (!b.isRecordingMovement)
                            {
                                b.isRecordingMovement = true;
                                b.recordingStartTime  =
                                    juce::Time::getMillisecondCounterHiRes() * 0.001;
                                b.recordingStartPos   = b.pos;
                                b.recordedMovement.clear();
                                recordingBlockSerial  = b.serial;
                                b.recordedMovement.push_back(
                                    MovementKeyFrame{ 0.0, b.pos });
                                DBG("Recording started on GL thread: block " << b.serial);

                                if (b.soundId >= 0)
                                {
                                    SequencerEvent startEv;
                                    startEv.type           = SequencerEventType::Start;
                                    startEv.blockSerial    = b.serial;
                                    startEv.soundId        = b.soundId;
                                    startEv.triggerTimeSec = 0.0;
                                    startEv.blockX = static_cast<float>(b.pos.x);
                                    startEv.blockY = static_cast<float>(b.pos.y);
                                    startEv.blockZ = static_cast<float>(b.pos.z);
                                    audioEngine.processEvents({ startEv });
                                }
                                juce::MessageManager::callAsync([this]() { repaint(); });
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // ── Drain pending recording stop ──────────────────────────────────────────
    // Placed AFTER pendingEditClick_ so that a fast click (AltRecordLMB start +
    // immediate mouseUp stop queued in the same frame) always processes the start
    // first.  All blockList / recordedMovement access is on the GL thread here.
    {
        PendingRecordingStop stopReq;
        {
            juce::ScopedLock lock(recordingStopMutex_);
            stopReq = pendingRecordingStop_;
            pendingRecordingStop_.active = false;
        }

        if (stopReq.active && recordingBlockSerial >= 0)
        {
            for (auto& b : blockList)
            {
                if (b.serial == recordingBlockSerial)
                {
                    double recordedDuration =
                        juce::Time::getMillisecondCounterHiRes() * 0.001
                        - b.recordingStartTime;

                    DBG("Recording stop processed on GL thread.  serial=" << b.serial
                        << "  keyframes=" << (int)b.recordedMovement.size()
                        << "  duration=" << recordedDuration);

                    // Stop the preview sound (audioEngine is GL-thread-safe here)
                    if (b.soundId >= 0)
                    {
                        SequencerEvent stopEv;
                        stopEv.type        = SequencerEventType::Stop;
                        stopEv.blockSerial = b.serial;
                        stopEv.soundId     = b.soundId;
                        audioEngine.processEvents({ stopEv });
                    }

                    if (b.recordedMovement.size() > 1)
                    {
                        b.isRecordingMovement = false;

                        // Copy keyframes by value so the message thread gets its
                        // own independent copy — no shared reference to blockList.
                        int     captSerial   = b.serial;
                        double  captDuration = recordedDuration;
                        auto    captKeyframes= b.recordedMovement;   // copy
                        auto    captPos      = stopReq.mousePos;

                        if (onRequestMovementConfirm)
                        {
                            juce::MessageManager::callAsync(
                                [this,
                                 captSerial, captDuration,
                                 kf  = std::move(captKeyframes),
                                 pos = captPos]() mutable
                                {
                                    if (onRequestMovementConfirm)
                                        onRequestMovementConfirm(captSerial, captDuration,
                                                                  kf, pos);
                                });
                        }
                    }
                    else
                    {
                        // Too few keyframes — cancel silently
                        b.recordedMovement.clear();
                        b.isRecordingMovement = false;
                        recordingBlockSerial  = -1;
                        DBG("Recording cancelled — insufficient movement");
                    }
                    break;
                }
            }
        }
    }

    // ── Drain pending undo (Ctrl+Z) ───────────────────────────────────────────
    if (pendingUndo_.exchange(false) && !undoStack_.empty())
    {
        const int serialToRemove = undoStack_.back();
        undoStack_.pop_back();

        auto it = std::find_if(blockList.begin(), blockList.end(),
            [&](const BlockEntry& e) { return e.serial == serialToRemove; });

        if (it != blockList.end())
        {
            const int removedSerial = it->serial;
            voxelGrid.remove(it->pos);
            blockList.erase(it);
            if (highlightedBlockSerial_ == removedSerial)
                highlightedBlockSerial_ = -1;
            if (selectedSerial == removedSerial)
                selectedSerial = -1;
            renderer.meshDirty = true;
            pushBlockListToUi(removedSerial);
        }
    }

    if (placeReq.active)
    {
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view = camera.getViewMatrix();
        const Mat4  proj = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(placeReq.x, placeReq.y,
                                               (float)w, (float)h, view, proj);
        Vec3f origin = camera.getPosition();

        Vec3i placePos;
        bool  valid = false;

        if (placeReq.shift)
        {
            // Use the same X,Z anchor captured during the preview hover.
            // If for some reason it isn't set yet, fall back to ground column.
            int ax = shiftAnchorSet ? shiftAnchorX : 0;
            int az = shiftAnchorSet ? shiftAnchorZ : 0;
            if (!shiftAnchorSet)
            {
                Vec3i gp = Raycaster::groundPlaneHit(origin, rayDir);
                if (gp != Vec3i{}) { ax = gp.x; az = gp.z; }
                else
                {
                    Vec3f pt = origin + rayDir * 12.0f;
                    ax = (int)std::floor(pt.x);
                    az = (int)std::floor(pt.z);
                }
            }
            placePos = { ax, shiftPlaneY, az };
            valid    = (placePos.y >= 0);
        }
        else
        {
            // ── Normal placement ──────────────────────────────────────────────
            RaycastResult hit = Raycaster::cast(origin, rayDir, voxelGrid);
            if (hit.hit)
            {
                bool clickedExistingBlock = false;
                for (const auto& b : blockList)
                {
                    if (b.pos == hit.voxelPos)
                    {
                        clickedExistingBlock = true;
                        highlightedBlockSerial_ = b.serial;
                        const int ser = b.serial;
                        juce::MessageManager::callAsync([this, ser]()
                        {
                            if (onBlockSelected)
                                onBlockSelected(ser);
                        });
                        break;
                    }
                }

                if (!clickedExistingBlock)
                {
                    placePos = Raycaster::getPlacementPos(hit);
                    valid    = (placePos.y >= 0);
                }
            }
            else
            {
                // Ground-plane placement only.  Mid-air columns require Shift
                // (shift-plane mode) — do not project into empty sky.
                Vec3i gp = Raycaster::groundPlaneHit(origin, rayDir);
                if (gp != Vec3i{})
                {
                    placePos = gp;
                    valid    = (placePos.y >= 0);
                }
            }
        }

        // Reject out-of-bounds, origin marker, and already-occupied cells
        if (valid && !isInBounds(placePos))
            valid = false;
        if (valid && placePos.x == 0 && placePos.y == 0 && placePos.z == 0)
            valid = false;
        if (valid && voxelGrid.contains(placePos))
            valid = false;

        if (valid)
        {
            voxelGrid.add(placePos);
            const auto placedType = static_cast<BlockType>(activeBlockType_.load());
            BlockEntry newBlock;
            newBlock.serial    = nextSerial++;
            newBlock.blockType = placedType;
            newBlock.pos       = placePos;
            {
                int sid = -1;
                if (libraryLoaded_)
                    sid = library_.defaultSoundForBlockType(placedType, audioEngine);
                else
                    sid = blockTypeDefaultSoundId(placedType);
                if (sid >= 0)
                    newBlock.soundId = sid;
            }
            newBlock.colour = newBlock.getBlockColor(newBlock.blockType, newBlock.soundId);

            // BUG-S1 fix: stagger start time so new blocks don't all pile up at t=0.
            // Assign start = end of the last block, so each new block follows the previous.
            double maxEnd = 0.0;
            for (const auto& e : blockList)
                maxEnd = std::max(maxEnd, e.endTimeSec());
            newBlock.startTimeSec = maxEnd;

            blockList.push_back(newBlock);
            lastPlacedPos = placePos;

            // Track for undo (Ctrl+Z removes the most recently placed block)
            undoStack_.push_back(newBlock.serial);
            if ((int)undoStack_.size() > kMaxUndoDepth)
                undoStack_.erase(undoStack_.begin());

            renderer.meshDirty = true;
            refreshWorkspaceAudioPanel();

            if (onBlockListChanged)
                onBlockListChanged();   
        }
    }

    // ── Remove ────────────────────────────────────────────────────────────────
    if (removeReq.active)
    {
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view = camera.getViewMatrix();
        const Mat4  proj = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(removeReq.x, removeReq.y,
                                               (float)w, (float)h, view, proj);
        RaycastResult hit = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
        if (hit.hit)
        {
            voxelGrid.remove(hit.voxelPos);
            auto it = std::find_if(blockList.begin(), blockList.end(),
                [&](const BlockEntry& e){ return e.pos == hit.voxelPos; });
            if (it != blockList.end())
            {
                const int removedSerial = it->serial;
                blockList.erase(it);
                if (highlightedBlockSerial_ == removedSerial)
                    highlightedBlockSerial_ = -1;
                if (selectedSerial == removedSerial)
                    selectedSerial = -1;
                renderer.meshDirty = true;
                pushBlockListToUi(removedSerial);
            }
        }
    }

    // Per-block rendering replaces the old batch VBO path.
    // VoxelGrid is still maintained for raycasting — just skip the GPU mesh.
    renderer.meshDirty = false; 

    // ── Sequencer + audio ────────────────────────────────────────────────
    {
        // Refresh sample length cache per block so Stretch / Speed modes
        // can compute the right playback rate.  Cheap: small hash lookup.
        const auto& lib = audioEngine.getSampleLibrary();
        const double sr = audioEngine.getOutputSampleRate();
        for (auto& b : blockList)
        {
            auto it = lib.find(b.soundId);
            b.sampleNaturalDurationSec = (it != lib.end() && sr > 0.0)
                ? (it->second.getNumSamples() / sr)
                : 0.0;

            // Compose per-block indefinite mute = block's own isMuted OR the
            // toolbar's per-type mute.  Time-window mute is handled inside
            // the sequencer because it depends on the current transport time.
            b.effectiveMuted = b.isMuted || isBlockTypeMuted(b.blockType);
        }

        const auto events = sequencer.update(transportClock, blockList);
        
        // Process movement events and update voxel grid
        for (const auto& ev : events)
        {
            if (ev.type == SequencerEventType::Movement)
            {
                for (auto& b : blockList)
                {
                    if (b.serial == ev.blockSerial)
                    {
                        // Remove from old position
                        voxelGrid.remove(b.pos);
                        
                        // Update to new position
                        Vec3i newPos = {
                            static_cast<int>(ev.blockX),
                            static_cast<int>(ev.blockY),
                            static_cast<int>(ev.blockZ)
                        };
                        b.pos = newPos;
                        
                        // Add at new position
                        voxelGrid.add(newPos);
                        renderer.meshDirty = true;
                        
                        DBG("Block " << b.serial << " moved to (" 
                            << newPos.x << "," << newPos.y << "," << newPos.z << ")");
                        break;
                    }
                }
            }
        }
        
        audioEngine.processEvents(events);
 
        // Detect loop wrap (transportClock time jumped backwards)
        const double curT = transportClock.currentTimeSec();
        if (transportClock.isLooping() && curT < prevTransportTime)
        {
            sequencer.resetAllBlocks(blockList);
            
            // Reset positions to initial keyframe
            for (auto& b : blockList)
            {
                if (b.hasRecordedMovement && b.movementEnabled
                    && !b.recordedMovement.empty())
                {
                    voxelGrid.remove(b.pos);
                    b.pos = b.recordedMovement[0].position;
                    voxelGrid.add(b.pos);
                }
            }
            renderer.meshDirty = true;
        }
        prevTransportTime = curT;
    }

    // ── Shift-plane preview position ─────────────────────────────────────────
    bool shiftHeld = juce::ModifierKeys::currentModifiers.isShiftDown();
    shiftPreviewValid = false;
    if (shiftHeld)
    {
        Vec3f origin = camera.getPosition();

        // On the first frame Shift is held, capture the X,Z column from
        // whatever the ray currently points at. This anchor never changes
        // until Shift is released and re-pressed, so scrolling only moves Y.
        if (!shiftAnchorSet)
        {
            // Prefer an actual voxel or ground hit for precision
            int ax = 0, az = 0;
            if (hasHit && currentHit.hit)
            {
                Vec3i pp = Raycaster::getPlacementPos(currentHit);
                ax = pp.x;  az = pp.z;
            }
            else
            {
                Vec3i gp = Raycaster::groundPlaneHit(origin, currentRayDir);
                if (gp != Vec3i{}) { ax = gp.x; az = gp.z; }
                else
                {
                    // Last resort: project to fixed depth
                    Vec3f pt = origin + currentRayDir * 12.0f;
                    ax = (int)std::floor(pt.x);
                    az = (int)std::floor(pt.z);
                }
            }
            shiftAnchorX   = ax;
            shiftAnchorZ   = az;
            shiftAnchorSet = true;
        }

        shiftPreviewPos = { shiftAnchorX, shiftPlaneY, shiftAnchorZ };
        shiftPreviewValid = (shiftPreviewPos.y >= 0)
            && !voxelGrid.contains(shiftPreviewPos)
            && !(shiftPreviewPos.x == 0
                 && shiftPreviewPos.y == 0
                 && shiftPreviewPos.z == 0);
    }
    else
    {
        // Reset anchor so next Shift press re-captures
        shiftAnchorSet = false;
    }

    // ── GL state ──────────────────────────────────────────────────────────────
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;

    // Use the physical framebuffer size for the GL viewport (correct for rendering),
    // but keep aspect ratio from logical size so it matches the raycast.
    const float scale = (float)openGLContext.getRenderingScale();
    glViewport(0, 0, (int)(w * scale), (int)(h * scale));
    if (editMode)
        glClearColor(0.28f, 0.04f, 0.04f, 1.f);   // dark red in edit mode
    else
        glClearColor(0.12f, 0.13f, 0.18f, 1.f);   // normal dark background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const float aspect = (float)w / (float)h;
    const Mat4  view   = camera.getViewMatrix();
    const Mat4  proj   = camera.getProjectionMatrix(aspect);
    const Mat4  vp     = proj * view;
    Vec3f lightDir     = Vec3f(0.55f, 1.f, 0.4f).normalized();

    // ── Drain pending camera-path ops (message thread → GL thread) ──────────
    {
        std::vector<PendingPathOp> ops;
        {
            juce::ScopedLock lk(pendingPathOpMutex_);
            ops.swap(pendingPathOps_);
        }
        for (auto& op : ops)
        {
            switch (op.type)
            {
                case PendingPathOp::Replace:
                {
                    juce::ScopedLock lk(cameraPathMutex_);
                    cameraPath_ = std::move(op.payload);
                    CameraPathUtil::sortByTime(cameraPath_);
                    cameraPathHasAny_.store(!cameraPath_.empty());
                    break;
                }
                case PendingPathOp::Clear:
                {
                    juce::ScopedLock lk(cameraPathMutex_);
                    cameraPath_.clear();
                    cameraPathHasAny_.store(false);
                    break;
                }
                case PendingPathOp::AddHold:
                {
                    CameraKeyframe kf;
                    kf.timeSec  = op.timeSec;
                    kf.pos      = camera.getPosition();
                    kf.yawRad   = camera.getYaw();
                    kf.pitchRad = camera.getPitch();
                    kf.mode     = CameraKeyframe::Hold;
                    juce::ScopedLock lk(cameraPathMutex_);
                    cameraPath_.push_back(kf);
                    CameraPathUtil::sortByTime(cameraPath_);
                    cameraPathHasAny_.store(true);
                    break;
                }
                case PendingPathOp::StartRecord:
                {
                    recordingActive_         = true;
                    recordingStartSec_       = transportClock.currentTimeSec();
                    recordingStartWallSec_   = juce::Time::getMillisecondCounterHiRes() * 0.001;
                    // Initialise so the FIRST tick captures immediately.
                    recordingLastCaptureWall_= recordingStartWallSec_
                                              - cameraRecordIntervalSec_.load();
                    recordingBuffer_.clear();
                    cameraPathRecording_.store(true);
                    break;
                }
                case PendingPathOp::StopRecord:
                {
                    if (recordingActive_)
                    {
                        recordingActive_ = false;
                        cameraPathRecording_.store(false);

                        if (recordingBuffer_.size() >= 1)
                        {
                            const double t0 = recordingStartSec_;
                            const double t1 = recordingBuffer_.back().timeSec;
                            juce::ScopedLock lk(cameraPathMutex_);
                            // Remove any existing keyframes inside [t0, t1]
                            cameraPath_.erase(
                                std::remove_if(cameraPath_.begin(), cameraPath_.end(),
                                    [t0, t1](const CameraKeyframe& k)
                                    { return k.timeSec >= t0 && k.timeSec <= t1; }),
                                cameraPath_.end());
                            cameraPath_.insert(cameraPath_.end(),
                                               recordingBuffer_.begin(),
                                               recordingBuffer_.end());
                            CameraPathUtil::sortByTime(cameraPath_);
                            cameraPathHasAny_.store(!cameraPath_.empty());
                        }
                        recordingBuffer_.clear();
                    }
                    break;
                }
                case PendingPathOp::FollowSet:
                {
                    cameraPathFollowEnabled_.store(op.enable);
                    break;
                }
                default: break;
            }
        }
    }

    // ── Capture recording samples (works whether playing or not) ────────────
    if (recordingActive_)
    {
        const double wallNow      = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double captureEvery = cameraRecordIntervalSec_.load();
        if (wallNow >= recordingLastCaptureWall_ + captureEvery)
        {
            // Effective scene time:
            //   * Playing  → ride the transport clock (handles playback rate)
            //   * Paused   → base playhead + wall-clock elapsed since record start
            // The "paused" branch is what lets the user stand still in time and
            // hand-author a moving shot.
            const double effTime = transportClock.isPlaying()
                ? transportClock.currentTimeSec()
                : (recordingStartSec_ + (wallNow - recordingStartWallSec_));

            CameraKeyframe kf;
            kf.timeSec  = effTime;
            kf.pos      = camera.getPosition();
            kf.yawRad   = camera.getYaw();
            kf.pitchRad = camera.getPitch();
            kf.mode     = CameraKeyframe::Lerp;
            recordingBuffer_.push_back(kf);
            recordingLastCaptureWall_ = wallNow;
        }
    }

    // ── Drive camera from path (live preview during play AND scrub) ─────────
    // The path always drives the camera when present, EXCEPT when:
    //   * the user has toggled Free Cam (toolbar) — they want manual control
    //   * a recording session is active — recording always overrides
    // Running the sampler while paused too means dragging the timeline (or
    // typing a time) gives the user an instant camera preview, matching how
    // the per-block keyframe popup previews block positions while scrubbing.
    if (cameraPathFollowEnabled_.load()
        && cameraPathHasAny_.load()
        && !freeCameraOverride_.load()
        && !recordingActive_)
    {
        CameraPose def;
        def.pos      = camera.getPosition();
        def.yawRad   = camera.getYaw();
        def.pitchRad = camera.getPitch();

        std::vector<CameraKeyframe> snap;
        {
            juce::ScopedLock lk(cameraPathMutex_);
            snap = cameraPath_;
        }
        const auto p = CameraPathUtil::sample(snap,
                                              transportClock.currentTimeSec(),
                                              def);
        camera.setPosition(p.pos);
        camera.setYawPitch(p.yawRad, p.pitchRad);
    }

    // Pending audio-anchor toggle (message thread → GL thread).
    if (pendingAnchorOp_.exchange(false))
    {
        const bool wantOn = pendingAnchorEnable_.load();
        if (wantOn && !audioAnchorActive_)
        {
            savedCameraPos_  = camera.getPosition();
            savedCameraLookAt_ = savedCameraPos_ + camera.getForward();
            audioAnchorPos_     = savedCameraPos_;
            audioAnchorForward_ = camera.getForward();
            audioAnchorRight_   = camera.getRight();
            audioAnchorActive_  = true;
        }
        else if (!wantOn && audioAnchorActive_)
        {
            audioAnchorActive_ = false;
            camera.setPosition(savedCameraPos_);
            camera.lookAtTarget(savedCameraLookAt_);
        }
    }

    // Feed listener pose to the audio engine each frame (camera or frozen
    // anchor).  Pan / distance / front-back all use this, not the origin.
    {
        Vec3f listenPos;
        Vec3f fwd;
        Vec3f rgt;

        if (audioAnchorActive_)
        {
            listenPos = audioAnchorPos_;
            fwd       = audioAnchorForward_;
            rgt       = audioAnchorRight_;
        }
        else
        {
            listenPos = camera.getPosition();
            fwd       = camera.getForward();
            rgt       = camera.getRight();
        }

        audioEngine.setListenerPosition(listenPos.x, listenPos.y, listenPos.z);
        audioEngine.setListenerOrientation(fwd.x, fwd.y, fwd.z,
                                           rgt.x, rgt.y, rgt.z);
    }

    if (showFloorPlane_.load())  renderer.renderPlaneXZ(vp);
    if (showWallXPlane_.load())  renderer.renderPlaneYZ(vp);
    if (showWallZPlane_.load())  renderer.renderPlaneXY(vp);
    renderer.renderOriginMarker(vp, lightDir);

    // Per-block colored rendering.  Hidden blocks are excluded so the
    // composer can hide / isolate categories of blocks while still keeping
    // them in the scene (and the sequencer).
    auto isBlockShownLocal = [this](const BlockEntry& b) {
        return !b.isHidden && isBlockTypeVisible(b.blockType);
    };

    for (const auto& b : blockList)
        if (isBlockShownLocal(b))
            renderer.renderSolidBlock(vp, lightDir, b.pos, b.colour);
    

    // ── Highlights ────────────────────────────────────────────────────────────
    if (shiftHeld)
    {
        // Cyan outline: shift-plane placement preview
        if (shiftPreviewValid)
            renderer.renderHighlight(vp, shiftPreviewPos, Vec3f{ 0.1f, 0.9f, 1.f });
    }
    else
    {
        const bool hoveringBlock = hoveredBlockSerial_ >= 0;

        if (!hoveringBlock)
        {
            // Yellow: empty voxel / ground under cursor (Backspace removal target)
            if (hasHit && currentHit.hit)
                renderer.renderHighlight(vp, currentHit.voxelPos,
                                         Vec3f{ 1.f, 0.85f, 0.1f });

            // Green: placement preview on the adjacent face
            Vec3i placePos;
            bool  validPlace = false;
            if (hasHit && currentHit.hit)
            {
                placePos   = Raycaster::getPlacementPos(currentHit);
                validPlace = (placePos.y >= 0) && isInBounds(placePos);
            }
            else
            {
                Vec3i gp = Raycaster::groundPlaneHit(camera.getPosition(), currentRayDir);
                if (gp != Vec3i{}) { placePos = gp; validPlace = (placePos.y >= 0) && isInBounds(placePos); }
            }

            if (validPlace && !voxelGrid.contains(placePos)
                && !(placePos.x == 0 && placePos.y == 0 && placePos.z == 0))
            {
                renderer.renderHighlight(vp, placePos, Vec3f{ 0.2f, 1.f, 0.3f });
            }
        }
        else
        {
            // Green: block under cursor (hover to select)
            for (const auto& b : blockList)
            {
                if (!isBlockShownLocal(b)) continue;
                if (b.serial == hoveredBlockSerial_)
                {
                    renderer.renderHighlight(vp, b.pos, Vec3f{ 0.2f, 1.f, 0.3f });
                    break;
                }
            }
        }

        // Orange: selected block (viewport or timeline click).  Green is now
        // reserved for the "firing right now" pulse so the two states don't
        // collide visually.
        if (highlightedBlockSerial_ >= 0)
        {
            for (const auto& b : blockList)
            {
                if (!isBlockShownLocal(b)) continue;
                if (b.serial == highlightedBlockSerial_)
                {
                    const Vec3f selCol = hoveringBlock && b.serial == hoveredBlockSerial_
                        ? Vec3f{ 1.0f,  0.65f, 0.20f }
                        : Vec3f{ 1.0f,  0.50f, 0.10f };
                    renderer.renderHighlight(vp, b.pos, selCol);
                    break;
                }
            }
        }

        // Brighter green while a block is actively playing
        for (const auto& b : blockList)
        {
            if (!isBlockShownLocal(b)) continue;
            if (b.isPlaying)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 0.f, 1.f, 0.3f });
        }
    }
    
    
    // Cyan: every block in the multi-selection set (Ctrl+A or future
    // click-drag bulk select).  Drawn before the orange "primary" pass
    // so the primary still stands out for the block shown in the sidebar.
    // Visible in both edit and normal mode so the user can confirm what
    // Ctrl+A actually picked up.
    if (!multiSelection_.empty())
    {
        for (const auto& b : blockList)
        {
            if (!isBlockShownLocal(b)) continue;
            if (b.serial == selectedSerial) continue;
            if (multiSelection_.count(b.serial))
                renderer.renderHighlight(vp, b.pos, Vec3f{ 0.25f, 0.75f, 1.f });
        }
    }

    // Orange: currently selected block in edit mode
    if (editMode && selectedSerial >= 0)
    {
        for (const auto& b : blockList)
            if (isBlockShownLocal(b) && b.serial == selectedSerial)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 1.f, 0.5f, 0.1f });
    }

    // Dim yellow highlight for ALL blocks in edit mode so user can see what's selectable
    if (editMode)
    {
        for (const auto& b : blockList)
            if (isBlockShownLocal(b)
                && b.serial != selectedSerial
                && multiSelection_.count(b.serial) == 0)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 0.6f, 0.5f, 0.1f });
    }

    // ── 3D move arrows on selected block (Blender-style gizmo) ─────────────────
    // Drawn last and with depth-test disabled so they're never hidden by the
    // block or terrain — matches user expectation for a manipulator.
    {
        Vec3f gOrigin;
        int   gSerial = -1;
        const bool show = showArrows_.load()
                       && getSelectedGizmoOrigin(gOrigin, gSerial);
        if (show)
        {
            // Re-pick under the current cursor so the hover highlight updates
            // every frame while idle.
            float mx, my;
            { juce::ScopedLock lock(mouseMutex); mx = mouse.curX; my = mouse.curY; }
            const int activeAxis = gizmoActiveAxis_.load();
            const int hoverAxis  = (activeAxis >= 0) ? activeAxis
                                                     : pickGizmoAxis(mx, my);
            gizmoHoveredAxis_.store(hoverAxis);

            const Vec3f red   { 0.95f, 0.20f, 0.20f };
            const Vec3f green { 0.20f, 0.95f, 0.20f };
            const Vec3f blue  { 0.20f, 0.45f, 0.95f };
            const Vec3f cols[3] { red, green, blue };

            glDisable(GL_DEPTH_TEST);
            for (int a = 0; a < 3; ++a)
            {
                const bool hl = (hoverAxis == a) || (activeAxis == a);
                renderer.renderArrow(vp, gOrigin, a, 1.0f, cols[a], hl);
            }
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            gizmoHoveredAxis_.store(-1);
        }
    }

    // ── Gizmo drag queue (message → GL thread) ────────────────────────────────
    {
        GizmoDragRequest req;
        {
            juce::ScopedLock lock(gizmoMutex_);
            req = pendingGizmoDrag_;
            pendingGizmoDrag_.type = GizmoDragRequest::None;
        }

        if (req.type == GizmoDragRequest::Start && req.axis >= 0)
        {
            // Confirm a selected block still exists; find it and snapshot pos.
            for (auto& b : blockList)
            {
                if (b.serial == highlightedBlockSerial_)
                {
                    gizmoActiveAxis_.store(req.axis);
                    gizmoDragSerial_   = b.serial;
                    gizmoDragOrigPos_  = b.pos;

                    const Vec3f origin{ (float) b.pos.x + 0.5f,
                                        (float) b.pos.y + 0.5f,
                                        (float) b.pos.z + 0.5f };
                    gizmoDragStartCoord_ =
                        projectRayOntoAxis(req.x, req.y, origin, req.axis);
                    break;
                }
            }
        }
        else if (req.type == GizmoDragRequest::Move && gizmoActiveAxis_.load() >= 0)
        {
            const int activeAxis = gizmoActiveAxis_.load();
            for (auto& b : blockList)
            {
                if (b.serial == gizmoDragSerial_)
                {
                    const Vec3f origin{ (float) gizmoDragOrigPos_.x + 0.5f,
                                        (float) gizmoDragOrigPos_.y + 0.5f,
                                        (float) gizmoDragOrigPos_.z + 0.5f };
                    const float now = projectRayOntoAxis(req.x, req.y,
                                                         origin, activeAxis);
                    const int delta = (int) std::lround(now - gizmoDragStartCoord_);

                    Vec3i target = gizmoDragOrigPos_;
                    if (activeAxis == 0) target.x += delta;
                    if (activeAxis == 1) target.y += delta;
                    if (activeAxis == 2) target.z += delta;

                    if (target == b.pos) break;             // no change
                    if (target.y < 0)    break;             // never sink below floor
                    if (!isInBounds(target)) break;

                    // Reject if another block occupies the cell.
                    bool occupied = false;
                    for (const auto& other : blockList)
                        if (other.serial != b.serial && other.pos == target)
                            { occupied = true; break; }
                    if (occupied) break;

                    voxelGrid.remove(b.pos);
                    b.pos = target;
                    voxelGrid.add(target);
                    renderer.meshDirty = true;

                    // Push to sidebar / timeline immediately so the Pos field updates.
                    pushBlockListToUi();
                    if (onBlockPropertiesChanged)
                    {
                        const int s = b.serial;
                        juce::MessageManager::callAsync([this, s]()
                        {
                            if (onBlockPropertiesChanged) onBlockPropertiesChanged(s);
                        });
                    }
                    break;
                }
            }
        }
        else if (req.type == GizmoDragRequest::End)
        {
            gizmoActiveAxis_.store(-1);
            gizmoDragSerial_ = -1;
        }
    }

    if (editMode && recordKeyHeld && recordingBlockSerial >= 0)
    {
        for (auto& b : blockList)
        {
            if (b.serial == recordingBlockSerial && b.isRecordingMovement)
            {
                double currentTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
                double relativeTime = currentTime - b.recordingStartTime;
                
                // Only record if position changed from last keyframe
                if (b.recordedMovement.empty() || 
                    b.recordedMovement.back().position != b.pos)
                {
                    b.recordedMovement.push_back(MovementKeyFrame{ relativeTime, b.pos });
                    DBG("Keyframe " << b.recordedMovement.size() 
                        << " at time " << relativeTime 
                        << " pos (" << b.pos.x << "," << b.pos.y << "," << b.pos.z << ")");

                    // Re-trigger preview sound at the new position so pitch/pan update
                    if (b.soundId >= 0)
                    {
                        SequencerEvent stopEv;
                        stopEv.type        = SequencerEventType::Stop;
                        stopEv.blockSerial = b.serial;
                        stopEv.soundId     = b.soundId;

                        SequencerEvent startEv;
                        startEv.type           = SequencerEventType::Start;
                        startEv.blockSerial    = b.serial;
                        startEv.soundId        = b.soundId;
                        startEv.triggerTimeSec = 0.0;
                        startEv.blockX         = static_cast<float>(b.pos.x);
                        startEv.blockY         = static_cast<float>(b.pos.y);
                        startEv.blockZ         = static_cast<float>(b.pos.z);

                        audioEngine.processEvents({ stopEv, startEv });
                    }
                }
                break;
            }
        }
    }

    // ── Process view snap request ───────────────────────────────────────────────
    {
        int snapDir = pendingViewSnap_.exchange(-1);
        if (snapDir >= 0)
            camera.snapToView(snapDir);
    }

    // ── Update gizmo axis projections for paint() ────────────────────────────
    {
        Vec3f fwd = camera.getForward();
        Vec3f rgt = camera.getRight();
        Vec3f up  = rgt.cross(fwd).normalized();

        juce::ScopedLock lock(gizmo_.lock);
        // X axis (1,0,0)
        gizmo_.axes[0] = { rgt.x, -up.x };
        // Y axis (0,1,0)
        gizmo_.axes[1] = { rgt.y, -up.y };
        // Z axis (0,0,1)
        gizmo_.axes[2] = { rgt.z, -up.z };
    }

    // ── Update HUD state (read by paint() on message thread) ─────────────────
    {
        // Compute cursor position for the HUD
        Vec3i curPos { 0, 0, 0 };
        if (shiftHeld && shiftPreviewValid)
        {
            curPos = shiftPreviewPos;
        }
        else if (hasHit && currentHit.hit)
        {
            curPos = Raycaster::getPlacementPos(currentHit);
        }
        else
        {
            Vec3i gp = Raycaster::groundPlaneHit(camera.getPosition(), currentRayDir);
            if (gp != Vec3i{})
                curPos = gp;
        }

        juce::ScopedLock lock(hud.lock);
        hud.isRecording = (editMode && recordKeyHeld && recordingBlockSerial >= 0);
        hud.isEditMode  = editMode;
        hud.isShiftMode = shiftHeld;
        hud.shiftY      = shiftPlaneY;
        hud.voxelCount  = static_cast<int>(voxelGrid.size());
        hud.cursorPos   = curPos;
    }

    // ── Refresh blockList snapshot for safe message-thread reads ─────────────
    // Done last, after all blockList mutations, so the snapshot is always
    // consistent with what was rendered this frame.
    {
        juce::ScopedLock lock(blockListSnapshotMutex_);
        blockListSnapshot_ = blockList;
    }

    {
        juce::ScopedLock lock(multiSelectionSnapshotMutex_);
        multiSelectionSnapshot_.clear();
        multiSelectionSnapshot_.reserve(multiSelection_.size());
        for (int s : multiSelection_)
            multiSelectionSnapshot_.push_back(s);
    }

    // ── Reset GL state for JUCE's 2D overlay compositor ──────────────────────
    // JUCE composites the paint() output as a textured quad on top of this
    // framebuffer. If we leave GL state dirty (depth test, cull face, scissor,
    // bound program / VAO / buffers), the overlay quad can be partially culled
    // or clipped — symptom: right halves of pills/circles/borders missing.
    // Force a known-good baseline before returning.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// processKeyboardMovement
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::processKeyboardMovement(float dt)
{
    using KP = juce::KeyPress;
    const float spd = camera.moveSpeed * dt;

    if (KP::isKeyCurrentlyDown('w') || KP::isKeyCurrentlyDown('W')) camera.moveForward( spd);
    if (KP::isKeyCurrentlyDown('s') || KP::isKeyCurrentlyDown('S')) camera.moveForward(-spd);
    if (KP::isKeyCurrentlyDown('a') || KP::isKeyCurrentlyDown('A')) camera.moveRight  (-spd);
    if (KP::isKeyCurrentlyDown('d') || KP::isKeyCurrentlyDown('D')) camera.moveRight  ( spd);
    if (KP::isKeyCurrentlyDown(juce::KeyPress::spaceKey))             camera.moveUp     ( spd);
    if (KP::isKeyCurrentlyDown('q') || KP::isKeyCurrentlyDown('Q')) camera.moveUp     (-spd);

    if (juce::ModifierKeys::currentModifiers.isAltDown())
    {
        const float extra = camera.moveSpeed * dt;
        if (KP::isKeyCurrentlyDown('w') || KP::isKeyCurrentlyDown('W')) camera.moveForward( extra);
        if (KP::isKeyCurrentlyDown('s') || KP::isKeyCurrentlyDown('S')) camera.moveForward(-extra);
    }

    // Clamp camera to grid bounds so you can't walk off the edge or above the ceiling
    Vec3f pos = camera.getPosition();
    const float kLimit = (float)kGridHalf - 0.5f;
    pos.x = std::clamp(pos.x, -kLimit, kLimit);
    pos.z = std::clamp(pos.z, -kLimit, kLimit);
    pos.y = std::clamp(pos.y, 0.1f, (float)kGridHalf);
    camera.setPosition(pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// doRaycast
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::doRaycast(float mx, float my)
{
    // Use the component's logical size (same coordinate space as mouse events).
    // Do NOT use the GL framebuffer physical pixel size — mouse coords are
    // in logical pixels, so the projection must match.
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    if (w <= 0.f || h <= 0.f) return;

    const float aspect = w / h;
    const Mat4  view = camera.getViewMatrix();
    const Mat4  proj = camera.getProjectionMatrix(aspect);

    Vec3f rayDir  = Raycaster::screenToRay(mx, my, w, h, view, proj);
    currentHit    = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
    hasHit        = currentHit.hit;
    currentRayDir = rayDir;

    hoveredBlockSerial_ = -1;
    if (currentHit.hit)
    {
        for (const auto& b : blockList)
        {
            if (b.pos == currentHit.voxelPos)
            {
                hoveredBlockSerial_ = b.serial;
                break;
            }
        }
    }
}

void ViewPortComponent::pushBlockListToUi(int removedSerial)
{
    juce::ignoreUnused(removedSerial);

    juce::MessageManager::callAsync([this]()
    {
        refreshWorkspaceAudioPanel();

        if (onBlockListChanged)
            onBlockListChanged();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// isPanelHit  –  returns true if screen point (x,y) is inside the block panel
// ─────────────────────────────────────────────────────────────────────────────

// bool ViewPortComponent::isPanelHit(float x, float y) const
// {
//     if (x >= (float)kPanelW)   return false;
//     if (y < (float)kPanelTopY) return false;
//     float localY = y - (float)kPanelTopY;
//     // Use a generous height so the check stays valid even while scrolling
//     int maxH = kHeaderH + (blockListOpen ? 600 : 0);
//     return localY < (float)maxH;
// }

// ─────────────────────────────────────────────────────────────────────────────
// paint  (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::paint(juce::Graphics& g)
{
    // ── Read HUD state ────────────────────────────────────────────────────────
    bool  isRec, isEdit, isShift;
    int   voxels, shiftY;
    Vec3i cur;
    {
        juce::ScopedLock lock(hud.lock);
        isRec   = hud.isRecording;
        isEdit  = hud.isEditMode;
        isShift = hud.isShiftMode;
        voxels  = hud.voxelCount;
        shiftY  = hud.shiftY;
        cur     = hud.cursorPos;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Palette
    // ─────────────────────────────────────────────────────────────────────────
    const juce::Colour pillBg     (0xd4101420);   // dark navy, 83% alpha
    const juce::Colour pillBorder (0x553c4b73);   // muted blue border
    const juce::Colour cPrimary   (0xffe8ecf5);   // near-white values
    const juce::Colour cMuted     (0xff4e5a78);   // muted labels
    const juce::Colour cDivider   (0xff252d44);   // subtle divider line
    const juce::Colour cX         (0xffee5050);   // X axis red
    const juce::Colour cY         (0xff44dd88);   // Y axis green
    const juce::Colour cZ         (0xff4488ee);   // Z axis blue
    const juce::Colour cEdit      (0xffaa77ff);   // edit mode purple
    const juce::Colour cShift     (0xff33ddcc);   // shift plane cyan
    const juce::Colour cRec       (0xffdd3333);   // recording red

    // ─────────────────────────────────────────────────────────────────────────
    // Fonts
    // ─────────────────────────────────────────────────────────────────────────
    const juce::Font fLabel (juce::Font("Inter", 9.f,  juce::Font::plain));
    const juce::Font fValue (juce::Font("Inter", 14.f, juce::Font::plain));
    const juce::Font fBadge (juce::Font("Inter", 11.f, juce::Font::bold));
    const juce::Font fHint  (juce::Font("Inter", 11.f, juce::Font::plain));

    // ─────────────────────────────────────────────────────────────────────────
    // Helper: draw a pill background + border
    // ─────────────────────────────────────────────────────────────────────────
    auto drawPill = [&](juce::Rectangle<float> r, float radius = 7.f)
    {
        g.setColour(pillBg);
        g.fillRoundedRectangle(r, radius);
        g.setColour(pillBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.f);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Stats pill  (top-left)
    //
    //  ┌──────────────────────────────────────────────────┐
    //  │  BLOCKS       │  X  -12   Y   1   Z  39          │
    //  │    42         │                                  │
    //  └──────────────────────────────────────────────────┘
    //
    // Two rows: labels on top (9 pt, muted), values below (14 pt, white).
    // X/Y/Z labels are colour-coded; values are monospaced-width cells.
    // ─────────────────────────────────────────────────────────────────────────
    {
        constexpr int kPY   = 10;    // pill top
        constexpr int kPH   = 36;    // pill height
        constexpr int kIPad = 14;    // inner horizontal padding
        constexpr int kColB = 52;    // width of BLOCKS column
        constexpr int kDivW = 18;    // divider region width (gap+line+gap)
        constexpr int kColC = 40;    // width of each XYZ column (label+value)
        const int     kPX   = 12;    // pill left edge

        const float pillW = kIPad + kColB + kDivW + kColC * 3 + kIPad;
        drawPill({ (float)kPX, (float)kPY, pillW, (float)kPH });

        // ── BLOCKS column ─────────────────────────────────────────────────────
        int cx = kPX + kIPad;

        g.setFont(fLabel);
        g.setColour(cMuted);
        g.drawText("BLOCKS", cx, kPY + 4, kColB, 11,
                   juce::Justification::centredLeft, false);

        g.setFont(fValue);
        g.setColour(cPrimary);
        g.drawText(juce::String(voxels), cx, kPY + 15, kColB, 17,
                   juce::Justification::centredLeft, false);

        cx += kColB;

        // ── Divider ───────────────────────────────────────────────────────────
        float divX = (float)cx + kDivW * 0.5f;
        g.setColour(cDivider);
        g.drawLine(divX, (float)kPY + 8.f, divX, (float)(kPY + kPH) - 8.f, 1.f);
        cx += kDivW;

        // ── X / Y / Z columns ─────────────────────────────────────────────────
        const juce::Colour axCols[] = { cX, cY, cZ };
        const char*        axLbls[] = { "X", "Y", "Z" };
        const int          axVals[] = { cur.x, cur.y, cur.z };

        for (int i = 0; i < 3; ++i)
        {
            // Label row
            g.setFont(fLabel);
            g.setColour(axCols[i]);
            g.drawText(axLbls[i], cx, kPY + 4, kColC, 11,
                       juce::Justification::centredLeft, false);

            // Value row
            g.setFont(fValue);
            g.setColour(cPrimary);
            g.drawText(juce::String(axVals[i]), cx, kPY + 15, kColC, 17,
                       juce::Justification::centredLeft, false);

            cx += kColC;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Mode badge  (inline, to the right of the stats pill)
    //
    // Only visible when in a non-default mode.
    // Uses a tinted fill + coloured border in the mode's accent colour.
    // ─────────────────────────────────────────────────────────────────────────
    if (isRec || isEdit || isShift)
    {
        juce::Colour accent;
        juce::String label;

        if (isRec)
        {
            accent = cRec;
            label  = "REC";
        }
        else if (isShift)
        {
            accent = cShift;
            label  = "SHIFT PLANE  Y = " + juce::String(shiftY);
        }
        else
        {
            accent = cEdit;
            label  = "EDIT MODE";
        }

        constexpr int kBY = 10;
        constexpr int kBH = 36;
        const     int kBX = 12 + 14 + 52 + 18 + 40 * 3 + 14 + 8; // right of stats pill + gap

        g.setFont(fBadge);
        const float textW  = g.getCurrentFont().getStringWidthFloat(label);
        const float dotGap = isRec ? 24.f : 0.f;   // extra room for the dot
        const float pillW  = dotGap + textW + 28.f;

        juce::Rectangle<float> badge((float)kBX, (float)kBY, pillW, (float)kBH);

        // Tinted fill (accent at low alpha)
        g.setColour(accent.withAlpha(0.12f));
        g.fillRoundedRectangle(badge, 7.f);

        // Accent border
        g.setColour(accent.withAlpha(0.65f));
        g.drawRoundedRectangle(badge.reduced(0.5f), 7.f, 1.f);

        if (isRec)
        {
            // Pulsing dot (static for now — no animation needed)
            g.setColour(cRec);
            g.fillEllipse((float)kBX + 12.f, (float)kBY + 12.f, 12.f, 12.f);
        }

        g.setFont(fBadge);
        g.setColour(accent);
        g.drawText(label,
                   (int)badge.getX() + (int)dotGap + 10,
                   kBY + 4,
                   (int)textW + 14,
                   kBH - 8,
                   juce::Justification::centredLeft, false);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Hints pill  (bottom-centre)  — text-hugging width, fully opaque fill.
    //
    // Render order inside this block matters:
    //   1) measure text width with the same font we'll draw it in
    //   2) fill a solid opaque pill sized exactly to that width + padding
    //   3) stroke a bright border
    //   4) draw text inside, with auto-shrink as a safety net for tiny windows
    // ─────────────────────────────────────────────────────────────────────────
    {
        const juce::String dot = juce::String::fromUTF8(" \xc2\xb7 ");

        juce::String hint;
        if (isRec)
            hint = "Release Mouse" + dot + "Finish Recording"
                 + "    Shift+Scroll" + dot + "Change Y (height)";
        else if (isShift)
            hint = "Scroll" + dot + "Raise / Lower Plane"
                 + "    LMB" + dot + "Place"
                 + "    RMB" + dot + "Look";
        else if (isEdit)
            hint = "LMB" + dot + "Select"
                 + "    Drag" + dot + "Box Select"
                 + "    Shift" + dot + "Add to Selection"
                 + "    RMB" + dot + "Edit Block"
                 + "    Alt+LMB" + dot + "Record Movement"
                 + "    Tab" + dot + "Exit Edit";
        else
            hint = "LMB" + dot + "Place"
                 + "    RMB" + dot + "Look / Remove"
                 + "    WASD" + dot + "Move"
                 + "    Space/Q" + dot + "Up / Down"
                 + "    Shift" + dot + "Air Place"
                 + "    Tab" + dot + "Edit"
                 + "    C" + dot + "Clear";

        // ── 1) Measure ───────────────────────────────────────────────────────
        g.setFont(fHint);
        const int textW = (int) std::ceil(g.getCurrentFont().getStringWidthFloat(hint));

        const int kHPad     = 16;
        const int kVPad     = 6;
        const int kBottomM  = 12;
        const int kSideSafe = 12;

        const int idealW = textW + kHPad * 2;
        const int maxW   = juce::jmax(120, getWidth() - kSideSafe * 2);
        const int pillW  = juce::jmin(idealW, maxW);
        const int pillH  = (int) fHint.getHeight() + kVPad * 2;
        const int pillX  = (getWidth() - pillW) / 2;
        const int pillY  = getHeight() - pillH - kBottomM;

        juce::Rectangle<int> pill(pillX, pillY, pillW, pillH);

        // ── 2) Solid fill ────────────────────────────────────────────────────
        // Fully opaque near-black; cannot blend into the floor grid.
        g.setColour(juce::Colour(0xff05070d));
        g.fillRoundedRectangle(pill.toFloat(), 6.f);

        // ── 3) Border ────────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xff3f6fff).withAlpha(0.55f));
        g.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 6.f, 1.2f);

        // ── 4) Text ──────────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xffe8ecf5));
        g.drawFittedText(hint,
                         pill.reduced(kHPad, 0),
                         juce::Justification::centred,
                         1, 0.5f);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // View gizmo + direction buttons  (top-right, unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    {
        const int w = getWidth();
        const int gizmoR    = 30;
        const int gizmoCx   = w - 16 - gizmoR;
        const int gizmoCy   = 30 + gizmoR;
        const float axisLen = static_cast<float>(gizmoR - 4);

        g.setColour(pillBg);
        g.fillEllipse(static_cast<float>(gizmoCx - gizmoR),
                       static_cast<float>(gizmoCy - gizmoR),
                       static_cast<float>(gizmoR * 2),
                       static_cast<float>(gizmoR * 2));
        g.setColour(pillBorder);
        g.drawEllipse(static_cast<float>(gizmoCx - gizmoR),
                       static_cast<float>(gizmoCy - gizmoR),
                       static_cast<float>(gizmoR * 2),
                       static_cast<float>(gizmoR * 2), 1.0f);

        GizmoAxis axes[3];
        {
            juce::ScopedLock lock(gizmo_.lock);
            axes[0] = gizmo_.axes[0];
            axes[1] = gizmo_.axes[1];
            axes[2] = gizmo_.axes[2];
        }

        const juce::Colour axisColors[] = { cX, cY, cZ };
        const char* axisLabels[]        = { "X", "Y", "Z" };

        for (int i = 0; i < 3; ++i)
        {
            float ex = static_cast<float>(gizmoCx) + axes[i].x * axisLen;
            float ey = static_cast<float>(gizmoCy) + axes[i].y * axisLen;

            g.setColour(axisColors[i]);
            g.drawLine(static_cast<float>(gizmoCx), static_cast<float>(gizmoCy),
                       ex, ey, 2.0f);
            g.fillEllipse(ex - 3.f, ey - 3.f, 6.f, 6.f);

            g.setFont(juce::Font(10.f, juce::Font::bold));
            g.drawText(axisLabels[i],
                       static_cast<int>(ex) - 6,
                       static_cast<int>(ey) - 14,
                       12, 12,
                       juce::Justification::centred, false);
        }

        const char* btnLabels[] = { "Front", "Back", "Right", "Left" };
        for (int i = 0; i < 4; ++i)
        {
            auto r = getGizmoButtonRect(i);
            g.setColour(pillBg);
            g.fillRoundedRectangle(r.toFloat(), 4.f);
            g.setColour(pillBorder);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.f, 1.f);
            g.setFont(juce::Font(11.f));
            g.setColour(cPrimary.withAlpha(0.8f));
            g.drawText(btnLabels[i], r, juce::Justification::centred, false);
        }
    }

    // ── Marquee (rubber-band) overlay while drag-selecting in edit mode ─────
    {
        MarqueeDragState md;
        {
            juce::ScopedLock lk(marqueeDragMutex_);
            md = marqueeDrag_;
        }
        if (md.active)
        {
            const float x0 = md.start.x;
            const float y0 = md.start.y;
            const float x1 = md.current.x;
            const float y1 = md.current.y;
            const float minX = std::min(x0, x1);
            const float minY = std::min(y0, y1);
            const float w = std::abs(x1 - x0);
            const float h = std::abs(y1 - y0);

            juce::Rectangle<float> box(minX, minY, w, h);
            g.setColour(juce::Colour(0x3340a8ff));
            g.fillRect(box);
            g.setColour(juce::Colour(0xff40a8ff));
            g.drawRect(box, 1.5f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo helpers
// ─────────────────────────────────────────────────────────────────────────────

juce::Rectangle<int> ViewPortComponent::getGizmoButtonRect(int index) const
{
    const int w = getWidth();
    const int gizmoR  = 30;
    const int btnW    = 52;
    const int btnH    = 22;
    const int btnGap  = 3;
    const int gridW   = btnW * 2 + btnGap;
    const int startX  = w - 16 - gizmoR - gizmoR + (gizmoR * 2 - gridW) / 2;
    const int startY  = 30 + gizmoR * 2 + 8;

    int col = index % 2;
    int row = index / 2;
    return { startX + col * (btnW + btnGap),
             startY + row * (btnH + btnGap),
             btnW, btnH };
}

bool ViewPortComponent::isInGizmoArea(float x, float y) const
{
    for (int i = 0; i < 4; ++i)
        if (getGizmoButtonRect(i).toFloat().contains(x, y))
            return true;

    const int w = getWidth();
    const int gizmoR  = 30;
    const int gizmoCx = w - 16 - gizmoR;
    const int gizmoCy = 30 + gizmoR;
    float dx = x - static_cast<float>(gizmoCx);
    float dy = y - static_cast<float>(gizmoCy);
    return (dx * dx + dy * dy) <= static_cast<float>(gizmoR * gizmoR);
}

void ViewPortComponent::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo helpers (GL thread)
// ─────────────────────────────────────────────────────────────────────────────
//
// We pick using simple world-space ray-vs-segment distance.  The shaft length
// matches kLength in Renderer::buildArrowMeshes() (1.0 unit), so the segment
// runs from the cube center to center+axis*1.0.
// ─────────────────────────────────────────────────────────────────────────────

bool ViewPortComponent::getSelectedGizmoOrigin(Vec3f& outOrigin, int& outSerial) const
{
    if (highlightedBlockSerial_ < 0) return false;
    for (const auto& b : blockList)
    {
        if (b.serial == highlightedBlockSerial_)
        {
            outOrigin = { (float) b.pos.x + 0.5f,
                          (float) b.pos.y + 0.5f,
                          (float) b.pos.z + 0.5f };
            outSerial = b.serial;
            return true;
        }
    }
    return false;
}

int ViewPortComponent::pickGizmoAxis(float mx, float my) const
{
    Vec3f origin; int serial = -1;
    if (!getSelectedGizmoOrigin(origin, serial)) return -1;

    const int   w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return -1;

    const float aspect = (float) w / (float) h;
    const Mat4 view = camera.getViewMatrix();
    const Mat4 proj = camera.getProjectionMatrix(aspect);
    const Vec3f rayDir = Raycaster::screenToRay(mx, my,
                                                (float) w, (float) h, view, proj);
    const Vec3f rayOrg = camera.getPosition();

    constexpr float kShaftLen   = 1.0f;
    constexpr float kHitRadius  = 0.18f;   // generous so the user can grab it

    int   bestAxis = -1;
    float bestT    = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis)
    {
        Vec3f e{ 0.f, 0.f, 0.f };
        if (axis == 0) e.x = 1.f;
        else if (axis == 1) e.y = 1.f;
        else e.z = 1.f;

        // Closest pair between ray (origin rayOrg, dir rayDir) and segment
        // (origin, e * kShaftLen).  Standard skew-line solve.
        const Vec3f w0  = rayOrg - origin;
        const float a   = rayDir.dot(rayDir);
        const float bD  = rayDir.dot(e);
        const float c   = e.dot(e);                // = 1
        const float dD  = rayDir.dot(w0);
        const float eD  = e.dot(w0);
        const float den = a * c - bD * bD;
        if (den < 1e-8f) continue;                 // parallel

        const float tRay = (bD * eD - c * dD) / den;
        const float sAx  = (a  * eD - bD * dD) / den;
        if (tRay <= 0.f) continue;                 // behind camera

        const float sClamped = juce::jlimit(0.f, kShaftLen, sAx);

        const Vec3f pAxis = origin + e * sClamped;
        const Vec3f pRay  = rayOrg + rayDir * tRay;
        const float d2    = (pAxis - pRay).lengthSq();

        if (d2 < kHitRadius * kHitRadius && tRay < bestT)
        {
            bestT    = tRay;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

float ViewPortComponent::projectRayOntoAxis(float mx, float my,
                                            const Vec3f& axisOrigin,
                                            int axis) const
{
    const int w = getWidth(), h = getHeight();
    const float aspect = (h > 0) ? (float) w / (float) h : 1.f;
    const Mat4 view = camera.getViewMatrix();
    const Mat4 proj = camera.getProjectionMatrix(aspect);
    const Vec3f rayDir = Raycaster::screenToRay(mx, my,
                                                (float) w, (float) h, view, proj);
    const Vec3f rayOrg = camera.getPosition();

    Vec3f e{ 0.f, 0.f, 0.f };
    if (axis == 0) e.x = 1.f;
    else if (axis == 1) e.y = 1.f;
    else e.z = 1.f;

    // Plane that contains the axis line and faces the camera most.
    // Normal = e × (rayDir × e) — perpendicular to axis, lying in the
    // axis-ray plane.  If degenerate (axis ≈ ray dir) we fall back to a
    // plane whose normal is the ray-perpendicular component of "up".
    Vec3f temp = rayDir.cross(e);
    Vec3f planeN = e.cross(temp);
    if (planeN.lengthSq() < 1e-8f)
    {
        const Vec3f up = (std::abs(e.y) > 0.9f) ? Vec3f{ 0.f, 0.f, 1.f }
                                                : Vec3f{ 0.f, 1.f, 0.f };
        planeN = up - e * up.dot(e);     // strip axis-aligned component
    }
    planeN = planeN.normalized();

    const float denom = rayDir.dot(planeN);
    if (std::abs(denom) < 1e-6f)
        return 0.f;                        // ray parallel to plane

    const float t = (axisOrigin - rayOrg).dot(planeN) / denom;
    const Vec3f hit = rayOrg + rayDir * t;
    const Vec3f rel = hit - axisOrigin;
    if (axis == 0) return rel.x;
    if (axis == 1) return rel.y;
    return rel.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events  (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // ── View gizmo button clicks ─────────────────────────────────────────────
    if (e.mods.isLeftButtonDown() && isInGizmoArea(e.position.x, e.position.y))
    {
        for (int i = 0; i < 4; ++i)
        {
            if (getGizmoButtonRect(i).toFloat().contains(e.position))
            {
                pendingViewSnap_.store(i);
                return;
            }
        }
        return;  // clicked gizmo circle itself — consume but do nothing
    }

    
    // ── Edit mode: RMB — queue GL-thread raycast to find block for edit popup ──
    // Previously: did Raycaster::cast() here, reading camera/voxelGrid owned by GL thread.
    // Fix: queue the screen position; GL thread does the raycast in renderOpenGL().
    if (editMode && e.mods.isRightButtonDown())
    {
        {
            juce::ScopedLock lock(editClickMutex_);
            pendingEditClick_ = { EditClickRequest::EditRMB,
                                  e.position.x, e.position.y, true };
        }
        // Tentatively start camera drag — GL thread cancels it if a block is hit.
        {
            juce::ScopedLock lock(mouseMutex);
            mouse.rightDown     = true;
            mouse.rightDragDist = 0.f;
            mouse.rightDownPos  = e.position;
        }
        return;
    }

    // ── RMB (normal mode): start look drag ────────────────────────────────────
    if (e.mods.isRightButtonDown())
    {
        juce::ScopedLock lock(mouseMutex);
        mouse.rightDown     = true;
        mouse.rightDragDist = 0.f;
        mouse.rightDownPos  = e.position;
        return;
    }

    // ── LMB on a 3D arrow gizmo: take over the click for axis-drag ───────────
    // Checked before the placement / edit branches so an arrow grab never
    // accidentally places a block or runs the edit raycast.
    if (e.mods.isLeftButtonDown() && !e.mods.isShiftDown() && !e.mods.isAltDown())
    {
        const int axis = gizmoHoveredAxis_.load();
        if (axis >= 0 && showArrows_.load())
        {
            juce::ScopedLock lock(gizmoMutex_);
            pendingGizmoDrag_ = { GizmoDragRequest::Start,
                                  e.position.x, e.position.y, axis };
            return;   // suppress placement / selection for this click
        }
    }

    // ── LMB ──────────────────────────────────────────────────────────────────
    if (e.mods.isLeftButtonDown())
    {
        if (editMode && e.mods.isAltDown())
        {
            // Set the flag NOW on the message thread, before queuing the GL
            // raycast.  This guarantees mouseUp sees recordKeyHeld == true even
            // if it fires before the GL thread processes the AltRecordLMB click
            // (fast-click / single-frame race).
            recordKeyHeld.store(true);

            // Queue GL-thread raycast — previously read camera/blockList on message thread.
            juce::ScopedLock lock(editClickMutex_);
            pendingEditClick_ = { EditClickRequest::AltRecordLMB,
                                  e.position.x, e.position.y, true };
            return;  // never place a block in this mode
        }

        if (editMode)
        {
            // Defer the raycast until mouseUp so a drag can become a rubber-band
            // multi-select instead of an immediate single click.
            juce::ScopedLock lk(marqueeDragMutex_);
            marqueeDrag_ = { true, false, e.mods.isShiftDown(),
                             e.position, e.position };
            return;  // never place a block in edit mode
        }

        // Normal mode LMB: queue for the GL thread.  If the ray hits an existing
        // block, the GL thread selects it instead of placing a new voxel.
        {
            juce::ScopedLock lock(clickMutex);
            pendingPlace = { true, e.position.x, e.position.y,
                             e.mods.isShiftDown() };
        }
    }
}

void ViewPortComponent::mouseUp(const juce::MouseEvent& e)
{
    // ── Finalize marquee drag or deferred edit-mode click ─────────────────────
    {
        MarqueeDragState md;
        {
            juce::ScopedLock lk(marqueeDragMutex_);
            md = marqueeDrag_;
            marqueeDrag_ = {};
        }

        if (md.pending)
        {
            if (md.active)
            {
                juce::ScopedLock lk(marqueeSelectMutex_);
                pendingMarquee_ = { true,
                                    md.start.x, md.start.y,
                                    md.current.x, md.current.y,
                                    md.shiftAdds };
            }
            else
            {
                juce::ScopedLock lock(editClickMutex_);
                pendingEditClick_ = { EditClickRequest::SelectLMB,
                                      md.start.x, md.start.y,
                                      true, md.shiftAdds };
            }
            repaint();
            return;
        }
    }

    // ── End an active gizmo drag ──────────────────────────────────────────────
    if (gizmoActiveAxis_.load() >= 0)
    {
        juce::ScopedLock lock(gizmoMutex_);
        pendingGizmoDrag_ = { GizmoDragRequest::End, e.position.x, e.position.y, -1 };
        repaint();
        return;
    }

    // ── Stop recording if Alt+drag was happening ──────────────────────────────
    // recordKeyHeld is set by mouseDown (message thread) so it is always visible
    // here, even on a fast click before the GL thread processed the start.
    if (recordKeyHeld.load())
    {
        recordKeyHeld.store(false);

        // Queue the stop for the GL thread.  The GL thread owns blockList and
        // recordedMovement — it will read keyframe count, copy keyframes by
        // value, stop preview audio, and fire the confirmation callback.
        // mouseUp must NOT touch blockList directly.
        {
            juce::ScopedLock lock(recordingStopMutex_);
            pendingRecordingStop_ = { true, getMouseXYRelative() };
        }

        {
            juce::ScopedLock lock(mouseMutex);
            mouse.rightDown = false;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
        juce::MessageManager::callAsync([this]() { repaint(); });
        return;
    }
    // Check our own rightDown flag — JUCE clears button from mods before mouseUp fires.
    bool wasRight;
    {
        juce::ScopedLock lock(mouseMutex);
        wasRight = mouse.rightDown;
        if (wasRight)
            mouse.rightDown = false;
    }

    if (wasRight)
    {
        // RMB released — just restore cursor. Removal is Backspace only.
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // LMB release — do nothing (placement happened on mouseDown)
}



// bool ViewPortComponent::keyStateChanged(bool isKeyDown)
// {
//     // Alt key released – stop recording and show popup
//     if (!isKeyDown && recordKeyHeld)
//     {
//         recordKeyHeld = false;
        
//         if (recordingBlockSerial >= 0)
//         {
//             for (auto& b : blockList)
//             {
//                 if (b.serial == recordingBlockSerial)
//                 {
//                     double recordedDuration = (juce::Time::getMillisecondCounterHiRes() * 0.001) 
//                                              - b.recordingStartTime;
                    
//                     if (b.recordedMovement.size() > 1)
//                     {
//                         b.isRecordingMovement = false;
                        
//                         // Show confirmation popup WITH KEYFRAMES
//                         if (onRequestMovementConfirm)
//                         {
//                             auto mousePos = getMouseXYRelative();
//                             // Pass the recorded movement data
//                             onRequestMovementConfirmWithPath(b.serial, 
//                                                             recordedDuration, 
//                                                             b.recordedMovement,
//                                                             mousePos);
//                         }
//                     }
//                     else
//                     {
//                         b.recordedMovement.clear();
//                         b.isRecordingMovement = false;
//                         recordingBlockSerial = -1;
//                         DBG("Recording cancelled - insufficient movement");
//                     }
//                     break;
//                 }
//             }
//         }
//         return true;
//     }
    
//     return Component::keyStateChanged(isKeyDown);
// }


void ViewPortComponent::mouseDrag(const juce::MouseEvent& e)
{
    // ── Active gizmo drag — feed the GL thread with the latest cursor ─────────
    {
        const int activeAxis = gizmoActiveAxis_.load();
        if (activeAxis >= 0)
        {
            juce::ScopedLock lock(gizmoMutex_);
            pendingGizmoDrag_ = { GizmoDragRequest::Move,
                                  e.position.x, e.position.y, activeAxis };
            // Keep mouse position fresh so the hover highlight stays on the arrow.
            { juce::ScopedLock m(mouseMutex); mouse.curX = e.position.x; mouse.curY = e.position.y; }
            repaint();
            return;
        }
    }

    // ── Rubber-band multi-select (edit mode) ──────────────────────────────────
    if (editMode && !e.mods.isAltDown())
    {
        juce::ScopedLock lk(marqueeDragMutex_);
        if (marqueeDrag_.pending || marqueeDrag_.active)
        {
            if (marqueeDrag_.pending && !marqueeDrag_.active)
            {
                const float dx = e.position.x - marqueeDrag_.start.x;
                const float dy = e.position.y - marqueeDrag_.start.y;
                if (dx * dx + dy * dy > 36.f)
                    marqueeDrag_.active = true;
            }
            if (marqueeDrag_.active)
            {
                marqueeDrag_.current = e.position;
                { juce::ScopedLock m(mouseMutex);
                  mouse.curX = e.position.x;
                  mouse.curY = e.position.y; }
                repaint();
                return;
            }
        }
    }

//   ── Recording mode: Alt+drag to move block ───────────────────────────────
    if (editMode && e.mods.isAltDown() && recordKeyHeld && selectedSerial >= 0)
    {
        DBG("Recording drag - selectedSerial: " << selectedSerial);
        
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view_  = camera.getViewMatrix();
        const Mat4  proj   = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(e.position.x, e.position.y,
                                              (float)w, (float)h, view_, proj);
        Vec3f origin = camera.getPosition();

        Vec3i targetPos;
        bool  validTarget = false;

        // ── Axis-locked movement (mirrors the shift-plane placement system) ──
        // Project the cursor ray onto the horizontal plane Y = moveDragPlaneY_.
        // This prevents the block from "flying" toward the camera.
        // Shift + scroll wheel raises / lowers the Y plane (see mouseWheelMove).
        const float planeY = static_cast<float>(moveDragPlaneY_) + 0.5f;
        if (std::abs(rayDir.y) > 0.001f)
        {
            float t = (planeY - origin.y) / rayDir.y;
            if (t > 0.5f)   // reject hits behind or right at the camera
            {
                Vec3f pt = origin + rayDir * t;
                targetPos   = { (int)std::floor(pt.x), moveDragPlaneY_, (int)std::floor(pt.z) };
                validTarget = isInBounds(targetPos) && targetPos.y >= 0;
            }
        }
        
        if (validTarget && targetPos != Vec3i{0, 0, 0})
        {
            // Move the block
            for (auto& b : blockList)
            {
                if (b.serial == selectedSerial)
                {
                    // Remove from old position
                    voxelGrid.remove(b.pos);
                    
                    // Check if target is occupied by another block
                    bool occupied = false;
                    for (const auto& other : blockList)
                    {
                        if (other.serial != selectedSerial && other.pos == targetPos)
                        {
                            occupied = true;
                            break;
                        }
                    }
                    
                    if (!occupied)
                    {
                        DBG("Moving block from (" << b.pos.x << "," << b.pos.y << "," << b.pos.z 
                            << ") to (" << targetPos.x << "," << targetPos.y << "," << targetPos.z << ")");
                        
                        b.pos = targetPos;
                        voxelGrid.add(targetPos);
                        renderer.meshDirty = true;
                    }
                    else
                    {
                        // Re-add at old position if target occupied
                        voxelGrid.add(b.pos);
                    }
                    break;
                }
            }
        }
        return;
    }

    {
        juce::ScopedLock lock(mouseMutex);
        mouse.curX = e.position.x;
        mouse.curY = e.position.y;

        // Only rotate the camera when RMB is the drag button.
        // LMB drags must never affect the camera.
        if (mouse.rightDown && e.mods.isRightButtonDown())
        {
            float dx = e.position.x - mouse.rightDownPos.x;
            float dy = e.position.y - mouse.rightDownPos.y;
            mouse.dX += dx;
            mouse.dY += dy;
            mouse.rightDragDist += std::sqrt(dx * dx + dy * dy);
            mouse.rightDownPos   = e.position;
            setMouseCursor(juce::MouseCursor::NoCursor);
        }
    }
    repaint();
}

void ViewPortComponent::mouseMove(const juce::MouseEvent& e)
{
    {
        juce::ScopedLock lock(mouseMutex);
        mouse.curX = e.position.x;
        mouse.curY = e.position.y;
    }
  
    repaint();
}

void ViewPortComponent::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    // Panel scroll
    // if (isPanelHit(e.position.x, e.position.y))
    // {
    //     blockListScroll = std::max(0, blockListScroll - (int)(w.deltaY * 60.f));
    //     repaint();
    //     return;
    // }

    // Shift held: move the air-placement plane up or down.
    // Also updates the movement drag Y plane when recording.
    if (e.mods.isShiftDown())
    {
        int delta = w.deltaY > 0.f ? 1 : -1;
        shiftScrollDelta.fetch_add(delta);

        if (recordKeyHeld && recordingBlockSerial >= 0)
            moveDragPlaneY_ = std::clamp(moveDragPlaneY_ + delta, 0, kGridHalf - 1);

        return;
    }

    // Normal: camera zoom
    camera.moveForward(w.deltaY * 3.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard
// ─────────────────────────────────────────────────────────────────────────────

bool ViewPortComponent::keyPressed(const juce::KeyPress& k)
{
    // Any Ctrl- / Cmd-combo is delegated to MainComponent (Save, Undo,
    // Copy / Paste / Select All, …).  Returning false here lets the event
    // bubble out of the focused viewport up to its parent, which is what
    // MainComponent::keyPressed expects.
    const auto mods = k.getModifiers();
    if (mods.isCtrlDown() || mods.isCommandDown())
        return false;

    // Esc – clear the multi-selection (Ctrl+A leftovers).
    // We don't deselect the *primary* selectedSerial here, so the sidebar
    // Info panel keeps showing whatever the user was inspecting.
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        requestClearMultiSelection();
        return true;
    }

    // Home – reset camera position (was 'R' before camera-path recording took it)
    if (k.getKeyCode() == juce::KeyPress::homeKey)
    {
        camera.setPosition({ 8.f, 8.f, 8.f });
        return true;
    }

    // R – toggle camera-path recording (only while transport is playing).
    //     Same toggle semantics as the popup's "Record" button.
    if (k.getKeyCode() == 'r' || k.getKeyCode() == 'R')
    {
        toggleCameraPathRecording();
        return true;
    }

    // Backspace only – remove hovered voxel (Delete key is reserved)
    if (k.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (hasHit && currentHit.hit)
        {
            juce::ScopedLock lock(opsMutex);
            pendingOps.push_back({ VoxelOp::REMOVE, currentHit.voxelPos });
        }
        return true;
    }

    // C – clear all voxels (with confirmation).
    // Modifier-free only: Ctrl+C is reserved for Copy (handled in
    // MainComponent), so we MUST NOT treat it as Clear.
    if (k.getKeyCode() == 'c' || k.getKeyCode() == 'C')
    {
        if (blockList.empty())
            return true;

        auto* dialog = new juce::AlertWindow("Clear scene",
                                             "Clear all blocks? This cannot be undone.",
                                             juce::AlertWindow::WarningIcon);
        dialog->addButton("Clear",  1);
        dialog->addButton("Cancel", 0);

        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create([this](int result)
            {
                if (result == 1)
                    pendingClear = true;
            }), true);   // true = delete dialog when dismissed

        return true;
    }

    // Tab – toggle edit mode
    if (k.getKeyCode() == juce::KeyPress::tabKey)
    {
        editMode = !editMode;
        selectedSerial = -1;
        {
            juce::ScopedLock lk(marqueeDragMutex_);
            marqueeDrag_ = {};
        }
        requestClearMultiSelection();
        juce::MessageManager::callAsync([this]() { repaint(); });
        return true;
    }

    if (mods.isAltDown())
    {
        // Only set the flag; actual recording starts on Alt+LMB mouseDown
        // so we know which block the user clicked on before starting.
        if (editMode && selectedSerial >= 0)
            recordKeyHeld = true;

        return true;
    }

    return false;
}

void ViewPortComponent::updateBlockTiming(int serial,int timeIndex,double start,double duration)
{
    // Queue for the GL thread — safe to call from the message thread.
    juce::ScopedLock lock(timingMutex_);
    pendingTimingUpdate_ = { serial, start, duration, true, timeIndex};
}

// ─────────────────────────────────────────────────────────────────────────────
// Clipboard / multi-selection API
//
// All four entry points run on the message thread (called from
// MainComponent::keyPressed).  Each one only mutates a tiny pending-op
// struct under a critical section — the actual work happens later on the
// GL thread when renderOpenGL() drains pendingClipboardOp_.
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::requestCopySelection()
{
    juce::ScopedLock lk(clipboardOpMutex_);
    pendingClipboardOp_.type = PendingClipboardOp::Copy;
}

void ViewPortComponent::requestPasteSelection()
{
    juce::ScopedLock lk(clipboardOpMutex_);
    pendingClipboardOp_.type = PendingClipboardOp::Paste;
}

void ViewPortComponent::requestSelectAll()
{
    juce::ScopedLock lk(clipboardOpMutex_);
    pendingClipboardOp_.type = PendingClipboardOp::SelectAll;
}

void ViewPortComponent::requestClearMultiSelection()
{
    juce::ScopedLock lk(clipboardOpMutex_);
    pendingClipboardOp_.type = PendingClipboardOp::ClearMulti;
}

std::vector<int> ViewPortComponent::getMultiSelectionCopy() const
{
    juce::ScopedLock lock(multiSelectionSnapshotMutex_);
    return multiSelectionSnapshot_;
}

void ViewPortComponent::applyMovementKeyframes(int serial,
                                               std::vector<MovementKeyFrame> frames)
{
    juce::ScopedLock lk(keyframeEditMutex_);
    pendingKeyframeEdits_.push_back({ serial, true, std::move(frames) });
}

void ViewPortComponent::focusGained(FocusChangeType) {}


void ViewPortComponent::highlightBlock(int serial)
{
    highlightedBlockSerial_ = serial;
    repaint();
}

std::optional<BlockEntry> ViewPortComponent::getBlockBySerial(int serial) const
{
    juce::ScopedLock lock(blockListSnapshotMutex_);
    for (const auto& b : blockListSnapshot_)
        if (b.serial == serial)
            return b;

    return std::nullopt;
}


void ViewPortComponent::applySidebarBlockInfo(
    int serial,
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
    std::vector<MuteWindow> muteWindows)
{
    // Queue for the GL thread — safe to call from the message thread.
    // The GL thread drains pendingSidebarEdit_ in renderOpenGL() and does
    // the voxelGrid.move() + blockList mutation there.
    juce::ScopedLock lock(sidebarEditMutex_);
    pendingSidebarEdits_.push_back({
        serial, pos, start, duration, movementEnabled, true,
        playbackMode, movementDurationSec, movementYOffset,
        isMuted, isHidden, loopBufferSec,
        isLooping, loopDurationSec, std::move(muteWindows)
    });
}

void ViewPortComponent::matchBlockDurationToSound(int serial)
{
    // Resolve the sample length on the message thread (read-only access to
    // the sample library; safe before audio is touched).
    const auto& lib = audioEngine.getSampleLibrary();
    const double sampleRate = audioEngine.getOutputSampleRate();
    if (sampleRate <= 0.0)
        return;

    juce::ScopedLock lock(blockListSnapshotMutex_);
    for (auto& b : blockListSnapshot_)
    {
        if (b.serial != serial) continue;
        if (b.soundId < 0) return;

        auto itLib = lib.find(b.soundId);
        if (itLib == lib.end() || itLib->second.getNumSamples() <= 0)
            return;

        const double natDur = itLib->second.getNumSamples() / sampleRate;
        if (natDur <= 0.001) return;

        const bool hasMov = b.hasRecordedMovement
                         && b.movementEnabled
                         && b.recordedMovement.size() >= 2;
        const double movDur = hasMov ? b.effectiveMovementDuration() : 0.0;

        // Same logic as the auto-adjust on sound-assign: preserve movement
        // span and grow the region to the longer of (sound, movement).
        const double newDur = std::max(natDur, movDur);

        juce::ScopedLock lk(sidebarEditMutex_);
        pendingSidebarEdits_.push_back({
            serial, b.pos, b.startTimeSec, newDur,
            b.movementEnabled, true,
            static_cast<uint8_t>(b.playbackMode),
            (hasMov && b.movementDurationSec <= 0.001) ? movDur : b.movementDurationSec,
            b.movementYOffset,
            b.isMuted, b.isHidden, b.loopBufferSec,
            b.isLooping, b.loopDurationSec, b.muteWindows
        });
        // Sidebar info will refresh on the next frame snapshot.
        return;
    }
}

bool ViewPortComponent::exportSceneAudioToFile(const juce::File& outputFile,
                                               SceneAudioExporter::Format format,
                                               juce::String& errorOut)
{
    const auto blocks = getBlockListCopy();
    const double sr   = audioEngine.getOutputSampleRate();
    const auto info   = getExportListenerInfo();

    SceneAudioExporter::ListenerPose pose;
    pose.anchored    = info.anchored;
    pose.posX        = info.pos.x;
    pose.posY        = info.pos.y;
    pose.posZ        = info.pos.z;
    pose.fwdX        = info.forward.x;
    pose.fwdY        = info.forward.y;
    pose.fwdZ        = info.forward.z;
    pose.rightX      = info.right.x;
    pose.rightY      = info.right.y;
    pose.rightZ      = info.right.z;
    pose.sensitivity = info.sensitivity;

    // If a camera path exists, always bake it into the export (regardless of
    // the live "Path On" toggle).  This matches user intuition: the bounce
    // is the rendered scene, not the current live preview state.
    pose.cameraPath = getCameraPathCopy();
    pose.pathFollow = !pose.cameraPath.empty();

    return SceneAudioExporter::bounceToFile(
        blocks,
        audioEngine.getSampleLibrary(),
        sr,
        pose,
        outputFile,
        format,
        errorOut);
}

ViewPortComponent::ExportListenerInfo ViewPortComponent::getExportListenerInfo() const
{
    ExportListenerInfo info;
    info.sensitivity = audioEngine.getSpatialSensitivity();

    if (audioAnchorActive_)
    {
        info.anchored = true;
        info.pos      = audioAnchorPos_;
        info.forward  = audioAnchorForward_;
        info.right    = audioAnchorRight_;
    }
    else
    {
        info.anchored = false;
        info.pos      = camera.getPosition();
        info.forward  = camera.getForward();
        info.right    = camera.getRight();
    }

    return info;
}

void ViewPortComponent::seekTransportClock(double newTimeSec)
{
    transportClock.seekTo(newTimeSec);

    // 1) Reset every block's sequencer state so the next update() can re-fire
    //    Start events for any block whose time range now covers the new
    //    transport position.  This also resets currentKeyframeIndex /
    //    triggeredKeyframes so movement keyframes get re-emitted.
    SequencerEngine::resetAllBlocks(blockList);

    // 2) Snap each moving block's visual position to where it would be at
    //    the new scrub time, even while the transport is paused.  Honours
    //    the user's movementDurationSec and movementYOffset (Phase 1).
    if (SequencerEngine::snapBlockPositionsToTime(blockList, newTimeSec))
    {
        // Voxel grid is keyed by integer positions — keep it consistent with
        // any block.pos that just moved.  Cheaper to rebuild than to track
        // individual moves here.
        voxelGrid.clear();
        for (const auto& b : blockList)
            voxelGrid.add(b.pos);
        renderer.meshDirty = true;
        pushBlockListToUi();
    }

    // 3) Kill anything currently ringing out so audio doesn't double-trigger
    //    or fight the new playhead position.  Voices restart cleanly from the
    //    next update() tick.
    audioEngine.killAllVoices();
}

void ViewPortComponent::addTimeRangeToBlock(int serial, double start, double duration)
{
    for (auto& b : blockList)
    {
        if (b.serial == serial)
        {
            b.addTimeRange(start, duration);
            break;
        }
    }
}

void ViewPortComponent::updateBlockTimeRange(int serial, int timeIndex, double start, double duration)
{
    for (auto& b : blockList)
    {
        if (b.serial == serial &&
            timeIndex >= 0 &&
            timeIndex < static_cast<int>(b.timesList.size()))
        {
            b.timesList[timeIndex].startTimeSec = start;
            b.timesList[timeIndex].durationSec = duration;
            break;
        }
    }
}

bool ViewPortComponent::deleteBlockOrRegion(int serial, int timeIndex)
{
    auto it = std::find_if(blockList.begin(),
                       blockList.end(),
                       [serial](const BlockEntry& b)
                       {
                           return b.serial == serial;
                       });
    if (it != blockList.end())
    {
        if (timeIndex >= 0 && timeIndex < static_cast<int>(it->timesList.size()))
        {
            it->timesList.erase(it->timesList.begin() + timeIndex);
            repaint();
            return true;
        }

        const int removedSerial = it->serial;
        voxelGrid.remove(it->pos);
        blockList.erase(it);
        if (highlightedBlockSerial_ == removedSerial)
            highlightedBlockSerial_ = -1;
        if (selectedSerial == removedSerial)
            selectedSerial = -1;
        renderer.meshDirty = true;
        pushBlockListToUi(removedSerial);
        repaint();
        return true;
    }
    return false;
}

AudioAnalysisResult ViewPortComponent::analyzeBlockAudio(const BlockEntry& block) const
{
    if (block.soundId < 0)
        return {};

    const auto& lib = audioEngine.getSampleLibrary();
    const auto it   = lib.find(block.soundId);
    if (it == lib.end() || it->second.getNumSamples() <= 0)
        return {};

    constexpr double kAnalysisRate = 44100.0;
    return AudioAnalysis::analyze(it->second, kAnalysisRate);
}

void ViewPortComponent::setAudioAnchorEnabled(bool enabled)
{
    pendingAnchorEnable_.store(enabled);
    pendingAnchorOp_.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera-path API (message thread)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<CameraKeyframe> ViewPortComponent::getCameraPathCopy() const
{
    juce::ScopedLock lk(cameraPathMutex_);
    return cameraPath_;
}

void ViewPortComponent::applyCameraPath(std::vector<CameraKeyframe> path)
{
    PendingPathOp op;
    op.type    = PendingPathOp::Replace;
    op.payload = std::move(path);
    juce::ScopedLock lk(pendingPathOpMutex_);
    pendingPathOps_.push_back(std::move(op));
}

void ViewPortComponent::clearCameraPath()
{
    PendingPathOp op;
    op.type = PendingPathOp::Clear;
    juce::ScopedLock lk(pendingPathOpMutex_);
    pendingPathOps_.push_back(std::move(op));
}

void ViewPortComponent::setCameraPathFollowEnabled(bool enabled)
{
    PendingPathOp op;
    op.type   = PendingPathOp::FollowSet;
    op.enable = enabled;
    juce::ScopedLock lk(pendingPathOpMutex_);
    pendingPathOps_.push_back(std::move(op));
}

void ViewPortComponent::toggleCameraPathRecording()
{
    PendingPathOp op;
    op.type = cameraPathRecording_.load()
             ? PendingPathOp::StopRecord
             : PendingPathOp::StartRecord;
    juce::ScopedLock lk(pendingPathOpMutex_);
    pendingPathOps_.push_back(std::move(op));
}

void ViewPortComponent::addCameraHoldFromCurrent(double atTimeSec)
{
    PendingPathOp op;
    op.type    = PendingPathOp::AddHold;
    op.timeSec = atTimeSec;
    juce::ScopedLock lk(pendingPathOpMutex_);
    pendingPathOps_.push_back(std::move(op));
}

void ViewPortComponent::beginDistancePick(int anchorSerial)
{
    distancePickAnchorSerial_.store(anchorSerial);
    distancePickActive_.store(anchorSerial >= 0);
}

juce::File ViewPortComponent::copyAudioToWorkspace(const juce::File& sourceFile)
{
    auto workspaceDir = getWorkspaceAudioDir();

    workspaceDir.createDirectory();

    auto targetFile = workspaceDir.getChildFile(sourceFile.getFileName());

    int counter = 1;

    while (targetFile.existsAsFile())
    {
        targetFile = workspaceDir.getChildFile(
            sourceFile.getFileNameWithoutExtension()
            + "_"
            + juce::String(counter)
            + sourceFile.getFileExtension()
        );

        ++counter;
    }

    if (!sourceFile.copyFileTo(targetFile))
        return {};

    return targetFile;
}


juce::File ViewPortComponent::getWorkspaceAudioDir() const
{
    return contentRoot_
        .getChildFile("Source")
        .getChildFile("workspaceAudios");
}


std::vector<SidebarComponent::AudioItem> ViewPortComponent::scanWorkspaceAudios() const
{
    std::vector<SidebarComponent::AudioItem> audioItems;

    auto dir = getWorkspaceAudioDir();

    if (!dir.isDirectory())
        dir.createDirectory();

    juce::Array<juce::File> files;

    dir.findChildFiles(files,
                       juce::File::findFiles,
                       false,
                       "*.wav;*.mp3;*.aiff;*.flac");

    for (const auto& file : files)
    {
        SidebarComponent::AudioItem item;
        item.fileName = file.getFileName();
        item.relativePath = "workspaceAudios/" + file.getFileName();
        item.fullPath = file.getFullPathName();

        audioItems.push_back(item);
    }

    return audioItems;
}


void ViewPortComponent::refreshWorkspaceAudioPanel()
{
    std::vector<SidebarComponent::AudioItem> items;

    auto dir = getWorkspaceAudioDir();

    if (!dir.exists())
        dir.createDirectory();

    juce::Array<juce::File> files;

    dir.findChildFiles(
        files,
        juce::File::findFiles,
        false,
        "*.wav;*.mp3;*.aiff;*.flac"
    );

    for (const auto& file : files)
    {
        SidebarComponent::AudioItem item;
        item.fileName = file.getFileName();
        item.relativePath = "workspaceAudios/" + file.getFileName();
        item.fullPath = file.getFullPathName();

        items.push_back(item);
    }

    juce::MessageManager::callAsync([this, items]()
    {
        if (sidebar != nullptr)
            sidebar->setAudioList(items);
    });
}