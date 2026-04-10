# Implementation Prompt for Claude Opus 4.6
## SIME — Feature: Real Sound + X/Y/Z Spatial Mapping

---

## 1. Codebase Findings (Actual State, Verified)

### What fully works today

- `AudioEngine::start()` — properly initializes default audio device, registers `AudioSourcePlayer`, adds callback. This IS called in `ViewPortComponent` constructor.
- `AudioEngine::loadSample()` — properly reads audio files via JUCE `AudioFormatManager`. The mechanism works.
- `AudioEngine::getNextAudioBlock()` — functional voice mixer. Drains FIFO, mixes `activeVoices_` into output buffer, cleans up finished voices.
- `AudioEngine::processEvents()` — correct lock-free FIFO write path from render thread to audio thread.
- `SequencerEngine::update()` — correctly scans `blockList`, emits `Start`/`Stop` events when transport time crosses block boundaries.
- `TransportClock` — fully implemented, drives transport time in `renderOpenGL()`.
- The full event pipeline exists: `transport.update(dt)` → `sequencer.update(transport, blockList)` → `audioEngine.processEvents(events)` — this chain runs every GL frame.

### What is broken or missing

1. **Placeholder sample path** — `ViewPortComponent` constructor calls:
   ```cpp
   audioEngine.loadSample(0, juce::File("/path/to/your.wav"));
   ```
   This silently fails on every machine. No sound ever loads. This is the primary blocker.

2. **New blocks default to `soundId = -1`** — `SequencerEngine` skips blocks with `soundId < 0`, so even if audio were wired, freshly placed blocks produce no events and no sound.

3. **No spatial data in `SequencerEvent`** — the struct is:
   ```cpp
   struct SequencerEvent { SequencerEventType type; int blockSerial; int soundId; double triggerTimeSec; };
   ```
   No position fields. Block position is in `BlockEntry::pos` (Vec3i) but is never passed to the audio layer.

4. **No spatial parameters in `ActiveVoice`** — the struct has `gain` (float) but no `pan`, no `pitchRate`. All voices play mono-centered at gain 1.0.

5. **Mixer doesn't apply pan** — `getNextAudioBlock()` applies the same `gain` to all channels. No stereo differentiation.

6. **No pitch/playback-rate scaling** — `samplePosition` is an integer incremented by `toCopy` per block. There is no fractional stepping for pitch shifting.

### Summary of the gap

The full plumbing exists but is inert. The two missing links are:
- a real sound source (test tone or real sample)
- position data flowing from `BlockEntry` through `SequencerEvent` into `ActiveVoice` and finally applied in the mix loop

---

## 2. Recommended Scope for This Iteration

### In scope — implement these two things only

**Feature 1: Make sound play for testing**
- Add a method `generateTestTone(int soundId, float frequencyHz, double durationSec)` to `AudioEngine` that synthesizes a sine wave into `sampleLibrary_`. No external files required.
- In `ViewPortComponent` constructor, replace the broken file path call with test tone generation.
- Change the default `soundId` for newly placed blocks from `-1` to `0` so they immediately trigger audio.

**Feature 2: Map block position to sound**
- X → stereo pan (left/right)
- Y → pitch (playback rate)
- Z → proximity gain attenuation

### Explicitly deferred (do not implement now)

- File import / WAV browser / drag-and-drop loading
- HRTF or 3D spatialization
- Reverb, delay, or any DSP effects
- Multiple sound IDs per block or sample library management UI
- Save/load
- Any refactor of transport, sequencer, or voxel architecture
- Any changes to renderer, camera, or viewport beyond what is strictly needed

---

## 3. Final Prompt for Claude Opus 4.6

---

### Your Task

You are working inside the SIME codebase — a 3D voxel-based spatial audio sequencer built with JUCE and OpenGL 3.3. The project is mostly working on the visual/editor side. Your job is to add two features:

1. **Real, audible sound output for testing** (currently silent due to a placeholder)
2. **Block position-to-sound mapping**: X → pan, Y → pitch, Z → gain/proximity

