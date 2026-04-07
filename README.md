# SIME

A 3D voxel-based spatial audio sequencer built with JUCE and OpenGL 3.3.  
Place blocks in 3D space, assign sounds and timing to them, and play back a spatial audio sequence.

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

## Controls

| Input | Action |
|-------|--------|
| **LMB click** | Place a voxel block |
| **RMB drag** | Rotate camera (look around) |
| **WASD** | Move camera horizontally |
| **Space** | Move camera up |
| **Ctrl** | Move camera down |
| **Shift + LMB** | Place block in mid-air on the shift plane |
| **Scroll wheel** (Shift held) | Raise / lower the shift plane Y level |
| **Scroll wheel** | Camera zoom |
| **E** | Toggle edit mode (click blocks to set timing/sound) |
| **R** | Reset camera to default position |
| **C** | Clear all blocks |
| **Delete / Backspace** | Remove currently hovered block |

---

## Where to Change Things

This section maps features to the files you need to edit.

### Camera behaviour
**`Source/Camera.cpp` / `Source/Camera.h`**
- Movement speed → `moveSpeed` in `Camera.h`
- Look sensitivity → `lookSpeed` in `Camera.h`
- Field of view → `fovY` in `Camera.h`
- Floor clamp (minimum Y) → `moveUp()` in `Camera.cpp`
- Near/far clip planes → `nearZ` / `farZ` in `Camera.h`

### Block placement & raycasting
**`Source/ViewPortComponent.cpp`**
- How blocks are placed (normal, shift-plane, ground fallback) → `renderOpenGL()` place section
- Shift-plane anchor logic → `shiftAnchorSet` block inside `renderOpenGL()`
- Raycast distance / max steps → `Raycaster::MAX_STEPS` / `MAX_DIST` in `Source/Raycaster.h`
- Block outline colours (green = preview, yellow = hover, cyan = shift, orange = selected) → `renderHighlight()` calls in `renderOpenGL()`

### Rendering & visuals
**`Source/Renderer.cpp` / `Source/Renderer.h`**
- Voxel colour → `glUniform3f(uColor_v, ...)` in `render()`
- Grid size → `buildGridMesh(halfSize)` — change the `40` argument in `init()`
- Origin marker colour → `renderOriginMarker()` in `Renderer.cpp`
- Lighting direction → `lightDir` in `renderOpenGL()` in `ViewPortComponent.cpp`

### Audio
**`Source/AudioEngine.cpp` / `Source/AudioEngine.h`**
- Load a sound file → `audioEngine.loadSample(soundId, juce::File("path/to/file.wav"))` in `ViewPortComponent` constructor
- Voice gain → `voice.gain` in `handleStartEvent()`
- Max polyphony → `activeVoices_` size (currently unlimited; add a cap in `handleStartEvent()`)

### Sequencer / timing
**`Source/SequencerEngine.cpp`**
- How block start/stop events are fired → `update()` method
- Loop behaviour → `TransportClock` in `Source/TransportClock.cpp`

### Sidebar (block list)
**`Source/SidebarComponent.cpp`**
- Row height, font size → `kRowH`, `kHeaderH` constants
- Collapsed width → `MainComponent::resized()` in `Source/MainComponent.cpp`

### Transport bar (Play/Pause/Stop)
**`Source/TransportBarComponent.cpp`**
- Button appearance / labels → constructor
- Progress bar colours → `paint()` gradient colours
- Auto-stop behaviour → `timerCallback()` in `Source/MainComponent.cpp`

### Block edit popup
**`Source/BlockEditPopup.cpp` / `Source/BlockEditPopup.h`**
- Fields shown (start time, duration, sound ID) → `showAt()` and `commit()`
- Popup size → `kWidth` / `kHeight` constants in the header

### Layout
**`Source/MainComponent.cpp`**
- Sidebar width (collapsed vs expanded) → `resized()`
- Transport bar height → `TransportBarComponent::kHeight` in the header

---

## Project Structure

```
SIME/
├── CMakeLists.txt          # Build config
├── JUCE/                   # JUCE framework (cloned separately)
└── Source/
    ├── Main.cpp                  # App entry point
    ├── MainComponent.cpp/h       # Top-level layout (sidebar + viewport + transport)
    ├── ViewPortComponent.cpp/h   # 3D OpenGL viewport, input handling, sequencer loop
    ├── Renderer.cpp/h            # OpenGL batch renderer (voxels, grid, highlights)
    ├── Camera.cpp/h              # First-person camera
    ├── Raycaster.cpp/h           # DDA voxel raycasting
    ├── VoxelGrid.h               # Sparse voxel data structure
    ├── MathUtils.h               # Vec3i, Vec3f, Mat4
    ├── AudioEngine.cpp/h         # JUCE audio playback engine
    ├── SequencerEngine.cpp/h     # Block → audio event sequencer
    ├── TransportClock.cpp/h      # Playback clock (play/pause/stop/loop)
    ├── SidebarComponent.cpp/h    # Left-side block list panel
    ├── TransportBarComponent.cpp/h # Bottom play/pause/stop bar
    └── BlockEditPopup.cpp/h      # Floating block edit dialog
```

---

## Contributing

1. Fork the repo and create a branch from `main`
2. Make your changes
3. Open a pull request with a clear description of what changed and why
