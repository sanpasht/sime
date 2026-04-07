# SIME Audio Architecture — Quick Reference

## Overview

The app now has a complete **music sequencer** pipeline: blocks in the viewport trigger audio playback in sync with a transport clock. The UI lets users edit block timing and listen to their compositions.

---

## Core Audio Components

### `TransportClock`
**What it does:** Keeps track of playback time.

**Key methods:**
- `start()` — begins playback
- `pause()` — keeps time but stops advancing
- `stop()` — resets to 0
- `seek(seconds)` — jumps to a specific time
- `setLoop(duration)` — enables looping at specified duration
- `update(dt)` — advances time by delta (called every frame)

**Example:** In `MainComponent`, the 30Hz timer calls `transportClock.update()` to advance playback.

---

### `SequencerEvent`
**What it does:** A message that tells the audio engine when to start or stop a sample.

**Fields:**
- `Start` / `Stop` — event type
- Payload: `soundId`, `voiceChannel` (which audio voice to use)

**How it's used:** `SequencerEngine` emits these events; `AudioEngine` receives them via lock-free FIFO.

---

### `SequencerEngine`
**What it does:** Scans all blocks and emits events when blocks should start/stop playing.

**Key methods:**
- `update(currentTime)` — checks if any blocks should start/stop at this time
- Emits `SequencerEvent` for each block that crosses a start/stop boundary

**Flow:**
1. `ViewPortComponent` owns a `SequencerEngine` instance
2. Each frame, `MainComponent` calls `transportClock.update(dt)`
3. Then calls `sequencerEngine.update(transportClock.getTime())`
4. Engine scans `blockList` and emits events to `AudioEngine`

---

### `AudioEngine`
**What it does:** Plays audio samples, manages voices (polyphony), and queues playback events.

**Key components:**
- **Sample library** — maps `soundId` → audio file path
- **Active voices** — up to N simultaneous playing samples
- **Lock-free FIFO** — thread-safe event queue from `SequencerEngine`

**Key methods:**
- `loadSample(soundId, filePath)` — register an audio file
- `start(deviceIndex)` — connect to audio device and begin callback
- `stop()` — disconnect from device
- `queueEvent(SequencerEvent)` — push event from sequencer (thread-safe)

**Note:** Currently not wired — see "Still Not Done" below.

---

### `ActiveVoice`
**What it does:** Represents one playing sample instance.

**Fields:**
- `soundId` — which sample is playing
- `playbackPosition` — how far into the sample (in frames)
- `isActive` — is this voice currently playing

---

### `BlockEntry`
**What it does:** Shared struct between UI and audio — represents one block in the viewport.

**Fields:**
- `serial` — unique block ID
- `pos` — position in voxel grid
- `soundId` — which sample to play
- `startSec` — when in the transport timeline this block starts
- `durationSec` — how long the sample plays
- `playingState` flags — whether block is currently playing (set by `SequencerEngine`)

---

## UI Components

### `BlockEditPopup`
**What it does:** Native OS popup window that lets users edit a block's timing and sound.

**How to use:**
1. User right-clicks a block (or presses Edit in viewport)
2. Popup appears with text fields:
   - `Start Time (sec)` — when block starts
   - `Duration (sec)` — how long it plays
   - `Sound ID` — which sample to use
3. User types and clicks OK → block updates, popup closes

**Note:** Rendered on top of OpenGL view using `addToDesktop()`.

---

### `TransportBarComponent`
**What it does:** Bottom UI bar with playback controls and progress display.

**Elements:**
- **Play/Pause button** — toggles `isTransportPlaying`
- **Stop button** — resets to time 0
- **Time display** — shows `currentTime / totalDuration`
- **Progress bar** — visual timeline with playhead tick and gradient fill

**How it's wired:** Callbacks in `MainComponent` connect button clicks to `ViewPortComponent` transport methods.

---

## Modified Existing Components

### `ViewPortComponent`
**Added:**
- `TransportClock` — owns playback clock
- `SequencerEngine` — owns sequencer, scans blocks
- `AudioEngine` — owns audio system
- Edit mode (toggle with E key)
- Block selection & highlight (visual feedback)
- `onRequestBlockEdit` callback — fires when user opens edit popup

**New public methods:**
- `transportPlay()` / `transportPause()` / `transportStop()`
- `isTransportPlaying()`
- `getTransportTime()`
- `getTransportDuration()`

