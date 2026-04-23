# View Gizmo & Direction Snap — Implementation Report

## Overview

Added a 3D orientation gizmo and four direction-snap buttons to the top-right corner of the viewport. Inspired by the view navigation widgets in Blender and Godot, this gives users a quick way to orient themselves in the scene and snap to standard views (Front, Back, Left, Right) looking at the origin.

---

## What Was Added

### 1. Camera View Snapping (`Camera.h/.cpp`)

Two new methods on the `Camera` class:

**`lookAtTarget(const Vec3f& target)`**
- Computes the yaw and pitch needed to point the camera at any world position from its current location.
- Uses `atan2` to derive yaw from the XZ direction and pitch from the vertical angle.
- Pitch is clamped to avoid gimbal lock.

**`snapToView(int direction, float distance = 15.f)`**
- Positions the camera at a fixed distance from the origin and orients it to look directly at (0,0,0).
- Distance defaults to 15 grid units. Height is `distance * 0.35f` for a natural slight-downward angle.
- Directions:
  - `0` = Front — camera at (0, h, +dist), looking at origin
  - `1` = Back — camera at (0, h, -dist), looking at origin
  - `2` = Left — camera at (-dist, h, 0), looking at origin
  - `3` = Right — camera at (+dist, h, 0), looking at origin

### 2. Axis Gizmo Display

Painted in `ViewPortComponent::paint()` in the top-right corner of the viewport.

**How the projection works:**
- Each GL frame, the camera's forward and right vectors are used to compute an up vector (`right × forward`).
- The three world axes (X, Y, Z) are projected into 2D screen space by dotting each axis with the camera's right and up vectors.
- The projected coordinates are stored in a `GizmoState` struct protected by a `CriticalSection`, then read by the paint thread.

**What's drawn:**
- A semi-transparent dark circle (60px diameter) as the gizmo background.
- Three colored axis lines from the center, with dots and labels at the endpoints:
  - **Red** = X axis
  - **Green** = Y axis
  - **Blue** = Z axis
- The lines rotate in real time as the camera moves, providing a constant visual reference for orientation.

### 3. Direction Buttons

Four buttons arranged in a 2×2 grid below the gizmo circle:

```
[Front] [Back ]
[Left ] [Right]
```

Each button is 52×22px, painted with a dark semi-transparent background and subtle border. Clicking any button stores a `pendingViewSnap_` value (atomic int) that the GL thread picks up on the next frame and executes via `camera.snapToView()`.

### 4. Click Handling

In `mouseDown()`, a check at the very top of the method tests whether the click falls inside the gizmo area (circle or buttons). If so, the click is consumed and never reaches the placement/edit/camera-drag logic. This prevents accidental block placement when using the gizmo.

The `isInGizmoArea()` helper checks:
1. Whether the point is inside any of the 4 button rectangles.
2. Whether the point is inside the gizmo circle (distance-from-center test).

---

## Files Changed

| File | What Changed |
|------|--------------|
| `Camera.h` | Added `lookAtTarget()` and `snapToView()` declarations |
| `Camera.cpp` | Implemented `lookAtTarget()` (yaw/pitch from target) and `snapToView()` (4 preset camera positions) |
| `ViewPortComponent.h` | Added `pendingViewSnap_` atomic, `GizmoState` struct, `getGizmoButtonRect()`, `isInGizmoArea()` |
| `ViewPortComponent.cpp` | Gizmo axis projection in `renderOpenGL()`, snap processing in `renderOpenGL()`, gizmo + button painting in `paint()`, click interception in `mouseDown()`, helper methods for button rects and hit testing |

---

## Thread Safety

- **Snap request:** Uses `std::atomic<int>` (`pendingViewSnap_`). The message thread writes it in `mouseDown()`, the GL thread reads and clears it in `renderOpenGL()`. No lock needed.
- **Gizmo projection data:** Uses `juce::CriticalSection` in `GizmoState`. Written by the GL thread each frame, read by the message thread in `paint()`. Brief lock, no contention risk.
- **Camera mutation:** `snapToView()` only runs on the GL thread (where the camera is exclusively owned), so there's no cross-thread write issue.

---

## Design Notes

- The gizmo is painted in JUCE's `paint()` method (2D overlay on top of the OpenGL framebuffer), not in OpenGL. This avoids adding another shader program and keeps the implementation simple.
- All four views target the origin (0,0,0), which is the red "listener block" / reference point for spatial audio.
- After snapping, the camera is in free-look mode — WASD and mouse drag work normally from the new position.
- The gizmo consumes clicks so users don't accidentally place blocks when clicking direction buttons.
