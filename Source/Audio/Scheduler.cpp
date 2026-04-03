// ─────────────────────────────────────────────────────────────────────────────
// Scheduler.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Scheduler.h"
#include <cmath>

bool Scheduler::isBlockActive(const SnapshotBlockEntry& entry,
                               double transportTime) const
{
    if (entry.samplesL == nullptr)
        return false;

    double start = static_cast<double>(entry.startTime);
    if (transportTime < start)
        return false;

    double dur = static_cast<double>(entry.duration);
    if (dur <= 0.0)
    {
        double clipDuration = static_cast<double>(entry.totalSamples)
                            / static_cast<double>(entry.nativeSampleRate);
        if (!entry.looping && transportTime >= start + clipDuration)
            return false;
    }
    else
    {
        if (!entry.looping && transportTime >= start + dur)
            return false;
    }

    return true;
}

void Scheduler::processSnapshot(const SceneSnapshot& snap, VoicePool& pool)
{
    if (!snap.isPlaying)
        return;

    double transportTime = snap.transportTimeSec;

    for (int i = 0; i < snap.numBlocks; ++i)
    {
        const auto& entry = snap.blocks[i];
        bool shouldBeActive = isBlockActive(entry, transportTime);
        Voice* existing = pool.findVoice(entry.id);

        if (shouldBeActive && existing == nullptr)
        {
            double elapsed = transportTime - static_cast<double>(entry.startTime);
            int offsetSamples = static_cast<int>(elapsed * entry.nativeSampleRate);

            if (entry.looping)
            {
                offsetSamples = offsetSamples % entry.totalSamples;
            }
            else if (offsetSamples >= entry.totalSamples)
            {
                continue;
            }

            pool.activateVoice(entry.id, entry.samplesL, entry.samplesR,
                               entry.totalSamples, entry.nativeSampleRate,
                               offsetSamples, entry.gainLinear,
                               entry.attenuationRadius, entry.spread,
                               entry.position, entry.looping);
        }
        else if (!shouldBeActive && existing != nullptr)
        {
            pool.deactivateVoice(entry.id);
        }
        else if (shouldBeActive && existing != nullptr)
        {
            existing->position         = entry.position;
            existing->gainLinear       = entry.gainLinear;
            existing->attenuationRadius = entry.attenuationRadius;
            existing->spread           = entry.spread;
        }
    }

    // Deactivate voices whose blocks are no longer in the snapshot
    Voice* voices = pool.getVoicesArray();
    for (int v = 0; v < pool.getMaxVoices(); ++v)
    {
        if (!voices[v].active)
            continue;

        bool found = false;
        for (int i = 0; i < snap.numBlocks; ++i)
        {
            if (snap.blocks[i].id == voices[v].blockId)
            {
                found = true;
                break;
            }
        }
        if (!found)
            voices[v].reset();
    }
}

void Scheduler::handleTransportCommand(TransportCommand cmd, VoicePool& pool,
                                        const SceneSnapshot& snap)
{
    switch (cmd.type)
    {
        case TransportCommandType::Stop:
            pool.deactivateAll();
            break;

        case TransportCommandType::Seek:
            pool.deactivateAll();
            break;

        case TransportCommandType::Play:
        case TransportCommandType::Pause:
            break;
    }
}