---

### `MainComponent`
**Added:**
- 30Hz juce::Timer that calls `update()`
- Wired transport bar callbacks → `ViewPortComponent` transport methods
- Wired edit popup callbacks → block editing
- Auto-stop logic: stops playback when playhead passes the last block

---

### `CMakeLists.txt`
**Added dependencies:**
- `juce_audio_basics` — core audio
- `juce_audio_devices` — audio device interface
- `juce_audio_formats` — WAV/MP3 loading

---

### `Renderer.cpp`
**Fixes:**
- Draw order: grid first, then voxels (prevents Z-fighting)
- Grid density adjustment for visual clarity

---

## How It All Works Together

```
Timeline:
1. User clicks Play in transport bar
2. MainComponent's timer fires every ~33ms
3. Timer calls transportClock.update(dt)
4. Timer calls sequencerEngine.update(currentTime)
5. SequencerEngine scans blockList:
   - If block.startSec == currentTime: emit Start event
   - If block.startSec + block.durationSec == currentTime: emit Stop event
6. Events queue in AudioEngine's lock-free FIFO
7. Audio callback thread reads FIFO and plays/stops samples
8. TransportBarComponent updates to show new time
9. Blocks highlight in viewport as they play
10. When playhead > lastBlock.end: auto-stop playback
```

---

## Testing Guide

### Test 1: Edit a Block
1. Press E to enter edit mode
2. Click a block → `BlockEditPopup` appears
3. Change `Start Time` (e.g., 0.5), `Duration` (e.g., 1.0), `Sound ID` (e.g., "kick")
4. Click OK
5. **Verify:** Block properties update in viewport; positions match your edits

### Test 2: Playback Timeline
1. Click Play in transport bar
2. Watch time counter increment at top-left
3. **Verify:** Time increases smoothly; counter is in sync with progress bar

### Test 3: Block Detection
1. Place a block at time 2.0 with duration 1.0
2. Click Play
3. Watch viewport as playhead reaches 2.0s
4. **Verify:** Block highlights when playhead reaches its start time; unhighlights at start + duration

### Test 4: Auto-Stop
1. Place a block at time 5.0
2. Click Play
3. Let playback run
4. **Verify:** Playback auto-stops after playhead passes 5.0s (the last block's end)

### Test 5: Transport Controls
- **Play/Pause:** Click button; playhead should pause then resume
- **Stop:** Click button; playhead should jump to 0, time counter resets
- **Seek:** (If implemented) Drag progress bar; playhead should jump to that time

### Test 6: Edit Popup Positioning
1. Click a block in various viewport areas (top-left, bottom-right, etc.)
2. **Verify:** Popup window appears centered on that block, readable, not cut off

---

## What's Still Not Done

### Audio Playback
- `AudioEngine::start()` — audio device connection is not active yet
- `AudioEngine::loadSample()` — samples are not loading from disk
- **Result:** Blocks play visually (highlight, events emit), but **no sound**

### Next Steps
1. Initialize audio device in `AudioEngine::start()`
2. Load sample files into sample library
3. Use JUCE's `AudioTransportSource` to play loaded files
4. Copy samples to active voice buffers in audio callback

---

## File Locations

- **Core classes:** [`Source/`](Source/)
  - `TransportClock.h` — not yet created (define alongside AudioEngine)
  - `SequencerEngine.h/cpp`
  - `AudioEngine.h/cpp`
  - `BlockEntry.h`
  - `SequencerEvent.h` — not yet created (define alongside SequencerEngine)
  
- **UI components:** [`Source/`](Source/)
  - `BlockEditPopup.h/cpp`
  - `TransportBarComponent.h/cpp`
  - `ViewPortComponent.h/cpp`
  - `MainComponent.h/cpp`

- **Configuration:** [`CMakeLists.txt`](CMakeLists.txt)

---

## Key Takeaways

- **Separation of concerns:** Sequencer emits events, audio engine plays them — decoupled by lock-free FIFO
- **Thread-safe:** Events are queued safely between UI thread (timer) and audio thread (callback)
- **Extensible:** Add new samples by calling `audioEngine.loadSample()`; add new blocks to viewport; block updates automatically trigger events
- **Audio not wired yet:** Visual feedback works; sound is the next step

