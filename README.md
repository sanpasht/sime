# SIME SPATIAL

A 3-D voxel construction tool built with JUCE + OpenGL 3.3.

## Prerequisites

| Requirement | Version |
|-------------|---------|
| CMake       | ≥ 3.22  |
| C++ compiler| C++17   |
| OpenGL      | ≥ 3.3   |
| JUCE        | 7.x or 8.x |

**Platform tested:** Linux (X11/Wayland), macOS 12+, Windows 10+.

---

## Build

```bash
# 1. Clone JUCE into the project root
cd VoxelBuilder
git clone https://github.com/juce-framework/JUCE.git JUCE

# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Compile  (use -j$(nproc) or -j8 etc. for parallel)
cmake --build build --parallel

# 4. Run
./build/VoxelBuilder_artefacts/Release/VoxelBuilder
# (macOS: open build/VoxelBuilder_artefacts/Release/VoxelBuilder.app)
```

---

## Controls

| Input | Action |
|-------|--------|
| **LMB click** | Place voxel at the highlighted green cell |
| **RMB drag** | Rotate camera (free-look) |
| **RMB click** (no drag) | Remove the hovered voxel |
| **W / S** | Move forward / backward |
| **A / D** | Strafe left / right |
| **Space** | Move up |
| **Ctrl** | Move down |
| **Alt + WASD** | Double movement speed |
| **Mouse wheel** | Dolly forward / backward |
| **Shift + LMB** | Axis-locked placement (constrain to one axis) |
| **Delete / Backspace** | Remove the currently hovered voxel |
| **R** | Reset camera to default position (8, 8, 8) |
| **C** | Clear all voxels |

---

## Architecture

```
SIME/
├── CMakeLists.txt
└── Source/
    ├── Main.cpp            – JUCE app entry point
    ├── MainComponent.h/.cpp – Top-level component; owns all subsystems;
    │                          bridges JUCE message thread ↔ GL thread
    ├── Camera.h/.cpp       – Free-look camera (yaw + pitch, WASD + mouse)
    ├── Raycaster.h/.cpp    – Screen→ray unprojection; Amanatides & Woo DDA
    ├── Renderer.h/.cpp     – OpenGL 3.3 batch renderer (shaders, VAO/VBO)
    ├── VoxelGrid.h         – Sparse voxel storage (unordered_set<Vec3i>)
    └── MathUtils.h         – Vec3i, Vec3f, Mat4 (no external math library)
```

### Thread model

* **Message thread** – handles mouse/keyboard events; enqueues `VoxelOp` structs
  protected by `opsMutex`.
* **GL thread** – `renderOpenGL()` drains the op queue, rebuilds the voxel mesh
  when dirty, raycasts at the cursor position, and draws the scene.

### Rendering pipeline (per frame)

1. Drain `pendingOps` → mutate `VoxelGrid`
2. Rebuild voxel VBO if `meshDirty`
3. Raycast at cursor → compute hit voxel + placement preview
4. `glClear`, set depth test + back-face cull
5. `Renderer::render()` – solid voxels (hemisphere lighting, blue tint)
6. `Renderer::renderGrid()` – grey reference grid at y = 0
7. `Renderer::renderHighlight()` – yellow on hit voxel, green on placement preview

---

## Extending

* **Face culling** – `Renderer::rebuildVoxelMesh()` currently emits all 6 faces
  per voxel. Checking `VoxelGrid::contains(neighbour)` before each face reduces
  geometry by ~85 % on dense structures.
* **Chunking** – split the grid into 16³ chunks; only rebuild dirty chunks.
* **Multiple colours** – store a colour index alongside each `Vec3i` in the
  grid, and pass it as a per-vertex attribute.
* **Save / load** – serialise `VoxelGrid::getVoxels()` to a binary or JSON file.
