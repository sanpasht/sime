# SIME — Project Status Report
**Date:** April 2026  
**Branch you are on:** `Nihar-Audio-Engine`  
**Report covers:** full codebase state, controls, and testing plan

---

## 1. What Is in the Codebase Right Now

### The Git Situation

The project has **three relevant branches**. You are currently on `Nihar-Audio-Engine`.

```
*   a729697  main  ← Keshav's work merged here (April 4)
|\
| * 90638af  AudioEngine, SequencerEngine, popup (Keshav)
| * 7256029  Modular UI refactor (Keshav)
|/
| * 1c8200b  Nihar-Audio-Engine ← YOU ARE HERE (April 3)
|/
* baea8cf    Update README (shared base)
* e0da3b8    Spatial Engine v1.0
```

**Key fact:** Your branch (`Nihar-Audio-Engine`) and Keshav's work on `main` diverged from the same base. They have **not been merged together yet**. These are two independent implementations that will need to be reconciled before the final integration.

---

## 2. What Is in YOUR Branch (`Nihar-Audio-Engine`)

This is the code that is built and running right now.

### Spatial Engine (original, unchanged)

| File | What it does |
|---|---|
| `Source/Main.cpp` | JUCE app entry point |
| `Source/MainComponent.h/.cpp` | Top-level component, owns all subsystems, bridges message ↔ GL thread |
| `Source/Camera.h/.cpp` | Free-look camera (yaw + pitch, WASD + mouse) |
| `Source/Raycaster.h/.cpp` | Screen-to-ray unprojection, Amanatides & Woo DDA voxel traversal |
| `Source/Renderer.h/.cpp` | OpenGL 3.3 batch renderer (shaders, VAO/VBO, lighting) |
| `Source/VoxelGrid.h` | Sparse voxel storage via `unordered_set<Vec3i>` |
| `Source/MathUtils.h` | `Vec3i`, `Vec3f`, `Mat4` — no external math library |

### Audio Engine (new — added by Nihar via Opus)

All 25 files live under `Source/Audio/`:

| File | Responsibility |
|---|---|
| `AudioConfig.h` | Compile-time constants (sample rate, buffer size, voice count, etc.) |
| `LockFreeTypes.h` | `AtomicSnapshotBuffer<T>` (double-buffer) + `SPSCQueue<T,N>` (ring buffer) |
| `AudioClip.h/.cpp` | Immutable, preloaded WAV buffer. Loaded by `AudioFormatReader`, normalized to float |
| `SampleLibrary.h/.cpp` | Clip registry; sync load + async background thread-pool load |
| `ParameterSmoother.h` | One-pole low-pass filter; prevents clicks when gains change |
| `VoicePool.h/.cpp` | 128 pre-allocated `Voice` objects, flat array, zero heap during playback |
| `TransportCommand.h` | `Play / Pause / Stop / Seek` structs for the SPSC queue |
| `PlaybackTransport.h` | Atomic transport state (`currentTimeSec`, `playing`); safe from any thread |
| `DeviceManagerWrapper.h/.cpp` | Wraps `juce::AudioDeviceManager`; opens WASAPI shared mode output |
| `AudioEngine.h/.cpp` | Top-level; owns all audio subsystems; implements `AudioIODeviceCallback` |
| `Mixer.h/.cpp` | Per-voice read loop: linear resampling + spatial gains + soft-clip limiter |
| `SceneSnapshot.h` | Flat POD struct (128 `SnapshotBlockEntry` fixed array); zero heap |
| `SoundBlock.h` | Authoring-time block (position, startTime, duration, clipId, gain, looping) |
| `SoundScene.h/.cpp` | Owns all `SoundBlock`s; generates snapshots; XML save/load |
| `Scheduler.h/.cpp` | Activates/deactivates voices by comparing transport time vs block windows |
| `Spatializer.h/.cpp` | Equal-power stereo panning + linear distance attenuation |

### Changes to Existing Files

