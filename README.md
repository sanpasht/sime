# SIME — Spatially-Interpreted Music Engine

A 3D voxel-based spatial audio sequencer built with JUCE and OpenGL 3.3.  
Place blocks in 3D space, assign sounds and timing to them, and play back a spatial audio composition where position directly shapes how everything sounds.

Move a block left or right and the audio pans. Place it higher and the pitch goes up. Push it further out on Z and it gets quieter. The goal is to make music composition spatial and visual instead of the traditional flat timeline.

---

## Table of Contents

1. [Building from Source](#building-from-source)
2. [Running the App](#running-the-app)
3. [Controls](#controls)
4. [Workflow](#workflow)
5. [Block Movement Recording](#block-movement-recording)
6. [Save / Load Scenes](#save--load-scenes)
7. [Audio Architecture](#audio-architecture)
8. [Project Structure](#project-structure)
9. [Where to Change Things](#where-to-change-things)
10. [Known Bugs & Issues](#known-bugs--issues)

---

## Building from Source

### Requirements

| Tool | Version | Download |
|------|---------|----------|
| Git | Any | https://git-scm.com/download/win |
| CMake | 3.22+ | https://cmake.org/download/ — check "Add to PATH" |
| Visual Studio Build Tools | 2022 | https://visualstudio.microsoft.com/visual-cpp-build-tools/ — select **Desktop development with C++** |

### Steps

```bash
# 1. Clone the repo
git clone --recurse-submodules https://github.com/sanpasht/sime
cd sime

# 2. Clone JUCE (if not already present as a submodule)
git clone https://github.com/juce-framework/JUCE.git JUCE

# 3. Configure and build
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel
```

The executable will be at:
```
build\SIME_artefacts\Release\SIME.exe
```

> **Note:** If CMake reports a generator mismatch, delete the `build/` folder and re-run the configure step.

---

## Running the App

From `C:\sime` in a terminal (e.g. Cursor):

```powershell
# One-time: clone JUCE if the folder is missing
git clone https://github.com/juce-framework/JUCE.git JUCE

# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Debug --parallel

# Run
.\build\SIME_artefacts\Debug\SIME.exe
```

If `cmake` is not found, add `C:\Program Files\CMake\bin` to PATH and reopen your terminal.

---

## Controls

### Camera

| Input | Action |
|-------|--------|
| `RMB + drag` | Rotate camera (look around) |
| `W / S` | Move forward / back |
| `A / D` | Strafe left / right |
| `Space` | Move up |
| `Ctrl` | Move down |
| `Scroll wheel` | Zoom / dolly |
| `R` | Reset camera to `(8, 8, 8)` |

> Click inside the viewport first to give it keyboard focus.

### View Gizmo (top-right corner)

A 3D axis compass is displayed in the top-right corner of the viewport. It shows the orientation of the X (red), Y (green), and Z (blue) axes relative to the current camera angle, and rotates in real time as you move.

Below the gizmo are four direction buttons:

| Button | Action |
|--------|--------|
| **Front** | Snap camera to view the origin from +Z |
| **Back** | Snap camera to view the origin from -Z |
| **Left** | Snap camera to view the origin from -X |
| **Right** | Snap camera to view the origin from +X |

All four views look at the origin (0,0,0) from a distance with a slight downward angle. After snapping you can freely navigate from the new position with WASD and mouse.

### Voxel Interaction

| Input | Action |
|-------|--------|
| `LMB` | Place block at preview position |
| `RMB click` (no drag) | Remove hovered block |
| `Backspace` | Remove hovered block |
| `C` | Clear all blocks |
| `Shift + LMB` | Place block in mid-air (shift plane) |
| `Shift + Scroll` | Raise / lower the shift plane Y level |

### Edit Mode & Transport

| Input | Action |
|-------|--------|
| `E` | Toggle edit mode |
| `RMB` on block (edit mode) | Open block edit popup |
| `Alt + LMB` on block (edit mode) | Select block and start movement recording |
| Play / Pause / Stop | Transport bar buttons at the bottom |

### Scene File Toolbar (top-right)

| Button | Action |
|--------|--------|
| **New** | Clear the current scene and start fresh |
| **Open** | Open a `.sime` file from disk |
| **Save** | Save to the current file (prompts if no file loaded) |
| **Save As** | Always prompts for a new file name |

The app also auto-saves to `%APPDATA%/SIME/autosave.sime` when you close it, and auto-loads that scene on the next launch.

---

## Workflow

**Quick start:**

1. Launch the app and click the viewport to focus it.
2. Fly the camera with `RMB drag` + `WASD`.
3. Place blocks with `LMB`. Use `Shift + LMB` to place blocks in mid-air.
4. Press `E` to enter edit mode.
5. `RMB`-click any block to set its **Start time**, **Duration**, and **Sound** in the popup.
6. Press **Play** in the transport bar.
7. Blocks highlight as they trigger. Listen for spatial differences — left/right panning, pitch changes by height, volume falloff by depth.

**Spatial audio mapping:**

| Axis | Effect |
|------|--------|
| X (left/right) | Stereo pan (equal-power) |
| Y (up/down) | Pitch — each grid unit = one semitone up |
| Z (near/far) | Volume — inverse distance falloff |

**Block types (23 total, organized by category):**

The toolbar at the top of the viewport shows the active block type as a color "pill" + a grouped dropdown listing every type. Picking a type changes which block flavor `LMB` will place.

| Category   | Types |
|------------|-------|
| Synth      | Piano |
| Strings    | Violin, Viola, Cello, Double Bass, Banjo, Mandolin, Guitar, Electric Guitar |
| Woodwinds  | Flute, Oboe, Clarinet, Bass Clarinet, Bassoon, Contrabassoon, Cor Anglais, Saxophone |
| Brass      | French Horn, Trumpet, Trombone, Tuba |
| Percussion | Drum, Percussion |
| Special    | Custom (user WAV) |

Each type has a distinct color in the 3D viewport. After placing a block, hit `E` and right-click it to open the **edit popup**, which embeds a searchable list of every sample for that block's category — type a note name (e.g. `A4`), dynamic (`forte`), or articulation (`arco`) to filter ~1,500 samples instantly. The chosen WAV is decoded on commit (lazy load) so the picker is responsive even with 13,759 samples in the library.

For full details of the sample library and lazy-loading strategy see [`md files/AUDIO_LIBRARY_REPORT.md`](md%20files/AUDIO_LIBRARY_REPORT.md).

---

## Block Movement Recording

Blocks can have a recorded movement path that plays back in sync with the transport.

### How to Record

**Step 1 — Enter edit mode**
Press `E`. The HUD shows `EDIT MODE` and all blocks get a dim yellow highlight.

**Step 2 — Select a block**
Hold `Alt` and `LMB`-click the block you want to record. The block highlights orange and recording starts immediately. A red **● REC** indicator appears in the top-right corner of the viewport, and you will hear the block's sound playing as a preview.

**Step 3 — Record movement**
Keep `Alt` held and drag the mouse. The block follows the cursor and snaps to the grid. Keyframes are captured automatically each time the block moves to a new position. The preview sound re-triggers with updated pitch and pan as the block moves, so you can hear the spatial result in real time.

**Step 4 — Stop recording**
Release the mouse button. The **● REC** indicator disappears and a confirmation popup appears showing:
- Block serial number and total duration
- Number of keyframes captured
- A top-down path visualization (cyan line, green→red keyframe dots, Y-level annotations, START/END labels)

**Step 5 — Confirm or cancel**
- **Confirm** — movement is saved, duration is locked to match the path timing.
- **Cancel** — movement is discarded, block resets.

**Step 6 — Play it back**
Press `E` to exit edit mode, then press **Play**. The block will travel through its recorded positions in sync with the transport clock.

### Important Notes

- **Duration locking** — after confirming a recording, the block's duration is locked and cannot be edited in the popup. This keeps keyframe timing in sync.
- **Start time is still editable** — the entire movement path shifts with it; relative timing stays intact.
- **Movement constraints** — positions must be valid grid cells, within ±40 on X/Z, Y ≥ 0, not occupied by another block, and not the origin `(0,0,0)`.

---

## Save / Load Scenes

Scenes are saved as `.sime` binary files — a compact, custom format that stores every block's full state.

### What Gets Saved

Each block record includes:
- Serial number and block type (Violin, Piano, Drum, Custom, Listener)
- 3D grid position (x, y, z)
- Sound ID and custom WAV file path (if applicable)
- Start time and duration
- Duration lock flag
- Recorded movement path (all keyframes with time and position)

### File Format

The `.sime` format uses a 12-byte header (`SIME` magic, version, block count) followed by tightly packed block records. Movement keyframes are stored inline — no padding, no JSON overhead. A scene with 100 blocks and no movement data is roughly 5 KB.

### Auto-Save / Auto-Load

When the app closes, it saves the current scene to `%APPDATA%/SIME/autosave.sime`. On the next launch, that file is automatically loaded so you pick up where you left off. The auto-save does not overwrite any manually saved `.sime` file.

### Workflow

1. Build a scene with blocks.
2. Click **Save** (or **Save As**) in the top-right toolbar — choose a location and name.
3. Close and reopen the app — your scene is restored from the auto-save.
4. Click **Open** to load a different `.sime` file at any time.
5. Click **New** to start a blank scene.

---

## Audio Architecture

### Component Overview

```
Transport clock advances each frame
        │
        ▼
SequencerEngine.update(clock, blockList)
        │  scans all blocks, fires Start/Stop/Movement events
        ▼
AudioEngine.processEvents(events)
        │  queues into lock-free FIFO (256 capacity)
        ▼
Audio callback thread drains FIFO
        │  mixes active voices into output buffer
        ▼
Speaker output
```

### Core Components

**`TransportClock`** — owns playback time. Methods: `start()`, `pause()`, `stop()`, `seek()`, `setLoop()`, `update(dt)`.

**`SequencerEngine`** — scans all blocks each frame and emits `Start`, `Stop`, and `Movement` events when blocks cross timing boundaries or keyframe positions.

**`AudioEngine`** — receives events via a `juce::AbstractFifo`-backed queue. Maintains a flat list of `ActiveVoice` instances, each with a sample read cursor, gain, pitch rate, and stereo pan. Runs mixing in the audio callback — no allocations, no locks.

**`BlockEntry`** — the shared data structure. Carries position, block type, sound ID, start/duration timing, playback state flags, and an optional recorded movement path (`std::vector<MovementKeyFrame>`).

### Threading Model

| Thread | Owns |
|--------|------|
| GL / render thread | `blockList`, `SequencerEngine`, `TransportClock`, raycasting |
| Audio thread | `activeVoices_`, mixing |
| Message thread | Mouse/keyboard input, UI callbacks |

Events flow from the GL thread → FIFO → audio thread. The FIFO is the only cross-thread boundary in the hot path.

### Synthesized Sounds

All sounds are generated procedurally at startup (no external files required for defaults):

| Sound | Method |
|-------|--------|
| Violin | Vibrato + harmonics, sustained amplitude envelope |
| Piano | Sharp attack, exponential decay, rich overtones |
| Kick drum | Pitch-dropping sine (150→50 Hz) + click transient |
| Snare | Low tone + noise burst |
| Hi-hat | High-frequency filtered noise, very short |
| Custom | User-supplied WAV loaded via `AudioEngine::loadSample()` |

---

## Project Structure

```
SIME/
├── CMakeLists.txt
├── JUCE/                          # JUCE framework (cloned separately)
└── Source/
    ├── Main.cpp                   # App entry point
    ├── MainComponent.cpp/h        # Top-level layout (sidebar + viewport + transport)
    ├── ViewPortComponent.cpp/h    # 3D OpenGL viewport, input, sequencer loop
    ├── Renderer.cpp/h             # OpenGL batch renderer (blocks, grid, highlights)
    ├── Camera.cpp/h               # First-person camera + view snapping
    ├── Raycaster.cpp/h            # DDA voxel raycasting
    ├── VoxelGrid.h                # Sparse voxel data structure
    ├── MathUtils.h                # Vec3i, Vec3f, Mat4
    ├── BlockEntry.h               # Shared block data struct
    ├── BlockType.h                # Block type enum + helpers
    ├── SequencerEvent.h           # Event value type (Start/Stop/Movement)
    ├── AudioEngine.cpp/h          # JUCE audio playback engine
    ├── SequencerEngine.cpp/h      # Block → audio event sequencer
    ├── TransportClock.cpp/h       # Playback clock
    ├── SidebarComponent.cpp/h     # Left-side block list panel
    ├── TransportBarComponent.cpp/h # Bottom play/pause/stop bar
    ├── BlockEditPopup.cpp/h       # Floating block edit dialog (embeds picker)
    ├── MovementConfirmPopup.h     # Movement recording confirm dialog
    ├── SceneFile.cpp/h            # Binary .sime scene save/load
    ├── SoundLibrary.cpp/h         # CSV index + lazy WAV cache (13,759 entries)
    └── SoundPickerComponent.cpp/h # Searchable list UI for the edit popup
```

Two **untracked** folders are also expected next to the executable's working
directory at runtime:

```
SIME/
├── Sounds/                        # 13,759 WAV files (~4.4 GB) — generated by Task 1
└── CSV/sound_library.csv          # master index — generated by Task 2
```

Without these the picker still works UI-wise but shows "Sound library not
loaded".

---

## Where to Change Things

### Camera
**`Camera.cpp` / `Camera.h`**
- Movement speed → `moveSpeed`
- Look sensitivity → `lookSpeed`
- Field of view → `fovY`
- Near/far clip → `nearZ` / `farZ`
- View snap distance → `snapToView()` `distance` parameter (default 15)
- View snap height → `height = distance * 0.35f` in `snapToView()`

### Block Placement & Raycasting
**`ViewPortComponent.cpp`**
- Placement logic (normal, shift-plane, ground fallback) → `renderOpenGL()` place section
- Shift-plane anchor → `shiftAnchorSet` block
- Raycast distance → `Raycaster::MAX_STEPS` / `MAX_DIST` in `Raycaster.h`
- Block highlight colours → `renderHighlight()` calls in `renderOpenGL()`

### Rendering
**`Renderer.cpp`**
- Block colours → `getBlockColor()` in `ViewPortComponent.cpp`
- Grid size → `buildGridMesh(40)` — change the `40` argument
- Lighting direction → `lightDir` in `renderOpenGL()`

### Audio
**`AudioEngine.cpp`**
- Load a custom sound → `audioEngine.loadSample(soundId, juce::File("path/to/file.wav"))` in the `ViewPortComponent` constructor
- Voice gain formula → `handleStartEvent()`
- Pitch mapping → `voice.pitchRate` formula in `handleStartEvent()`
- Pan law → `voice.leftGain` / `voice.rightGain` in `handleStartEvent()`

### Sound Library
**`SoundLibrary.cpp/h`**
- Master index path → `CSV/sound_library.csv` (loaded in `ViewPortComponent::newOpenGLContextCreated`)
- Sounds folder root → `Sounds/` (relative to the executable's working directory)
- Search field set → `SoundLibrary::search()` — substring match across note/duration/dynamic/articulation/key/path/displayName
- Lazy load policy → `SoundLibrary::ensureLoaded()` — called by `ViewPortComponent::applyBlockEdit()` on commit only
- Block-type colors → `blockTypeColor()` in `BlockType.h`
- Block-type categories (combo box section grouping) → `blockTypeCategory()` in `BlockType.h`
- Picker row appearance → `SoundPickerComponent::paintListBoxItem()`

### Sequencer / Timing
**`SequencerEngine.cpp`**
- Start/stop event logic → `update()`
- Loop behaviour → `TransportClock.cpp`

### Sidebar
**`SidebarComponent.cpp`**
- Row height, font size → `kRowH`, `kHeaderH`
- Collapsed width → `MainComponent::resized()`

### Transport Bar
**`TransportBarComponent.cpp`**
- Button appearance → constructor
- Progress bar colours → `paint()` gradient
- Auto-stop → `timerCallback()` in `MainComponent.cpp`

### Block Edit Popup
**`BlockEditPopup.cpp`**
- Fields shown → `showAt()` and `commit()`
- Popup size → `kWidth` / `kHeight`

### Scene Persistence
**`SceneFile.cpp` / `SceneFile.h`**
- File format version → `kVersion` constant (bump when adding new fields)
- Auto-save location → `autoSave()` in `MainComponent.cpp`
- Block serialization order → `save()` / `load()` in `SceneFile.cpp`

---

## Known Bugs & Issues

### Editor / Lint — Not Real Bugs

All red-line errors in `AudioEngine.cpp` cascade from one root cause: Cursor's clangd linter can't find JUCE headers because they resolve through CMake, not standard include paths. The MSVC build compiles fine.

To fix the editor cosmetics only, add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to the CMake configure step and point a `.clangd` file at the result.

### Thread Safety

| ID | Severity | Summary |
|----|----------|---------|
| T1 | High | `applyBlockEdit()` writes GL-thread-owned `blockList` from the message thread — unguarded data race |
| T2 | Medium | `getTransportDuration()` iterates `blockList` from the 30Hz timer on the message thread |
| T3 | Low–Med | Transport play/pause/stop called cross-thread on a non-thread-safe `TransportClock` |
| T4 | Low | `hasHit` / `currentHit` read in `keyPressed()` without a lock |

### Audio

| ID | Severity | Summary |
|----|----------|---------|
| A1 | Low | Synth samples hardcoded at 44100 Hz; devices running at 48000 Hz will be ~9% sharp |
| A2 | Low | `activeVoices_.push_back()` can allocate on the audio thread if >32 simultaneous voices |
| A3 | Low | Brief voice overlap (one audio block) on rapid transport stop/start |

### Sequencer

| ID | Severity | Summary |
|----|----------|---------|
| S1 | Medium | All newly placed blocks default to `startTimeSec = 0.0` — everything fires at once until manually staggered |
| S2 | Low | Pitch only goes up (Y ≥ 0); no way to pitch below normal |

### UI

| ID | Severity | Summary |
|----|----------|---------|
| U1 | Low | Two debug alert dialogs appear on every startup (leftover from development) |

### Recommended Fix Order

1. Remove debug startup alerts (U1) — trivial
2. Thread-safe block edits (T1, T2) — highest crash risk; queue edits through the GL thread like placements already do
3. Auto-stagger block start times (S1) — or at minimum, increment based on existing block end times
4. Regenerate synth tones at the device's actual sample rate (A1)

