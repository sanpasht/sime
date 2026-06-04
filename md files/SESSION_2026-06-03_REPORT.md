# SIME — Session Report, 2026-06-03

> The "ambitious feature" groundwork session.  Covers the **selected-block
> audition** toolbar (Play / @Time), **per-block movement freeze**, the
> **duration / distortion (Fit sound ↔ movement)** model, **path-interval
> resampling** (no more teleport on snap), the entire **Sound Schedule** system
> (multiple timed sounds per block, each with its own loop + gap), **multi-segment
> movement recording**, **per-segment movement looping**, **per-sound movement
> binding**, a **resizable / overlap-fixed sidebar**, and a `.sime` file format
> bumped from **v11 → v12 → v13 → v14**.

This document is the canonical reference for the architecture changes made in
this session.  Read it before opening a PR that touches movement, the sequencer,
scheduled sounds, or the sidebar.  The next planned work (**E**) is the **tab
bar + full-screen Timeline tab** and the **Synthesizer tab + render-to-WAV +
Synth block type** — both build directly on the data model described here.

---

## Contents

1. [Top-level summary](#1-top-level-summary)
2. [File-by-file changes](#2-file-by-file-changes)
3. [Data model changes](#3-data-model-changes)
4. [Persistence: v11 → v12 → v13 → v14](#4-persistence-v11--v12--v13--v14)
5. [Threading touchpoints](#5-threading-touchpoints)
6. [Feature deep-dives](#6-feature-deep-dives)
   - 6.1 [Selected-block audition (Play / @Time)](#61-selected-block-audition-play--time)
   - 6.2 [Per-block movement freeze](#62-per-block-movement-freeze)
   - 6.3 [Duration / distortion model (Fit sound ↔ movement)](#63-duration--distortion-model-fit-sound--movement)
   - 6.4 [Path-interval resampling (no teleport on snap)](#64-path-interval-resampling-no-teleport-on-snap)
   - 6.5 [Sound Schedule (multiple timed sounds per block)](#65-sound-schedule-multiple-timed-sounds-per-block)
   - 6.6 [Time-aware Play + scheduled sounds extend the timeline](#66-time-aware-play--scheduled-sounds-extend-the-timeline)
   - 6.7 [Per-scheduled-sound loop + gap](#67-per-scheduled-sound-loop--gap)
   - 6.8 [Multi-segment movement recording](#68-multi-segment-movement-recording)
   - 6.9 [Per-segment movement looping](#69-per-segment-movement-looping)
   - 6.10 [Per-sound movement binding](#610-per-sound-movement-binding)
   - 6.11 [Movement record no longer cuts the sound](#611-movement-record-no-longer-cuts-the-sound)
   - 6.12 [Resizable + overlap-fixed sidebar](#612-resizable--overlap-fixed-sidebar)
7. [Bug log](#7-bug-log)
8. [What's still queued](#8-whats-still-queued)
9. [How to extend](#9-how-to-extend)

---

## 1. Top-level summary

| Category | What changed |
|----------|--------------|
| **New systems** | Selected-block audition (toolbar Play / @Time); per-block movement freeze; `DurationSyncAction` "Fit sound / movement" dropdown; Sound Schedule (`SoundEvent` + `SoundSchedulePopup`) with per-row loop + gap; multi-segment movement recording; per-segment movement looping; per-sound movement binding; draggable sidebar resize handle. |
| **Refactored systems** | "Snap times" in the keyframe editor now **resamples** the path (linear interpolation between keyframes) instead of rounding times — eliminates teleporting; `snapBlockPositionsToTime()` split into a reusable per-block `snapBlockToTime()`; transport length now accounts for scheduled sounds + movement keyframes. |
| **Behaviour fixes** | `@Time` only seeks (no autoplay); freeze/unfreeze repositions live voices instead of killing audio; movement cancel/redo restores the block's original position; recording a movement no longer auto-cuts the sound region. |
| **Persistence** | `.sime` schema bumped **v11 → v12** (`soundSchedule`) **→ v13** (`movementLoop`) **→ v14** (per-`SoundEvent` `loop` + `loopGapSec`).  Older files load with sensible defaults; scheduled-sound `soundId` is re-resolved from `relativePath` on load. |
| **UI** | Toolbar gains `Play` + `@Time` audition buttons (grouped with the block-type selector); sidebar gains "Freeze this block", "Loop movement", "Fit sound / movement…" dropdown, and "Sound Schedule…"; sidebar `paint()`/`resized()` re-synced; sidebar is drag-resizable (200–520 px). |

---

## 2. File-by-file changes

| File | Touched in this session for… |
|------|------------------------------|
| `Source/SoundSchedulePopup.h` (new) | Two-pane Sound Schedule editor API (list + sound picker); `EntryRow` with start / duration / loop / gap / pick / delete |
| `Source/SoundSchedulePopup.cpp` (new) | Row build / layout / pull, sound assignment, loop+gap toggles, Apply emits cleaned `std::vector<SoundEvent>` |
| `Source/BlockEntry.h` | `SoundEvent` struct (+ `loop`, `loopGapSec`); `soundSchedule`; `DurationSyncAction` enum; `movementFrozen` / `wasMovementFrozen`; `movementLoop` / `movementLoopIndex`; `recordingTimeOffset` / `recordedMovementBackup`; `resetPlaybackState()` resets the new fields |
| `Source/SequencerEngine.h` / `.cpp` | `snapBlockToTime()` (single-block); scheduled-sound firing on synthetic serials; per-segment movement looping; per-sound movement mirroring; scheduled loop buffer flags |
| `Source/ViewPortComponent.h` / `.cpp` | Audition queue (`auditionRequested_` / `auditionSerial_`); per-block freeze + movement-loop toggle queues; `applyDurationSync()`; sound-schedule replacement queue; multi-segment record splice; movement confirm "extend, don't cut"; transport length includes scheduled sounds + movement |
| `Source/MainComponent.h` / `.cpp` | `SidebarResizer` child + `sidebarWidth_`; audition button wiring; `onSetMovementLoop`; `onDurationSyncAction` (with distortion warning); `onEditSoundSchedule` popup management; `playSelectedBlockInTime()` seek-only |
| `Source/SidebarComponent.h` / `.cpp` | "Freeze this block" + "Loop movement" toggles; "Fit sound / movement…" combo (`onDurationSyncAction`); "Sound Schedule…" button (`onEditSoundSchedule`); `paint()`/`resized()` row re-sync |
| `Source/KeyframeEditorPopup.h` / `.cpp` | `originalFrames_` pristine path; `applySnapToDraft()` resamples via interpolation instead of rounding |
| `Source/SceneFile.cpp` | `kVersion` 12 → 13 → 14; save/load of `soundSchedule` (+ per-sound loop/gap), `movementLoop` |
| `Source/SceneAudioExporter.cpp` | `resetAllBlocks()` before bounce so scheduled sounds fire from t=0 |
| `Source/HelpPopup.cpp` | Documented audition, freeze, loop, Fit dropdown, Sound Schedule (loop/gap), multi-segment recording, resizable sidebar |
| `CMakeLists.txt` | Added `SoundSchedulePopup.cpp` to the build |

---

## 3. Data model changes

All new fields live on `BlockEntry` / its nested structs in `Source/BlockEntry.h`.

### `SoundEvent` (new struct)

A single timed sound a block fires **in addition to** its main region.

| Field | Persisted | Meaning |
|-------|-----------|---------|
| `startSec` | yes | when the sound fires (transport seconds) |
| `durationSec` | yes | play window; sample is cut at the end if longer |
| `soundId` | no (runtime) | resolved from `relativePath` on load |
| `relativePath` | yes | library-relative path used to persist + re-resolve |
| `loop` | yes (v14) | repeat the sound across `[startSec, endSec)` |
| `loopGapSec` | yes (v14) | silence between loop repeats (0 = tight) |
| `started` / `finished` | no (runtime) | sequencer one-shot guards |

`endSec()` returns `startSec + max(0.05, durationSec)`.

### `DurationSyncAction` (new enum)

Drives the sidebar "Fit sound / movement…" dropdown:

| Value | Effect |
|-------|--------|
| `MatchDurationToSound` | region = sound's natural length |
| `DistortSoundToMovement` | speed/slow the **audio** to fit the movement length (warns — pitch shifts) |
| `DistortMovementToSound` | stretch/compress the **movement** to the sound length (audio untouched) |
| `HardCutAtMovement` | region = movement length; sound plays natural and is cut at the end |

### New `BlockEntry` fields

| Field | Persisted | Meaning |
|-------|-----------|---------|
| `soundSchedule` (`std::vector<SoundEvent>`) | yes | the block's scheduled sounds |
| `movementFrozen` / `wasMovementFrozen` | no | per-block freeze + GL-thread transition tracker |
| `movementLoop` | yes (v13) | loop the recorded movement (per segment) |
| `movementLoopIndex` | no | lap/segment key to re-arm keyframe triggers; reset to **-1** |
| `recordingTimeOffset` | no | block-relative offset of an in-progress later segment |
| `recordedMovementBackup` | no | prior path snapshot so Cancel can restore a segment |

`resetPlaybackState()` now also clears every `SoundEvent`'s `started`/`finished`
and resets `movementLoopIndex` to `-1`.

---

## 4. Persistence: v11 → v12 → v13 → v14

`SceneFile::kVersion` is now **14**.  Each bump is additive and backward-readable.

- **v12** — per-block scheduled sounds.  Save writes `count`, then for each
  entry `startSec`, `durationSec`, and a length-prefixed `relativePath`.  Load
  reads them back; `soundId` stays `-1` and is re-resolved in
  `ViewPortComponent::loadScene()` via `SoundLibrary::findByRelativePath` +
  `ensureLoaded` (or by hashing for custom WAVs).
- **v13** — per-block `movementLoop` flag (one `uint8`).
- **v14** — per-`SoundEvent` `loop` (`uint8`) + `loopGapSec` (`double`), written
  inside the schedule loop after `relativePath`.

Load guards every new block behind `if (version >= N)` so v11 and older files
still parse with defaults (no schedule, no loop).

---

## 5. Threading touchpoints

The audio/GL render thread owns `blockList`; the message thread owns the UI.
All cross-thread mutations are queued and drained at the top of
`ViewPortComponent::renderOpenGL()`:

| Queue / atomic | Producer (message thread) | Drained on (GL thread) |
|----------------|---------------------------|------------------------|
| `auditionRequested_` / `auditionSerial_` (atomics) | toolbar Play | fires a Start event for the time-active sound |
| `pendingFreezeToggles_` (`freezeToggleMutex_`) | sidebar "Freeze this block" | sets `b.movementFrozen` |
| `pendingMovementLoopToggles_` (same mutex) | sidebar "Loop movement" | sets `b.movementLoop`, resets `movementLoopIndex` |
| `pendingSoundSchedules_` (`soundScheduleMutex_`) | Sound Schedule Apply | replaces `b.soundSchedule`, fresh runtime state |
| `pendingSidebarEdits_` (`sidebarEditMutex_`) | sidebar Apply / `applyDurationSync()` | updates duration / mode / movement duration |
| `pendingMovementOp_` (`movementOpMutex_`) | movement confirm popup | confirm/cancel splice + position restore |

Scheduled sounds play on **synthetic serials** (`scheduledSerial(blockSerial,
entryIndex)` = `kScheduledSerialBase + serial * kMaxScheduledPerBlock + index`)
so the block's region Stop never cuts them and each note is an independent voice.

---

## 6. Feature deep-dives

### 6.1 Selected-block audition (Play / @Time)

Two toolbar buttons, enabled only when a block is selected (Edit Mode):

- **Play** — previews the block's sound immediately (see 6.6 for time-awareness).
  `MainComponent` reads `sidebar.getSelectedBlockCopy()` and calls
  `view.auditionBlock(serial)`, which sets `auditionRequested_`/`auditionSerial_`;
  the GL thread fires a one-shot Start event spatialised at the block's pos.
- **@Time** — seeks the playhead to the block's `startTimeSec` so blocks + camera
  snap to that moment.  It **does not** start playback (a deliberate fix — see
  the bug log).  `playSelectedBlockInTime()` only calls `seekTransportClock()`
  and refreshes the transport UI.

### 6.2 Per-block movement freeze

The global "Freeze Move" toolbar toggle was generalised to a per-block
`movementFrozen` flag, exposed as the sidebar "Freeze this block (hold
position)" toggle.  `ViewPortComponent` tracks `wasMovementFrozen` to detect the
**unfreeze** transition; on unfreeze it calls `SequencerEngine::snapBlockToTime`
and emits a `Movement` event for **only** that block so its live voice is
repositioned rather than killed (fixing the "unfreeze silences audio" bug).
Freezing affects movement only — never the audio gate.

### 6.3 Duration / distortion model (Fit sound ↔ movement)

The single "Match Duration to Sound" button is supplemented by a "Fit sound /
movement…" dropdown wired to `DurationSyncAction`.  `applyDurationSync(serial,
action)` computes the new region duration, movement duration, and playback mode,
then pushes a sidebar edit.  Choosing `DistortSoundToMovement` pops a
`NativeMessageBox` warning that audio pitch will be affected.

### 6.4 Path-interval resampling (no teleport on snap)

Previously "Snap times" rounded each keyframe's time to the grid, which made the
block jump straight to a far position when intervals were compressed.
`KeyframeEditorPopup` now keeps an `originalFrames_` copy and
`applySnapToDraft()` **resamples** the path: it walks the grid at the chosen
interval and linearly interpolates `Vec3i` positions between the original
keyframes (rounding each coordinate).  "Off" restores the exact recorded times.
The motion trajectory is preserved; only the temporal sampling changes.

### 6.5 Sound Schedule (multiple timed sounds per block)

`SoundSchedulePopup` is a two-pane editor (left: list of `SoundEvent` rows;
right: a `SoundPickerComponent` filtered to the block's instrument type).  Each
row edits start, duration, loop, gap and a sound.  Apply emits a cleaned
`std::vector<SoundEvent>` (rows without a sound or with non-positive duration are
dropped), queued to the GL thread.  In `SequencerEngine::update()`, each
`SoundEvent` fires a Start at `startSec` on its synthetic serial and a Stop at
`endSec`, independent of the block's main region.

### 6.6 Time-aware Play + scheduled sounds extend the timeline

Two coupled fixes so scheduled sounds actually play:

- **Transport length** (`ViewPortComponent::getTransportDuration()`) now takes
  the max over the main region, copied regions, **scheduled sounds**
  (`se.endSec()`), and the **movement path end**.  A note at 15s no longer
  requires inflating the main duration to be reachable.
- **Play** is time-aware: it previews whichever sound the block would be making
  at the current playhead — the scheduled sound with the latest `startSec ≤ now`,
  else the block's main sound.  So with notes at 5s and 40s, Play before 5s
  previews the main sound, 5–40s previews note A, after 40s previews note B.

### 6.7 Per-scheduled-sound loop + gap

Each Sound Schedule row has a **Loop** toggle and **GAP (s)** field
(`SoundEvent::loop` / `loopGapSec`).  When looping, the scheduled Start event
sets `loopBuffer = true` and `loopBufferSec = loopGapSec`, reusing the
audio-thread sample-wrap loop the main block uses — the sound repeats seamlessly
across its window with the chosen gap between repeats, instead of firing once.

### 6.8 Multi-segment movement recording

Recording is anchored to the **playhead**, not the block start.  If you move the
playhead past the block's start (the block snaps to its position there) and
Alt+drag again, the new keyframes are spliced into the existing path at
`recordingTimeOffset` (block-relative) instead of replacing it; the prior path
is preserved in `recordedMovementBackup`.  On stop, the new segment's times are
offset and merged (sorted) into the path, and the proposed region duration is
extended to cover the whole path.  Cancel restores only the prior segments.
The block **holds** between segments and resumes from its current position — no
teleport is introduced by recording.

### 6.9 Per-segment movement looping

The sidebar "Loop movement (teleport to start)" toggle loops **each recorded
segment within its own window**.  A segment is a contiguous run of keyframes;
boundaries are detected by a gap `> 0.35 s` between consecutive keyframe times
(the hold left by recording a later segment).  In `processOccurrence()`, the
sequencer locates the segment containing the current playback time, then wraps
the local time within that segment's span — teleporting back to the segment's
first keyframe each lap and re-arming its triggers — until the next segment's
time arrives, at which point that segment plays normally.  This is the **only**
sanctioned teleport.  Single-segment paths behave as a simple loop.

### 6.10 Per-sound movement binding

Because segments are time-anchored, aligning a movement segment with a scheduled
sound's `startSec` gives that sound its own motion.  To keep the audio
spatialised correctly, `SequencerEngine::update()` records where each block's
events begin, detects the block's `Movement` events for the tick, and **mirrors**
them onto every active scheduled voice (same positions/velocity, retargeted to
the scheduled synthetic serial + `soundId`).  A note scheduled during a moving
segment now pans/attenuates as the block moves.

### 6.11 Movement record no longer cuts the sound

Confirming a recorded movement used to force `durationSec` down to the movement
length and lock it, cutting the audio.  Now the confirm handler pins
`movementDurationSec` to the recorded path length (so movement stays
independent) and only **extends** the region (`durationSec = max(durationSec,
pathEnd)`), never shrinking and never locking it.  The user remains free to set
the sound duration manually, and assigning a new sound still auto-fits to
`max(sample, movement)`.

### 6.12 Resizable + overlap-fixed sidebar

- **Overlap fix** — the sidebar's `paint()` drew section labels/graphs at
  hard-coded y-offsets that had drifted out of sync with `resized()` after new
  rows were inserted.  Both now advance identical row budgets for the Freeze,
  Loop-movement, Fit-dropdown, and Sound-Schedule rows, so labels line up with
  their controls at any scroll position.
- **Resize handle** — a thin `SidebarResizer` child sits on the sidebar's right
  edge with a left/right resize cursor.  Dragging updates `sidebarWidth_`
  (clamped 200–520 px) and re-lays-out the app, IDE-style.  Hidden while the
  sidebar is collapsed.

---

## 7. Bug log

| Symptom | Root cause | Fix |
|---------|-----------|-----|
| Play / @Time did nothing | audition used the viewport atomic, not the sidebar's selected block | route through `sidebar.getSelectedBlockCopy()` → `auditionBlock(serial)` |
| Unfreeze silenced the block | freeze toggle called `killAllVoices()` | emit `Movement` events to reposition the live voice instead |
| Movement cancel/redo left block at drag-end | confirm/cancel didn't restore `pos` | restore to `recordingStartPos` (cancel) / path front (confirm) + fix voxel grid |
| @Time started playback | `playSelectedBlockInTime()` called `transportPlay()` | seek-only; press Play yourself |
| Path snap teleported the block | snap rounded keyframe times | resample with interpolation from `originalFrames_` |
| Scheduled sound at 15s never played | transport length ignored `soundSchedule` | include `se.endSec()` (and path end) in `getTransportDuration()` |
| 2nd scheduled sound didn't loop | loop flag was block-only | per-`SoundEvent` `loop` + `loopGapSec` on the scheduled Start event |
| Segment 1 + 2 replayed together when looping | loop wrapped the whole path | loop per segment within its own window |
| Recording a movement cut the sound | confirm forced + locked `durationSec` | extend-only, pin `movementDurationSec`, no lock |
| Sidebar labels overlapped controls | `paint()` vs `resized()` y-math drift | re-synced both for the new rows |

---

## 8. What's still queued

- **Future synth polish** — dual oscillators, LFO, effects rack, live keyboard in the Synth tab.
- **Docs** — keep this report + README current as features land.

---

## 10. E2 — Synthesizer tab (implemented)

### Architecture

The synth is **offline-first**: `SynthRenderer` renders a mono buffer from a
`SynthPatch` struct (oscillator + ADSR amp envelope + resonant low-pass filter).
The buffer is either previewed through `AudioEngine::setSampleBuffer()` or
written to `workspaceAudios/` via `SynthRenderer::writeWav()`.

Live playback in the 3D scene still uses the existing sample-voice path — no
oscillators run on the audio thread.

### New files

| File | Role |
|------|------|
| `SynthPatch.h` | Parameter bundle (waveform, MIDI note, ADSR, filter, gain, duration) |
| `SynthRenderer.cpp/h` | Offline render + 16-bit PCM WAV export |
| `SynthComponent.cpp/h` | Full-screen UI on the Synth workspace tab |

### Block type

`BlockType::Synth` appended before `_Count` (scene v14 compatible — stored as
uint8). Category: **Synth**. Default `soundId = -1`. Edit popup uses the same
file-browse flow as **Custom** (`customFilePath` → `workspaceAudios/...`).

### Integration points

- `MainComponent` hosts `SynthComponent`, wires `onPreview` / `onExport` to
  `ViewPortComponent::previewSynthBuffer()` and `exportSynthBufferToWorkspace()`.
- `AudioEngine::setSampleBuffer()` registers in-memory buffers for preview.
- Sidebar **Audio** list refreshes after export.

### E1 — Timeline tab (implemented)

- Shared transport clock via `wireTimelineCallbacks()` on both transport bars.
- OpenGL viewport parked at 1×1 off-screen on non-Scene tabs so JUCE keeps the
  context attached (0×0 detaches and freezes transport).

---

## 9. How to extend

- **A new scheduled-sound property** → add the field to `SoundEvent`, bump
  `SceneFile::kVersion`, write/read it inside the schedule loop (guard load with
  `version >= N`), surface it in `SoundSchedulePopup::rebuildRows()` /
  `layoutRows()` / `pullValuesFromRows()` / the Apply cleaner, and consume it in
  `SequencerEngine::update()`'s scheduled-sound block.
- **A new per-block movement behaviour** → add a flag to `BlockEntry`, a sidebar
  toggle + `onXxx` callback, a queue drained in `renderOpenGL()`, and the logic
  in `processOccurrence()` (and `snapBlockToTime()` if it affects scrubbing).
- **Anything that changes scene length** → update `getTransportDuration()` so the
  transport + timeline cover it, or it won't play to the end.