| File | What changed |
|---|---|
| `CMakeLists.txt` | Added `juce_audio_basics/devices/formats/utils`; registered all 9 Audio `.cpp` files; disabled OGG/FLAC/MP3 formats |
| `Source/MainComponent.h` | Added `AudioEngine` + `SoundScene` members; `activeClipId`, `FileChooser`, helper methods |
| `Source/MainComponent.cpp` | Wires engine init/shutdown; pushes `SceneSnapshot` every GL frame; Space=play/pause, T=stop, L=load WAV, E/Q=camera up/down; creates/removes `SoundBlock` on voxel place/delete |

---

## 3. What Is in KESHAV's Branch (`Keshav-Playback/UI` → merged to `main`)

Keshav's work is a **parallel UI and audio architecture** — it is NOT on your current branch but exists on `main` and `origin/Keshav-Playback/UI`. Here is what he built:

| New File | What it does |
|---|---|
| `Source/ViewPortComponent.h/.cpp` | Extracted the 3D viewport (camera, voxel rendering, raycasting) from MainComponent into its own `juce::Component` |
| `Source/SidebarComponent.h/.cpp` | Extracted the block list panel into its own component; fixes voxel count display bug |
| `Source/AudioEngine.h/.cpp` | A different AudioEngine — extends `juce::AudioSource`, manages sample loading and playback via `SequencerEvents` |
| `Source/SequencerEngine.h/.cpp` | Polls a list of `SequencerEvent`s and dispatches Play/Stop events to `AudioEngine` |
| `Source/SequencerEvent.h` | Event struct (blockSerial, soundId, eventType=Play/Stop, triggerTimeMs) |
| `Source/TransportClock.h/.cpp` | Manages global play/pause/stop state and time |
| `Source/BlockEntry.h` | Richer block model with `soundId`, `blockSerial`, position, display name |
| `Source/Blockeditpopup.h/.cpp` | Right-click context popup to edit block properties (sound ID, etc.) |

Keshav's `CMakeLists.txt` **does not** include `juce_audio_utils` or WAV-only format flags. It links `juce_audio_basics`, `juce_audio_devices`, `juce_audio_formats`.

### Critical Difference: Two `AudioEngine` Implementations

| | Nihar (`Source/Audio/AudioEngine`) | Keshav (`Source/AudioEngine`) |
|---|---|---|
| Base class | `juce::AudioIODeviceCallback` | `juce::AudioSource` |
| Sample management | `SampleLibrary` with background loading | Direct map of `soundId → AudioBuffer` |
| Playback model | `SceneSnapshot` → `Scheduler` → `VoicePool` | `SequencerEvent` FIFO → `ActiveVoice` list |
| Spatialization | Yes (`Spatializer` with panning + attenuation) | No (mono mix only) |
| Thread safety | Lock-free `AtomicSnapshotBuffer` + `SPSCQueue` | `AbstractFifo`-backed event queue |
| Save/load | XML serialization in `SoundScene` | Not implemented |
| Real-time safety | Strict — no alloc/lock on audio thread | Not fully enforced |

---

## 4. The README Is Outdated

The `README.md` still shows the original spatial engine controls (Space=move up, Ctrl=move down, Alt+WASD). These are **wrong** for the current build. The correct controls are documented in Section 5 below.

---

## 5. How to Build and Run

### Prerequisites

- CMake ≥ 3.22
- Visual Studio 2022 (MSVC C++17)
- OpenGL 3.3 capable GPU/driver
- Windows 10+

### Build Steps

```powershell
# 1. JUCE must be in C:\sime\JUCE\ (already cloned — skip if present)
git clone --depth 1 https://github.com/juce-framework/JUCE.git JUCE

# 2. Configure (use VS cmake — cmake.exe not in PATH by default)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -B build

# 3. Build
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release

# 4. Run
& "C:\sime\build\SIME_artefacts\Release\SIME.exe"
```

The last successful build produced: `C:\sime\build\SIME_artefacts\Release\SIME.exe`

---

## 6. Complete Current Controls

> These are the controls for the `Nihar-Audio-Engine` branch build.

