# SIME

A 3D voxel-based spatial audio sequencer built with JUCE and OpenGL 3.3.  
Place blocks in 3D space, choose instrument types, assign sounds, and play back a spatial audio sequence.

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

> **Note:** If CMake reports a generator mismatch, delete the `build/` folder and re-run the cmake configure step.

---

## Block Types

Select a block type from the toolbar at the top of the viewport before placing.

| Type | Color | Sound | Description |
|------|-------|-------|-------------|
| **Violin** | Red | Vibrato + harmonics (string-like) | 3 pitch presets (A3, D4, G3) |
| **Piano** | Blue | Sharp attack, decaying harmonics | 3 pitch presets (C4, A4, C5) |
| **Drum** | Green | Kick / Snare / Hi-Hat | 3 percussive sounds |
| **Custom** | Varies | User WAV file | Pick any `.wav` via file browser |

Custom blocks get a deterministic color (white, yellow, cyan, magenta, orange, or purple) based on their assigned sound.

---

## Controls

### Navigation

| Input | Action |
|-------|--------|
| **WASD** | Move camera horizontally |
| **Space** | Move camera up |
| **Ctrl** | Move camera down |
| **RMB drag** | Rotate camera (look around) |
| **Scroll wheel** | Camera zoom |
| **R** | Reset camera to default position |
| **Alt + WASD** | Move camera faster |

### Block Placement

| Input | Action |
|-------|--------|
| **Toolbar buttons** | Select active block type (Violin / Piano / Drum / Custom) |
| **LMB click** | Place a block of the selected type |
| **Shift + LMB** | Place block in mid-air on the shift plane |
| **Scroll wheel** (Shift held) | Raise / lower the shift plane Y level |
| **Backspace** | Remove currently hovered block |
| **C** | Clear all blocks |

### Editing

| Input | Action |
|-------|--------|
| **E** | Toggle edit mode |
| **RMB click** (edit mode) | Open block edit popup on clicked block |

The edit popup shows:
- **Block type** (read-only)
- **Start time** and **Duration** (editable)
- **Sound dropdown** (for Violin/Piano/Drum — choose from available presets)
- **Browse button** (for Custom blocks — pick a `.wav` file from disk)

### Playback

| Input | Action |
|-------|--------|
| **Play button** | Start transport playback |
| **Pause button** | Pause playback |
| **Stop button** | Stop and reset to beginning |

Spatial audio is position-based:
- **X axis** → stereo pan (left/right)
- **Y axis** → pitch shift (higher = higher pitch)
- **Z axis** → volume/proximity (closer to origin = louder)

---

## Project Structure

```
SIME/
├── CMakeLists.txt              # Build config
├── JUCE/                       # JUCE framework (cloned separately)
└── Source/
    ├── Main.cpp                # App entry point
    ├── MainComponent.cpp/h     # Top-level layout + block type toolbar
    ├── ViewPortComponent.cpp/h # 3D viewport, input, placement, sequencer loop
    ├── Renderer.cpp/h          # OpenGL renderer (per-block colored cubes, grid, highlights)
    ├── Camera.cpp/h            # First-person camera
    ├── Raycaster.cpp/h         # DDA voxel raycasting
    ├── VoxelGrid.h             # Sparse voxel data structure
    ├── MathUtils.h             # Vec3i, Vec3f, Mat4
    ├── BlockType.h             # Block type enum (Violin, Piano, Drum, Custom)
    ├── BlockEntry.h            # Block data model (type, position, timing, sound, file path)
    ├── AudioEngine.cpp/h       # Audio playback + instrument/drum synthesis
    ├── SequencerEngine.cpp/h   # Block → audio event sequencer
    ├── SequencerEvent.h        # Event struct passed between sequencer and audio engine
    ├── TransportClock.cpp/h    # Playback clock (play/pause/stop/loop)
    ├── SidebarComponent.cpp/h  # Left-side block list panel
    ├── TransportBarComponent.cpp/h # Bottom play/pause/stop bar
    └── BlockEditPopup.cpp/h    # Floating block edit dialog (type-aware)
```

---

## Contributing

1. Fork the repo and create a branch from `main`
2. Make your changes
3. Open a pull request with a clear description of what changed and why
