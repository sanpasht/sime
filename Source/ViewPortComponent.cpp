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

static bool isInBounds(const Vec3i& pos)
{
    return pos.x >= -kGridHalf && pos.x < kGridHalf
        && pos.z >= -kGridHalf && pos.z < kGridHalf
        && pos.y >= 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-block color based on type.  Custom blocks get a deterministic palette
// color derived from soundId so different custom WAVs look distinct.
// ─────────────────────────────────────────────────────────────────────────────

static Vec3f getBlockColor(BlockType type, int soundId)
{
    switch (type)
    {
        case BlockType::Violin:   return { 0.85f, 0.22f, 0.18f };
        case BlockType::Piano:    return { 0.25f, 0.45f, 0.90f };
        case BlockType::Drum:     return { 0.22f, 0.78f, 0.32f };
        case BlockType::Listener: return { 1.00f, 0.55f, 0.10f };  // orange
        case BlockType::Custom:
        {
            static const Vec3f kPalette[] = {
                { 0.92f, 0.92f, 0.92f },   // white
                { 0.95f, 0.85f, 0.20f },   // yellow
                { 0.20f, 0.85f, 0.85f },   // cyan
                { 0.85f, 0.38f, 0.85f },   // magenta
                { 0.95f, 0.55f, 0.18f },   // orange
                { 0.65f, 0.48f, 0.90f },   // purple
            };
            constexpr int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);
            int idx = ((soundId % kPaletteSize) + kPaletteSize) % kPaletteSize;
            return kPalette[idx];
        }
    }
    return { 0.5f, 0.5f, 0.5f };
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

ViewPortComponent::ViewPortComponent()
{
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
}

ViewPortComponent::~ViewPortComponent()
{
    openGLContext.detach();
    audioEngine.stop();
}

void ViewPortComponent::loadScene(std::vector<BlockEntry> newBlocks)
{
    // Re-register custom WAV samples so the audio engine knows them
    for (auto& b : newBlocks)
    {
        if (b.blockType == BlockType::Custom && !b.customFilePath.empty())
        {
            juce::File wav(b.customFilePath);
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
        renderer.meshDirty = true;
        juce::MessageManager::callAsync([this]()
        {
            if (sidebar != nullptr)
                sidebar->setBlocks({});
        });
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
        renderer.meshDirty = true;

        std::vector<SidebarComponent::BlockEntry> uiBlocks;
        uiBlocks.reserve(blockList.size());
        for (const auto& e : blockList)
            uiBlocks.push_back({ e.serial, e.pos });

        juce::MessageManager::callAsync([this, uiBlocks]()
        {
            if (sidebar != nullptr)
                sidebar->setBlocks(uiBlocks);
        });
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
        bool changed = !pendingOps.empty();
        for (auto& op : pendingOps)
        {
            if (op.type == VoxelOp::REMOVE)
            {
                voxelGrid.remove(op.pos);
                auto it = std::find_if(blockList.begin(), blockList.end(),
                    [&](const BlockEntry& e){ return e.pos == op.pos; });
                if (it != blockList.end()) blockList.erase(it);
                renderer.meshDirty = true;
            }
        }
        pendingOps.clear();
        if (changed) {
            std::vector<SidebarComponent::BlockEntry> uiBlocks;
            uiBlocks.reserve(blockList.size());

            for (const auto& e : blockList)
                uiBlocks.push_back({ e.serial, e.pos });

            juce::MessageManager::callAsync([this, uiBlocks]()
            {
                if (sidebar != nullptr)
                    sidebar->setBlocks(uiBlocks);
            });
        }
    }

    // ── Handle pending place / remove clicks ──────────────────────────────────
    ClickRequest placeReq, removeReq;
    {
        juce::ScopedLock lock(clickMutex);
        placeReq  = pendingPlace;   pendingPlace.active  = false;
        removeReq = pendingRemove;  pendingRemove.active = false;
    }

    // ── Place ─────────────────────────────────────────────────────────────────
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
                placePos = Raycaster::getPlacementPos(hit);
                valid    = (placePos.y >= 0);
            }
            else
            {
                // Ground plane fallback
                Vec3i gp = Raycaster::groundPlaneHit(origin, rayDir);
                if (gp != Vec3i{})
                {
                    placePos = gp;
                    valid    = (placePos.y >= 0);
                }
                else
                {
                    // Ray pointing up or horizontal: place at fixed distance,
                    // clamped to y >= 0 so it lands on or above the grid floor.
                    Vec3f pt = origin + rayDir * 8.0f;
                    placePos = pt.floor();
                    if (placePos.y < 0) placePos.y = 0;
                    valid = true;
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
            newBlock.soundId   = blockTypeDefaultSoundId(placedType);
            blockList.push_back(newBlock);
            lastPlacedPos = placePos;
            renderer.meshDirty = true;
            std::vector<SidebarComponent::BlockEntry> uiBlocks;
            uiBlocks.reserve(blockList.size());

            for (const auto& e : blockList)
                uiBlocks.push_back({ e.serial, e.pos });

            juce::MessageManager::callAsync([this, uiBlocks]()
            {
                if(sidebar != nullptr)
                    sidebar->setBlocks(uiBlocks);
            });
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
            if (it != blockList.end()) blockList.erase(it);
            renderer.meshDirty = true;
            std::vector<SidebarComponent::BlockEntry> uiBlocks;
            uiBlocks.reserve(blockList.size());

            for (const auto& e : blockList)
                uiBlocks.push_back({ e.serial, e.pos });

            juce::MessageManager::callAsync([this, uiBlocks]()
            {
                if (sidebar != nullptr)
                    sidebar->setBlocks(uiBlocks);
            });
        }
    }

    // Per-block rendering replaces the old batch VBO path.
    // VoxelGrid is still maintained for raycasting — just skip the GPU mesh.
    renderer.meshDirty = false;

    // ── Sequencer + audio ────────────────────────────────────────────────
    {
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
            SequencerEngine::resetAllBlocks(blockList);
            
            // Reset positions to initial keyframe
            for (auto& b : blockList)
            {
                if (b.hasRecordedMovement && !b.recordedMovement.empty())
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
    glClearColor(0.12f, 0.13f, 0.18f, 1.f);
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

    renderer.renderGrid(vp);
    renderer.renderOriginMarker(vp, lightDir);

    // Per-block colored rendering
    for (const auto& b : blockList)
        renderer.renderSolidBlock(vp, lightDir, b.pos,
                                  getBlockColor(b.blockType, b.soundId));
    

    // ── Highlights ────────────────────────────────────────────────────────────
    if (shiftHeld)
    {
        // Cyan outline: shift-plane placement preview
        if (shiftPreviewValid)
            renderer.renderHighlight(vp, shiftPreviewPos, Vec3f{ 0.1f, 0.9f, 1.f });
    }
    else
    {
        // Yellow: targeted voxel (hover for removal)
        if (hasHit && currentHit.hit)
            renderer.renderHighlight(vp, currentHit.voxelPos,
                                     Vec3f{ 1.f, 0.85f, 0.1f });

        // Green: placement preview — always visible whenever the cursor points at
        // something. We show it on the adjacent face of the hit block even if that
        // face is already occupied, so the outline never disappears mid-scene.
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

        if (validPlace && !voxelGrid.contains(placePos) && !(placePos.x == 0 && placePos.y == 0 && placePos.z == 0))
        {
            renderer.renderHighlight(vp, placePos, Vec3f{ 0.2f, 1.f, 0.3f });
        }
        // Highlight playing blocks
        for (const auto& b : blockList)
            if (b.isPlaying)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 0.f, 1.f, 0.3f });
    }
    
    
    // Orange: currently selected block in edit mode
    if (editMode && selectedSerial >= 0)
    {
        for (const auto& b : blockList)
            if (b.serial == selectedSerial)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 1.f, 0.5f, 0.1f });
    }

    // Dim yellow highlight for ALL blocks in edit mode so user can see what's selectable
    if (editMode)
    {
        for (const auto& b : blockList)
            if (b.serial != selectedSerial)
                renderer.renderHighlight(vp, b.pos, Vec3f{ 0.6f, 0.5f, 0.1f });
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

    // ── Update HUD recording flag (read by paint() on message thread) ─────────
    {
        juce::ScopedLock lock(hud.lock);
        hud.isRecording = (editMode && recordKeyHeld && recordingBlockSerial >= 0);
    }
// ── HUD ─────────────────────────────────────────────────────────
    
    {
        juce::String info = "Voxels: " + juce::String((int)voxelGrid.size());

        if (shiftHeld && shiftPreviewValid)
        {
            info += "  Pos: (" + juce::String(shiftPreviewPos.x) + ","
                            + juce::String(shiftPreviewPos.y) + ","
                            + juce::String(shiftPreviewPos.z) + ")";
        }
        else if (hasHit && currentHit.hit)
        {
            Vec3i placePos = Raycaster::getPlacementPos(currentHit);

            info += "  Pos: (" + juce::String(placePos.x) + ","
                            + juce::String(placePos.y) + ","
                            + juce::String(placePos.z) + ")";
        }
        else
        {
            Vec3i gp = Raycaster::groundPlaneHit(camera.getPosition(), currentRayDir);

            if (gp != Vec3i{})
            {
                info += "  Pos: (" + juce::String(gp.x) + ","
                                + juce::String(gp.y) + ","
                                + juce::String(gp.z) + ")";
            }
            else
            {
                Vec3f pt = camera.getPosition() + currentRayDir * 8.0f;
                Vec3i pos = pt.floor();

                info += "  Pos: (" + juce::String(pos.x) + ","
                                + juce::String(pos.y) + ","
                                + juce::String(pos.z) + ")";
            }
        }

        if (shiftHeld){
            info += "  SHIFT PLANE Y=" + juce::String(shiftPlaneY)
                + "  (scroll to raise/lower)";
        }
        else if (editMode && recordKeyHeld && recordingBlockSerial >= 0){
            info += "  RECORDING MOVEMENT  (release mouse to finish)";
        }
        else if (editMode){
            info += "  EDIT MODE  Click a block to set time  |  Alt+Click = Record movement  |  E = Exit";
        }
        else
            info += "  LMB=Place  RMB=Look/Remove  WASD = Move  Shift=AirPlace  E = Edit  C = Clear";

        juce::ScopedLock lock(hud.lock);
        hud.text = info;
    }
  
    
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
    if (KP::isKeyCurrentlyDown(KP::spaceKey))                       camera.moveUp     ( spd);
    if (juce::ModifierKeys::currentModifiers.isCtrlDown())          camera.moveUp     (-spd);

    if (juce::ModifierKeys::currentModifiers.isAltDown())
    {
        const float extra = camera.moveSpeed * dt;
        if (KP::isKeyCurrentlyDown('w') || KP::isKeyCurrentlyDown('W')) camera.moveForward( extra);
        if (KP::isKeyCurrentlyDown('s') || KP::isKeyCurrentlyDown('S')) camera.moveForward(-extra);
    }

    // Clamp camera to grid bounds so you can't walk off the edge
    Vec3f pos = camera.getPosition();
    const float kLimit = (float)kGridHalf - 0.5f;
    pos.x = std::clamp(pos.x, -kLimit, kLimit);
    pos.z = std::clamp(pos.z, -kLimit, kLimit);
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
    // ── HUD bar ───────────────────────────────────────────────────────────────
    juce::String txt;
    bool isRec = false;
    {
        juce::ScopedLock lock(hud.lock);
        txt   = hud.text;
        isRec = hud.isRecording;
    }
    if (txt.isNotEmpty())
    {
        g.setColour(juce::Colours::black.withAlpha(0.65f));
        g.fillRect(0, 0, getWidth(), 22);

        g.setFont(juce::Font(13.f));
        g.setColour(juce::Colour(0xffdddddd));
        g.drawText(txt, 8, 3, getWidth() - 16, 18,
                   juce::Justification::centredLeft, true);
    }

    // ── Recording indicator (red dot + REC label) ─────────────────────────────
    if (isRec)
    {
        constexpr int kIndW = 70, kIndH = 24;
        int indX = getWidth() - kIndW - 12;
        int indY = 30;

        g.setColour(juce::Colours::black.withAlpha(0.60f));
        g.fillRoundedRectangle((float)indX, (float)indY, (float)kIndW, (float)kIndH, 6.f);

        g.setColour(juce::Colour(0xffff2020));
        g.fillEllipse((float)(indX + 8), (float)(indY + 6), 12.f, 12.f);

        g.setFont(juce::Font(13.f, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawText("REC", indX + 26, indY + 4, 36, 16,
                   juce::Justification::centredLeft, false);
    }

    // ── View gizmo + direction buttons (top-right) ──────────────────────────
    {
        const int w = getWidth();
        const int gizmoR    = 30;   // radius of gizmo circle
        const int gizmoCx   = w - 16 - gizmoR;
        const int gizmoCy   = 30 + gizmoR;
        const float axisLen = static_cast<float>(gizmoR - 4);

        // Background circle
        g.setColour(juce::Colours::black.withAlpha(0.50f));
        g.fillEllipse(static_cast<float>(gizmoCx - gizmoR),
                       static_cast<float>(gizmoCy - gizmoR),
                       static_cast<float>(gizmoR * 2),
                       static_cast<float>(gizmoR * 2));
        g.setColour(juce::Colour(0xff334466));
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

        // Draw axis lines: X=red, Y=green, Z=blue
        const juce::Colour axisColors[] = {
            juce::Colour(0xffee3333),
            juce::Colour(0xff33cc33),
            juce::Colour(0xff3388ee)
        };
        const char* axisLabels[] = { "X", "Y", "Z" };

        for (int i = 0; i < 3; ++i)
        {
            float ex = static_cast<float>(gizmoCx) + axes[i].x * axisLen;
            float ey = static_cast<float>(gizmoCy) + axes[i].y * axisLen;

            g.setColour(axisColors[i]);
            g.drawLine(static_cast<float>(gizmoCx), static_cast<float>(gizmoCy),
                       ex, ey, 2.0f);

            // Small dot + label at endpoint
            g.fillEllipse(ex - 3.f, ey - 3.f, 6.f, 6.f);
            g.setFont(juce::Font(10.f, juce::Font::bold));
            g.drawText(axisLabels[i],
                       static_cast<int>(ex) - 6,
                       static_cast<int>(ey) - 14,
                       12, 12,
                       juce::Justification::centred, false);
        }

        // Direction buttons below the gizmo
        const char* btnLabels[] = { "Front", "Back", "Left", "Right" };
        const juce::Colour btnHover(0xff445577);
        const juce::Colour btnNormal(0xcc1a1e2e);

        for (int i = 0; i < 4; ++i)
        {
            auto r = getGizmoButtonRect(i);
            g.setColour(btnNormal);
            g.fillRoundedRectangle(r.toFloat(), 4.f);
            g.setColour(juce::Colour(0xff556688));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.f, 1.f);
            g.setFont(juce::Font(11.f));
            g.setColour(juce::Colour(0xffccddee));
            g.drawText(btnLabels[i], r, juce::Justification::centred, false);
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

    
    // ── Edit mode: right-click on existing block ───────────────────────────────
    if (editMode && e.mods.isRightButtonDown())
    {
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view_  = camera.getViewMatrix();
        const Mat4  proj   = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(e.position.x, e.position.y,
                                              (float)w, (float)h, view_, proj);
        RaycastResult hit = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
 
        if (hit.hit)
        {
            for (const auto& b : blockList)
            {
                if (b.pos == hit.voxelPos)
                {
                    selectedSerial = b.serial;

                    if (onRequestBlockEdit)
                        onRequestBlockEdit(b.serial,
                                           b.blockType,
                                           b.startTimeSec,
                                           b.durationSec,
                                           b.soundId,
                                           juce::String(b.customFilePath),
                                           e.getPosition());
                    return;
                }
            }
        }
 
        // Clicked empty space — deselect and fall through to start camera drag
        selectedSerial = -1;
    }

    // ── RMB: start look drag ──────────────────────────────────────────────────
    if (e.mods.isRightButtonDown())
    {
        juce::ScopedLock lock(mouseMutex);
        mouse.rightDown     = true;
        mouse.rightDragDist = 0.f;
        mouse.rightDownPos  = e.position;
        return;
    }

    // ── LMB: queue place request on the GL thread ─────────────────────────────
    // Storing the pixel position + shift state and letting the GL thread do the
    // raycast avoids the camera race-condition that caused missed placements.
    if (e.mods.isLeftButtonDown())
    {
        if (editMode){

        }
        else{
            juce::ScopedLock lock(clickMutex);
            pendingPlace = { true, e.position.x, e.position.y, e.mods.isShiftDown() };

        } // no placements in edit mode
      
    }



    // ── Edit mode + Alt: LEFT-click to select and start recording ────────────
    if (editMode && e.mods.isLeftButtonDown() && e.mods.isAltDown())
    {
        DBG("Alt+Left click - selecting block for recording");
        
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view_  = camera.getViewMatrix();
        const Mat4  proj   = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(e.position.x, e.position.y,
                                              (float)w, (float)h, view_, proj);
        RaycastResult hit = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
 
        if (hit.hit)
        {
            DBG("Alt+LMB hit voxel at (" << hit.voxelPos.x << "," << hit.voxelPos.y << "," << hit.voxelPos.z << ")");
            
            for (auto& b : blockList)
            {
                if (b.pos == hit.voxelPos)
                {
                    selectedSerial = b.serial;
                    dragStartPos   = b.pos;
                    recordKeyHeld  = true;

                    if (!b.isRecordingMovement)
                    {
                        // ── Start recording ───────────────────────────────────
                        b.isRecordingMovement = true;
                        b.recordingStartTime  = juce::Time::getMillisecondCounterHiRes() * 0.001;
                        b.recordingStartPos   = b.pos;
                        b.recordedMovement.clear();
                        recordingBlockSerial  = b.serial;

                        // Initial keyframe at t=0
                        b.recordedMovement.push_back(MovementKeyFrame{ 0.0, b.pos });
                        DBG("Started recording movement for block " << b.serial);

                        // Trigger a repaint so the REC indicator appears right away
                        juce::MessageManager::callAsync([this]() { repaint(); });

                        // ── Play preview sound so user can hear the block ─────
                        if (b.soundId >= 0)
                        {
                            SequencerEvent startEv;
                            startEv.type           = SequencerEventType::Start;
                            startEv.blockSerial    = b.serial;
                            startEv.soundId        = b.soundId;
                            startEv.triggerTimeSec = 0.0;
                            startEv.blockX         = static_cast<float>(b.pos.x);
                            startEv.blockY         = static_cast<float>(b.pos.y);
                            startEv.blockZ         = static_cast<float>(b.pos.z);
                            audioEngine.processEvents({ startEv });
                        }
                    }
                    return;
                }
            }
        }
        
        return;  // Don't do normal placement while Alt is held
    }


     if (editMode && e.mods.isLeftButtonDown() && !e.mods.isAltDown())
    {
        // ... existing selection code without recording ...
        const int   w = getWidth(), h = getHeight();
        const float aspect = (h > 0) ? (float)w / h : 1.f;
        const Mat4  view_  = camera.getViewMatrix();
        const Mat4  proj   = camera.getProjectionMatrix(aspect);
        Vec3f rayDir = Raycaster::screenToRay(e.position.x, e.position.y,
                                              (float)w, (float)h, view_, proj);
        RaycastResult hit = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
 
        if (hit.hit)
        {
            for (auto& b : blockList)
            {
                if (b.pos == hit.voxelPos)
                {
                    selectedSerial = b.serial;
                    dragStartPos = b.pos;
                    DBG("Block " << b.serial << " selected (no recording)");
                    return;
                }
            }
        }
        
        selectedSerial = -1;
        return;
    }
}

void ViewPortComponent::mouseUp(const juce::MouseEvent& e)
{
    
    // ── Stop recording if Alt+drag was happening ──────────────────────────────
    if (recordKeyHeld)
    {
        recordKeyHeld = false;
        
        if (recordingBlockSerial >= 0)
        {
            for (auto& b : blockList)
            {
                if (b.serial == recordingBlockSerial)
                {
                    double recordedDuration = (juce::Time::getMillisecondCounterHiRes() * 0.001) 
                                             - b.recordingStartTime;
                    
                    DBG("Recording ended. Duration: " << recordedDuration 
                        << ", Keyframes: " << b.recordedMovement.size());
                    
                    // Stop the preview sound that was playing during recording
                    {
                        SequencerEvent stopEv;
                        stopEv.type        = SequencerEventType::Stop;
                        stopEv.blockSerial = b.serial;
                        stopEv.soundId     = b.soundId;
                        audioEngine.processEvents({ stopEv });
                    }

                    if (b.recordedMovement.size() > 1)  // Need at least 2 keyframes
                    {
                        b.isRecordingMovement = false;
                        
                        // Show confirmation popup
                        if (onRequestMovementConfirm)
                        {
                            auto mousePos = getMouseXYRelative();
                            onRequestMovementConfirm(b.serial, recordedDuration, b.recordedMovement, mousePos);
                        }
                    }
                    else
                    {
                        // Not enough movement, cancel
                        b.recordedMovement.clear();
                        b.isRecordingMovement = false;
                        recordingBlockSerial = -1;
                        DBG("Recording cancelled - insufficient movement");
                    }
                    break;
                }
            }
        }

        // Bug 1 fix: always restore cursor, regardless of whether recording succeeded.
        // The early return below would bypass the wasRight cursor-restore block.
        {
            juce::ScopedLock lock(mouseMutex);
            mouse.rightDown = false;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);

        // Clear the REC indicator on the message thread
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
        
        // Find target position (ground plane or existing block face)
        Vec3i targetPos;
        bool validTarget = false;
        
        RaycastResult hit = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
        if (hit.hit)
        {
            targetPos = Raycaster::getPlacementPos(hit);
            validTarget = (targetPos.y >= 0) && isInBounds(targetPos);
        }
        else
        {
            Vec3i gp = Raycaster::groundPlaneHit(camera.getPosition(), rayDir);
            if (gp != Vec3i{})
            {
                targetPos = gp;
                validTarget = (targetPos.y >= 0) && isInBounds(targetPos);
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

    // Shift held: move the air-placement plane up or down
    if (e.mods.isShiftDown())
    {
        shiftScrollDelta.fetch_add(w.deltaY > 0.f ? 1 : -1);
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
    // R – reset camera
    if (k.getKeyCode() == 'r' || k.getKeyCode() == 'R')
    {
        camera.setPosition({ 8.f, 8.f, 8.f });
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

    // C – clear all voxels
    if (k.getKeyCode() == 'c' || k.getKeyCode() == 'C')
    {
        pendingClear = true;
        return true;
    }

    // E – toggle edit mode
    if (k.getKeyCode() == 'e' || k.getKeyCode() == 'E')
    {
        editMode = !editMode;
        selectedSerial = -1;
        juce::MessageManager::callAsync([this]() { repaint(); });
        return true;
    }

    if (k.getModifiers().isAltDown())
    {
        // Only set the flag; actual recording starts on Alt+LMB mouseDown
        // so we know which block the user clicked on before starting.
        if (editMode && selectedSerial >= 0)
            recordKeyHeld = true;

        return true;
    }

    return false;
}

void ViewPortComponent::focusGained(FocusChangeType) {}
