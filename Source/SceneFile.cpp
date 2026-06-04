// ---------------------------------------------------------------------------
// SceneFile.cpp — Binary .sime scene serialization.
// ---------------------------------------------------------------------------

#include "SceneFile.h"
#include <fstream>
#include <cstring>
#include "ViewPortComponent.h" // 

namespace
{
    static constexpr char     kMagic[4] = { 'S', 'I', 'M', 'E' };
    static constexpr uint16_t kVersion  = 14;  // v14: per-scheduled-sound loop + gap
    static constexpr char     kCamPathMagic[4] = { 'C', 'P', 'T', 'H' };

    // Tiny endian-agnostic helpers (no-op on x86 but keeps intent clear)
    template <typename T>
    void writeVal(std::ofstream& f, T v)  { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

    template <typename T>
    bool readVal(std::ifstream& f, T& v)  { f.read(reinterpret_cast<char*>(&v), sizeof(T)); return f.good(); }
}

bool SceneFile::save(const std::string& path, const std::vector<BlockEntry>& blocks)
{
    return save(path, blocks, {});
}

bool SceneFile::save(const std::string& path,
                     const std::vector<BlockEntry>&     blocks,
                     const std::vector<CameraKeyframe>& cameraPath)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;

    // --- Header ---
    f.write(kMagic, 4);
    writeVal<uint16_t>(f, kVersion);
    writeVal<uint32_t>(f, static_cast<uint32_t>(blocks.size()));
    writeVal<uint16_t>(f, 0); // reserved