### Camera / Navigation

| Input | Action |
|---|---|
| **RMB hold + drag** | Free-look (rotate camera) |
| **W / S** | Move forward / backward |
| **A / D** | Strafe left / right |
| **E** | Move camera up |
| **Q** | Move camera down |
| **Mouse wheel** | Dolly forward / backward |
| **R** | Reset camera to position (8, 8, 8) |

### Voxel Editing

| Input | Action |
|---|---|
| **LMB click** | Place voxel at the highlighted green preview cell. If a WAV clip is loaded, also creates a `SoundBlock` at that position |
| **RMB click** (short, no drag) | Remove the hovered yellow-highlighted voxel (and its SoundBlock if present) |
| **Shift + LMB** | Axis-locked placement on a horizontal plane; scroll wheel adjusts Y height |
| **Delete / Backspace** | Remove currently hovered voxel |
| **C** | Clear all voxels and all sound blocks |

### Audio Transport

| Input | Action |
|---|---|
| **L** | Open file dialog to load a WAV file. This sets the "active clip" for all subsequent block placements |
| **Space** | Toggle play / pause |
| **T** | Stop and reset transport to t = 0.0 s |

### HUD Display

The top bar shows a live status line, for example:
```
Voxels: 5  Pos: (4.2, 8.0, 3.1)  [PLAY 3.2s V:3 B:5 dB:-14.2]  WASD E/Q Space=Play T=Stop L=LoadWAV
```

| Field | Meaning |
|---|---|
| `Voxels: N` | Number of voxels in the 3D grid |
| `Pos: (x,y,z)` | Camera/listener world position |
| `PLAY` / `STOP` | Current transport state |
| `3.2s` | Current transport time in seconds |
| `V:N` | Active voice count (audio thread) |
| `B:N` | Sound block count in the scene |
| `dB: -14.2` | Peak output level in dB |

### Block List Panel (left side)

- Shows all placed voxels with their serial number and grid position
- Click the **header row** to collapse/expand the panel
- **Scroll wheel** over the panel scrolls the list

---

## 7. What to Test and How

### Test 1 — App Launches and Audio Device Opens

**What:** Verify the WASAPI audio device opens without error.

**How:**
1. Launch `SIME.exe`
2. The app should open the 3D editor immediately — no freeze, no crash
3. The HUD should show `[STOP 0.0s V:0 B:0 dB:-60.0]`
4. If the transport bracket is missing from the HUD, the audio device failed to open. Check Windows Sound → Playback devices → ensure a default output is set.

**Pass:** App opens, HUD shows transport bracket.

---

### Test 2 — Transport Play/Pause/Stop

**What:** Verify the transport state machine works correctly before any audio is loaded.

**How:**
1. Press **Space** — HUD should show `[PLAY` and time should start counting up
2. Press **Space** again — should show `[STOP` and time should freeze (pause)
3. Press **Space** again — should resume from where it paused
4. Press **T** — should show `[STOP 0.0s` (reset to zero)

**Pass:** Play/pause/stop transitions work, time counter behaves correctly.

---

### Test 3 — WAV File Loading

**What:** Verify WAV loading doesn't crash or freeze the UI.

**How:**
1. Press **L** — a file picker should open
2. Navigate to any `.wav` file on your machine (any length, any sample rate)
3. Select it and confirm
4. The UI should remain responsive — no freeze
5. Check `DBG()` output in the debug console (if running Debug build) for a line like: `AudioClip loaded: filename.wav | 44100 samples | 44100 Hz | 2 ch`

**Edge cases to test:**
- Load a short WAV (< 1 second)
- Load a long WAV (> 1 minute) — should still load in the background without UI stall
- Press L and cancel the dialog (X) — nothing should crash

**Pass:** Clip loads without crash or freeze. Console shows the clip info.

---

### Test 4 — First Sound

**What:** Verify audio plays when a sound block is in the scene.

