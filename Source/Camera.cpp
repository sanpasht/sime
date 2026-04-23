// ─────────────────────────────────────────────────────────────────────────────
// Camera.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Camera.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi     = 3.14159265358979323846f;
static constexpr float kHalfPi = kPi * 0.5f;

Camera::Camera()
{
    // Default position / orientation already set in the header in-class initializers.
}

// ── Orientation ───────────────────────────────────────────────────────────────

void Camera::rotate(float dyaw, float dpitch)
{
    yaw   += dyaw;
    pitch += dpitch;

    // Keep yaw in [-π, π] to avoid float precision drift over time
    if      (yaw >  kPi) yaw -= 2.f * kPi;
    else if (yaw < -kPi) yaw += 2.f * kPi;

    // Clamp pitch to slightly less than ±90° to prevent gimbal lock
    constexpr float kMaxPitch = kHalfPi * 0.995f;
    pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

// ── Derived vectors ───────────────────────────────────────────────────────────

Vec3f Camera::getForward() const
{
    // Full 3-D forward vector (includes pitch)
    return Vec3f(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * (-std::cos(yaw))   // yaw 0 → forward = (0,0,-1)
    );
}

Vec3f Camera::getHorizontalForward() const
{
    // Yaw-only; used for WASD so the camera never dives into the ground
    return Vec3f(
        std::sin(yaw),
        0.f,
        -std::cos(yaw)
    );
}

Vec3f Camera::getRight() const
{
    // Right vector (always horizontal, no roll in this camera model)
    return Vec3f(
        std::cos(yaw),
        0.f,
        std::sin(yaw)
    );
}

// ── Translation ───────────────────────────────────────────────────────────────

void Camera::moveForward(float delta)
{
    Vec3f h = getHorizontalForward();
    position.x += h.x * delta;
    position.z += h.z * delta;
}

void Camera::moveRight(float delta)
{
    Vec3f r = getRight();
    position.x += r.x * delta;
    position.z += r.z * delta;
}

void Camera::moveUp(float delta)
{
    position.y += delta;
    // Don't let the camera go below eye level above the grid floor
    if (position.y < 0.1f)
        position.y = 0.1f;
}

// ── Target tracking ──────────────────────────────────────────────────────────

void Camera::lookAtTarget(const Vec3f& target)
{
    Vec3f dir = target - position;
    float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);

    yaw   = std::atan2(dir.x, -dir.z);
    pitch = std::atan2(dir.y, horizLen);

    constexpr float kMaxPitch = kHalfPi * 0.995f;
    pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

void Camera::snapToView(int direction, float distance)
{
    const float height = distance * 0.35f;
    const Vec3f origin { 0.f, 0.f, 0.f };

    switch (direction)
    {
        case 0: position = { 0.f, height, distance };  break;  // Front: from +Z
        case 1: position = { 0.f, height, -distance }; break;  // Back:  from -Z
        case 2: position = { -distance, height, 0.f };  break;  // Left:  from -X
        case 3: position = { distance, height, 0.f };   break;  // Right: from +X
        default: return;
    }

    lookAtTarget(origin);
}

// ── Matrix generation ─────────────────────────────────────────────────────────

Mat4 Camera::getViewMatrix() const
{
    Vec3f fwd    = getForward();
    Vec3f center = position + fwd;
    Vec3f up     { 0.f, 1.f, 0.f };
    return Mat4::lookAt(position, center, up);
}

Mat4 Camera::getProjectionMatrix(float aspect) const
{
    return Mat4::perspective(fovY, aspect, nearZ, farZ);
}
