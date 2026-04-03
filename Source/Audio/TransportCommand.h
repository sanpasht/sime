#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TransportCommand.h  –  Messages from UI thread to audio thread.
// ─────────────────────────────────────────────────────────────────────────────

enum class TransportCommandType
{
    Play,
    Pause,
    Stop,
    Seek
};

struct TransportCommand
{
    TransportCommandType type = TransportCommandType::Stop;
    double seekTarget = 0.0;
};
