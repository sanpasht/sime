# Block Audio Library & Block Type System — Implementation Report

## Overview

This iteration added a **block type system** and **audio library** to SIME. Before this work, every block was identical — same blue color, same generic beep sound. Now blocks have distinct identities (Violin, Piano, Drum, Custom), unique colors, unique sounds, and a UI for selecting and editing them.

---

## What Was Added

### 1. Block Type Enum (`Source/BlockType.h` — new file)

A `BlockType` enum with four values:

| Type | Default SoundId | Purpose |
|------|----------------|---------|
| `Violin` | 100 | String instrument block |
| `Piano` | 200 | Keyboard instrument block |
| `Drum` | 300 | Percussion block |
| `Custom` | -1 (unassigned) | User-provided WAV file |

Helper functions:
- `blockTypeName(BlockType)` — returns `"Violin"`, `"Piano"`, etc.
- `blockTypeDefaultSoundId(BlockType)` — returns the starting soundId for each type.

### 2. Extended Block Data Model (`Source/BlockEntry.h`)

Added two new fields to `BlockEntry`:

```cpp
BlockType   blockType = BlockType::Violin;   // which instrument this block is
std::string customFilePath;                   // WAV path for Custom blocks
```

Every block now carries its type and (for custom blocks) the file path that backs it. This is designed with future `.sime` project save/load in mind — each block stores its own complete state.

### 3. Per-Block Colored Rendering (`Source/Renderer.h/.cpp`)

**Problem:** The old renderer drew ALL voxels as a single batch with one hardcoded blue color. No way to color blocks individually.

**Solution:** Added a `uModelOffset` vec3 uniform to the voxel vertex shader and a new `renderSolidBlock(vp, lightDir, pos, color)` method that draws a single cube at any position with any color. The old batch renderer is still present but no longer used for blocks.

Color assignments:

| Block Type | RGB | Visual |
|-----------|-----|--------|
| Violin | (0.85, 0.22, 0.18) | Red |
| Piano | (0.25, 0.45, 0.90) | Blue |
| Drum | (0.22, 0.78, 0.32) | Green |
| Custom | Deterministic palette | White, Yellow, Cyan, Magenta, Orange, or Purple (based on soundId) |

Custom blocks use a 6-color palette indexed by `soundId % 6`, so different WAV files get different colors automatically.

### 4. Block Type Toolbar (`Source/MainComponent.h/.cpp`)

Added a 34px toolbar strip at the top of the viewport area with four buttons:

```
[Violin] [Piano] [Drum] [Custom]
```

- The active button is highlighted with the instrument's signature color
- Inactive buttons are dimmed gray
- Clicking a button sets the active block type for future placements

### 5. Distinct Instrument Sounds (`Source/AudioEngine.h/.cpp`)

**Problem:** All instruments used the same `generateTestTone()` which creates a plain sine wave. Everything sounded like a beep regardless of block type.

**Solution:** Added three new synthesis methods with distinct timbres:

#### `generateViolinTone(soundId, frequency, duration)`
- Sine wave with **vibrato** (5.5 Hz modulation, ~6 Hz depth)
- **4 harmonics** (fundamental + 2nd, 3rd, 4th) for a richer string-like tone
- Gentle attack (80ms) and release (150ms) envelope

#### `generatePianoTone(soundId, frequency, duration)`
- **6 harmonics** with decreasing amplitude
- **Sharp attack** (5ms) simulating a hammer strike
- **Exponential decay** — higher harmonics die faster than lower ones

#### `generateDrumHit(soundId, drumType, duration)`
Three distinct drum types:
- **Kick (type 0):** Pitch-dropping sine (150→50 Hz) with fast exponential decay + noise click on attack
- **Snare (type 1):** Low 180 Hz body tone + white noise burst, medium decay
- **Hi-Hat (type 2):** Short noise burst with very fast decay (metallic click)

#### Registered Presets

| SoundId | Sound | Method |
|---------|-------|--------|
| 100 | Violin A3 (220 Hz) | `generateViolinTone` |
| 101 | Violin D4 (294 Hz) | `generateViolinTone` |
| 102 | Violin G3 (196 Hz) | `generateViolinTone` |
| 200 | Piano C4 (262 Hz) | `generatePianoTone` |
| 201 | Piano A4 (440 Hz) | `generatePianoTone` |
| 202 | Piano C5 (523 Hz) | `generatePianoTone` |
| 300 | Kick | `generateDrumHit(type=0)` |
| 301 | Snare | `generateDrumHit(type=1)` |
| 302 | Hi-Hat | `generateDrumHit(type=2)` |

