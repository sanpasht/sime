#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Camera.h  –  Free-look first-person camera.
//
// Orientation is expressed as yaw (rotation around world Y-axis) and pitch
// (rotation around the camera's local X-axis).
//
//   Yaw   0     → looking at  -Z  (OpenGL default)
//   Yaw  +π/2   → looking at  +X
//   Pitch 0     → horizontal
//   Pitch +π/2  → looking straight up
//
// WASD moves horizontally (ignores pitch so the camera stays level).
// Space / Ctrl move vertically.
// Mouse drag feeds rotate().
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"

class Camera
{
public:
    Camera();

    // ── Orientation ───────────────────────────────────────────────────────────

    /// Apply a yaw and pitch delta (in radians).
    /// Pitch is clamped to prevent gimbal flip.
    void rotate(float dyaw, float dpitch);

    // ── Translation ───────────────────────────────────────────────────────────

    /// Move along the horizontal forward vector (no vertical component).
    void moveForward(float delta);

    /// Strafe left/right.
    void moveRight(float delta);

    /// Move up/down along the world Y-axis.
    void moveUp(float delta);

    void setPosition(const Vec3f& pos) { position = pos; }

    // ── Accessors ─────────────────────────────────────────────────────────────

    Vec3f getPosition() const { return position; }
    float getYaw()      const { return yaw;      }
    float getPitch()    const { return pitch;     }

    /// Full 3-D forward vector (accounts for pitch).
    Vec3f getForward()  const;

    /// Horizontal forward vector (pitch ignored; useful for WASD movement).
    Vec3f getHorizontalForward() const;

    /// Right vector (always horizontal, no roll).
    Vec3f getRight() const;

    // ── Matrix generation ─────────────────────────────────────────────────────

    Mat4 getViewMatrix()                      const;
    Mat4 getProjectionMatrix(float aspect)    const;

    // ── Settings ──────────────────────────────────────────────────────────────

    float fovY       = 1.047f;  ///< Vertical FOV in radians (~60°)
    float nearZ      = 0.05f;
    float farZ       = 600.f;
    float moveSpeed  = 10.f;    ///< World units per second
    float lookSpeed  = 0.003f;  ///< Radians per pixel

private:
    Vec3f position { 6.f, 6.f, -4.f };
    float yaw   = -2.3f;   ///< Radians; initial angle looks roughly toward origin
    float pitch = -0.45f;  ///< Radians; slight downward tilt
};
