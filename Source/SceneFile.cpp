// ---------------------------------------------------------------------------
// SceneFile.cpp — Binary .sime scene serialization.
// ---------------------------------------------------------------------------

#include "SceneFile.h"
#include <fstream>
#include <cstring>

namespace
{
    static constexpr char     kMagic[4] = { 'S', 'I', 'M', 'E' };
    static constexpr uint16_t kVersion  = 2;   // bumped from 1: adds loop fields

    // Tiny endian-agnostic helpers (no-op on x86 but keeps intent clear)
    template <typename T>
    void writeVal(std::ofstream& f, T v)  { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

    template <typename T>
    bool readVal(std::ifstream& f, T& v)  { f.read(reinterpret_cast<char*>(&v), sizeof(T)); return f.good(); }
}

bool SceneFile::save(const std::string& path, const std::vector<BlockEntry>& blocks)
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

        uint16_t pathLen = static_cast<uint16_t>(b.customFilePath.size());
        writeVal<uint16_t>(f, pathLen);
        if (pathLen > 0)
            f.write(b.customFilePath.data(), pathLen);

        writeVal<double>(f, b.startTimeSec);
        writeVal<double>(f, b.durationSec);
        writeVal<uint8_t>(f, b.durationLocked ? 1 : 0);

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
    }

    return f.good();
}

bool SceneFile::load(const std::string& path, std::vector<BlockEntry>& outBlocks)
{
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

        // --- v2 additions (only present in v >= 2 files) ---
        if (version >= 2)
        {
            uint8_t lp = 0;
            if (!readVal(f, lp)) return false;
            b.isLooping = (lp != 0);

            if (!readVal(f, b.loopDurationSec)) return false;
        }
        else
        {
            // v1 default
            b.isLooping       = false;
            b.loopDurationSec = 4.0;
        }

        b.resetPlaybackState();
        outBlocks.push_back(std::move(b));
    }

    return f.good() || f.eof();
}