Read every relevant file before writing a single line of code. Do not assume anything matches what comments or docs say — verify it yourself.

---

### Step 1: Read These Files First

Before doing anything, read the full contents of these files:

- `Source/AudioEngine.h`
- `Source/AudioEngine.cpp`
- `Source/SequencerEngine.h`
- `Source/SequencerEngine.cpp`
- `Source/SequencerEvent.h`
- `Source/BlockEntry.h`
- `Source/MathUtils.h`
- `Source/ViewPortComponent.h`
- `Source/ViewPortComponent.cpp`

---

### Step 2: Write an Implementation Plan

Before writing any code, output a short numbered plan:
- The exact files you will edit
- What change you will make in each file
- Why (one sentence per file)

---

### Step 3: Implement the Changes

Implement the plan. Full details on each required change follow below.

---

### Required Change A — Add `generateTestTone()` to `AudioEngine`

Add a public method to `AudioEngine`:

```cpp
void generateTestTone(int soundId, float frequencyHz, double durationSec);
```

This method must:
- Synthesize a sine wave at `frequencyHz` for `durationSec` seconds at `sampleRate_` (default 44100)
- Store it in `sampleLibrary_[soundId]` as a mono `juce::AudioBuffer<float>`
- Be callable from the message thread before `start()`
- Not allocate on the audio thread

Since `sampleRate_` is set in `prepareToPlay()` which runs after `start()`, the simplest safe approach is to use a hardcoded default of 44100.0 for tone generation at startup, or to generate after `start()` is called. Choose whichever is simpler. A fixed 44100 Hz is fine for MVP.

---

### Required Change B — Fix `ViewPortComponent` constructor

In `ViewPortComponent.cpp`, replace the broken placeholder:

```cpp
// REMOVE THIS:
audioEngine.loadSample(0, juce::File("/path/to/your.wav"));
```

With calls to generate test tones:

```cpp
audioEngine.generateTestTone(0, 440.0f,  2.0);   // A4, 2 seconds
audioEngine.generateTestTone(1, 660.0f,  2.0);   // E5, 2 seconds
audioEngine.generateTestTone(2, 880.0f,  2.0);   // A5, 2 seconds
```

Keep `audioEngine.start()` immediately after, as it already is.

---

### Required Change C — Default new blocks to `soundId = 0`

In `ViewPortComponent.cpp`, find where new blocks are pushed into `blockList` (search for `blockList.push_back`). Change the default `soundId` from `-1` to `0`:

```cpp
// Currently:
blockList.push_back({ nextSerial++, placePos });
// Change to initialize soundId = 0 so blocks play on placement:
BlockEntry entry;
entry.serial = nextSerial++;
entry.pos    = placePos;
entry.soundId = 0;
blockList.push_back(entry);
```

---

### Required Change D — Add position fields to `SequencerEvent`

In `SequencerEvent.h`, add block position to the struct:

```cpp
struct SequencerEvent
{
    SequencerEventType type         = SequencerEventType::Start;
    int    blockSerial              = -1;
    int    soundId                  = -1;
    double triggerTimeSec           = 0.0;
    float  blockX                   = 0.0f;   // ADD
    float  blockY                   = 0.0f;   // ADD
    float  blockZ                   = 0.0f;   // ADD
};
```

---

### Required Change E — Populate position in `SequencerEngine`

In `SequencerEngine.cpp`, when constructing the Start event, copy block position:

```cpp
SequencerEvent ev;
ev.type           = SequencerEventType::Start;
ev.blockSerial    = block.serial;
ev.soundId        = block.soundId;
ev.triggerTimeSec = now;
ev.blockX         = static_cast<float>(block.pos.x);   // ADD
ev.blockY         = static_cast<float>(block.pos.y);   // ADD
ev.blockZ         = static_cast<float>(block.pos.z);   // ADD
eventBuffer_.push_back(ev);
```

Position is not needed on Stop events — leave those unchanged.

---

### Required Change F — Add spatial parameters to `ActiveVoice`

In `AudioEngine.h`, extend `ActiveVoice`:

