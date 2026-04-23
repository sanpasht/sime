# Save / Load Scene to Disk — Implementation Report

## Overview

This feature adds full scene persistence to SIME via a custom `.sime` binary file format. Users can save their 3D audio scenes to disk, open them later, share `.sime` files, and benefit from automatic session recovery on restart.

---

## File Format Specification

### Header (12 bytes)

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 0 | 4 | char[4] | Magic bytes `"SIME"` |
| 4 | 2 | uint16 | Format version (currently `1`) |
| 6 | 4 | uint32 | Block count |
| 10 | 2 | uint16 | Reserved (zero) |

### Block Record (variable length, one per block)

| Field | Size | Type | Notes |
|-------|------|------|-------|
| serial | 4 | int32 | Unique block ID |
| blockType | 1 | uint8 | `BlockType` enum value (0=Violin, 1=Piano, 2=Drum, 3=Custom, 4=Listener) |
| pos.x | 4 | int32 | Grid X coordinate |
| pos.y | 4 | int32 | Grid Y coordinate |
| pos.z | 4 | int32 | Grid Z coordinate |
| soundId | 4 | int32 | Audio sample ID (-1 = silent) |
| pathLen | 2 | uint16 | Length of custom file path string |
| customFilePath | pathLen | UTF-8 | WAV file path (only for Custom blocks) |
| startTimeSec | 8 | double | Playback start time |
| durationSec | 8 | double | Playback duration |
| durationLocked | 1 | uint8 | Boolean — locked by movement recording |
| hasMovement | 1 | uint8 | Boolean — has recorded movement path |
| keyframeCount | 4 | uint32 | Only present if hasMovement = 1 |
| keyframes[] | 20 × N | — | Only present if hasMovement = 1 |

### Keyframe Record (20 bytes each)

| Field | Size | Type |
|-------|------|------|
| timeSec | 8 | double |
| position.x | 4 | int32 |
| position.y | 4 | int32 |
| position.z | 4 | int32 |

### Size Estimates

- Header: 12 bytes (fixed)
- Block with no movement, no custom path: ~49 bytes
- Block with 50 keyframes: ~1,049 bytes
- Typical scene (100 blocks, 10 with movement): ~6 KB

All values are little-endian (native x86/x64). No padding, no alignment gaps.

---

## Implementation Details

### SceneFile.h / SceneFile.cpp

A standalone utility namespace with two functions:

- **`SceneFile::save(path, blocks)`** — opens a binary output stream, writes the header, then iterates the block list writing each record. Movement keyframes are inlined per block. Returns `false` if the file cannot be opened or a write fails.

- **`SceneFile::load(path, outBlocks)`** — opens a binary input stream, validates the magic bytes and version, reads the block count, then deserializes each block record. Calls `resetPlaybackState()` on each loaded block so playback flags start clean. Returns `false` on magic mismatch, unsupported version, or truncated data.

Both functions use simple `writeVal<T>()` / `readVal<T>()` template helpers that write/read raw bytes — no serialization library needed.

### ViewPortComponent Changes

Two new public methods:

- **`getBlockListCopy()`** — returns a copy of the GL-thread-owned `blockList` for the message thread to serialize.

- **`loadScene(newBlocks)`** — accepts a vector of `BlockEntry`, re-registers any Custom WAV samples with the `AudioEngine`, then stores the blocks in a pending buffer. The GL thread picks them up on the next frame: it clears the `VoxelGrid`, replaces `blockList`, rebuilds the grid from block positions, updates `nextSerial`, marks the mesh dirty, and refreshes the sidebar.

- **`clearScene()`** — delegates to the existing `pendingClear` path.

The pending-load mechanism (`pendingLoadBlocks_`, `pendingLoad_` atomic, `loadMutex_`) follows the same pattern as the existing `pendingClear` to ensure GL-thread safety.

### MainComponent Changes

Four new toolbar buttons on the right side of the existing block-type toolbar row:

| Button | Behavior |
|--------|----------|
| **New** | Calls `view.clearScene()`, clears `currentFilePath_` |
| **Open** | Launches an async `FileChooser` filtered to `*.sime`, loads the selected file via `SceneFile::load()`, then calls `view.loadScene()` |
| **Save** | If `currentFilePath_` is set, saves directly. Otherwise prompts with a save dialog |
| **Save As** | Clears `currentFilePath_` then calls `saveScene()` to always prompt |

The file chooser is stored as a `std::unique_ptr<juce::FileChooser>` member to keep it alive during the async callback.

**Error handling:** If `SceneFile::load()` returns `false`, an `AlertWindow` notifies the user that the file is corrupted or unsupported.

**Auto-save:** The destructor calls `autoSave()`, which writes to `%APPDATA%/SIME/autosave.sime`. Only non-empty scenes are saved.

### Main.cpp Changes

- **`systemRequestedQuit()`** — calls `mainComponent->autoSave()` before `quit()`.
- **`initialise()`** — after creating the window, checks for `autosave.sime`. If found, defers a `loadSceneFromFile()` call via `MessageManager::callAsync` so the GL context has time to initialize first.
- A raw `MainComponent*` pointer is stored alongside the window for the quit hook.

---

## Files Changed

| File | Change |
|------|--------|
| `Source/SceneFile.h` | **New** — format spec, `save()` / `load()` declarations |
| `Source/SceneFile.cpp` | **New** — binary serialization implementation |
| `Source/ViewPortComponent.h` | Added `getBlockListCopy()`, `loadScene()`, `clearScene()`, pending load state |
| `Source/ViewPortComponent.cpp` | Implemented `loadScene()`, GL-thread pending load processing |
| `Source/MainComponent.h` | Added destructor, scene methods, file toolbar buttons, `currentFilePath_`, `fileChooser_` |
| `Source/MainComponent.cpp` | Implemented `newScene()`, `saveScene()`, `openScene()`, `autoSave()`, `loadSceneFromFile()`, file button wiring and layout |
| `Source/Main.cpp` | Auto-save on quit, auto-load on startup |
| `CMakeLists.txt` | Added `SceneFile.cpp` to build |
| `README.md` | Added Save/Load section, file toolbar controls, project structure entry |

---

## Design Notes

1. **Binary over JSON/XML** — chosen for compactness and speed. A 100-block scene serializes in microseconds and is ~5 KB instead of ~50 KB in JSON. Version field allows backward-compatible evolution.

2. **Movement keyframes inline** — rather than a separate movement file, keyframes are packed directly after their parent block. This keeps the format self-contained — one `.sime` file = one complete scene.

3. **Custom WAV re-registration** — on load, any Custom block whose `customFilePath` is non-empty triggers a `loadSample()` call so the `AudioEngine` can play it. If the WAV file has been moved or deleted, the sample simply won't load and the block plays silently.

4. **GL-thread safety** — the load uses the same pending-op pattern as block placement and clear: message thread writes to a guarded buffer, sets an atomic flag, GL thread consumes it next frame. No locks in the render loop.

5. **Auto-save location** — `%APPDATA%/SIME/autosave.sime` is user-specific and survives project directory changes. The directory is created if missing.

6. **Forward compatibility** — the version field in the header allows future additions (e.g., camera position, global BPM, undo stack). Older loaders reject versions they don't understand rather than silently misreading data.
