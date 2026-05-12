# Export Audio — Implementation Report

## Overview

SIME can bounce the **full stereo mix** of the current scene to a disk file. The export covers the **entire timeline** from time zero through the latest block end time (the same span implied by the transport bar when duration is derived from blocks). Rendering is **offline** (as fast as the CPU can run the bounce loop), not real-time through the audio device.

This complements **Save / Load** (`.sime` project files): `.sime` stores editor state; **Export** writes a normal audio file you can play in any media player or DAW.

---

## User Flow

1. Choose **File → Export Audio…** from the top-right **File** menu (`MainComponent`).
2. In the dialog (`ExportAudioDialog`), pick a format (lossless vs lossy options are labeled in the combo).
3. Click **Choose file and export**, then pick a path in the system save dialog.
4. On success, an info alert shows the output path; on failure, a warning shows the error string.

Scene operations (**New / Open / Save / Save As**) live in the same **File** menu for a conventional DAW-style pattern.

---

## Supported Formats

| Format | Extension | Category | Notes |
|--------|-----------|----------|--------|
| WAV | `.wav` | Lossless | 16-bit PCM, stereo |
| AIFF | `.aif` | Lossless | 16-bit PCM, stereo |
| FLAC | `.flac` | Lossless | Via JUCE (`JUCE_USE_FLAC`, on by default) |
| Ogg Vorbis | `.ogg` | Lossy | Via JUCE (`JUCE_USE_OGGVORBIS`, on by default); quality index 4 in `writerOptionsFor()` |

### Not included in this implementation

- **MP3** — JUCE’s `MP3AudioFormat::createWriterFor` is a stub (`jassertfalse; not yet implemented`). Decoding MP3 is optional; encoding would require a third-party encoder (e.g. LAME) and separate licensing/build wiring.
- **AAC / M4A (“MP4 audio”)** — In JUCE, `CoreAudioFormat` is registered on Apple platforms only under `registerBasicFormats()`. A cross-platform AAC export path would need OS APIs or external libraries on Windows.

For a portable **lossy** export today, **Ogg Vorbis** is the practical choice.

---

## Technical Design

### Modules

| File | Role |
|------|------|
| `SceneAudioExporter.h` / `SceneAudioExporter.cpp` | Computes duration, resets block playback state, runs offline clock + `SequencerEngine`, applies `Movement` events to a block copy (mirroring `ViewPortComponent::renderOpenGL`), dispatches Start/Stop/Movement into a local voice list with **the same math** as `AudioEngine` (`handleStartEvent`, `handleStopEvent`, movement pan update, per-sample mix loop), accumulates interleaved stereo floats, then writes via `juce::AudioFormatWriter`. |
| `ExportAudioDialog.h` / `ExportAudioDialog.cpp` | Small modal content: format `ComboBox` + confirm / cancel. |
| `ViewPortComponent` | `exportSceneAudioToFile()` copies `blockList`, reads `audioEngine.getOutputSampleRate()` and `getSampleLibrary()`, calls `SceneAudioExporter::bounceToFile()`. |
| `AudioEngine` | Adds `getOutputSampleRate()` and `getSampleLibrary()` so export can read the same buffers the live callback uses (no duplicate sample library). |

### Timeline length

Duration is `max(block.endTimeSec())` over all blocks, where `endTimeSec()` is `startTimeSec + (isLooping ? loopDurationSec : durationSec)` (`BlockEntry.h`). If that maximum is ≤ 0, export aborts with a clear error.

### Offline clock and sequencer

- A fresh `TransportClock` is stopped, rewound to 0, looping disabled, then **started** so `SequencerEngine::update()` runs (it returns early if not playing).
- Each iteration advances the clock by `chunkSamples / sampleRate` seconds (see `kChunkSamples` in `SceneAudioExporter.cpp`). Then `SequencerEngine::update(clock, blocks)` runs, events are applied to voices and block positions (for loop retriggers that read `block.pos`), then one chunk is mixed into an accumulation buffer.
- Chunk size **64 samples** balances accuracy (events land closer to their true time than one GL frame at 60 Hz) and CPU cost.

### Sample rate

The writer uses `AudioEngine::getOutputSampleRate()` (the rate set in `prepareToPlay` for the live device). If the chosen `AudioFormat` does not list that exact rate in `getPossibleSampleRates()`, the exporter picks the **closest** supported rate so `createWriterFor` succeeds. WAV/FLAC/AIFF/Ogg generally accept common device rates (44100 / 48000).

### Safety cap

Exports longer than **20 minutes** are rejected to avoid accidental multi-gigabyte RAM use (the bounce currently holds the full float stereo buffer before writing). This constant is `kMaxDurationSec` in `SceneAudioExporter.cpp`.

### Threading note

`exportSceneAudioToFile` uses `getBlockListCopy()` from the message thread, the same pattern as **Save**. The underlying `blockList` is owned by the OpenGL thread; concurrent edits during export are the same theoretical race as saving a scene.

---

## Parity with live playback

The exporter intentionally duplicates:

- Start: Z proximity gain, Y semitone pitch rate, X pan with cos/sin law (`AudioEngine::handleStartEvent`).
- Stop: mark voices `stopping`, remove after one mix chunk (`AudioEngine::getNextAudioBlock` ordering).
- Movement: linear pan remap from X, then left/right gain from pan (`AudioEngine::dispatchEvent` Movement branch).

If live mixing code changes, **`SceneAudioExporter.cpp` should be updated in sync** (or factored into a shared helper in a future refactor).

---

## CMake

`SceneAudioExporter.cpp` and `ExportAudioDialog.cpp` are added to the `SIME` target in `CMakeLists.txt`. No extra JUCE modules beyond existing `juce_audio_formats` are required for the supported writers.

---

## Possible follow-ups

1. **Streaming writer** — Write in large blocks instead of holding the full `AudioBuffer` in RAM to support very long sessions beyond 20 minutes.
2. **MP3 / AAC** — Integrate an external encoder or platform-specific APIs; surface separate “Install encoder” documentation and CI flags.
3. **True rate match** — If the writer picks a non-device sample rate, optionally resample the float buffer so the file length in seconds matches the scene exactly at the device rate.
4. **Progress UI** — For long scenes, show a progress bar on the message thread while bouncing.