```cpp
struct ActiveVoice
{
    int    blockSerial     = -1;
    int    soundId         = -1;
    float  samplePositionF = 0.0f;  // CHANGE from int to float for pitch rate
    bool   stopping        = false;
    float  gain            = 1.0f;
    float  pan             = 0.0f;  // ADD: -1 = full left, 0 = center, +1 = full right
    float  pitchRate       = 1.0f;  // ADD: 1.0 = normal, >1 = higher pitch
    float  leftGain        = 1.0f;  // ADD: precomputed per-channel gains
    float  rightGain       = 1.0f;  // ADD

    const juce::AudioBuffer<float>* buffer = nullptr;

    bool isFinished() const noexcept
    {
        return buffer == nullptr
            || static_cast<int>(samplePositionF) >= buffer->getNumSamples();
    }
};
```

---

### Required Change G — Compute spatial parameters in `handleStartEvent`

In `AudioEngine.cpp`, update `handleStartEvent` to compute pan, pitch, and gain from position fields in the event:

**Spatial mapping rules (MVP):**

- **Pan from X**: normalize X into [-1, 1] using a reference range of ±20 world units (the grid is 80 units wide, ±40). Clamp.
  ```cpp
  float pan = juce::jlimit(-1.0f, 1.0f, ev.blockX / 20.0f);
  ```
  Then compute equal-power stereo gains:
  ```cpp
  float angle    = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0 to pi/2
  voice.leftGain  = voice.gain * std::cos(angle);
  voice.rightGain = voice.gain * std::sin(angle);
  ```

- **Pitch from Y**: each voxel unit = one semitone up. Use standard tuning math.
  ```cpp
  voice.pitchRate = std::pow(2.0f, ev.blockY / 12.0f);
  ```
  Clamp to `[0.25f, 4.0f]` to avoid extremes.

- **Gain from Z**: treat Z as distance from the listener. Use simple inverse falloff.
  ```cpp
  float refDist = 5.0f;
  float dist    = std::abs(ev.blockZ);
  voice.gain    = juce::jlimit(0.0f, 1.0f, refDist / (refDist + dist));
  ```
  Apply this gain into the left/right calculation above (substitute `voice.gain` with the attenuated value when computing `leftGain`/`rightGain`).

---

### Required Change H — Apply spatial parameters in `getNextAudioBlock`

The current mixing loop copies whole chunks. Replace it with a **per-sample loop** that supports fractional `samplePositionF` and per-channel gain:

```cpp
for (auto& voice : activeVoices_)
{
    if (voice.buffer == nullptr) continue;

    const int   srcChannels = voice.buffer->getNumChannels();
    const int   totalSrc    = voice.buffer->getNumSamples();
    auto*       outL        = outputBuffer->getWritePointer(0, startSample);
    auto*       outR        = (outputChannels > 1)
                              ? outputBuffer->getWritePointer(1, startSample)
                              : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        int srcIdx = static_cast<int>(voice.samplePositionF);
        if (srcIdx >= totalSrc) break;

        // Read from source (use channel 0 for mono-to-stereo)
        float sample = voice.buffer->getSample(0, srcIdx);

        outL[i] += sample * voice.leftGain;
        if (outR) outR[i] += sample * voice.rightGain;

        voice.samplePositionF += voice.pitchRate;
    }
}
```

This replaces the existing `addFrom()` block. Remove the old integer `samplePosition` field from `ActiveVoice` entirely (it is now `samplePositionF`).

---

### Required Change I — Ensure `isFinished()` still works

After renaming `samplePosition` to `samplePositionF` (float), verify that `isFinished()` in `ActiveVoice` casts correctly:

```cpp
bool isFinished() const noexcept
{
    return buffer == nullptr
        || static_cast<int>(samplePositionF) >= buffer->getNumSamples();
}
```

Also verify the stop-event handler still compiles — it only checks `voice.blockSerial` and sets `voice.stopping`, so it needs no changes.

---

### Audio Thread Safety Rules

