// ─────────────────────────────────────────────────────────────────────────────
// ViewPortComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "ViewPortComponent.h"
#include <juce_opengl/juce_opengl.h>
using namespace juce::gl;
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

ViewPortComponent::ViewPortComponent()
{
    setSize(1060, 720);
    setWantsKeyboardFocus(true);
    openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    openGLContext.setContinuousRepainting(true);
    audioEngine.loadSample(0, juce::File("/path/to/your.wav"));
    audioEngine.start();
    
}

ViewPortComponent::~ViewPortComponent()
{
    openGLContext.detach();
    audioEngine.stop();
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

    transport.update(static_cast<double>(dt));
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
        renderer.meshDirty = true;
        juce::MessageManager::callAsync([this]()
        {
            if (sidebar != nullptr)
                sidebar->setBlocks({});
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

    // ── Rebuild mesh ──────────────────────────────────────────────────────────
    if (renderer.meshDirty)
        renderer.rebuildVoxelMesh(voxelGrid);

    // ── Sequencer + audio ────────────────────────────────────────────────
    {
        const auto events = sequencer.update(transport, blockList);
        audioEngine.processEvents(events);
 
        // Detect loop wrap (transport time jumped backwards)
        const double curT = transport.currentTimeSec();
        if (transport.isLooping() && curT < prevTransportTime)
            SequencerEngine::resetAllBlocks(blockList);
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

    renderer.renderGrid(vp);
    renderer.render(vp, lightDir);
    renderer.renderOriginMarker(vp, lightDir);
    

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
        else if (editMode){
            info += "  EDIT MODE  Click a block to set time  |  E = Exit";
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
}

// ─────────────────────────────────────────────────────────────────────────────
// doRaycast
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::doRaycast(float mx, float my)
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
    {
        juce::ScopedLock lock(hud.lock);
        txt = hud.text;
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
}

void ViewPortComponent::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events  (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void ViewPortComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // ── Panel interaction ─────────────────────────────────────────────────────
    // if (isPanelHit(e.position.x, e.position.y))
    // {
    //     // Click on header row → toggle open/closed
    //     if (e.position.y - (float)kPanelTopY < (float)kHeaderH)
    //     {
    //         blockListOpen = !blockListOpen;
    //         repaint();
    //     }
    //     return;   // Never treat panel clicks as world placements
    // }

    
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
                                           b.startTimeSec,
                                           b.durationSec,
                                           b.soundId,
                                           e.getPosition());  // ViewPort-local coords
                    return;
                }
            }
        }
 
        // Clicked empty space — deselect
        selectedSerial = -1;
        return;   // don't place while in edit mode
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

void ViewPortComponent::mouseUp(const juce::MouseEvent& e)
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
        if (dragDist < kClickThreshold)   //&& !isPanelHit(e.position.x, e.position.y)
        {
            juce::ScopedLock lock(clickMutex);
            pendingRemove = { true, e.position.x, e.position.y, false };
        }
    }
}

void ViewPortComponent::mouseDrag(const juce::MouseEvent& e)
{
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

    // E – toggle edit mode
    if (k.getKeyCode() == 'e' || k.getKeyCode() == 'E')
    {
        editMode = !editMode;
        selectedSerial = -1;
        juce::MessageManager::callAsync([this]() { repaint(); });
        return true;
    }

    return false;
}

void ViewPortComponent::focusGained(FocusChangeType) {}