### 6. Extended Block Edit Popup (`Source/BlockEditPopup.h/.cpp`)

The floating edit popup was rewritten to be block-type-aware:

**For instrument blocks (Violin/Piano/Drum):**
- Shows the block type as a read-only label
- Start time and duration fields (same as before)
- A **ComboBox dropdown** with the available sounds for that instrument type
  - Example: editing a Violin block shows "Violin A (220 Hz)", "Violin D (294 Hz)", "Violin G (196 Hz)"

**For Custom blocks:**
- Shows block type label
- Start time and duration fields
- A **file path display** and **Browse...** button
- Browse opens an async native file picker filtered to `*.wav`
- Selected file path is stored on the block and loaded into the audio engine on commit

### 7. Updated Placement Logic (`Source/ViewPortComponent.cpp`)

When placing a block, it now inherits the active block type from the toolbar:

```
1. User clicks "Drum" in toolbar
2. User left-clicks to place a block
3. Block is created with blockType = Drum, soundId = 300
4. Block renders as green
5. On playback, the block triggers the Kick drum sound
```

The active block type is stored as an `std::atomic<int>` for safe cross-thread access (toolbar runs on message thread, placement happens on GL thread).

### 8. Custom WAV Loading Flow

For Custom blocks:
1. User places a Custom block (starts with soundId = -1, silent)
2. User enters edit mode (E), right-clicks the block
3. Popup shows Browse button, user picks a WAV file
4. On Apply, `ViewPortComponent::applyBlockEdit()`:
   - Assigns a new soundId from an incrementing counter (starting at 1000)
   - Calls `audioEngine.loadSample(newId, file)` to decode the WAV
   - Stores the soundId and file path on the block
5. On next playback, the block triggers the loaded WAV audio

Also added `AudioEngine::hasSample(int soundId)` for checking if a sample is already loaded.

---

## Files Changed

| File | Change Type | What Changed |
|------|------------|--------------|
| `Source/BlockType.h` | **NEW** | Block type enum + helpers |
| `Source/BlockEntry.h` | Modified | Added `blockType`, `customFilePath` |
| `Source/Renderer.h` | Modified | Added `renderSolidBlock()`, `uModelOffset_vox` |
| `Source/Renderer.cpp` | Modified | Shader `uModelOffset` uniform, `renderSolidBlock()` impl, offset set to 0 in existing methods |
| `Source/AudioEngine.h` | Modified | Added `generateViolinTone()`, `generatePianoTone()`, `generateDrumHit()`, `hasSample()` |
| `Source/AudioEngine.cpp` | Modified | Implemented all three new synth methods + `hasSample()` |
| `Source/ViewPortComponent.h` | Modified | Added `setActiveBlockType()`, `activeBlockType_` atomic, `nextCustomSoundId_`, updated `onRequestBlockEdit` and `applyBlockEdit` signatures |
| `Source/ViewPortComponent.cpp` | Modified | Per-block color rendering, active type on placement, preset registration with new synth methods, `getBlockColor()` helper |
| `Source/BlockEditPopup.h` | Modified | Block type awareness, ComboBox, file browser, updated `showAt`/`onCommit` signatures |
| `Source/Blockeditpopup.cpp` | Modified | Complete rewrite of popup with type-aware layout, sound dropdown, WAV file picker |
| `Source/MainComponent.h` | Modified | Toolbar buttons, `activeType_`, `refreshToolbarColors()`, `paint()` |
| `Source/MainComponent.cpp` | Modified | Toolbar wiring, updated edit popup callbacks, dark background paint |

---

## Architecture Decisions

**Per-block rendering instead of batch:** Switched from a single VBO containing all voxel geometry to rendering each block individually via `renderSolidBlock()`. This trades some GPU efficiency for per-block color control. At MVP scale (< 100 blocks) this has zero performance impact. The VoxelGrid sparse set is still maintained for raycasting.

**Synthesised sounds instead of bundled WAVs:** All instrument sounds are generated in code. This avoids shipping audio assets, keeps the repo lightweight, and makes it easy to add/tweak sounds. Real WAV samples can replace these later by changing the constructor calls.

**Atomic block type:** The active block type uses `std::atomic<int>` because the toolbar (message thread) writes it and the GL thread reads it during placement.

**Custom sound IDs start at 1000:** Instrument presets use 100-302. Custom WAV loads get IDs starting at 1000 and incrementing, avoiding collisions.

---

## What's NOT Included (Future Work)

- MP4 / video file support
- Save/load `.sime` project files (block data model is ready for it)
- User-editable block colors
- Advanced asset browser / drag-and-drop
- Plugin hosting
- Real instrument sample packs (currently synthesised)