You must not violate these:
- No heap allocation inside `getNextAudioBlock()`. The `activeVoices_` vector may grow in `handleStartEvent()` (also called from audio thread via the FIFO drain), but this is bounded in practice by the small event count per frame. If concerned, pre-reserve `activeVoices_` in `prepareToPlay()`.
- No locks in `getNextAudioBlock()`. The existing FIFO pattern is already lock-free — do not add a mutex.
- No JUCE message-thread calls inside `getNextAudioBlock()`.

---

### Step 4: Manual Test Plan

After implementing, verify the following by running the app:

1. **Sound plays** — Place a block anywhere. Press Play. You should hear a 440 Hz tone.
2. **Pan follows X** — Place one block far left (e.g. x = -15), one far right (x = +15). Press Play. Left block should sound left-heavy, right block should sound right-heavy.
3. **Pitch follows Y** — Place one block at y = 0, one at y = 12. Press Play. y=12 block should sound an octave higher than y=0.
4. **Volume follows Z** — Place one block at z = 0 and one at z = 20. Press Play. The distant block (z=20) should be noticeably quieter.
5. **Transport still works** — Play, Pause, Stop all work. Block highlights fire at the right time. Auto-stop works after last block ends.
6. **No crashes or dropouts** — Run for 30+ seconds with 5+ blocks playing simultaneously. No audio glitches or app crashes.

---

### Files You Are Expected to Edit

| File | What Changes |
|------|--------------|
| `Source/AudioEngine.h` | Add `generateTestTone()` declaration; extend `ActiveVoice` with `samplePositionF`, `pan`, `pitchRate`, `leftGain`, `rightGain` |
| `Source/AudioEngine.cpp` | Implement `generateTestTone()`; update `handleStartEvent()` for spatial params; rewrite mix loop in `getNextAudioBlock()` |
| `Source/SequencerEvent.h` | Add `blockX`, `blockY`, `blockZ` fields |
| `Source/SequencerEngine.cpp` | Populate position fields on Start events |
| `Source/ViewPortComponent.cpp` | Replace broken `loadSample()` call with `generateTestTone()` calls; change default `soundId` for new blocks to `0` |

**Do not edit any other files unless a compiler error forces you to.**

---

## 4. Notes / Watchouts for Opus

- **`samplePosition` is currently an `int`** in `ActiveVoice`. Changing it to `float samplePositionF` touches every reference to it. There are only a few: `handleStartEvent()` (sets to 0), `getNextAudioBlock()` (reads and increments), and `isFinished()`. Check all three.

- **The mix loop currently uses `addFrom()` with a chunk copy.** Replacing it with a per-sample loop is a deliberate tradeoff — it's slower for large block sizes but required for fractional pitch stepping. For an MVP with ≤20 voices this is totally fine.

- **`generateTestTone()` is called before `start()`, which means before `prepareToPlay()` sets `sampleRate_`.** Use a hardcoded `44100.0` in `generateTestTone()` to avoid this ordering issue. It is an MVP and the device will almost certainly run at 44100.

- **`BlockEntry` uses `Vec3i` (integer coordinates), not float.** Position values are small integers (voxel grid coordinates), so the spatial mapping ranges need to be calibrated to that. The grid runs from -40 to +40, Y is 0 and up. The suggested ranges (±20 for X pan, semitone-per-unit for Y pitch, inverse falloff for Z) are calibrated to this grid.

- **New blocks get `soundId = 0` after this change, but `BlockEditPopup` still lets users set any Sound ID.** If a user sets `Sound ID = 2` and tone 2 is loaded, it will play. If they set something not loaded, it will silently skip. This is correct existing behavior — don't change it.

- **`SequencerEngine` skips blocks with `soundId < 0`.** After changing the default to 0, this gate is only hit if the user explicitly sets Sound ID back to a negative number in the edit popup.

- **Do not remove the `blockListOpen` check or any sidebar/voxel logic.** Only touch audio-related code paths.

- **The `juce::AbstractFifo` FIFO writes happen on the render/GL thread and reads happen on the audio thread.** The existing pattern is correct and thread-safe — don't add locks around it.