**How:**
1. Press **L** and load a WAV file
2. Left-click somewhere in the 3D view to place a voxel — a SoundBlock is created at that position. HUD should show `B:1`
3. Press **Space** to play
4. **You should hear the WAV through your speakers/headphones**
5. HUD should show `V:1` and `dB:` should rise above -60

**Pass:** Audible sound, `V:1` in HUD.

---

### Test 5 — Spatialization (Panning)

**What:** Verify that block position affects stereo output.

**How:**
1. Load a WAV and place a single block to the **left** of origin (e.g., at grid position -5, 0, 0)
2. Move the camera to origin (press **R** then adjust) looking forward (+Z direction)
3. Press **Space** to play
4. The sound should be noticeably **louder in the left ear**
5. Rotate the camera 180° (right-click drag all the way around) — the sound should now be louder in the **right ear**

**Pass:** Stereo panning responds to listener orientation.

---

### Test 6 — Distance Attenuation

**What:** Verify sounds get quieter with distance.

**How:**
1. Load a WAV and place a block near the origin
2. Play and note the `dB:` reading in the HUD while close
3. Back the camera away (hold **S**) until you're ~40 units away
4. The `dB:` value should drop significantly
5. Back away to ~50+ units — sound should approach silence (`dB: -60`)

**Pass:** `dB:` reading decreases proportionally with distance.

---

### Test 7 — Multiple Simultaneous Blocks

**What:** Verify multiple voices play without dropout or crash.

**How:**
1. Load a WAV (preferably a looping-friendly one — ambient sound, drone, etc.)
2. Place **10 voxels** spread around the scene
3. Press **Space** — all 10 should play simultaneously
4. HUD shows `V:10`
5. Listen: no crackling, no dropout, stable audio
6. Add more blocks up to ~20–30. Still clean?

**Pass:** Multiple simultaneous voices, stable audio, no crash.

---

### Test 8 — Moving Listener (Spatial Update in Real Time)

**What:** Verify that moving the camera while audio is playing updates the spatialization smoothly.

**How:**
1. Place a block somewhere specific (e.g., 5 units to the right of origin)
2. Start playback
3. While audio is playing, slowly **rotate the camera** left and right with RMB drag
4. **Walk toward and away** from the block with W/S
5. Panning and volume should change **smoothly** — no sudden jumps, no clicks

**Pass:** Smooth, continuous panning and attenuation changes while moving.

---

### Test 9 — Delete Block During Playback

**What:** Verify removing a block mid-playback stops its voice cleanly.

**How:**
1. Load a WAV, place 3 blocks, start playback (`V:3`)
2. While playing, **right-click** one of the voxels to remove it
3. `V:` should drop to 2 within one frame
4. `B:` should also drop by 1
5. No crash, no audio artifact

**Pass:** Voice stops cleanly when block is removed during playback.

---

### Test 10 — Clear All During Playback

**What:** Verify C key during playback stops all voices cleanly.

**How:**
1. Load a WAV, place 5 blocks, start playback
2. While playing, press **C**
3. All voxels and sound blocks cleared
4. `V:0`, `B:0`, silence
5. No crash

**Pass:** Clean silence after clear, no crash.

---

### Test 11 — Invalid / Edge-Case WAV Files

**What:** Verify the engine handles bad input gracefully.

**How:**
1. Press **L**, select a non-WAV file (rename a `.txt` file to `.wav` if needed) — app should not crash, block should not play
2. Press **L**, cancel the dialog without selecting — no crash
3. Press **L**, select a very short WAV (< 100ms) — should play and stop cleanly

**Pass:** No crash in any of these cases.

---

### Test 12 — Transport Seek

**What:** Verify stop + replay works correctly (seek to 0).

**How:**
1. Load a WAV and place a block
2. Play for 5 seconds (watch HUD time counter reach 5.0s)
3. Press **T** — transport resets to 0.0s, voices deactivate
4. Press **Space** again — playback resumes from the beginning of the clip
5. The clip should play from its start, not from where it was

**Pass:** After T, replay starts from the beginning.

---

### Test 13 — Peak Limiter (No Clipping)

**What:** Verify the soft limiter prevents output from clipping.

