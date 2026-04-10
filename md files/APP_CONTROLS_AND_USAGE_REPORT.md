# SIME Controls + Run Guide (Windows/Cursor)

This document summarizes how to run and use the app in its current state.

## Run the app

From `C:\sime` in Cursor terminal:

```powershell
# one-time if JUCE folder is missing
git clone https://github.com/juce-framework/JUCE.git JUCE

# configure
cmake -B build -G "Visual Studio 17 2022"

# build
cmake --build build --config Debug --parallel

# run
.\build\SIME_artefacts\Debug\SIME.exe
```

Release executable:

```text
build\SIME_artefacts\Release\SIME.exe
```

If `cmake` is not found, add `C:\Program Files\CMake\bin` to PATH and reopen Cursor.

---

## Controls (current implementation)

## Camera and movement

- Click inside the viewport first to give it keyboard focus.
- `RMB + drag`: rotate camera (look around).
- `W / S`: move forward/back.
- `A / D`: strafe left/right.
- `Space`: move up.
- `Ctrl`: move down.
- `Mouse wheel`: dolly/zoom camera.
- `R`: reset camera position to `(8, 8, 8)`.

## Voxel interaction

- `LMB`: place voxel at preview position.
- `RMB click` (no drag): remove hovered voxel.
- `Delete` or `Backspace`: remove hovered voxel.
- `C`: clear all voxels.

## Shift-plane placement

- `Shift + LMB`: place voxel on shift plane (air placement).
- `Shift + Mouse wheel`: move shift-plane Y up/down.

## Edit mode + sequencer UI

- `E`: toggle Edit Mode.
- In Edit Mode, `RMB` on a block opens edit popup.
- Popup fields: `Start (s)`, `Duration (s)`, `Sound ID`.
- Popup actions: `Apply`, `Cancel`, `Esc` (cancel/close).
- Bottom transport bar buttons: `Play/Pause`, `Stop`.

## Sidebar

- Left-side toggle button collapses/expands sidebar (`☰` / `X`).
- Sidebar shows block list (serial and coordinates).

---

## How to use it (quick workflow)

1. Launch app.
2. Click viewport.
3. Move camera with `RMB drag` + `WASD`.
4. Place blocks with `LMB`.
5. Press `E` to enter Edit Mode.
6. Right-click a block to edit `Start`, `Duration`, `Sound ID`.
7. Use transport bar `Play` / `Pause` / `Stop`.
8. Watch timeline and block highlights.

---

## Current limitations and caveats

- Audio sample load path in code is a placeholder (`/path/to/your.wav`), so audio may be silent unless replaced with a real file path.
- New blocks default to `soundId = -1` (silent) until edited.
- For sound to play, both conditions must be true:
  - a sample is loaded for a given sound ID, and
  - block `Sound ID` matches that loaded ID.
- In Edit Mode, right-click is used for popup/block selection (not remove).
- Two startup message dialogs currently appear on launch.

---

## Quick test checklist

- Place 3 blocks.
- Edit them with `Start` times `0.0`, `1.0`, `2.0`.
- Set `Duration` around `0.5`.
- Set `Sound ID` to a loaded ID (for example `0` if sample 0 is loaded).
- Press Play and verify transport/progress/highlighting behavior.