    // --- Block records ---
    for (const auto& b : blocks)
    {
        writeVal<int32_t>(f, b.serial);
        writeVal<uint8_t>(f, static_cast<uint8_t>(b.blockType));

        writeVal<int32_t>(f, b.pos.x);
        writeVal<int32_t>(f, b.pos.y);
        writeVal<int32_t>(f, b.pos.z);

        writeVal<int32_t>(f, b.soundId);
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.x * 255.0f));
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.y * 255.0f));
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.z * 255.0f));

        uint16_t pathLen = static_cast<uint16_t>(b.customFilePath.size());
        writeVal<uint16_t>(f, pathLen);
        if (pathLen > 0)
            f.write(b.customFilePath.data(), pathLen);

        writeVal<double>(f, b.startTimeSec);
        writeVal<double>(f, b.durationSec);
        writeVal<uint8_t>(f, b.durationLocked ? 1 : 0);

        writeVal<uint32_t>(f, static_cast<uint32_t>(b.timesList.size()));

        for (const auto& t : b.timesList)
        {
            writeVal<double>(f, t.startTimeSec);
            writeVal<double>(f, t.durationSec);
        }

        writeVal<uint8_t>(f, b.hasRecordedMovement ? 1 : 0);
        if (b.hasRecordedMovement)
        {
            writeVal<uint32_t>(f, static_cast<uint32_t>(b.recordedMovement.size()));
            for (const auto& kf : b.recordedMovement)
            {
                writeVal<double>(f, kf.timeSec);
                writeVal<int32_t>(f, kf.position.x);
                writeVal<int32_t>(f, kf.position.y);
                writeVal<int32_t>(f, kf.position.z);
            }
        }

        // --- v2 additions ---
        writeVal<uint8_t>(f, b.isLooping ? 1 : 0);
        writeVal<double>(f, b.loopDurationSec);

        // --- v5 additions: Phase 1 movement controls ---
        writeVal<uint8_t>(f, static_cast<uint8_t>(b.playbackMode));
        writeVal<double>(f, b.movementDurationSec);
        writeVal<int32_t>(f, b.movementYOffset);

        // --- v6 additions ---
        writeVal<uint8_t>(f, b.movementEnabled ? 1 : 0);

        // --- v7 additions ---
        writeVal<uint8_t>(f, b.isMuted  ? 1 : 0);
        writeVal<uint8_t>(f, b.isHidden ? 1 : 0);
        writeVal<double>(f, b.loopBufferSec);

        // --- v8 additions: legacy single time-window mute ---
        // We still write zeros here so v8 readers don't see garbage; the
        // real schedule now lives in muteWindows below (v9+).
        writeVal<double>(f, 0.0);
        writeVal<double>(f, 0.0);

        // --- v9 additions: scheduled mute windows (multi-window) ---
        writeVal<uint32_t>(f, static_cast<uint32_t>(b.muteWindows.size()));
        for (const auto& w : b.muteWindows)
        {
            writeVal<double>(f, w.startSec);
            writeVal<double>(f, w.durationSec);
        }

        // --- v12 additions: scheduled sounds (multi-sound) ---
        // soundId is runtime-only; persist the library-relative path so the
        // sound can be re-resolved on load (same scheme as customFilePath).
        writeVal<uint32_t>(f, static_cast<uint32_t>(b.soundSchedule.size()));
        for (const auto& se : b.soundSchedule)
        {
            writeVal<double>(f, se.startSec);
            writeVal<double>(f, se.durationSec);
            uint16_t rpLen = static_cast<uint16_t>(se.relativePath.size());
            writeVal<uint16_t>(f, rpLen);
            if (rpLen > 0)
                f.write(se.relativePath.data(), rpLen);

            // --- v14 additions: per-scheduled-sound loop settings ---
            writeVal<uint8_t>(f, se.loop ? 1 : 0);
            writeVal<double>(f, se.loopGapSec);
        }

        // --- v13 additions: movement-loop flag ---
        writeVal<uint8_t>(f, b.movementLoop ? 1 : 0);
    }

    // --- v10: optional camera-path trailer (always emitted; can be empty) ---
    f.write(kCamPathMagic, 4);
    writeVal<uint32_t>(f, static_cast<uint32_t>(cameraPath.size()));
    for (const auto& k : cameraPath)
    {
        writeVal<double>(f, k.timeSec);
        writeVal<float>(f, k.pos.x);
        writeVal<float>(f, k.pos.y);
        writeVal<float>(f, k.pos.z);
        writeVal<float>(f, k.yawRad);
        writeVal<float>(f, k.pitchRad);
        writeVal<uint8_t>(f, k.mode);
        writeVal<float>(f, k.holdDurationSec);   // v11
    }

    return f.good();
}

bool SceneFile::load(const std::string& path, std::vector<BlockEntry>& outBlocks)
{
    std::vector<CameraKeyframe> ignored;
    return load(path, outBlocks, ignored);
}

