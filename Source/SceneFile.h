#pragma once
// ---------------------------------------------------------------------------
// SceneFile.h — Save / load SIME scenes to .sime binary files.
//
// File layout (little-endian):
//
//   [Header]
//     4 bytes  magic          "SIME"
//     2 bytes  version        uint16  (currently 2)
//     4 bytes  blockCount     uint32
//     2 bytes  reserved       (zero)
//
//   [Block record] × blockCount
//     4 bytes  serial         int32
//     1 byte   blockType      uint8   (BlockType enum)
//     12 bytes pos            int32 × 3  (x, y, z)
//     4 bytes  soundId        int32
//     2 bytes  pathLen        uint16
//     pathLen  customFilePath UTF-8 bytes (no null terminator)
//     8 bytes  startTimeSec   double
//     8 bytes  durationSec    double
//     1 byte   durationLocked uint8  (bool)
//     1 byte   hasMovement    uint8  (bool)
//     4 bytes  keyframeCount  uint32  (only if hasMovement)
//       [Keyframe] × keyframeCount
//         8 bytes  timeSec    double
//         12 bytes position   int32 × 3
//     ── v2 additions ──
//     1 byte   isLooping       uint8  (bool)
//     8 bytes  loopDurationSec double
//
// All multi-byte integers are little-endian (native on x86/x64).
// v1 files still load: missing fields default to isLooping=false,
// loopDurationSec=4.0.
// ---------------------------------------------------------------------------

#include "MathUtils.h"
#include "BlockEntry.h"
#include <vector>
#include <string>

namespace SceneFile
{
    bool save(const std::string& path, const std::vector<BlockEntry>& blocks);
    bool load(const std::string& path, std::vector<BlockEntry>& outBlocks);
}