**How:**
1. Load a loud WAV (maximally loud recording)
2. Place 10+ copies of it all at the same position
3. Press **Space** — all voices mix together
4. `dB:` in HUD should stay at or below 0 dB (will approach but not exceed due to `tanh` soft clip)
5. No crackling or distortion artifacts beyond the expected compression

**Pass:** `dB:` stays below 0, audio is compressed but not harshly clipped.

---

## 8. Known Limitations in Current Build (Deferred by Design)

These are intentional MVP deferments — **not bugs**:

| Feature | Status |
|---|---|
| HRTF / binaural rendering | Not implemented — panning only |
| Doppler effect | Not implemented |
| Pitch shifting | Data model stores `pitchSemitones` but it is ignored |
| Block looping transport | Individual block `looping=true` works; global transport loop does not |
| Timeline scrubbing | Seek to position 0 works (via T); arbitrary scrub deferred |
| Save/load scene | `SoundScene::saveToFile` / `loadFromFile` implemented but no UI hotkey for it yet |
| OGG, FLAC, MP3 formats | Disabled on purpose — WAV only for MVP |
| Multichannel surround | Stereo only |

---

## 9. Integration with Keshav's Branch — What Needs to Happen

Keshav's `main` branch has a completely different modular UI that needs to be integrated with Nihar's audio engine. The two are currently incompatible because:

1. **Two `AudioEngine` classes** exist with different APIs and different base classes — these need to be reconciled into one
2. **Keshav's `MainComponent` was gutted** — spatial logic moved to `ViewPortComponent`, block panel moved to `SidebarComponent`. Nihar's changes directly on `MainComponent` will conflict
3. **Keshav's CMakeLists** does not include `juce_audio_utils` or the Nihar audio `.cpp` files
4. **Keshav's audio engine** has no spatialization, no save/load, no background loading — Nihar's is more complete

**Recommended integration path:**
- Keep Nihar's `Source/Audio/` directory as-is (the production audio engine)
- Adopt Keshav's UI modularization (`SidebarComponent`, `ViewPortComponent`)  
- Replace Keshav's `Source/AudioEngine` with a bridge/adapter to Nihar's `Source/Audio/AudioEngine`
- Merge CMakeLists.txt to include both UI component files and all Audio `.cpp` files
- Update Keshav's `SequencerEngine` to produce `SoundBlock`s that feed into `SoundScene` instead of `SequencerEvents` that feed his `AudioEngine`

---

## 10. File Structure Summary (Current Branch)

```
C:\sime\
├── CMakeLists.txt              ← Updated: audio modules + 9 Audio .cpp files
├── README.md                   ← OUTDATED — still shows old controls
├── AUDIO_ENGINE_PLAN.md        ← Architecture blueprint (reference)
├── STATUS_REPORT.md            ← This file
├── JUCE/                       ← Cloned JUCE framework (gitignored)
├── build/
│   └── SIME_artefacts/Release/SIME.exe  ← Latest built executable
└── Source/
    ├── Main.cpp
    ├── MainComponent.h/.cpp    ← Extended with AudioEngine + SoundScene
    ├── Camera.h/.cpp
    ├── Raycaster.h/.cpp
    ├── Renderer.h/.cpp
    ├── VoxelGrid.h
    ├── MathUtils.h
    └── Audio/                  ← All new audio engine code
        ├── AudioConfig.h
        ├── LockFreeTypes.h
        ├── AudioClip.h/.cpp
        ├── SampleLibrary.h/.cpp
        ├── ParameterSmoother.h
        ├── VoicePool.h/.cpp
        ├── TransportCommand.h
        ├── PlaybackTransport.h
        ├── DeviceManagerWrapper.h/.cpp
        ├── AudioEngine.h/.cpp
        ├── Mixer.h/.cpp
        ├── SceneSnapshot.h
        ├── SoundBlock.h
        ├── SoundScene.h/.cpp
        ├── Scheduler.h/.cpp
        └── Spatializer.h/.cpp
```