bool SceneFile::load(const std::string& path,
                     std::vector<BlockEntry>&    outBlocks,
                     std::vector<CameraKeyframe>& outCameraPath)
{
    outCameraPath.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // --- Header ---
    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return false;

    uint16_t version = 0;
    if (!readVal(f, version) || version > kVersion) return false;

    uint32_t blockCount = 0;
    if (!readVal(f, blockCount)) return false;

    uint16_t reserved = 0;
    readVal(f, reserved); // discard

    // --- Block records ---
    outBlocks.clear();
    outBlocks.reserve(blockCount);

    for (uint32_t i = 0; i < blockCount; ++i)
    {
        BlockEntry b;

        if (!readVal<int32_t>(f, b.serial)) return false;

        uint8_t bt = 0;
        if (!readVal(f, bt)) return false;
        b.blockType = static_cast<BlockType>(bt);

        if (!readVal<int32_t>(f, b.pos.x)) return false;
        if (!readVal<int32_t>(f, b.pos.y)) return false;
        if (!readVal<int32_t>(f, b.pos.z)) return false;

        if (!readVal<int32_t>(f, b.soundId)) return false;

        // --- v3: colour stored here — immediately after soundId (matches save order) ---
        // NOTE: v1/v2 files did not have this field; derive colour from block type instead.
        if (version >= 3)
        {
            int32_t r = 0, g = 0, bl = 0;
            if (!readVal<int32_t>(f, r))  return false;
            if (!readVal<int32_t>(f, g))  return false;
            if (!readVal<int32_t>(f, bl)) return false;
            b.colour = Vec3f {
                juce::jlimit(0.0f, 1.0f, r / 255.0f),
                juce::jlimit(0.0f, 1.0f, g / 255.0f),
                juce::jlimit(0.0f, 1.0f, bl / 255.0f)
            };
        }
        else
        {
            b.colour = b.getBlockColor(b.blockType, b.soundId);
        }

        uint16_t pathLen = 0;
        if (!readVal(f, pathLen)) return false;
        if (pathLen > 0)
        {
            b.customFilePath.resize(pathLen);
            f.read(&b.customFilePath[0], pathLen);
            if (!f.good()) return false;
        }

        if (!readVal(f, b.startTimeSec)) return false;
        if (!readVal(f, b.durationSec))  return false;

        uint8_t dl = 0;
        if (!readVal(f, dl)) return false;
        b.durationLocked = (dl != 0);
        
        if (version >= 4)
        {
            uint32_t timeCount = 0;

            if (!readVal(f, timeCount))
                return false;

            b.timesList.resize(timeCount);

            for (uint32_t t = 0; t < timeCount; ++t)
            {
                if (!readVal(f, b.timesList[t].startTimeSec))
                    return false;

                if (!readVal(f, b.timesList[t].durationSec))
                    return false;
            }
        }

        uint8_t hm = 0;
        if (!readVal(f, hm)) return false;
        b.hasRecordedMovement = (hm != 0);

        if (b.hasRecordedMovement)
        {
            uint32_t kfCount = 0;
            if (!readVal(f, kfCount)) return false;

            b.recordedMovement.resize(kfCount);
            for (uint32_t k = 0; k < kfCount; ++k)
            {
                auto& kf = b.recordedMovement[k];
                if (!readVal(f, kf.timeSec))    return false;
                if (!readVal(f, kf.position.x)) return false;
                if (!readVal(f, kf.position.y)) return false;
                if (!readVal(f, kf.position.z)) return false;
            }
        }

        // --- v2 additions (isLooping + loopDurationSec, not present in v1) ---
        if (version >= 2)
        {
            uint8_t lp = 0;
            if (!readVal(f, lp)) return false;
            b.isLooping = (lp != 0);

            if (!readVal(f, b.loopDurationSec)) return false;
        }
        else
        {
            b.isLooping       = false;
            b.loopDurationSec = 4.0;
        }

        // --- v5 additions: playbackMode / movementDurationSec / movementYOffset ---
        if (version >= 5)
        {
            uint8_t pm = 0;
            if (!readVal(f, pm)) return false;
            b.playbackMode = static_cast<BlockPlaybackMode>(pm);

            if (!readVal(f, b.movementDurationSec)) return false;

            int32_t yo = 0;
            if (!readVal(f, yo)) return false;
            b.movementYOffset = yo;
        }
        else
        {
            // Map the legacy isLooping flag into the new mode enum.
            b.playbackMode        = b.isLooping ? BlockPlaybackMode::Loop
                                                : BlockPlaybackMode::Natural;
            b.movementDurationSec = 0.0;
            b.movementYOffset     = 0;
        }

        if (version >= 6)
        {
            uint8_t me = 1;
            if (!readVal(f, me)) return false;
            b.movementEnabled = (me != 0);
        }
        else
        {
            // Older files: movement plays whenever a path was saved.
            b.movementEnabled = b.hasRecordedMovement;
        }

        if (version >= 7)
        {
            uint8_t mu = 0, hd = 0;
            if (!readVal(f, mu)) return false;
            if (!readVal(f, hd)) return false;
            if (!readVal(f, b.loopBufferSec)) return false;
            b.isMuted  = (mu != 0);
            b.isHidden = (hd != 0);
        }

        if (version >= 8)
        {
            if (!readVal(f, b.muteStartSec)) return false;
            if (!readVal(f, b.muteEndSec))   return false;

            // v8 → v9 migration: lift the legacy single window into the new
            // muteWindows vector so the audio engine actually applies it.
            if (version == 8
                && b.muteEndSec > b.muteStartSec
                && b.muteWindows.empty())
            {
                b.muteWindows.push_back({ b.muteStartSec,
                                          b.muteEndSec - b.muteStartSec });
            }
        }

        if (version >= 9)
        {
            uint32_t winCount = 0;
            if (!readVal(f, winCount)) return false;

            // Sanity guard against corrupted files claiming a giant count.
            if (winCount > 4096) return false;

            b.muteWindows.clear();
            b.muteWindows.reserve(winCount);
            for (uint32_t w = 0; w < winCount; ++w)
            {
                MuteWindow win;
                if (!readVal(f, win.startSec))    return false;
                if (!readVal(f, win.durationSec)) return false;
                if (win.durationSec > 0.0)
                    b.muteWindows.push_back(win);
            }
        }

        if (version >= 12)
        {
            uint32_t sndCount = 0;
            if (!readVal(f, sndCount)) return false;
            if (sndCount > 4096) return false;

            b.soundSchedule.clear();
            b.soundSchedule.reserve(sndCount);
            for (uint32_t s = 0; s < sndCount; ++s)
            {
                SoundEvent se;
                if (!readVal(f, se.startSec))    return false;
                if (!readVal(f, se.durationSec)) return false;
                uint16_t rpLen = 0;
                if (!readVal(f, rpLen)) return false;
                if (rpLen > 0)
                {
                    se.relativePath.resize(rpLen);
                    f.read(&se.relativePath[0], rpLen);
                    if (!f.good()) return false;
                }
                if (version >= 14)
                {
                    uint8_t lp = 0;
                    if (!readVal(f, lp)) return false;
                    se.loop = (lp != 0);
                    if (!readVal(f, se.loopGapSec)) return false;
                }
                b.soundSchedule.push_back(std::move(se));
            }
        }

        if (version >= 13)
        {
            uint8_t ml = 0;
            if (!readVal(f, ml)) return false;
            b.movementLoop = (ml != 0);
        }

        b.resetPlaybackState();
        outBlocks.push_back(std::move(b));
    }

    // --- v10: optional camera-path trailer.  Sniff for the magic; if it's
    // not there, this is an older file with no path — leave outCameraPath
    // empty and treat the load as successful.
    if (version >= 10)
    {
        char tag[4] = { 0, 0, 0, 0 };
        if (f.read(tag, 4) && std::memcmp(tag, kCamPathMagic, 4) == 0)
        {
            uint32_t kfCount = 0;
            if (!readVal(f, kfCount)) return f.eof();
            if (kfCount > 1000000)    return false;

            outCameraPath.reserve(kfCount);
            for (uint32_t i = 0; i < kfCount; ++i)
            {
                CameraKeyframe k;
                if (!readVal(f, k.timeSec))   return false;
                if (!readVal(f, k.pos.x))     return false;
                if (!readVal(f, k.pos.y))     return false;
                if (!readVal(f, k.pos.z))     return false;
                if (!readVal(f, k.yawRad))    return false;
                if (!readVal(f, k.pitchRad))  return false;
                if (!readVal(f, k.mode))      return false;

                // v11: per-keyframe Hold duration.  Pre-v11 files don't
                // store it; default to 0 (= hold until next keyframe).
                if (version >= 11)
                {
                    if (!readVal(f, k.holdDurationSec)) return false;
                }
                else
                {
                    k.holdDurationSec = 0.0f;
                }
                outCameraPath.push_back(k);
            }
        }
    }

    return f.good() || f.eof();
}
