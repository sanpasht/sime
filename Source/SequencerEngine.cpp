// SequencerEngine.cpp
#include "MathUtils.h"        // Vec3i — must precede BlockEntry.h
#include "SequencerEngine.h"

std::vector<SequencerEvent> SequencerEngine::update(const TransportClock&    clock,
                                                     std::vector<BlockEntry>& blocks)
{
    eventBuffer_.clear();

    if (!clock.isPlaying())
        return eventBuffer_;

    const double now = clock.currentTimeSec();

    for (auto& block : blocks)
    {
        if (block.soundId < 0)
            continue;

        // ── Start ─────────────────────────────────────────────────────────────
        if (!block.hasStarted && now >= block.startTimeSec)
        {
            block.hasStarted = true;
            block.isPlaying  = true;

            SequencerEvent ev;
            ev.type           = SequencerEventType::Start;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            ev.blockX         = static_cast<float>(block.pos.x);
            ev.blockY         = static_cast<float>(block.pos.y);
            ev.blockZ         = static_cast<float>(block.pos.z);
            eventBuffer_.push_back(ev);
        }

        // ── Stop ──────────────────────────────────────────────────────────────
        if (block.hasStarted && !block.hasFinished && now >= block.endTimeSec())
        {
            block.hasFinished = true;
            block.isPlaying   = false;

            SequencerEvent ev;
            ev.type           = SequencerEventType::Stop;
            ev.blockSerial    = block.serial;
            ev.soundId        = block.soundId;
            ev.triggerTimeSec = now;
            eventBuffer_.push_back(ev);
        }
    }

    return eventBuffer_;
}

void SequencerEngine::resetAllBlocks(std::vector<BlockEntry>& blocks) noexcept
{
    for (auto& b : blocks)
        b.resetPlaybackState();
}