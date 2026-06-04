# SIME — Spatially-Interpreted Music Engine

A 3D voxel-based spatial audio sequencer built with JUCE and OpenGL 3.3.  
Place blocks in 3D space, assign sounds and timing to them, and play back a spatial audio composition where position directly shapes how everything sounds.

Move a block and the mix changes with **where you are listening from** (the camera): left/right pan, louder when close and quieter when far, higher pitch when the block is above you. One grid unit is treated as **one metre**. The goal is to make music composition spatial and visual instead of the traditional flat timeline.

---

## Table of Contents

1. [Building from Source](#building-from-source)
2. [Rebuilding After a Pull](#rebuilding-after-a-pull)
3. [Running the App](#running-the-app)
4. [Controls](#controls)
5. [Toolbar](#toolbar)
6. [Workspace Tabs](#workspace-tabs)
7. [Block Info Panel](#block-info-panel)
8. [Transport](#transport)
9. [Workflow](#workflow)
10. [Block Movement Recording](#block-movement-recording)
11. [Block Playback Modes (Natural / Loop / Stretch / Speed)](#block-playback-modes-natural--loop--stretch--speed)
12. [Mute, Hide, and Type Filters](#mute-hide-and-type-filters)
13. [Audio Analysis (frequency & oscilloscope)](#audio-analysis-frequency--oscilloscope)
14. [Doppler Effect](#doppler-effect)
15. [Save / Load Scenes](#save--load-scenes)
16. [Export Audio](#export-audio)
17. [Audio Architecture](#audio-architecture)
18. [Project Structure](#project-structure)
19. [Where to Change Things](#where-to-change-things)
20. [Known Bugs & Issues](#known-bugs--issues)
21. [Audio library (detailed report)](md%20files/AUDIO_LIBRARY_REPORT.md) — 24 block types, CSV index, lazy-loaded WAV picker
22. [Export audio (detailed report)](md%20files/EXPORT_AUDIO_REPORT.md) — Offline bounce, formats, limitations
23. [Session 2026-05-23 (detailed report)](md%20files/SESSION_2026-05-23_REPORT.md) — Export, movement, gizmos, audio analysis, Doppler, mute / hide / type filters, loop overhaul, persistence v6→v7→v8
24. [Testing scenarios](md%20files/TESTING_SCENARIOS.md) — Cross-feature user-story playbook (duration ↔ sound, movement, loop, mute schedule) + UX improvement suggestions
25. [Session 2026-06-03 (detailed report)](md%20files/SESSION_2026-06-03_REPORT.md) — Sound Schedule, movement refinements, **workspace tabs** (Scene / Timeline / Synthesizer), subtractive synth + **Synth block type**, persistence v14

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

## Rebuilding After a Pull

Every time you pull new code, rebuild before relaunching, otherwise the running `SIME.exe` will still be the old binary.

### Quick rebuild (Release, recommended)

```powershell
cd c:\sime
cmake --build build --config Release --parallel 4
.\build\SIME_artefacts\Release\SIME.exe
```

### Full reconfigure + rebuild (use after CMakeLists changes or generator mismatch)

```powershell
cd c:\sime
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
.\build\SIME_artefacts\Release\SIME.exe
```

### Debug build

```powershell
cd c:\sime
cmake --build build --config Debug --parallel 1
.\build\SIME_artefacts\Debug\SIME.exe
```

`--parallel 1` avoids the MSVC `vc143.pdb` (`C1041`) lock you can hit with parallel Debug builds.

### If the change still doesn't show up

1. Confirm the build actually compiled the file (look for the source name in the build output).
2. Close any running `SIME.exe` window — running processes hold the old binary open and the new build silently lands beside the locked one.
3. If CMake complains about generator platform (`Does not match the platform used previously`), delete `build\CMakeCache.txt` and `build\CMakeFiles\` and re-run the full reconfigure command above.

---

## Running the App

From `C:\sime` in a terminal (e.g. Cursor):

```powershell
# One-time: clone JUCE if the folder is missing
git clone https://github.com/juce-framework/JUCE.git JUCE

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release --parallel 4

# Run
.\build\SIME_artefacts\Release\SIME.exe
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
| `Home` | Reset camera to `(8, 8, 8)` |
| `R` | Toggle camera-path recording (see [Camera Path](#camera-path-listener-keyframes)) |

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
| `C` | Clear all blocks (modifier-free only — `Ctrl+C` is **Copy**) |
| `Shift + LMB` | Place block in mid-air (shift plane) |
| `Shift + Scroll` | Raise / lower the shift plane Y level |

### Editor Shortcuts (work anywhere the viewport has focus)

| Input | Action |
|-------|--------|
| `Ctrl + S` | Save the current scene |
| `Ctrl + Z` | Undo the most recent block placement |
| `Ctrl + C` | Copy the current selection (the primary block, plus anything added by `Ctrl + A`) to the clipboard |
| `Ctrl + V` | Paste the clipboard back into the scene.  Each pasted block is offset along +X until it finds a free cell, and the freshly-pasted blocks become the new multi-selection so you can keep pasting (or bulk-edit them) without re-clicking |
| `Ctrl + A` | Select every block in the scene (multi-select highlight is cyan) |
| `Esc` | Drop the multi-selection (keeps the primary selection / sidebar Info panel intact) |
| Edit mode: **drag** on empty space | Rubber-band box — every block whose centre falls inside the rectangle is selected |
| Edit mode: **Shift + drag** | Add every block in the box to the current selection |
| Edit mode: **Shift + click** | Toggle one block in/out of the selection |
| **Apply** (with 2+ selected) | Bulk-applies mute, hide, loop, and mute-schedule fields to all selected blocks; position/timing stay per-block |

### Edit Mode & Transport

| Input | Action |
|-------|--------|
| `Tab` | Toggle edit mode |
| `RMB` on block (edit mode) | Open block edit popup |
| `LMB` on block (normal mode) | Select block (Block Info panel updates) |
| `Alt + LMB` on block (edit mode) | Select block and start movement recording |
| Click + drag a red/green/blue arrow on the selected block | Move it along that axis (Blender-style gizmo) |
| Play / Pause / Stop | Transport bar buttons at the bottom |
| Speed dropdown (transport bar) | 0.25× / 0.5× / 0.75× / 1× / 2× / 3× — selectable before or during playback |

> The square (Stop) button rewinds the playhead **and** snaps every block with a recorded movement back to its starting keyframe.  Pause holds in place and audio resumes at the exact sample on play.  Scrubbing the timeline visually snaps blocks to their interpolated position at that time.

---

## Toolbar

The toolbar at the top of the viewport lays out left-to-right as:

```
[Type pill] [Type dropdown] [Play] [@Time]  …  [Layers ▾] [Doppler] [Anchor] [Path…] [Free Cam] [Freeze Move] [Spatial━━]  [Mute ▾] [View ▾] [File ▾] [Help]
```

### Type pill + dropdown (left)

| UI | What it does |
|----|----------------|
| **Color pill** | Shows the active instrument name and a swatch in that type’s color. |
| **Dropdown** | Picks one of **24 block types** grouped by category (Synth, Strings, Woodwinds, Brass, Percussion, Special). This is what the next `LMB` placement will create. |
| **Play** | Audition the selected block's sound right now.  Time-aware: previews whichever sound the block would be making at the current playhead (the latest scheduled sound that has started, else the block's main sound).  Enabled only when a block is selected. |
| **@Time** | Move the playhead to the selected block's start time so blocks + camera snap to that moment.  Does **not** start playback — press Play yourself. |

### View-element controls (centre-right)

| Control | What it does |
|--------|--------------|
| **Layers ▾** | Drop-down menu of viewport overlays (ticked = visible): **Floor**, **YZ Wall**, **XY Wall**, **Move arrows**. |
| **Doppler** | Toggle the Doppler-effect pitch shift on moving voices.  Off by default. |
| **Anchor** | Freeze the **audio** listener at the current camera pose; fly around to compose “from here”; toggle off to snap the view back. |
| **Path…** | Open the camera-path editor (list of Hold / Lerp keyframes; add static viewpoints or record live segments).  Whenever the path contains at least one keyframe the camera **auto-follows** during playback **and** while scrubbing the timeline (so you get a live preview).  Recording always overrides following. |
| **Free Cam** | Override the path's hold on the camera while keeping the path data intact.  ON = user keeps full manual control even mid-playback.  OFF = camera follows path.  Useful when you want to scout shots while a path is loaded. |
| **Freeze Move** | Globally hold **all** blocks in place during playback (paths stay intact; audio keeps playing).  For a single block, use the sidebar's "Freeze this block" toggle instead.  Un-freezing resumes motion from the current playhead — voices are repositioned, not cut. |
| **Spatial** | Falloff sensitivity (0.25 = gentle … 3.0 = aggressive).  Default 1.0. |
| **Help** | Show the keyboard / mouse cheat-sheet. |

### `Mute ▾` menu

Per-type indefinite mute.  Grouped by category; ticked entries are currently
silenced.  Shortcuts at the top: **Mute All Types** / **Unmute All Types**.
A muted type's blocks still animate and emit Movement events — the audio just
goes silent.  The filter is transient (not saved to `.sime`, not baked into
exports).

### `View ▾` menu

Per-type visibility filter.  Same grouped layout as Mute.  Hidden blocks are
skipped by the renderer (mesh **and** highlights) but stay in the scene for
sequencing / persistence.  Useful for isolating one instrument while
composing.

### `File ▾` menu

| Item | Action |
|--------|--------|
| **New Scene** | Clear the current scene and start fresh |
| **Open Scene…** | Open a `.sime` file from disk |
| **Save** | Save to the current file (prompts if no file loaded) |
| **Save As…** | Always prompts for a new file name |
| **Export Audio…** | Bounce the full timeline mix to WAV, FLAC, AIFF, or Ogg Vorbis |

The app also auto-saves to `%APPDATA%/SIME/autosave.sime` when you close it.
Auto-load on launch was removed so **New Scene always starts empty** —
use `File → Open Scene…` or your OS file association to load the auto-save
manually if you want it back.

---

## Workspace Tabs

Three tabs across the top of the window switch the whole workspace:

| Tab | What you get |
|-----|--------------|
| **Scene** | The original 3D editor — sidebar, toolbar, viewport, and collapsible transport bar at the bottom. |
| **Timeline** | A full-screen DAW-style timeline (same transport + region editing as the Scene tab bar). Play, scrub, speed, BPM, and region edits stay in sync with Scene. |
| **Synthesizer** | A subtractive synth for designing sounds offline, previewing them, and exporting WAVs into `workspaceAudios/`. |

> **Why the 3D view keeps working off-tab:** the viewport's OpenGL loop drives
> the transport clock, sequencer, and edit queues. JUCE detaches an OpenGL
> context the moment its component is hidden or sized 0×0, so on the Timeline /
> Synthesizer tabs the viewport is parked **1×1 off-screen** instead — the engine
> keeps ticking, it just isn't drawn. This is why playback stays in sync when you
> switch tabs mid-song.

### The Synthesizer tab

A dark-techno / cyberpunk subtractive synth. The layout (oscillators, mixer,
amp envelope, filter, modulation/LFOs, effects, voice, mod matrix, arp/seq,
performance, master, macros, keyboard) is modelled on a hardware-style mockup
and scales to the window.

**Controls wired to sound:** OSC 1 **waveform**, the **A / D / S / R** bars,
**Filter Cutoff + Resonance**, **Master** level, and the **on-screen keyboard**
(click a key to audition that note). The remaining panels (extra oscillators,
LFOs, effects, mod matrix, macros, etc.) are styled performance surfaces — fully
interactive visually, not yet routed into the DSP (a deliberate extension point).

#### Workflow

1. Open the **Synthesizer** tab.
2. Shape the sound: pick the OSC 1 **waveform**, set **A/D/S/R**, dial in
   **Cutoff/Res**, set **Master** level.
3. Click a **keyboard** key (or **Preview**) to hear the patch.
4. Type a name in **Enter File Name**, click **Export WAV** — the file lands in
   `Source/workspaceAudios/` and appears in the sidebar **Audio** list.
5. Switch to **Scene**, pick **Synth** from the type dropdown, place a block.
6. **Right-click** the block → **Browse File** → assign the exported WAV (same
   flow as **Custom** blocks).

The **Synth** block type (purple) behaves like **Custom**: silent until a
workspace WAV is assigned, then played by the sequencer with full spatial audio.

#### A quick patch to try

- **Bass:** OSC 1 = **Saw**, A≈0.01, D≈0.25, S≈0.5, R≈0.3, Cutoff low
  (~600–900 Hz), Res≈0.4, Master≈0.8 → click a **low** key.
- **Lead:** OSC 1 = **Square**, push Cutoff up for brightness → click a higher key.

---

## Block Info Panel

Selecting a block (LMB in normal mode) opens the Block Info panel on the
sidebar.  The panel is scrollable — use the mouse wheel to reach rows below
the fold.  Apply commits everything atomically.

| Section | Controls |
|---------|----------|
| **Identity** | Block type label + serial (e.g. `Violin 12`) |
| **Position** | `X / Y / Z` editors |
| **Timing** | `Start (s)` and `Duration (s)` editors |
| **Movement** | `Enable Recorded Movement` toggle; **`Freeze this block (hold position)`** toggle (per-block freeze, independent of the global Freeze Move); **`Loop movement (teleport to start)`** toggle (loops each recorded **segment** within its own window — see Block Movement Recording); **`Keyframes...`** button that opens the position-keyframe editor (manual `time / x / y / z` rows; alternative to Alt-drag recording; `Snap times` now **resamples** the path so it never teleports); `Mode` combo (Natural / Loop / Stretch / Speed); `Movement Duration` editor (0 = use region duration); `Path Y lift` integer offset |
| **Loop** | `Loop sound` toggle; `Loop length (s)` editor + `Loop = Block Dur.` button (one-click match); `Loop gap (s)` editor (silence between repeats) |
| **Mute / Hide** | `Mute (no audio, forever)` toggle; `Hide block in viewport` toggle; **`Mute Schedule...`** button that opens a floating editor where you can add any number of timed mute windows (start + duration, in seconds).  The button label shows the active window count, e.g. `Mute Schedule (3)...`. |
| **Duration / sound** | `Match Duration to Sound` — sets region duration to the loaded sample's natural length, preserving any recorded movement span; **`Fit sound / movement…`** dropdown reconciles a sound that's a different length than the movement (distort sound → movement [warns: pitch shifts], distort movement → sound, or hard-cut at the movement end) |
| **Sound Schedule** | **`Sound Schedule...`** button opens a two-pane editor for playing **multiple timed sounds** from this block (e.g. note A at 5s, note B at 45s).  Each row has start, duration, a **Loop** toggle + **Gap (s)** (repeat that sound across its window), and a sound picker (filtered to the block's instrument type). |
| **Audio analysis** | Pitch line (Hz / note / duration / period) + filled-blue oscilloscope graph for the loaded sample |
| **Movement map** | Top-down keyframe path graph (when movement is recorded) |
| **Buttons** | `Apply` (commit) and `Reset to Default` (clear movement-mode fields) |

### Block edit popup (right-click in edit mode)

In **edit mode** (`Tab`), **RMB** a block to open a floating edit window.  The
popup is now a focused timing + sound editor — it does **not** carry loop
controls anymore (they live in the Block Info panel above so loop, length,
and gap stay together).  For non-Custom blocks it embeds the searchable
sample list from `CSV/sound_library.csv` / `Sounds/`.  Type a note name
(e.g. `A4`), dynamic (`forte`), or articulation (`arco`) to filter ~1,500
samples.  **Double-click** a row to apply immediately, or select + press
**Apply**.  **Custom** blocks use **Browse File** for any WAV on disk.

---

## Transport

The bottom transport bar holds Play / Pause / Stop plus the **speed
dropdown** (0.25× / 0.5× / 0.75× / 1× / 2× / 3×).  Speed can be changed
before or during playback; it scales the transport clock so the whole
composition accelerates uniformly (no time-stretch artefacts).

* **Play** — start or resume.
* **Pause** — freeze the audio at the exact sample so resume is seamless.
* **Stop (square)** — kill all voices, reset the clock to 0, and **snap
  every block with a recorded path back to its starting keyframe**.
* **Scrub the timeline** — drag the playhead to seek.  Blocks visually snap
  to their interpolated keyframe position at the new time.

Run the app from anywhere (double-clicking `SIME.exe`, Visual Studio **Local Windows Debugger**, or `cd C:\sime` in a terminal). The app finds the **content root** by walking **up** from (1) the current working directory and (2) the folder containing `SIME.exe`, until it sees **both** `Sounds/` and `CSV/sound_library.csv`. Typical layout:

```
sime/                    ← content root (detected automatically)
  Sounds/
  CSV/sound_library.csv
  build/SIME_artefacts/Debug/SIME.exe
```

If the picker still says the library is not loaded, see **Troubleshooting (sound picker empty)** in [`md files/AUDIO_LIBRARY_REPORT.md`](md%20files/AUDIO_LIBRARY_REPORT.md). Full design notes are there too.

---

## Workflow

**Quick start:**

1. Launch the app and click the viewport to focus it.
2. Fly the camera with `RMB drag` + `WASD`.
3. Place blocks with `LMB`. Use `Shift + LMB` to place blocks in mid-air.
4. Press `Tab` to enter edit mode.
5. `RMB`-click any block to set its **Start time**, **Duration**, and **Sound** in the popup.
6. Press **Play** in the transport bar.
7. Blocks highlight as they trigger. Fly around with WASD while playing — pan and level should follow **your head**, not the world origin.

**Spatial audio mapping** (listener = camera unless **Anchor** is on):

| Input | Effect |
|-------|--------|
| **Listener pose** | Camera position + look direction each frame (`AudioEngine::setListenerPosition` / `setListenerOrientation`) |
| **Horizontal offset** | Stereo pan (equal-power), projected onto camera right vs forward |
| **Distance** | Level — `refDist / (refDist + distance)` with `refDist ≈ 3 m`, scaled by toolbar **Spatial** sensitivity |
| **Behind listener** | Extra attenuation (~35% at full rear) so “behind you” is obvious in headphones |
| **World Y** | Pitch — each grid unit = one semitone (`2^(y/12)`) |
| **Toolbar Spatial** | Slider 0.25–3.0 (default 1.0); higher = steeper falloff |
| **Toolbar Anchor** | Freeze the audio listener at the current view; camera can still move; toggle off restores the view |
| **Sidebar → SPATIAL** | Live “from listener” distance (m) and approximate dB for the selected block; **Distance…** measures A→B in metres (click block B in the viewport) |
| **New placements** | Default sample from the CSV library per block type (violin synth fallback if the library is missing) |

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

Each type has a distinct color in the 3D viewport. After placing a block, hit `Tab` and right-click it to open the **edit popup**, which embeds a searchable list of every sample for that block's category — type a note name (e.g. `A4`), dynamic (`forte`), or articulation (`arco`) to filter ~1,500 samples instantly. The chosen WAV is decoded on commit (lazy load) so the picker is responsive even with 13,759 samples in the library.

For full details of the sample library and lazy-loading strategy see [`md files/AUDIO_LIBRARY_REPORT.md`](md%20files/AUDIO_LIBRARY_REPORT.md).

---

## Block Movement Recording

Blocks can have a recorded movement path that plays back in sync with the transport.

### How to Record

**Step 1 — Enter edit mode**
Press `Tab`. The HUD shows `EDIT MODE` and all blocks get a dim yellow highlight.

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
- **Confirm** — movement is saved.  The block's region is **extended** to cover the motion if needed, but the sound is **never cut**: recording a movement no longer shrinks (or locks) the sound duration.  Set the sound length yourself, or use `Match Duration to Sound` / the `Fit sound / movement…` dropdown.
- **Cancel** — the in-progress segment is discarded and the block returns to its original position (multi-segment: only the new segment is dropped; earlier segments are kept).

**Step 6 — Play it back**
Press `Tab` to exit edit mode, then press **Play**. The block will travel through its recorded positions in sync with the transport clock.

### Multi-segment recording (record movement at a later time)

Recording is anchored to the **playhead**, not the block start.  To add a second
motion after the first:

1. Move the playhead to the time where you want the next motion to begin (the
   block snaps to its position there).
2. `Alt + LMB`-drag again to record an **additional** segment starting at that
   time.

The earlier path is preserved, the new keyframes are spliced in at the playhead,
and the block **holds** between segments (resuming from its current position — no
teleport).  Align a segment with a scheduled sound's start time to give that
sound its own movement.

### Looping movement

Enable **`Loop movement (teleport to start)`** in the sidebar MOVEMENT section.
Looping is **per segment**: each recorded segment repeats within its own window —
the block teleports back to that segment's first keyframe and replays until the
next segment's time (or the region end for the last segment), then the next
segment plays.  So you can loop segment 1, then have segment 2 play afterward.
This instant teleport-to-start is the **only** case where a block teleports;
everywhere else motion steps smoothly along the path.

### Authoring keyframes by hand (alternative to Alt-drag)

If the Alt-drag capture is too noisy — or if you just want surgical
control over where the block is at a specific second — open the **Block
Info panel**, hit the **`Keyframes...`** button in the MOVEMENT section,
and edit the path as a flat table.

| Field | Meaning |
|-------|---------|
| `Time (s)` | Seconds from the block's `Start`.  Sort order is enforced on Apply. |
| `X / Y / Z` | Integer grid position at that moment. |
| `+ Add Keyframe` | Appends a row prefilled with the last row's time + 0.5 s and position. |
| `Clear All` | Removes every keyframe — equivalent to "no movement". |
| `Apply` | Sorts by time, shifts so the first row sits at `t = 0`, and replaces the block's `recordedMovement`. |
| `Cancel` | Closes the popup without touching the block. |

Opening the popup on a block that **already** has a recorded path
loads its keyframes for editing, so this is also the cleanup tool for
wobbly Alt-drag captures.  Sub-2-keyframe lists clear the path (the
engine needs at least two points to interpolate).

### Important Notes

- **Duration locking** — after confirming a recording, the block's duration is locked and cannot be edited in the popup. This keeps keyframe timing in sync.
- **Start time is still editable** — the entire movement path shifts with it; relative timing stays intact.
- **Movement constraints** — positions must be valid grid cells, within ±40 on X/Z, Y ≥ 0, not occupied by another block, and not the origin `(0,0,0)`.
- **Movement Duration** — the Block Info panel lets you scale how long the path takes to play out independently of the region duration (0 = follow region).
- **Path Y lift** — globally raises / lowers every keyframe at playback time without re-recording.
- **Enable Recorded Movement** — keep keyframes on disk but disable playback on a per-block basis.

---

## Block Playback Modes (Natural / Loop / Stretch / Speed)

Every block carries a **playback mode** that controls how its sample plays
across the region.  Set it in the Block Info panel's `Mode` combo (or via
the dedicated `Loop sound` toggle for the Loop mode).

| Mode | Behaviour |
|------|-----------|
| **Natural** | Sample plays once at its natural rate and stops at sample end. |
| **Loop** | Sample wraps inside the audio thread for the full region duration (gapless by default).  Use `Loop length (s)` to stop the loop early (movement still plays through the full region).  Use `Loop gap (s)` to insert silence between repeats. |
| **Stretch** | Sample's playback rate is scaled so it spans the region; pitch follows. |
| **Speed** | Like Stretch but only speeds up (rate ≥ 1.0); useful when the sample is longer than the region. |

The `Loop = Block Dur.` button next to the `Loop length` editor one-clicks
the current block duration into the field — handy for "loop fills the
region" intent.

---

## Mute, Hide, and Type Filters

Three layered controls for keeping the scene focused.

### Per-block toggles (Block Info)

* **Mute (no audio, forever)** — silence this block.  Movement still plays.
  Useful for keeping a part recorded but out of the mix.
* **Hide block in viewport** — renderer skip (mesh and highlights).  Block
  still ticks the sequencer and emits audio.
* **Mute from / to (s)** — time-window mute.  While the playhead is inside
  `[from, to)`, the audio cuts and resumes automatically as the playhead
  enters / leaves the window.  Set both to 0 to disable.

### Toolbar `Mute ▾` and `View ▾`

Per-type filters.  Each menu lists every block type grouped by category
(Synth / Strings / Woodwinds / Brass / Percussion / Special) with check
marks for currently muted / hidden types.  Use "Show All Types" or
"Mute All Types" for bulk toggles.  Both filters are **transient** —
they're not persisted to `.sime` and are intentionally ignored by the
audio exporter (exports only honour per-block `Mute (no audio, forever)`).

### Combined precedence

Indefinite mute (per-block forever + per-type) wins over the time window.
A muted block still ticks its movement state machine, so animation keeps
going regardless of mute combination.

---

## Audio Analysis (frequency & oscilloscope)

Selecting a block runs an offline analysis on its loaded sample and shows
the result in the Info panel:

* **Pitch line** — estimated fundamental frequency, nearest note name,
  duration, and period.  Estimation is autocorrelation-based over a
  musically reasonable lag window (≈ 60–1500 Hz).  When the sample is
  noise-like or too short to estimate reliably, the line marks the pitch
  as "(uncertain)" so percussion doesn't pretend to have a tuned pitch.
* **Oscilloscope graph** — filled blue min/max envelope (128 columns).
  Quick visual for attack / decay shape.

Analysis is cached per selection; opening a different block re-analyzes.

---

## Camera Path (listener keyframes)

A per-scene **camera path** lets you choreograph the listener — the same
way the [position-keyframe popup](#block-movement-recording) choreographs
blocks.  As soon as the path contains at least one keyframe the camera
**auto-follows** during playback (and during export); to "let go" of the
camera again, clear the path from the **Path…** editor.

### Two keyframe modes

| Mode | Meaning |
|------|---------|
| **Hold** | Freeze the pose until the next keyframe time, then snap.  Use for static "shots" — e.g. front of orchestra for 10 s, then a teleport behind the cellos.  A Hold keyframe also has a **HOLD (s)** value that defines how long it occupies the timeline; the next "+ Hold @ cam now" insert and the next recording session land at `time + holdDuration`. |
| **Lerp** | Smoothly interpolate position + yaw + pitch toward the next keyframe.  Live R-recording emits a stream of `Lerp` keyframes at the chosen **Capture every** rate (default 1 s). |

Position is straight linear, yaw is **shortest-arc** lerped (no
unwinding through a full circle), pitch is linear.  Before the first
keyframe and after the last, the path holds at the endpoint pose — so
you don't need to clamp times yourself.

### Authoring

Open **Path…** in the toolbar (or just click **Help** for a one-shot
reminder).  The popup shows every keyframe as a row with editable Time /
X / Y / Z / Yaw° / Pitch° fields plus a `Hold ▾ Lerp` combo and a
remove (×) button.

Two ways to author:

* **+ Hold @ cam now** — drops a `Hold` keyframe at the **next available
  time slot** using the live camera pose.  "Next slot" = the end of the
  last segment in the draft (`time + HOLD (s)` for Hold tails, plain
  `time` for Lerp tails) — so consecutive Hold inserts stack neatly
  rather than piling on top of each other at `t=0`.  If the draft is
  empty, the first insert uses the current playhead.  The popup is the
  source of truth while it's open, so clicks add to the local draft
  immediately and aren't overwritten by background polling.
* **Record (R)** — starts a live capture session.  **Works whether or
  not the transport is playing.**  When playing, timestamps ride the
  transport clock.  When paused, timestamps start from the current
  playhead and advance with wall-clock time — so you can stand still in
  the timeline and hand-author a moving shot.  Press **R** (or click
  the button) again to stop; the captured keyframes splice into the
  path at the start time and anything inside the recording window is
  replaced.

Apply commits the list; Cancel keeps whatever was there before.  **Clear
all** and the per-row × button operate on the local draft and only flow
to the scene when you press Apply.

### Live playback & scrub preview

The camera auto-follows the path whenever at least one keyframe exists,
**both during playback and while paused** — so dragging the timeline
(or typing a time into the transport field) gives you an instant
camera preview from the path, the same way the per-block keyframe
popup previews block positions while scrubbing.  Recording always
overrides following (so you can fly the camera while capturing without
fighting the active path).

To override the path **without clearing it**, toggle the toolbar
**Free Cam** pill ON — the path data is preserved (and still used for
the audio listener pose during export) but the live viewport camera
goes back under your control.

### Export

When a camera path exists, the offline bouncer ignores the static
anchor / camera pose and animates the listener through the path.  The
**Export Audio** dialog shows the active mode:

* `CAMERA PATH active (N keyframes, T0 → T1 s).` (green) — exporter
  will animate the listener.
* `ANCHORED at (…)` (blue) — exporter freezes at the anchor.
* `Anchor not set. Export will use the current camera pose…` (yellow) —
  exporter uses whatever the camera is doing right now.

The path is saved with the scene in `.sime` v11 (older files load
cleanly: v10 files come back without `holdDurationSec`, which defaults
to 0; pre-v10 files load with an empty path entirely — the binary
trailer is identified by a `CPTH` magic so older readers ignore it
cleanly).

### Where it lives in the code

| Concern | File |
|---------|------|
| Data + interpolation | `Source/CameraPath.h` |
| Live following + R hotkey + GL-thread recording | `Source/ViewPortComponent.cpp` |
| Editor popup | `Source/CameraPathPopup.{h,cpp}` |
| Offline bake | `Source/SceneAudioExporter.cpp` (`resolveListener` + per-chunk re-mix) |
| Persistence v10 | `Source/SceneFile.cpp` |

Detailed engineering notes:
[`md files/CAMERA_PATH_REPORT.md`](md%20files/CAMERA_PATH_REPORT.md).

---

## Doppler Effect

The audio engine carries a Doppler model that pitch-shifts moving voices
based on their velocity relative to the listener (the camera).

* Toggle: `Doppler` pill in the toolbar.  **Off by default** in this build
  while we tune defaults.
* Source velocity is derived from recorded-movement keyframes by the
  sequencer (per segment).
* Listener position is fed each frame from the live camera by the GL
  thread.
* Speed of sound is 343 m/s by default (single grid unit ≈ 1 m).
* The exporter does **not** bake Doppler into rendered files by default
  (matches the live default-off behaviour).  Flip it on in
  `SceneAudioExporter::dispatchEvents` if you ever want a Doppler-baked
  bounce.

---

## Save / Load Scenes

Scenes are saved as `.sime` binary files — a compact, custom format that stores every block's full state.

### What Gets Saved

Each block record includes:
- Serial number and block type (one of the 23 instrument types)
- 3D grid position (x, y, z) + color
- Sound ID, custom WAV file path
- Start time, duration, duration-lock flag, copied-region list (`timesList`)
- Recorded movement path (keyframes with time and position)
- Movement controls — `movementEnabled`, `playbackMode` (Natural / Loop / Stretch / Speed), `movementDurationSec`, `movementYOffset`
- Loop controls — `isLooping`, `loopDurationSec`, `loopBufferSec`
- Per-block flags — `isMuted`, `isHidden`
- Mute schedule — `muteWindows` vector of `{ startSec, durationSec }` entries

### File Format

The `.sime` format uses a 12-byte header (`SIME` magic, version, block count) followed by tightly packed block records.  The current version is **v14**.  Older files still load — additive trailing fields fall back to sensible defaults, and v8's single `muteStartSec` / `muteEndSec` pair is auto-migrated into the first entry of `muteWindows`.  Anything written by the current build is **not** readable by older binaries.

Recent additive bumps: **v12** adds each block's **Sound Schedule** (timed sounds, persisted by library-relative path and re-resolved to a runtime sound id on load); **v13** adds the per-block **`movementLoop`** flag; **v14** adds per-scheduled-sound **`loop`** + **`loopGapSec`**.  See [`md files/SESSION_2026-06-03_REPORT.md`](md%20files/SESSION_2026-06-03_REPORT.md) for the field-by-field layout.

### Auto-Save

When the app closes, it saves the current scene to `%APPDATA%/SIME/autosave.sime`.  The previous "silent auto-load on startup" behaviour was removed — **New Scene now always starts empty**, and `File → Open Scene…` is the explicit way to restore a saved scene (including the auto-save).

### Workflow

1. Build a scene with blocks.
2. Open the **File ▾** menu, then choose **Save** or **Save As** and pick a location and name.
3. **File → Open Scene…** to load a different `.sime` file at any time, or open `%APPDATA%/SIME/autosave.sime` if you want the auto-save back.
4. **File → New Scene** to start a blank scene.

---

## Export Audio

You can bounce the **full mixed stereo output** for the entire timeline (from time 0 through the latest block end time) to a file. This is an **offline** render: it does not play through the speakers; it steps the same `TransportClock` + `SequencerEngine` logic in small time slices and mixes with the same gain, pan, and pitch rules as `AudioEngine`.

- **File → Export Audio…** — choose a format, then a save path.
- **Formats** — **WAV** and **AIFF** (16-bit PCM, lossless), **FLAC** (lossless), **Ogg Vorbis** (lossy).
- **Why no MP3 / MP4?** JUCE's built-in `juce_audio_formats` module is read-only for MP3 and does not ship an MP4 / AAC encoder.  Adding either one means **either** bundling a third-party encoder into the build (LAME for MP3 — LGPL; an AAC encoder for MP4 — patent-encumbered) **or** shelling out to a tool like FFmpeg at export time, which requires the user to have FFmpeg installed.  Both options were considered for this build; the current decision is to keep the export pipeline dependency-free.  If you need MP3 / MP4, export to WAV and run a free online converter — the WAV is the same audio you'd get out of any encoder we add.
- **Length** — matches the timeline span (same idea as the transport bar duration: maximum of all blocks’ `endTimeSec()`). Empty scenes cannot be exported.
- **Sample rate** — matches the live audio device rate when possible; if the chosen file format only supports specific rates, the exporter picks the closest supported rate.

For implementation notes, limits (for example the 20-minute safety cap), and file layout references, see [md files/EXPORT_AUDIO_REPORT.md](md%20files/EXPORT_AUDIO_REPORT.md).

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

**`BlockEntry`** — the shared data structure. Carries position, block type, sound ID, start/duration timing, playback state flags, recorded movement path (`std::vector<MovementKeyFrame>`), playback mode (`Natural / Loop / Stretch / Speed`), movement duration / Y-lift / enabled flag, loop length / loop gap, per-block mute / hide flags, a `muteWindows` vector of scheduled silence ranges (`MuteWindow { startSec, durationSec }`), and transient runtime flags (`effectiveMuted`, `wasMutedLastTick`, `sampleNaturalDurationSec`).

**`AudioAnalysis`** — offline pitch (autocorrelation F0) + waveform-envelope helper called by the sidebar when a block is selected.

**`SceneAudioExporter`** — deterministic offline mixer that mirrors the live audio path.  Stepwise simulation of the real `TransportClock` + `SequencerEngine`, with a private `MixerVoice` per-voice carrier that copies `ActiveVoice`'s loop-gap, Doppler, gain, and pan logic.

**Doppler:** `AudioEngine` carries listener position + speed-of-sound atomics; the GL render thread feeds the camera each frame; per-voice rate is `pitchRate × blockRate × dopplerRate × transport playbackRate`.

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
    ├── MainComponent.cpp/h        # Top-level layout (tabs, sidebar + viewport + transport)
    ├── ViewPortComponent.cpp/h    # 3D OpenGL viewport, input, sequencer loop
    ├── Renderer.cpp/h             # OpenGL batch renderer (blocks, grid, highlights)
    ├── Camera.cpp/h               # First-person camera + view snapping
    ├── Raycaster.cpp/h            # DDA voxel raycasting
    ├── VoxelGrid.h                # Sparse voxel data structure
    ├── MathUtils.h                # Vec3i, Vec3f, Mat4
    ├── BlockEntry.h               # Shared block data struct (incl. mute, hide, loop gap, mute window)
    ├── BlockType.h                # Block type enum + helpers (incl. Synth)
    ├── SequencerEvent.h           # Event value type (Start/Stop/Movement) — carries velocity for Doppler
    ├── AudioEngine.cpp/h          # JUCE audio engine — atomic pause/stop, listener pos, Doppler, loop gap
    ├── AudioAnalysis.cpp/h        # Offline F0 estimate + waveform envelope for the sidebar
    ├── SequencerEngine.cpp/h      # Block → audio event sequencer (incl. mute transitions + loop length)
    ├── TransportClock.cpp/h       # Playback clock (with playbackRate)
    ├── SidebarComponent.cpp/h     # Left-side block info panel — scrollable, embeds analyzer + movement graph
    ├── TransportBarComponent.cpp/h # Bottom play/pause/stop bar + speed dropdown
    ├── SynthPatch.h               # Synth parameter struct (osc / ADSR / filter)
    ├── SynthRenderer.cpp/h        # Offline subtractive render + WAV writer
    ├── SynthComponent.cpp/h       # Full-screen Synthesizer tab UI
    ├── BlockEditPopup.cpp/h       # Floating block edit dialog (timing + sound only; loop UI lives in Info)
    ├── MovementConfirmPopup.h     # Movement recording confirm dialog
    ├── SoundSchedulePopup.cpp/h   # Two-pane editor for per-block timed sounds (start/dur/loop/gap + picker)
    ├── SceneFile.cpp/h            # Binary .sime scene save/load (v14 — see SESSION_2026-06-03_REPORT.md)
    ├── SceneAudioExporter.cpp/h   # Offline timeline bounce + format writers — mirrors live audio engine
    ├── ExportAudioDialog.cpp/h    # Format picker for export
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

### Export
**`SceneAudioExporter.cpp`**
- Offline step size → `kChunkSamples` (smaller = finer event timing, slower bounce)
- Maximum length → `kMaxDurationSec`
- New lossy/lossless codec → extend `SceneAudioExporter::Format`, `pickFormat()`, and the dialog combo in `ExportAudioDialog.cpp`

### Sound Library
**`SoundLibrary.cpp/h`**
- Master index path → `CSV/sound_library.csv` (loaded in `ViewPortComponent::newOpenGLContextCreated`)
- Sounds folder root → `Sounds/` under the auto-resolved **content root** (walks up from CWD and from the `.exe` folder — see `resolveContentRoot()` in `ViewPortComponent.cpp`)
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
- Adding a new persisted field: bump `kVersion`, append `writeVal(...)` at the
  end of the per-block write block, and add a `if (version >= N) readVal(...)`
  branch in `load()` with a sensible default for older files

### Block Info Panel
**`SidebarComponent.cpp` / `SidebarComponent.h`**
- Add a new editor → declare the control, `addAndMakeVisible` it, set bounds in `resized()`, paint its label in `paint()`, hydrate it in `showBlockInfo`, clear it in `clearSelectedBlock`, plumb it through `onApplyBlockInfo` (widen the std::function signature), and forward to `view.applySidebarBlockInfo` from `MainComponent`
- Scrolling math → `infoScrollY_`, `infoContentBottomY_`, `mouseWheelMove`

### Toolbar Menus
**`MainComponent.cpp` / `MainComponent.h`**
- `File ▾` → `showFileMenu()` / `handleFileMenu()`
- `View ▾` (per-type visibility) → `showViewMenu()`
- `Mute ▾` (per-type indefinite mute) → `showMuteMenu()`
- Add another `Xxx ▾` filter menu: declare a `TextButton`, follow the existing patterns; add a `std::array<std::atomic<bool>, kNumBlockTypes>` to `ViewPortComponent` with `setBlockTypeXxx` / `isBlockTypeXxx` accessors

### Doppler & Listener
**`AudioEngine.cpp` / `AudioEngine.h`**
- Listener position → `setListenerPosition(x, y, z)` (called from `ViewPortComponent::renderOpenGL` each frame)
- Speed of sound → `setSpeedOfSound(c)`
- Doppler enable → `setDopplerEnabled(bool)` (the toolbar pill flips this)

### Playback Speed
**`TransportClock.cpp` / `TransportClock.h`**
- `playbackRate` member; `update(dt)` multiplies by it before advancing
**`TransportBarComponent.cpp`**
- Speed dropdown options live in the `juce::PopupMenu` built in `showSpeedMenu()`

---

## Known Bugs & Issues

### Editor / Lint — Not Real Bugs

All red-line errors in `AudioEngine.cpp` cascade from one root cause: Cursor's clangd linter can't find JUCE headers because they resolve through CMake, not standard include paths. The MSVC build compiles fine.

To fix the editor cosmetics only, add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to the CMake configure step and point a `.clangd` file at the result.

### Fixed in the 2026-05-23 session

* `T1` — Block / sidebar edits are now queued through the GL thread via
  `pendingBlockEdit_` / `pendingSidebarEdit_` (mutex-guarded), drained in
  `ViewPortComponent::renderOpenGL`.
* HUD overlay rendering bug (washed-off right half of the controls pill) —
  fixed by a full GL state reset at the end of `renderOpenGL`.
* "Block 251" after New Scene — `nextSerial` resets to 1 in `clearScene()`.
* New Scene loading the previous scene — silent auto-load removed from
  `Main.cpp`.
* Block Info persisting after deletion / scene change — `clearSelectedBlock`
  + `clearSelectedBlockIfSerial` plumbed through the deletion paths.
* Hover / select interaction — green hover highlight + LMB-on-block selection
  now work in normal mode.
* Loop toggle "turns off inconsistently" — Loop UI consolidated in the
  sidebar Info panel; the popup no longer carries its own toggle.

### Fixed after the 2026-05-23 session

* **Loop length editor + "= Block Dur." button floated over the sidebar
  header when scrolling** — they bypassed the `placeRow` helper and used
  raw `setBounds(x, y, ...)` with the unscrolled logical y.  Now go
  through a `placeRowPair` helper that applies the scroll offset and the
  same offscreen-clip rule as the other rows.
* **"Movement" graph label clipped into the oscilloscope** — the label
  was painted at `graphArea.getY() - 16` but the previous row only left a
  10-px gap, so the label was drawn straight on top of the wave's bottom
  edge.  `paint()` now reserves the same 16-px label band that
  `resized()` already allocates.

### Added after the 2026-05-23 session

* **Mute Schedule popup** — replaces the inline "Mute from / to" editor
  pair with a floating, scrollable editor that holds any number of
  scheduled mute windows (`MuteWindow { startSec, durationSec }`) per
  block.  Scene format bumped to v9; v8 files auto-migrate their single
  window into the new vector.

See [`md files/SESSION_2026-05-23_REPORT.md`](md%20files/SESSION_2026-05-23_REPORT.md)
for the full bug log + root causes.

### Fixed after the 2026-05-24 session

* **`Ctrl + C` accidentally cleared the entire scene** —
  `ViewPortComponent::keyPressed()` matched the bare letter `'C'` and
  treated it as Clear-All without ever inspecting the modifier flags,
  so the standard copy shortcut was a destructive operation.  The
  handler now requires *no* Ctrl / Cmd to fire, and `ViewPortComponent`
  early-returns `false` for any Ctrl-/Cmd-combo so the event bubbles
  cleanly up to `MainComponent` (where the real shortcut table lives).

### Fixed after the 2026-05-24 session (continued)

* **`= Block` loop-length button protruded past the sidebar** — the loop
  row used `std::max(40, editorW - btnW - gap)`, which on the 220-px
  sidebar forced a 40-px editor and shoved the 72-px button ~14 px past
  the right margin.  The button is now right-aligned inside the editor
  column (`btnX = getWidth() - margin - btnW`), the artificial 40-px
  floor is gone, and the control uses flat colours + `ConnectedOnLeft`
  so it reads as one row with the loop-length field.

### Added after the 2026-06-01 session (camera path + help popup)

* **Camera (listener) path** — a per-scene sequence of `CameraKeyframe`s
  that drives the camera and the audio listener during playback **and**
  export.  Two modes per keyframe, modelled after the user's spec:
  * **Hold** — freeze the pose until the next keyframe (instant cut).
  * **Lerp** — smoothly interpolate position + yaw + pitch to the next
    keyframe.  Live R-recording emits a stream of Lerp keyframes at ~20 Hz.
  Authored from the new **Path…** popup (toolbar) — manual rows for time /
  pos / yaw / pitch / mode + **Hold @ cam now** and **Record (R)** buttons.
  The camera auto-follows whenever the path is non-empty; recording
  always overrides following.  See
  [`md files/CAMERA_PATH_REPORT.md`](md%20files/CAMERA_PATH_REPORT.md).

* **Export honors the path** — when a camera path is set, the offline
  bouncer samples it per chunk and feeds chunk-constant listener gains to
  every active voice, so a sustained note slides through pan / level as
  the listener moves.  Otherwise it falls back to the anchor (if set) or
  the current camera pose.  The export dialog shows which of the three
  modes will be baked.

* **Help popup** — toolbar **Help** button opens a single-panel cheat
  sheet listing every key + mouse action, kept in sync with the actual
  bindings in `ViewPortComponent::keyPressed` and
  `MainComponent::keyPressed`.

* **Camera reset moved to `Home`** — frees `R` for camera-path recording.

### Follow-ups on the 2026-06-01 session (UX feedback round)

* **R works anywhere in the window** — the shortcut is now also handled by
  `MainComponent::keyPressed`, so a focused popup or sidebar control no
  longer eats it.  Pressing **R** toggles camera-path recording whether
  the viewport, the toolbar, or the path editor has focus.
* **R works whether or not the transport is playing** — when playing,
  keyframe times ride the transport clock; when paused, they start from
  the current playhead and advance with wall-clock time, so you can
  stand still in the timeline and hand-author a moving shot.
* **Path editor: Clear / × actually clear** — the popup is now the
  source of truth while it's open.  Background polling only re-fetches
  the path when a recording session ends; manual edits, deletes, and
  Clear All persist until you press Apply (or close to discard).
* **Path On toggle removed** — the camera auto-follows whenever a path
  exists, recording always overrides following.  One less control, one
  fewer mode for the user to keep track of.
* **Layers menu** — Floor / YZ Wall / XY Wall / Move arrows moved into
  a single **Layers ▾** dropdown to declutter the toolbar (which had
  grown to 11 buttons + a slider).
* **Help popup title fix** — replaced the em-dash that was rendering as
  garbled bytes (`â…`) with a plain hyphen.
* **Type-to-seek time field** — the transport-bar time readout is now a
  text input.  Click it and type `10` or `1:23` (or `1:02:30`), press
  Enter to jump.  Esc cancels.  Drag-scrubbing the timeline still works
  unchanged.

### Follow-ups round 2 (camera scheduling round)

* **Hold keyframes carry a duration** — `CameraKeyframe::holdDurationSec`
  defines how long a static pose "owns" the timeline.  `+ Hold @ cam now`
  now lands at `effectiveEndTime(draft)` (last keyframe time, plus its
  hold duration if it's a Hold tail), so successive inserts stack instead
  of piling at `t=0`.
* **HOLD (s) column** in the camera-path popup — editable for Hold rows,
  greyed out and read-only for Lerp rows.  Default is 2 s for a freshly
  inserted `+ Hold @ cam now`.
* **Capture every: dropdown** in the camera-path popup — picks the
  R-recording sample rate.  Options: 0.1 / 0.25 / 0.5 / 1 / 2 / 5 s.
  Default 1 s, so a live take produces a clean, manageable list (the
  old 20 Hz hardcoded value generated ~600 keyframes for a 30-second
  flight, which the user fairly called "way too specific").
* **Live camera preview while scrubbing** — the path now drives the
  camera whenever it has at least one keyframe, regardless of whether
  the transport is playing.  Drag the timeline (or type a time) and
  the viewport jumps to the path's pose at that time, mirroring how the
  block movement popup previews block positions.
* **Free Cam toolbar toggle** — when ON, the user keeps manual camera
  control even with a path loaded.  Path data is preserved and still
  used for the audio listener during export.
* **Block movement: Snap times dropdown** — the per-block keyframe
  editor (sidebar → Edit movement…) gets a `Snap times: [Off / 0.5 / 1
  / 2 / 5 s]` selector that rounds every row's time to the chosen grid.
  Default Off so existing scenes look unchanged; pick `1 s` for a
  "show every second" view that's much easier to skim and edit.
* **`.sime` v11** — `CPTH` keyframes now persist `holdDurationSec`.
  v10 files load with all hold durations defaulting to 0 (= hold until
  the next keyframe, the pre-v11 behaviour).

### Added after the 2026-06-01 session (spatial perception overhaul)

* **Camera-relative spatial mix** — pan, distance gain, and front/back
  attenuation are computed from the **listener** (camera position +
  forward/right), not the world origin.  Movement events refresh the same
  `applySpatialPosition` path.  **1 grid unit = 1 metre.**
* **Default sound on placement** — new blocks call
  `SoundLibrary::defaultSoundForBlockType` when the library is loaded.
* **Toolbar Anchor** — freezes listener pose; camera restores when toggled off.
* **Toolbar Spatial** slider — scales distance falloff (`refDist ≈ 3 m / sensitivity`).
* **Sidebar SPATIAL** — live listener distance + approximate dB for the
  selected block; **Distance…** picks block B in the viewport and shows
  separation (Δx, Δy, Δz, metres) plus level at B from the listener.

### Added after the 2026-05-27 session

* **Position-keyframe editor (alternative to Alt-drag recording)** —
  new floating `KeyframeEditorPopup`, opened from the new **"Keyframes…"**
  button at the top of the sidebar's MOVEMENT section.  Each row is a
  manually-typed `(timeSec, x, y, z)` tuple; the popup writes directly
  into the same `BlockEntry::recordedMovement` storage the engine
  already plays back, so:
  * Authoring a path by hand is identical to recording one — same
    sequencer code path, same movement graph in the sidebar, same
    transport scrub behaviour (`SequencerEngine::snapBlockPositionsToTime`
    already understands integer keyframes).
  * Loading the popup on a block that already has a recorded path lets
    the user **clean up** wobbly Alt-drag captures — the exact use case
    flagged by users as "annoying".
  * Apply sorts by time, shifts so the first keyframe sits at `t = 0`,
    and snaps `block.pos` (with a `voxelGrid.move`) to the first
    keyframe so playback at `t = 0` doesn't look like a jump.  Collisions
    against another block are detected and the move is rejected
    silently so `voxelGrid` stays in sync.
  * `hasRecordedMovement` is set iff there are at least 2 keyframes
    (matching the engine's "needs a path" check).  A single keyframe or
    an empty list clears the path.
  * Plumbing follows the same pending-op pattern as every other
    cross-thread edit in the codebase: a new `PendingKeyframeEdit`
    queue drained inside `renderOpenGL()`, exposed to the message
    thread as `ViewPortComponent::applyMovementKeyframes()`.

### Added after the 2026-05-24 session

* **Rubber-band multi-select (edit mode)** — LMB drag draws a cyan
  rectangle; on release every block whose voxel centre projects inside
  the box joins `multiSelection_` (Shift+drag adds instead of replacing).
  Shift+click toggles a single block.  Works with `Ctrl+C` / `Ctrl+V` /
  `Ctrl+A` / `Esc`.  Sidebar **Apply** pushes mute, hide, loop, and
  mute-schedule to every selected block when two or more are highlighted.

* **Standard editor shortcuts: `Ctrl + C` / `Ctrl + V` / `Ctrl + A`
  (+ `Esc`)** — implemented as a small clipboard + multi-selection set
  on `ViewPortComponent`:
  * `requestCopySelection()` deep-copies the primary selection plus
    everything in the multi-selection set into an in-memory
    `clipboardBlocks_` vector (movement keyframes, mute windows, loop
    state, custom file path — everything).
  * `requestPasteSelection()` re-emits the clipboard with fresh
    serials, translating each entry by `(+1, 0, 0)` and sliding further
    along +X until it finds a free, in-bounds cell.  Pasted blocks
    become the new multi-selection so the next `Ctrl + V` chains
    cleanly and the first pasted block is promoted to the sidebar Info
    panel.
  * `requestSelectAll()` populates `multiSelection_` with every serial
    in the scene; the renderer paints them in cyan
    (`Vec3f{ 0.25f, 0.75f, 1.f }`) so the user can visually confirm
    what got picked up — alongside the existing orange "primary" pass
    in edit mode.
  * `Esc` calls `requestClearMultiSelection()` and drops the cyan
    overlay without disturbing the primary selection / sidebar.
  * All four entry points only mutate a tiny `pendingClipboardOp_`
    under a critical section.  The actual `blockList` / `voxelGrid`
    mutations happen on the GL thread inside `renderOpenGL()`, matching
    the existing pattern used by `pendingOps`, `pendingSidebarEdit_`,
    `pendingTimingUpdate_`, etc.

### Thread Safety (still open)

| ID | Severity | Summary |
|----|----------|---------|
| T2 | Medium | `getTransportDuration()` iterates `blockList` from the 30 Hz timer on the message thread |
| T3 | Low–Med | Transport play/pause/stop called cross-thread on a non-thread-safe `TransportClock` |
| T4 | Low | `hasHit` / `currentHit` read in `keyPressed()` without a lock |

### Audio (still open)

| ID | Severity | Summary |
|----|----------|---------|
| A1 | Low | Synth samples hardcoded at 44100 Hz; devices running at 48000 Hz will be ~9% sharp |
| A2 | Low | `activeVoices_.push_back()` can allocate on the audio thread if >32 simultaneous voices |
| A3 | Low | Brief voice overlap (one audio block) on rapid transport stop/start |

### Sequencer (still open)

| ID | Severity | Summary |
|----|----------|---------|
| S1 | Medium | Newly placed blocks default to `startTimeSec = 0.0` — everything fires at once until manually staggered |
| S2 | Low | Pitch only goes up (Y ≥ 0); no way to pitch below normal |

### UI (still open)

| ID | Severity | Summary |
|----|----------|---------|
| U1 | Low | Two debug alert dialogs may still appear on first run if the sound library isn't found |
| U2 | Low | Doppler default is off and the model needs more tuning (toggle in the toolbar to experiment) |

### Recommended Fix Order

1. Auto-stagger block start times (S1) — or at minimum, increment based on existing block end times
2. Replace `activeVoices_.push_back` with a fixed-capacity pool (A2)
3. Regenerate synth tones at the device's actual sample rate (A1)
4. Thread-safe transport (T3) — wrap `TransportClock` mutators in atomics or fold them into the existing pending-edit queue
5. Tune Doppler defaults (U2) — possibly expose speed-of-sound + global gain in the toolbar

### Pending feature queue (not bugs)

* **Click-and-drag multi-select** for bulk mute / hide
* **User-adjustable per-block frequency** (the analyzer is currently read-only)
* **Multiple sounds at different times** on the same block
* **Position keyframes** as an alternative to recorded movement
* **Block-type change mid-movement** (e.g. violin → piano halfway)
* **MP3 / AAC export** — extend `SceneAudioExporter::Format` (see the
  "How to extend" section of [`SESSION_2026-05-23_REPORT.md`](md%20files/SESSION_2026-05-23_REPORT.md))



* ~~**Fix the control+c and control+v option to add copy paste, work on control+a as well**~~ — done 2026-05-24 (see *Fixed after the 2026-05-24 session* below)
- Some adjustments are needed but they do work
* ~~**UI change for Loop length button in block info page** (`= Block` protruding past the sidebar edge)~~ — done 2026-05-24
* ~~**Click-and-drag multi-select** for bulk mute / hide / copying~~ — done 2026-05-24 (edit-mode rubber band + Shift modifiers; bulk Apply for mute/hide/loop; `Ctrl+C` on the set). *Assigning the same sound to all selected blocks of one type* still goes through the block edit popup per block for now.
* **Im thinking also say when a block is invisible, like Violin 1 is visible till 5 seconds and then goes invisible, then maybe the user is able to place a block on the same position as the invisible block, however no two blocks can be visible within the same position, they can only be in the same position if one is visible and the other is not**
* **Multiple sounds at different times** on the same block, so say violin 1 is placed, plays A note, then after 5 seconds or however long the user wants, then it plays a different note, and so on
* ~~**Position keyframes** as an alternative to recorded movement~~ — done 2026-05-27 (see *Added after the 2026-05-27 session* below)