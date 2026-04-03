#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Scheduler.h  –  Determines which blocks are active and manages voices.
//
// Audio thread only. No locking.
// ─────────────────────────────────────────────────────────────────────────────

#include "SceneSnapshot.h"
#include "VoicePool.h"
#include "TransportCommand.h"

class Scheduler
{
public:
    void processSnapshot(const SceneSnapshot& snap, VoicePool& pool);
    void handleTransportCommand(TransportCommand cmd, VoicePool& pool,
                                const SceneSnapshot& snap);

private:
    bool isBlockActive(const SnapshotBlockEntry& entry, double transportTime) const;
};
