// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"
#include "Audio/AudioConfig.h"
#include <juce_opengl/juce_opengl.h>
using namespace juce::gl;
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

MainComponent::MainComponent()
{
    setSize(1280, 720);
    setWantsKeyboardFocus(true);
    openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    openGLContext.setContinuousRepainting(true);

    audioEngine.initialise();
}

MainComponent::~MainComponent()
{
    audioEngine.shutdown();
    openGLContext.detach();
}

// ─────────────────────────────────────────────────────────────────────────────
// GL callbacks
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::newOpenGLContextCreated()
{
    renderer.init();
    renderer.meshDirty = true;
}

void MainComponent::openGLContextClosing()
{
    renderer.shutdown();
}

void MainComponent::renderOpenGL()
{
    // ── Frame timing ──────────────────────────────────────────────────────────
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float  dt  = (lastRenderTime > 0.0)
               ? static_cast<float>(now - lastRenderTime) : 0.016f;
    dt = std::min(dt, 0.1f);
    lastRenderTime = now;

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
            shiftPlaneY = std::max(0, shiftPlaneY + delta);
    }

    // ── Clear all ─────────────────────────────────────────────────────────────
    if (pendingClear.exchange(false))
    {
        voxelGrid.clear();
        blockList.clear();
        soundScene.clear();
        renderer.meshDirty = true;
        juce::ScopedLock lk(blockListMutex);
        blockListUI.clear();
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
                removeSoundBlockAt(op.pos);
                auto it = std::find_if(blockList.begin(), blockList.end(),
                    [&](const BlockEntry& e){ return e.pos == op.pos; });
                if (it != blockList.end()) blockList.erase(it);
                renderer.meshDirty = true;
            }
        }
        pendingOps.clear();
        if (changed) {
            juce::ScopedLock lk(blockListMutex);
            blockListUI = blockList;
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

        // Reject origin marker and already-occupied cells
        if (valid && placePos.x == 0 && placePos.y == 0 && placePos.z == 0)
            valid = false;
        if (valid && voxelGrid.contains(placePos))
            valid = false;

        if (valid)
        {
            voxelGrid.add(placePos);
            blockList.push_back({ nextSerial++, placePos });
            lastPlacedPos = placePos;
            renderer.meshDirty = true;

            createSoundBlockAt(Vec3f(static_cast<float>(placePos.x) + 0.5f,
                                     static_cast<float>(placePos.y) + 0.5f,
                                     static_cast<float>(placePos.z) + 0.5f));

            juce::ScopedLock lk(blockListMutex);
            blockListUI = blockList;
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
            removeSoundBlockAt(hit.voxelPos);
            auto it = std::find_if(blockList.begin(), blockList.end(),
                [&](const BlockEntry& e){ return e.pos == hit.voxelPos; });
            if (it != blockList.end()) blockList.erase(it);
            renderer.meshDirty = true;
            juce::ScopedLock lk(blockListMutex);
            blockListUI = blockList;
        }
    }

    // ── Rebuild mesh ──────────────────────────────────────────────────────────
    if (renderer.meshDirty)
        renderer.rebuildVoxelMesh(voxelGrid);

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

    glViewport(0, 0, w, h);
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

    renderer.render(vp, lightDir);
    renderer.renderOriginMarker(vp, lightDir);
    renderer.renderGrid(vp);

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

        // Green: normal placement preview
        Vec3i placePos;
        bool  validPlace = false;
        if (hasHit && currentHit.hit)
        {
            placePos   = Raycaster::getPlacementPos(currentHit);
            validPlace = (placePos.y >= 0);
        }
        else
        {
            Vec3i gp = Raycaster::groundPlaneHit(camera.getPosition(), currentRayDir);
            if (gp != Vec3i{}) { placePos = gp; validPlace = (placePos.y >= 0); }
        }

        if (validPlace
            && !voxelGrid.contains(placePos)
            && !(placePos.x == 0 && placePos.y == 0 && placePos.z == 0))
        {
            renderer.renderHighlight(vp, placePos, Vec3f{ 0.2f, 1.f, 0.3f });
        }
    }

    // ── HUD ───────────────────────────────────────────────────────────────────
    {
        const Vec3f pos = camera.getPosition();
        juce::String info = "Voxels: " + juce::String((int)voxelGrid.size());
        info += "  Pos: (" + juce::String(pos.x, 1) + ","
                           + juce::String(pos.y, 1) + ","
                           + juce::String(pos.z, 1) + ")";
        bool isPlaying = audioEngine.getTransport().isPlaying();
        double tTime   = audioEngine.getTransport().getCurrentTime();
        int voices     = audioEngine.getStatus().activeVoices.load(std::memory_order_relaxed);
        float peakL    = audioEngine.getStatus().peakLevelL.load(std::memory_order_relaxed);
        float peakR    = audioEngine.getStatus().peakLevelR.load(std::memory_order_relaxed);
        float peakMax  = std::max(peakL, peakR);
        int soundBlocks = soundScene.getBlockCount();

        info += "  [" + juce::String(isPlaying ? "PLAY" : "STOP") + " "
              + juce::String(tTime, 1) + "s V:" + juce::String(voices)
              + " B:" + juce::String(soundBlocks)
              + " dB:" + juce::String(peakMax > 0.0001f ? 20.0f * std::log10(peakMax) : -60.0f, 1) + "]";

        if (shiftHeld)
            info += "  SHIFT PLANE Y=" + juce::String(shiftPlaneY)
                  + "  (scroll to raise/lower)";
        else
            info += "  WASD E/Q Space=Play T=Stop L=LoadWAV";
        juce::ScopedLock lock(hud.lock);
        hud.text = info;
    }

    // Push audio scene snapshot every frame
    {
        Vec3f lPos = camera.getPosition();
        Vec3f lFwd = camera.getForward();
        Vec3f lRgt = camera.getRight();
        double tTime = audioEngine.getTransport().getCurrentTime();
        bool playing = audioEngine.getTransport().isPlaying();

        SceneSnapshot snap = soundScene.buildSnapshot(
            lPos, lFwd, lRgt, tTime, playing,
            audioEngine.getSampleLibrary());
        audioEngine.pushSnapshot(snap);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processKeyboardMovement
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::processKeyboardMovement(float dt)
{
    using KP = juce::KeyPress;
    const float spd = camera.moveSpeed * dt;

    if (KP::isKeyCurrentlyDown('w') || KP::isKeyCurrentlyDown('W')) camera.moveForward( spd);
    if (KP::isKeyCurrentlyDown('s') || KP::isKeyCurrentlyDown('S')) camera.moveForward(-spd);
    if (KP::isKeyCurrentlyDown('a') || KP::isKeyCurrentlyDown('A')) camera.moveRight  (-spd);
    if (KP::isKeyCurrentlyDown('d') || KP::isKeyCurrentlyDown('D')) camera.moveRight  ( spd);
    if (KP::isKeyCurrentlyDown('e') || KP::isKeyCurrentlyDown('E')) camera.moveUp     ( spd);
    if (KP::isKeyCurrentlyDown('q') || KP::isKeyCurrentlyDown('Q')) camera.moveUp     (-spd);

    if (juce::ModifierKeys::currentModifiers.isAltDown())
    {
        const float extra = camera.moveSpeed * dt;
        if (KP::isKeyCurrentlyDown('w') || KP::isKeyCurrentlyDown('W')) camera.moveForward( extra);
        if (KP::isKeyCurrentlyDown('s') || KP::isKeyCurrentlyDown('S')) camera.moveForward(-extra);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// doRaycast
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::doRaycast(float mx, float my)
{
    const int   w = getWidth(), h = getHeight();
    const float aspect = (h > 0) ? (float)w / h : 1.f;
    const Mat4  view = camera.getViewMatrix();
    const Mat4  proj = camera.getProjectionMatrix(aspect);

    Vec3f rayDir  = Raycaster::screenToRay(mx, my, (float)w, (float)h, view, proj);
    currentHit    = Raycaster::cast(camera.getPosition(), rayDir, voxelGrid);
    hasHit        = currentHit.hit;
    currentRayDir = rayDir;
}

// ─────────────────────────────────────────────────────────────────────────────
// isPanelHit  –  returns true if screen point (x,y) is inside the block panel
// ─────────────────────────────────────────────────────────────────────────────

bool MainComponent::isPanelHit(float x, float y) const
{
    if (x >= (float)kPanelW)   return false;
    if (y < (float)kPanelTopY) return false;
    float localY = y - (float)kPanelTopY;
    // Use a generous height so the check stays valid even while scrolling
    int maxH = kHeaderH + (blockListOpen ? 600 : 0);
    return localY < (float)maxH;
}

// ─────────────────────────────────────────────────────────────────────────────
// paint  (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    // ── HUD bar ───────────────────────────────────────────────────────────────
    juce::String txt;
    {
        juce::ScopedLock lock(hud.lock);
        txt = hud.text;
    }
    if (txt.isNotEmpty())
    {
        g.setColour(juce::Colours::black.withAlpha(0.65f));
        g.fillRect(0, 0, getWidth(), kPanelTopY);
        g.setFont(juce::Font(13.f));
        g.setColour(juce::Colour(0xffdddddd));
        g.drawText(txt, 8, 3, getWidth() - 16, kPanelTopY - 4,
                   juce::Justification::centredLeft, true);
    }

    // ── Block list panel ──────────────────────────────────────────────────────
    std::vector<BlockEntry> snapshot;
    {
        juce::ScopedLock lock(blockListMutex);
        snapshot = blockListUI;
    }

    const int itemCount = (int)snapshot.size();
    const int contentH  = blockListOpen ? std::max(1, itemCount * kRowH) + 6 : 0;
    const int panelH    = kHeaderH + contentH;
    const int panelY    = kPanelTopY;

    // Background
    g.setColour(juce::Colour(0xdd0d1120));
    g.fillRect(0, panelY, kPanelW, panelH);

    // Border
    g.setColour(juce::Colour(0xff2a3558));
    g.drawRect(0, panelY, kPanelW, panelH, 1);

    // Header
    g.setFont(juce::Font(12.5f).boldened());
    g.setColour(juce::Colour(0xff88aacc));
    juce::String arrow  = blockListOpen ? "v " : "> ";
    juce::String header = arrow + "Blocks (" + juce::String(itemCount) + ")";
    g.drawText(header, 8, panelY + 5, kPanelW - 16, kHeaderH - 10,
               juce::Justification::centredLeft, true);

    if (!blockListOpen || itemCount == 0) return;

    // Scroll clamp
    int maxScroll = std::max(0, itemCount * kRowH - contentH + 6);
    blockListScroll = std::clamp(blockListScroll, 0, maxScroll);

    // Clip to content area
    g.saveState();
    g.reduceClipRegion(0, panelY + kHeaderH, kPanelW, contentH);

    g.setFont(juce::Font(11.f));
    for (int i = 0; i < itemCount; ++i)
    {
        int rowY = panelY + kHeaderH + 3 + i * kRowH - blockListScroll;
        if (rowY + kRowH < panelY + kHeaderH) continue;
        if (rowY > panelY + panelH)            break;

        // Alternating row background
        if (i % 2 == 0)
        {
            g.setColour(juce::Colour(0x15ffffff));
            g.fillRect(1, rowY, kPanelW - 2, kRowH);
        }

        const auto& e = snapshot[i];
        juce::String row = "Block " + juce::String(e.serial)
                         + "  (" + juce::String(e.pos.x)
                         + ", "  + juce::String(e.pos.y)
                         + ", "  + juce::String(e.pos.z) + ")";

        g.setColour(juce::Colour(0xffaac8e8));
        g.drawText(row, 8, rowY + 3, kPanelW - 16, kRowH - 6,
                   juce::Justification::centredLeft, true);
    }

    g.restoreState();
}

void MainComponent::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events  (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // ── Panel interaction ─────────────────────────────────────────────────────
    if (isPanelHit(e.position.x, e.position.y))
    {
        // Click on header row → toggle open/closed
        if (e.position.y - (float)kPanelTopY < (float)kHeaderH)
        {
            blockListOpen = !blockListOpen;
            repaint();
        }
        return;   // Never treat panel clicks as world placements
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
        juce::ScopedLock lock(clickMutex);
        pendingPlace = { true, e.position.x, e.position.y, e.mods.isShiftDown() };
    }
}

void MainComponent::mouseUp(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
    {
        float dragDist;
        {
            juce::ScopedLock lock(mouseMutex);
            dragDist        = mouse.rightDragDist;
            mouse.rightDown = false;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);

        // Short drag with no significant movement → treat as a remove click
        constexpr float kClickThreshold = 5.f;
        if (dragDist < kClickThreshold
            && !isPanelHit(e.position.x, e.position.y))
        {
            juce::ScopedLock lock(clickMutex);
            pendingRemove = { true, e.position.x, e.position.y, false };
        }
    }
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    juce::ScopedLock lock(mouseMutex);
    mouse.curX = e.position.x;
    mouse.curY = e.position.y;

    if (mouse.rightDown)
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

void MainComponent::mouseMove(const juce::MouseEvent& e)
{
    juce::ScopedLock lock(mouseMutex);
    mouse.curX = e.position.x;
    mouse.curY = e.position.y;
}

void MainComponent::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    // Panel scroll
    if (isPanelHit(e.position.x, e.position.y))
    {
        blockListScroll = std::max(0, blockListScroll - (int)(w.deltaY * 60.f));
        repaint();
        return;
    }

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

bool MainComponent::keyPressed(const juce::KeyPress& k)
{
    // R – reset camera
    if (k.getKeyCode() == 'r' || k.getKeyCode() == 'R')
    {
        camera.setPosition({ 8.f, 8.f, 8.f });
        return true;
    }

    // Delete / Backspace – remove hovered voxel
    if (k.getKeyCode() == juce::KeyPress::deleteKey
     || k.getKeyCode() == juce::KeyPress::backspaceKey)
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

    // Space – toggle transport play/pause
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
    {
        auto& t = audioEngine.getTransport();
        if (t.isPlaying())
            audioEngine.pushTransportCommand({ TransportCommandType::Pause });
        else
            audioEngine.pushTransportCommand({ TransportCommandType::Play });
        return true;
    }

    // L – load a WAV file
    if (k.getKeyCode() == 'l' || k.getKeyCode() == 'L')
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load WAV File", juce::File{}, "*.wav");

        fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto results = fc.getResults();
                if (results.isEmpty()) return;

                auto file = results.getFirst();
                audioEngine.getSampleLibrary().requestLoad(file,
                    [this, file](std::shared_ptr<AudioClip> clip)
                    {
                        if (clip != nullptr)
                        {
                            activeClipId = clip->getClipId();
                            DBG("Active clip set: " + file.getFileName()
                                + " (id=" + activeClipId + ")");
                        }
                    });
            });
        return true;
    }

    // T – stop transport
    if (k.getKeyCode() == 't' || k.getKeyCode() == 'T')
    {
        audioEngine.pushTransportCommand({ TransportCommandType::Stop });
        return true;
    }

    return false;
}

void MainComponent::focusGained(FocusChangeType) {}

void MainComponent::removeSoundBlockAt(const Vec3i& voxelPos)
{
    Vec3f center(static_cast<float>(voxelPos.x) + 0.5f,
                 static_cast<float>(voxelPos.y) + 0.5f,
                 static_cast<float>(voxelPos.z) + 0.5f);

    for (const auto& block : soundScene.getAllBlocks())
    {
        float dx = block.position.x - center.x;
        float dy = block.position.y - center.y;
        float dz = block.position.z - center.z;
        if (dx * dx + dy * dy + dz * dz < 0.5f)
        {
            soundScene.removeBlock(block.id);
            return;
        }
    }
}

void MainComponent::createSoundBlockAt(const Vec3f& pos)
{
    if (activeClipId.isEmpty())
        return;

    SoundBlock block;
    block.position = pos;
    block.startTime = 0.0f;
    block.duration = 0.0f;
    block.clipId = activeClipId;
    block.gainDb = 0.0f;
    block.looping = true;
    block.attenuationRadius = AudioConfig::kDefaultAttenRadius;
    block.spread = 0.0f;
    block.active = true;

    soundScene.addBlock(block);
}
