#include "TransportClock.h"
#include <algorithm>

// ---------------------------------------------------------------------------
void TransportClock::start() noexcept
{
    playing_ = true;
    paused_  = false;
}

// ---------------------------------------------------------------------------
void TransportClock::pause() noexcept
{
    if (playing_)
    {
        playing_ = false;
        paused_  = true;
    }
}

// ---------------------------------------------------------------------------
void TransportClock::stop() noexcept
{
    playing_     = false;
    paused_      = false;
    currentTime_ = 0.0;
}

// ---------------------------------------------------------------------------
void TransportClock::update(double deltaTimeSec) noexcept
{
    if (!playing_) return;

    currentTime_ += deltaTimeSec;

    if (looping_ && loopEnd_ > 0.0 && currentTime_ >= loopEnd_)
    {
        // Wrap time, preserving any overshoot so timing stays accurate
        const double overshoot = currentTime_ - loopEnd_;
        currentTime_ = overshoot;   // loops back to 0 + overshoot
    }
}

// ---------------------------------------------------------------------------
void TransportClock::setLooping(bool loop, double loopEndSec) noexcept
{
    looping_ = loop;
    loopEnd_ = std::max(0.0, loopEndSec);
}

// ---------------------------------------------------------------------------
void TransportClock::seekTo(double timeSec) noexcept
{
    currentTime_ = std::max(0.0, timeSec);
}
