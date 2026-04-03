# SIME Audio Engine — Master Execution Plan for Claude Opus 4.6

> **Author:** Claude Sonnet 4.6  
> **Target implementer:** Claude Opus 4.6  
> **Date:** April 2026  
> **Branch:** `Nihar-Audio-Engine`

---

## Preamble: How to Use This Document

Read this document completely before writing a single line of code. Every section feeds into the next. Sections 3–9 are the permanent reference architecture. Section 10 is the step-by-step build order. Follow Section 10 sequentially; consult the architecture sections whenever you need the *why* behind a decision.

---

## 1. Executive Summary

SIME's audio engine must do one thing extraordinarily well: take a 3D scene full of sound-emitting blocks, know where the listener (camera) is at every moment, and produce correct, low-latency, spatially plausible stereo audio on headphones — in real time, without ever stalling, allocating, or locking on the audio callback thread.

For MVP the engine is deliberately narrow:

- WAV files only, preloaded entirely into memory.
- Up to ~100 simultaneously active blocks.
- Stereo headphone output via panning + distance attenuation (no HRTF convolution).
- A simple global transport (play/pause/seek/stop) that drives block activation from start times.
- Block positions and gains read from a lock-free snapshot that the UI/spatial thread refreshes on every frame.
- No plugin hosting, no synthesis, no surround, no time-stretch, no convolution reverb.

The audio engine slots alongside the existing `MainComponent`/`Camera`/`VoxelGrid` architecture. It does **not** replace or deeply modify any existing spatial code. It reads spatial state; it does not write it.

---

## 2. Codebase Integration Assessment

### 2.1 What Already Exists (Relevant to Audio)

| Existing element | Audio relevance |
|---|---|
| `MainComponent` | Central integration point; owns camera, handles frame timing (`dt`), input events. Will own the `AudioEngine` instance and the transport UI. |
| `Camera` | Provides listener position (`getPosition()`), forward vector (`getForward()`), right vector (`getRight()`). Audio engine reads these every frame to compute listener-relative panning. |
| `VoxelGrid` | Currently a sparse `unordered_set<Vec3i>` — purely positional, no audio metadata. Will need a parallel **SoundBlock** data structure; do NOT add audio fields directly to `VoxelGrid`. |
| `MathUtils.h` (`Vec3f`, `Mat4`) | Shared math; reuse for all spatial audio calculations. No changes needed. |
| `pendingOps` / `CriticalSection` pattern | Establishes the project's threading idiom: UI/GL thread pushes commands; consuming thread drains. Mirror this for audio parameter updates. |
| `CMakeLists.txt` | Does **not** link any JUCE audio modules. First code task is adding them. |

### 2.2 Modules That Need Extension

| Module | What changes |
|---|---|
| `CMakeLists.txt` | Add `juce_audio_basics`, `juce_audio_devices`, `juce_audio_formats`, `juce_audio_utils` to `target_link_libraries`. Add `NEEDS_CURL=0`, keep `MICROPHONE_PERMISSION_ENABLED FALSE`. |
| `MainComponent` | Add `AudioEngine` member; call `audioEngine.start()` / `stop()` in constructor/destructor. Wire transport keyboard shortcuts. Push `SceneSnapshot` to audio engine once per GL frame. |

### 2.3 New Subsystems (Fully Isolated)

All new audio code lives under `Source/Audio/`. It has zero compile-time dependency on `Renderer`, `Raycaster`, or `VoxelGrid`. It communicates with the rest of the app only through three interfaces:

1. **`SceneSnapshot`** — a plain-old-data struct the GL/message thread writes and the audio thread reads (lock-free).
2. **`TransportCommand`** — a small command struct pushed through a lock-free SPSC queue from UI to audio thread.
3. **`AudioEngineStatus`** — a small status struct the audio thread writes and the UI reads (for diagnostics / HUD).

### 2.4 Assumptions Opus Must Verify in the Codebase Before Coding

- [ ] Confirm JUCE is present at `C:\sime\JUCE\` (gitignored, must be cloned per README before build).
- [ ] Confirm the project builds and runs cleanly on the target machine before any audio changes.
- [ ] Confirm `juce::AudioDeviceManager` is not already instantiated anywhere.
- [ ] Confirm there is no existing audio branch merge conflict on `Nihar-Audio-Engine`.
- [ ] Confirm `juce_audio_devices` is available in the JUCE version present (it is in JUCE 7+).
- [ ] Confirm the Windows audio backend default (WASAPI exclusive vs shared) is acceptable for MVP (shared mode is fine for MVP).
- [ ] Confirm `Vec3f` in `MathUtils.h` has `dot()` and `normalized()` — it does per the codebase scan.

---

## 3. Architecture Proposal

All new files live under `Source/Audio/` unless stated otherwise.

---

### 3.1 `AudioEngine` (`AudioEngine.h / .cpp`)

**Responsibility:** The single top-level audio object. Owned by `MainComponent`. Manages device I/O, owns all audio subsystems, implements `juce::AudioIODeviceCallback`.

**Ownership/Lifetime:** Created in `MainComponent` constructor, destroyed in destructor. Lives on the message thread but its callback fires on the audio thread.

**Thread affinity:** Constructed/destroyed on message thread. `audioDeviceIOCallbackWithContext()` fires on audio thread.

**Key methods:**
- `void initialise()` — opens device via `DeviceManager`, registers self as callback.
- `void shutdown()` — unregisters callback, closes device.
- `void pushSnapshot(const SceneSnapshot& s)` — called from GL/message thread every frame; writes to atomic double-buffer.
- `void pushTransportCommand(TransportCommand cmd)` — called from UI; writes to SPSC queue.
- `AudioEngineStatus getStatus() const` — reads diagnostic atomic fields.
- `void audioDeviceIOCallbackWithContext(...)` — the hot path; never allocates, never locks.

**Performance concerns:** This class must be a thin dispatcher. All real work happens in `Mixer`, `Scheduler`, `Spatializer`. Keep the callback body under ~50 lines.

**Dependencies:** `DeviceManagerWrapper`, `Scheduler`, `Mixer`, `VoicePool`, lock-free queues.

---

### 3.2 `DeviceManagerWrapper` (`DeviceManagerWrapper.h / .cpp`)

**Responsibility:** Wraps `juce::AudioDeviceManager`. Handles device enumeration, selection, opening, and teardown. Insulates the rest of the engine from JUCE device API verbosity.

**Ownership/Lifetime:** Owned by `AudioEngine`.

**Thread affinity:** All methods called from message thread only.

**Key methods:**
- `bool openDefaultDevice(int sampleRate, int bufferSize)` — opens WASAPI shared mode, 44100 Hz, 512 samples for MVP.
- `void close()`
- `int getSampleRate() const`, `int getBufferSize() const`
- `juce::AudioDeviceManager& getManager()`

**Notes:** For MVP, hardcode preferred sample rate 44100, buffer size 512. Expose these as constants in a `AudioConfig.h` header for easy tuning.

---

### 3.3 `AudioClip` (`AudioClip.h / .cpp`)

**Responsibility:** Holds a fully decoded, preloaded, immutable PCM audio buffer for one WAV file. Reference-counted so multiple `SoundBlock`s can reference the same clip without duplicating memory.

**Ownership/Lifetime:** Created by `SampleLibrary` during load. Shared via `std::shared_ptr<AudioClip>`. Once created, **never mutated**. Destroyed when all references drop.

**Thread affinity:** Data is immutable after load; safe to read from any thread.

**Key fields:**
- `juce::AudioBuffer<float> samples` — interleaved float PCM, always normalized to [-1, 1].
- `int sampleRate` — native sample rate of the file.
- `int numChannels` — 1 or 2 for MVP.
- `juce::String filePath` — for diagnostics only.
- `juce::String clipId` — UUID string assigned at load time.

**Key methods:**
- `static std::shared_ptr<AudioClip> loadFromFile(const juce::File& f)` — blocking, call from background thread only.
- `const float* getReadPointer(int channel) const`
- `int getNumSamples() const`

**Performance:** Samples are contiguous floats. No virtual dispatch. All access via raw pointer arithmetic in the audio callback.

---

### 3.4 `SampleLibrary` (`SampleLibrary.h / .cpp`)

**Responsibility:** Central registry of loaded `AudioClip`s. Accepts load requests from the UI thread, executes them on a background `juce::ThreadPool`, makes clips available once loaded.

**Ownership/Lifetime:** Owned by `AudioEngine`. Lives for the app lifetime.

**Thread affinity:** `requestLoad()` called from message thread. Load work happens on a thread pool. Loaded clips registered under a mutex (NOT the audio callback mutex — clips are only registered before playback starts or while audio thread is not using them, enforced by design: the audio thread only reads clips already in the registry).

**Key methods:**
- `void requestLoad(const juce::File& f, std::function<void(std::shared_ptr<AudioClip>)> onDone)` — non-blocking, dispatches to thread pool.
- `std::shared_ptr<AudioClip> getClip(const juce::String& clipId) const` — safe to call from message thread after load completes.
- `void unloadAll()`

**Safety note:** The audio thread accesses `AudioClip` data through `Voice` objects only. A `Voice` holds a `const AudioClip*` (raw pointer, never null during playback). `SampleLibrary` never unloads a clip while voices referencing it are active. Enforcement: a clip may only be unloaded when the `Scheduler` has confirmed no active voice references it.

---

### 3.5 `SoundBlock` (`SoundBlock.h`)

**Responsibility:** The authoring-time description of one sound event in the scene. Lives in editor/UI state. This is what the user creates, moves, and edits. **Not** used directly by the audio thread.

**Ownership/Lifetime:** Owned by `SoundScene` (see 3.8). One per sound event. Persistent for the app lifetime of that event.

**Fields:**
```
uint64_t    id              // stable unique ID, assigned at creation
Vec3f       position        // world-space position (mutable by UI)
float       startTime       // seconds from transport zero
float       duration        // seconds; 0 = play to end of clip
juce::String clipId         // references AudioClip in SampleLibrary
float       gainDb          // [-60, +6] dB
float       pitchSemitones  // [-24, +24]; 0 = no shift (MVP: no shift)
bool        looping         // loop within duration window
float       attenuationRadius  // distance at which gain = 0 (world units)
float       spread          // stereo spread factor [0, 1]
bool        active          // soft-deleted flag
```

**Thread affinity:** Only the message/UI thread reads or writes this. Never passed to audio thread directly.

---

### 3.6 `SoundScene` (`SoundScene.h / .cpp`)

**Responsibility:** Owns all `SoundBlock` objects. Authoritative editor-side state. Provides the snapshot-generation function.

**Ownership/Lifetime:** Owned by `MainComponent`.

**Thread affinity:** All mutation from message thread only.

**Key methods:**
- `uint64_t addBlock(SoundBlock block) -> id`
- `void removeBlock(uint64_t id)`
- `SoundBlock* getBlock(uint64_t id)`
- `const std::vector<SoundBlock>& getAllBlocks() const`
- `SceneSnapshot buildSnapshot(const Vec3f& listenerPos, const Vec3f& listenerForward, const Vec3f& listenerRight, double transportTimeSec) const` — called once per GL frame, returns a value-type snapshot.

---

### 3.7 `SceneSnapshot` (`SceneSnapshot.h`)

**Responsibility:** A completely flat, POD-safe description of the scene as the audio thread needs it for one processing cycle. Immutable once written. Zero pointers to non-trivial objects.

**Ownership/Lifetime:** Value type. Written by `SoundScene::buildSnapshot()` on the message/GL thread. Read by `Scheduler` and `Spatializer` on the audio thread. Transferred via lock-free double-buffer (see Section 4).

**Fields:**
```
struct BlockEntry {
    uint64_t    id
    Vec3f       position
    float       startTime
    float       duration
    const float* samplesL       // raw pointer into AudioClip (read-only, never null)
    const float* samplesR       // same channel or same as L for mono
    int         totalSamples
    int         nativeSampleRate
    float       gainLinear
    float       attenuationRadius
    float       spread
    bool        looping
};

Vec3f       listenerPos
Vec3f       listenerForward
Vec3f       listenerRight
double      transportTimeSec
bool        isPlaying
int         maxBlocks           // always <= 128 for MVP
int         numBlocks
BlockEntry  blocks[128]         // fixed-size array, no heap
```

**Critical constraint:** The `samplesL` / `samplesR` pointers are safe because `AudioClip` buffers are never freed while a snapshot referencing them could be alive. `SampleLibrary` enforces this (see 3.4).

**Why fixed-size array:** No heap allocation. The snapshot is stack-constructable and trivially copyable. 128 slots × ~80 bytes = ~10 KB total — fits in L1/L2 cache comfortably.

---

### 3.8 `PlaybackTransport` (`PlaybackTransport.h / .cpp`)

**Responsibility:** Tracks global playback state: current time, play/pause state, loop region. Written from the UI thread, readable from any thread via atomics.

**Ownership/Lifetime:** Owned by `AudioEngine`.

**Thread affinity:** `play()`, `pause()`, `stop()`, `seekTo()` called from message thread. `getCurrentTime()` and `isPlaying()` readable from any thread.

**Key methods:**
- `void play()`, `void pause()`, `void stop()`, `void seekTo(double seconds)`
- `double getCurrentTime() const` — reads atomic, valid from any thread.
- `bool isPlaying() const`
- `void advanceTime(double seconds)` — called by audio thread each buffer; increments internal atomic counter.

**Implementation detail:** Use `std::atomic<double>` for `currentTimeSec` and `std::atomic<bool>` for `playing`. `seekTo` also pushes a `TransportCommand::SEEK` into the SPSC queue so the audio thread can flush voices cleanly at the exact right moment.

---

### 3.9 `Scheduler` (`Scheduler.h / .cpp`)

**Responsibility:** Given a `SceneSnapshot` and the current transport time, determines which blocks are active (within their `[startTime, startTime+duration]` window), activates new voices, retires finished ones, and manages voice playback positions.

**Ownership/Lifetime:** Owned by `AudioEngine`. Operates entirely on audio thread.

**Thread affinity:** Audio thread only. No locking.

**Key methods:**
- `void processSnapshot(const SceneSnapshot& snap, VoicePool& pool)` — activates/deactivates voices. Called at the start of each audio callback.
- `void handleTransportCommand(TransportCommand cmd, VoicePool& pool)` — handles seek/stop by resetting voice positions.

**Algorithm (MVP):**
1. For each `BlockEntry` in snapshot: check if `transportTime` is in `[startTime, startTime + duration]`.
2. If yes and no voice is active for this `id`: call `pool.activateVoice(entry)`.
3. If no and a voice is active for this `id`: call `pool.deactivateVoice(id)`.
4. On seek: deactivate all voices, recompute which should be active at new time, set their sample read positions accordingly (jump to `(seekTime - startTime) * nativeSampleRate`).

---

### 3.10 `VoicePool` (`VoicePool.h / .cpp`)

**Responsibility:** Pre-allocated pool of `Voice` objects. No heap allocation during playback. Audio thread only.

**Ownership/Lifetime:** Owned by `AudioEngine`. Preallocated on construction (128 voices for MVP).

**Thread affinity:** Audio thread only.

**`Voice` struct (internal):**
```
bool        active
uint64_t    blockId
const float* samplesL
const float* samplesR
int         totalSamples
int         nativeSampleRate
int         readPosition        // current sample index in native rate
float       gainLinear
float       attenuationRadius
float       spread
Vec3f       position            // current position from snapshot
bool        looping
// Smoothing state
float       smoothedGainL
float       smoothedGainR
```

**Key methods:**
- `void preallocate(int maxVoices)` — called at init, allocates all `Voice` storage.
- `Voice* activateVoice(const BlockEntry& entry, int startSampleOffset)` — finds free slot, fills it.
- `void deactivateVoice(uint64_t id)`
- `int getActiveCount() const`
- `Voice* getActiveVoices(int& count)` — returns pointer to internal contiguous array slice.

**Design note:** Voices are stored in a flat array. The `active` flag determines participation. No linked lists. Cache-friendly linear scan over 128 elements is fine at MVP scale.

---

### 3.11 `Spatializer` (`Spatializer.h / .cpp`)

**Responsibility:** Given a voice's world position and the listener's position/orientation, computes per-voice stereo gain factors (`gainL`, `gainR`) for the current buffer. Called on audio thread.

**Ownership/Lifetime:** Stateless utility; owned by `AudioEngine` or simply static methods.

**Thread affinity:** Audio thread.

**Key methods:**
- `static void computeGains(const Vec3f& sourcePos, const Vec3f& listenerPos, const Vec3f& listenerRight, float attenuationRadius, float spread, float& outGainL, float& outGainR)`

**Algorithm (MVP — no HRTF):**

1. **Distance attenuation:** `dist = (sourcePos - listenerPos).length()`. Inverse-square law clamped: `attenGain = clamp(1.0 / max(dist, 1.0)^2, 0, 1)`. Normalize by `attenuationRadius` so at `attenuationRadius` distance, gain ≈ 0.
2. **Panning:** Project `(sourcePos - listenerPos)` onto `listenerRight`. `panValue = dot(normalized(sourcePos - listenerPos), listenerRight)` clamped to [-1, 1]. Apply equal-power pan law: `gainL = cos((panValue + 1) * PI/4)`, `gainR = sin((panValue + 1) * PI/4)`.
3. **Apply attenuation to both channels:** `gainL *= attenGain * voiceGainLinear`, same for `gainR`.
4. **Spread:** `spread` widens the stereo image; for MVP it is a simple blend between the panned result and a centered (mono) version.

No Doppler for MVP. No HRTF. No room reverb.

---

### 3.12 `Mixer` (`Mixer.h / .cpp`)

**Responsibility:** The innermost audio loop. Iterates active voices, reads samples, resamples if needed, applies pre-computed stereo gains, accumulates into the output buffer. Also applies a simple peak limiter at the output stage.

**Ownership/Lifetime:** Owned by `AudioEngine`.

**Thread affinity:** Audio thread only.

**Key methods:**
- `void process(Voice* voices, int numVoices, float* outL, float* outR, int numSamples, int outputSampleRate, const Vec3f& listenerPos, const Vec3f& listenerForward, const Vec3f& listenerRight)`

**Algorithm:**
1. Zero-fill `outL` and `outR`.
2. For each active voice: compute `gainL`, `gainR` via `Spatializer`. Smooth gains (see 3.13). Read `numSamples` samples from voice (with linear resampling if native rate ≠ output rate). Accumulate into `outL`, `outR`. Advance `readPosition`.
3. After all voices: apply peak limiter (simple threshold + gain reduction, not brickwall for MVP — just `tanh` soft clip or a simple gain-reduction coefficient).
4. Copy `outL`, `outR` into JUCE output buffer.

**Resampling (MVP):** Linear interpolation. Compute `resampleRatio = nativeSampleRate / outputSampleRate`. Advance a float read pointer by `resampleRatio` per output sample. Lerp between adjacent samples. Good enough for MVP; no need for sinc/polyphase at this stage.

---

### 3.13 `ParameterSmoother` (`ParameterSmoother.h`)

**Responsibility:** Prevents zipper noise when gains/panning change between buffers by applying a one-pole low-pass smoothing filter to any parameter value.

**Ownership/Lifetime:** Header-only value type. Embedded directly in `Voice` for `smoothedGainL` and `smoothedGainR`.

**Thread affinity:** Audio thread.

**Interface:**
```cpp
struct ParameterSmoother {
    float current { 0.0f };
    float coeff   { 0.99f };   // set based on desired smoothing time and sample rate

    float next(float target) {
        current += (target - current) * (1.0f - coeff);
        return current;
    }
};
```

Smoothing time ≈ `-1 / (sampleRate * log(coeff))` seconds. At 44100 Hz, `coeff = 0.9995` gives ~45 ms smoothing — enough to prevent clicks without noticeable lag.

---

### 3.14 Lock-free Transfer Types (`LockFreeTypes.h`)

**Responsibility:** Defines the two lock-free communication primitives used across thread boundaries.

**Contents:**

**`AtomicSnapshotBuffer<T>`** — double buffer for `SceneSnapshot`. Writer atomically flips a flag after writing. Reader atomically reads the latest version. No locking, no blocking. Implemented with two `alignas(64)` instances of `T` and an `std::atomic<int>` index.

**`SPSCQueue<T, N>`** — lock-free single-producer single-consumer ring buffer of capacity `N`. Used for `TransportCommand` messages from UI to audio thread. Implemented with head/tail atomics. `N=64` is sufficient.

---

### 3.15 `AudioConfig.h`

A single header of compile-time and runtime constants:

```
kPreferredSampleRate    = 44100
kPreferredBufferSize    = 512
kMaxVoices              = 128
kMaxBlocks              = 128
kSmoothingCoeff         = 0.9995f
kDefaultAttenRadius     = 50.0f     // world units
kAttenuationMinDist     = 1.0f      // clamp denominator
```

---

## 4. Threading and Real-Time Safety Model

### 4.1 Thread Inventory

| Thread | Role | May block? | May allocate? | May lock? |
|---|---|---|---|---|
| **Message thread** | JUCE UI, input, scene editing, loading | Yes | Yes | Yes |
| **GL thread** | Renders scene, drives `renderOpenGL()` | Short sleeps only | Rarely | Only GL-safe locks |
| **Audio callback thread** | Runs `audioDeviceIOCallbackWithContext` | **NEVER** | **NEVER** | **NEVER** |
| **Background thread** (pool) | WAV file loading | Yes | Yes | Yes |

### 4.2 Cross-Thread Data Flow

```
Message Thread                GL Thread                 Audio Thread
─────────────────────         ─────────────────────     ─────────────────
SoundScene (mutable)          reads SceneSnapshot       reads SceneSnapshot (atomic double-buf)
SampleLibrary (loads clips)   writes SceneSnapshot ──►  Scheduler / VoicePool / Mixer / Spatializer
PlaybackTransport (controls)  ─────────────────────     reads TransportCommands (SPSC queue)
                                                        writes AudioEngineStatus (atomic)
```

**Key rules:**
- The GL/message thread calls `audioEngine.pushSnapshot(scene.buildSnapshot(...))` once per frame inside `renderOpenGL()`. This is the only path for spatial data to reach the audio thread.
- The message thread pushes `TransportCommand` structs into the SPSC queue for play/pause/seek/stop. The audio thread drains this at the top of each callback.
- The audio thread writes `AudioEngineStatus` (active voice count, output level, underrun count) via atomics. The GL thread reads these for HUD display. No lock needed.

### 4.3 SceneSnapshot Double-Buffer Protocol

```
Message/GL thread:
  1. Build new SceneSnapshot into back buffer (write slot).
  2. Atomically swap index to make it the front buffer.

Audio thread (top of callback):
  1. Read current front index atomically.
  2. Read from that slot — guaranteed consistent view.
```

There is no spin-wait or blocking. If the audio thread and GL thread happen to access the same slot simultaneously (which the double-buffer prevents), the worst case is the audio thread reads a one-frame-old snapshot — which is acceptable.

### 4.4 Preallocation Checklist

Everything the audio thread touches must be preallocated:

- `VoicePool`: 128 `Voice` objects, allocated in `preallocate()` before device opens.
- `Mixer` output scratch buffers: two `float[kPreferredBufferSize]` arrays, allocated at init.
- `SceneSnapshot` double buffer: two `SceneSnapshot` instances, stack/static allocated.
- `SPSCQueue<TransportCommand, 64>`: fixed-size ring buffer, no heap during use.
- No `std::vector`, no `std::string`, no `juce::String` on the audio thread callback path.

### 4.5 WAV Loading Safety

- Load always happens on the background thread pool.
- Once `AudioClip` is constructed, it is immutable.
- `SampleLibrary` stores clips in a `std::unordered_map<juce::String, std::shared_ptr<AudioClip>>` protected by a `juce::CriticalSection`. This lock is only acquired on the message thread (to insert) and the message thread (to read for snapshot building). **Never** on the audio thread.
- `SceneSnapshot::BlockEntry` carries raw `const float*` pointers. These are assigned during `buildSnapshot()` on the message/GL thread, where the lock is held briefly. After that, the audio thread accesses only the raw pointer — no lock.
- A clip must never be unloaded while any `SceneSnapshot` that references it could be in flight. For MVP: never unload clips during playback; only unload on project close or explicit user unload when transport is stopped.

---

## 5. Data Model for Sound Blocks

Three representations, each with a clear owner and thread:

### 5.1 Authoring State (`SoundBlock` in `SoundScene`)

Owned by message thread. Mutable. Contains everything the user sees and edits: position, start time, duration, clip reference (by ID string), gain, looping, spatial parameters. This is serialized to disk.

### 5.2 Runtime Snapshot State (`SceneSnapshot::BlockEntry`)

Built from authoring state once per GL frame. Flat, POD. Carries resolved pointers (`const float*` into `AudioClip`). No strings. No shared_ptr. Copied into double-buffer. Audio thread reads this.

### 5.3 Audio-Thread Voice State (`Voice` in `VoicePool`)

Activated by `Scheduler` when a block becomes active. Tracks `readPosition` (current sample offset), smoothed gain values, and all parameters needed to produce audio samples. Deactivated when the block's window ends or on transport stop/seek.

### 5.4 Separation Rules

- **Never** give the audio thread a `SoundBlock*`.
- **Never** give the audio thread a `std::shared_ptr<AudioClip>`.
- **Never** put a `juce::String` or any heap-owning type in `SceneSnapshot::BlockEntry`.
- **Always** resolve string IDs to raw pointers before the snapshot crosses thread boundaries.

---

## 6. MVP Audio Feature Set

### Phase A — Foundational Audio Output (WAV Playback)

**In scope:**
- `CMakeLists.txt` updated with JUCE audio modules.
- `AudioConfig.h` with constants.
- `DeviceManagerWrapper` opening default WASAPI shared output.
- `AudioClip` loading a WAV from disk (blocking, message thread, for initial testing).
- `VoicePool` with 128 pre-allocated voices.
- `Mixer` generating silence then playing one hardcoded clip through both channels.
- `AudioEngine` wired into `MainComponent`, device opens on launch.
- **Success criterion:** App plays a WAV file to default output at launch.

### Phase B — Block-Triggered Playback and Transport

**In scope:**
- `SoundBlock`, `SoundScene` (bare minimum fields).
- `SceneSnapshot` and `AtomicSnapshotBuffer`.
- `PlaybackTransport` with play/pause/stop/seek.
- `Scheduler` activating/deactivating voices based on transport time.
- `SampleLibrary` with background loading.
- `SPSCQueue` for `TransportCommand`.
- `MainComponent` builds snapshot in `renderOpenGL()`, pushes it.
- Basic keyboard shortcut: Spacebar = play/pause.
- **Success criterion:** A `SoundBlock` at `startTime=0` plays its WAV when transport plays; stops when paused; resets on stop.

### Phase C — Spatialization and Attenuation

**In scope:**
- `Spatializer` with distance attenuation and equal-power stereo panning.
- `ParameterSmoother` embedded in voices.
- Camera position and orientation passed through `SceneSnapshot`.
- Block position drives panning and attenuation per buffer.
- **Success criterion:** A block behind the listener sounds different from one to the left; blocks far away are quieter.

### Phase D — Moving Blocks + Continuous Parameter Updates

**In scope:**
- `SoundScene::buildSnapshot()` reads current block positions every frame (no caching).
- Blocks whose position changes (from UI drag) have their spatial gains updated in the next audio buffer.
- Gain and position changes are smoothed via `ParameterSmoother` to prevent clicks.
- Multiple simultaneous blocks (test with 10, 50, 100).
- **Success criterion:** Dragging a block while playing produces smooth audio panning movement, no clicks or dropouts.

### Phase E — Save/Load, Diagnostics, Stress Validation

**In scope:**
- `SoundScene` serialization to JSON/XML via JUCE: block ID, position, startTime, duration, clipId, gain, looping, spatial params.
- `SampleLibrary` serialization (file paths).
- HUD display of active voice count, output peak level, underrun counter.
- Stress test: 100 blocks simultaneously active.
- Edge case handling: invalid WAV path, block deleted during playback, rapid seek.
- **Success criterion:** Scene saves and loads correctly; 100 voices play without dropout; edge cases don't crash.

### Deferred (Explicitly Out of Scope for MVP)

- HRTF / binaural convolution
- Room reverb / early reflections
- Advanced pitch shifting (beyond MVP's "no shift" default)
- Doppler effect
- Plugin hosting (VST/AU)
- Multichannel surround output
- Synthesis (oscillators, noise, etc.)
- Time-stretching
- Automation lanes / parameter envelopes
- MIDI
- Audio recording / input

---

## 7. Spatial Audio / DSP Strategy

### 7.1 Distance Attenuation

Use inverse-square law with a minimum distance clamp and a maximum radius:

```
float dist = max((sourcePos - listenerPos).length(), kAttenuationMinDist);
float distGain = 1.0f / (dist * dist);
float normalizedGain = clamp(distGain / (1.0f / (kAttenuationMinDist * kAttenuationMinDist)), 0.0f, 1.0f);
// or simpler: linear falloff from 1.0 at dist=1 to 0.0 at dist=attenuationRadius
float linearGain = clamp(1.0f - (dist / attenuationRadius), 0.0f, 1.0f);
```

For MVP, linear falloff is more intuitive and artistically controllable. Start there. Add inverse-square as an option later.

### 7.2 Stereo Panning

Equal-power (constant-power) panning law. Pan value derived from dot product of listener-right axis and source direction:

```
panValue = clamp(dot(normalize(sourcePos - listenerPos), listenerRight), -1.0f, 1.0f);
// pan = [-1, 1] → [full left, full right]
float panAngle = (panValue + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
gainL = cos(panAngle);
gainR = sin(panAngle);
```

This sounds correct for headphones. No HRTF elevation encoding. Sources directly above/below the listener will appear centered — acceptable for MVP.

### 7.3 Position-to-Audio Mapping

The listener is always the camera. `Vec3f listenerPos = camera.getPosition()`. `Vec3f listenerRight = camera.getRight()`. These are updated every frame and pushed in the snapshot.

### 7.4 Doppler

**No Doppler for MVP.** Doppler adds perceptible pitch shift artifacts when blocks move quickly. For MVP, blocks move slowly enough that the absence is unnoticeable. Add later when motion system is more mature.

### 7.5 Pitch Shifting

**No pitch shifting for MVP.** Keep `pitchSemitones` in the data model for future use. In MVP, the field is stored but ignored. Resampling for sample-rate conversion is sufficient.

### 7.6 Sample Rate Conversion / Resampling

Linear interpolation resampler per voice. Compute `ratio = nativeSampleRate / outputSampleRate`. For each output sample, advance a float read pointer by `ratio` and lerp between `floor(ptr)` and `ceil(ptr)` samples. Simple, low-overhead, sounds fine for music and sound effects. Not suitable for pure tones (aliasing) but acceptable for MVP WAV playback.

### 7.7 Voice Mixing

Accumulate all voices into two `float[]` scratch buffers (L and R). No intermediate buses for MVP. Sum directly. Apply output limiter after mixing.

### 7.8 Output Limiter

Simple soft-clip on the final mixed output: apply `tanh(x * drive) / drive` where `drive = 1.5f`. This prevents clipping without a hard knee. Not a true mastering limiter — just a safety valve for MVP.

### 7.9 Smoothing

Every voice carries two `ParameterSmoother` instances (`smoothedGainL`, `smoothedGainR`). Each buffer, compute the target gain via `Spatializer`, then call `smoother.next(target)` per sample (or per buffer at the start of the buffer and linearly interpolate across the buffer — the latter is cheaper and fine for MVP). The per-buffer interpolation approach: compute `startGainL`, `startGainR` at buffer start, compute `endGainL`, `endGainR` at buffer end using new position, linearly interpolate across the `numSamples`. This is the standard "parameter ramp" technique.

### 7.10 Moving Sources in Real Time

The snapshot carries current positions. The audio thread computes gains fresh each buffer using those positions. Since positions update at GL frame rate (~60 Hz = every ~16 ms) and the audio buffer is 512 samples at 44100 Hz (~11.6 ms), positions update slightly less frequently than audio buffers. This means up to one extra buffer of lag in spatial response — completely imperceptible. Smooth gains across the buffer to eliminate any seam.

---

## 8. Scheduling and Transport Design

### 8.1 Global Transport

`PlaybackTransport` holds:
- `std::atomic<double> currentTimeSec` — advances in `advanceTime()`.
- `std::atomic<bool>   playing`
- `double loopStart`, `double loopEnd` (for future use; for MVP, no looping of transport).

`advanceTime(double dt)` is called by `AudioEngine` at the start of each callback: `currentTimeSec += (numSamples / outputSampleRate)`.

### 8.2 Play / Pause / Stop / Seek

All commands originate on the message thread and are pushed as `TransportCommand` structs into the SPSC queue. The audio thread drains the queue at the top of each callback, in order:

```
enum class TransportCommandType { PLAY, PAUSE, STOP, SEEK };
struct TransportCommand {
    TransportCommandType type;
    double seekTarget;  // only used for SEEK
};
```

On `STOP`: deactivate all voices, reset transport time to 0.  
On `SEEK`: deactivate all voices, set transport time to `seekTarget`, re-evaluate which blocks should be active (via `Scheduler::processSnapshot`) and activate them with computed `startSampleOffset`.  
On `PLAY`: set `playing = true`.  
On `PAUSE`: set `playing = false`. Do NOT deactivate voices. Resume positions maintained.

### 8.3 Block Activation

`Scheduler::processSnapshot()` runs at the top of every audio callback (when playing). It compares each `BlockEntry.startTime` and `startTime + duration` against `currentTimeSec`. Newly-in-window blocks get a voice activated with `startSampleOffset = (currentTimeSec - block.startTime) * block.nativeSampleRate`. This handles the common case where transport jumps into the middle of a block's window.

### 8.4 Duration

If `duration == 0`, play to end of clip. Otherwise clamp playback window to `duration` seconds. `Scheduler` deactivates a voice when `readPosition >= min(totalSamples, duration * nativeSampleRate)`.

### 8.5 Looping

If `looping == true`, when `readPosition >= loopEndSample`, reset to `loopStartSample` (0 for MVP). This is handled in `Mixer` when reading samples: if `readPosition + resampleStep >= loopEnd`, wrap. Use a crossfade window of ~64 samples for click-free looping (compute a short fade-out/fade-in blend). For MVP, zero-crossfade looping is acceptable first.

### 8.6 Transport Scrubbing

**Partially deferred for MVP.** Seek is supported (jumps to new position cleanly). Live scrubbing (continuously moving the playhead while dragging a timeline cursor) is deferred. For MVP, seek is triggered by a discrete UI action (e.g., clicking a time display). Implement seek first; scrub later.

### 8.7 How Transport State Reaches the Audio Thread

The audio thread reads `playing` and `currentTimeSec` directly from `PlaybackTransport`'s atomics, and drains the `TransportCommand` SPSC queue. The `SceneSnapshot` also carries `transportTimeSec` and `isPlaying` fields, derived from `PlaybackTransport` at snapshot-build time. The audio thread should trust the snapshot's values (since they were consistent at snapshot build time) rather than re-reading the atomics independently, to avoid any races.

---

## 9. Latency and Performance Strategy

### 9.1 Latency Budget

Target: audio latency < 20 ms from block activation event to sound.

| Component | Latency contribution |
|---|---|
| WASAPI shared mode output buffer | ~10–20 ms (buffer size 512 at 44100 = 11.6 ms) |
| Snapshot propagation lag | ≤ 1 GL frame ≈ 16 ms (worst case) |
| Scheduler activation | Within one audio buffer ≈ 11.6 ms |
| Total (typical) | ~15–25 ms |

This meets the stated ≤20 ms goal most of the time. For exact MVP validation, measure from `TransportCommand::PLAY` received by audio thread to first non-silent sample in output — should be < one buffer period.

### 9.2 Preloading

All WAV files are preloaded to memory before playback starts. Never do disk I/O on the audio thread or on the GL thread. Loading always happens on the background thread pool. `SoundScene::buildSnapshot()` must not call `getClip()` if the clip is not yet loaded — it should check and skip that block (produce a `BlockEntry` with `samplesL = nullptr`, which `Scheduler` skips). This is the correct graceful handling for a clip still loading.

### 9.3 Voice Pool Scan

128 voices, linear scan per buffer. At 512 samples and 44100 Hz, the callback runs in ~11.6 ms of real time. Scanning 128 voice structs is ~8 KB of data — fits in L1 cache. Cost is negligible.

### 9.4 Per-Buffer vs Per-Sample Work

| Work type | Granularity |
|---|---|
| Spatializer gain computation | Per buffer (once per voice per buffer) |
| Parameter smoothing (gain ramp) | Per buffer (compute start/end, ramp linearly) |
| Sample read + resample + accumulate | Per sample (innermost loop — keep this tight) |
| Transport time advance | Per buffer |
| Scheduler activation check | Per buffer |
| Snapshot consumption | Per buffer |

Keep the per-sample loop (`Mixer` inner loop) free of branches, virtual calls, and memory allocations. Compute all per-voice constants outside the inner loop.

### 9.5 Cache-Friendly Layout

`Voice` objects are stored in a flat array of 128. All voices are iterated in order. `AudioClip` sample data is a contiguous `float[]` — sequential read access is cache-friendly. The `SceneSnapshot` is 64-byte aligned to avoid false sharing.

### 9.6 Expected Scale

| Active blocks | CPU budget (rough) |
|---|---|
| 10 blocks | Trivially fast; ~0.1–0.3% CPU |
| 50 blocks | Fine; ~1–2% CPU |
| 100 blocks | Still fine; ~2–5% CPU |

These are estimates for MVP's simple linear resampler + panning. No heavy DSP. Profile with JUCE's `PerformanceMeasurement` or a high-resolution timer around the callback if there are doubts.

### 9.7 Measuring Latency Compliance

Add an `AudioEngineStatus` field: `int64_t lastCallbackTimestampUs`. Write it at the top of each callback with `juce::Time::getHighResolutionTicks()`. In `MainComponent::paint()`, compare against current time. If the gap is > 2× the buffer period, flag a warning in the HUD. This detects audio thread starvation early.

### 9.8 Parameter Interpolation at Buffer Boundaries

The standard technique: at the start of each buffer, record `prevGainL[voiceIdx]`. Compute new `targetGainL` via `Spatializer`. In the inner loop, ramp linearly: `gain = prevGain + (targetGain - prevGain) * (sampleIdx / numSamples)`. This fully eliminates zipper noise at buffer boundaries. The `ParameterSmoother` one-pole approach handles within-buffer smoothing; the linear ramp handles inter-buffer transitions.

---

## 10. Implementation Roadmap for Opus

Execute these steps in order. Do not skip ahead. Each step has clear success criteria.

---

### Step 1 — Build System: Add JUCE Audio Modules

**Goal:** The project compiles with JUCE audio modules linked.

**Files to modify:**
- `CMakeLists.txt`

**Changes:**
- Add to `target_link_libraries`: `juce::juce_audio_basics`, `juce::juce_audio_devices`, `juce::juce_audio_formats`, `juce::juce_audio_utils`
- Add JUCE compile definitions: `JUCE_USE_OGGVORBIS=0`, `JUCE_USE_FLAC=0`, `JUCE_USE_MP3AUDIOFORMAT=0` (WAV only for MVP to minimize dependencies)

**Dependencies:** JUCE must be present at `C:\sime\JUCE\`

**Success criterion:** `cmake --build` succeeds. Existing spatial app still launches and works.

**Test:** Launch app, verify voxel editing still works. No audio yet.

---

### Step 2 — Infrastructure: AudioConfig and LockFreeTypes

**Goal:** Define constants and lock-free primitives before any audio class uses them.

**New files:**
- `Source/Audio/AudioConfig.h`
- `Source/Audio/LockFreeTypes.h`

**Contents:**
- `AudioConfig.h`: all constants listed in 3.15.
- `LockFreeTypes.h`: `AtomicSnapshotBuffer<T>` and `SPSCQueue<T, N>` as described in 3.14.

**Dependencies:** None.

**Success criterion:** Both headers compile cleanly with no warnings. Write a tiny standalone test (in a comment block, or a scratch `main()`) confirming `SPSCQueue` push/pop works.

---

### Step 3 — Data Model: AudioClip and SampleLibrary (Stub)

**Goal:** Be able to load a WAV file into memory from the message thread.

**New files:**
- `Source/Audio/AudioClip.h / .cpp`
- `Source/Audio/SampleLibrary.h / .cpp`

**For this step:** `SampleLibrary` may use a simple synchronous `loadFromFile` (no thread pool yet). Background loading comes in Step 6.

**Dependencies:** `juce_audio_formats`, `juce_audio_basics`, `AudioConfig.h`

**Success criterion:** Call `AudioClip::loadFromFile(someWav)` from `MainComponent::MainComponent()`. Verify `clip->getNumSamples() > 0` in a JUCE `DBG()` log. No crash.

---

### Step 4 — Voice Layer: VoicePool and ParameterSmoother

**Goal:** Pre-allocated voice pool ready for use before device opens.

**New files:**
- `Source/Audio/ParameterSmoother.h`
- `Source/Audio/VoicePool.h / .cpp`

**Implementation:** Full `Voice` struct, `preallocate(128)`, `activateVoice`, `deactivateVoice`, `getActiveVoices`. No audio thread use yet — just construct and verify in `MainComponent`.

**Success criterion:** `VoicePool pool; pool.preallocate(128);` compiles and runs. `pool.getActiveCount()` returns 0. Activate 3 dummy voices, verify count = 3. Deactivate 1, verify count = 2.

---

### Step 5 — Transport: PlaybackTransport and TransportCommand

**Goal:** Transport state machine with thread-safe access.

**New files:**
- `Source/Audio/TransportCommand.h`
- `Source/Audio/PlaybackTransport.h / .cpp`

**Bind to keyboard in `MainComponent`:** Spacebar → `transport.play()` / `transport.pause()` toggle. Verify via `DBG()` logging that state transitions work.

**Success criterion:** Spacebar toggles `transport.isPlaying()`. `transport.getCurrentTime()` advances correctly when ticked manually in a test.

---

### Step 6 — Core Engine: AudioEngine + DeviceManagerWrapper (Silence)

**Goal:** Audio device opens, callback fires, produces silence. No crash.

**New files:**
- `Source/Audio/DeviceManagerWrapper.h / .cpp`
- `Source/Audio/AudioEngine.h / .cpp`

**At this stage:** `audioDeviceIOCallbackWithContext` just zero-fills the output buffer and returns.

**Wire into `MainComponent`:**
- Add `AudioEngine audioEngine` member.
- Call `audioEngine.initialise()` in `MainComponent()` constructor.
- Call `audioEngine.shutdown()` in `MainComponent()` destructor.

**Success criterion:** App launches, audio device opens (no JUCE assertion failures), produces silence (no audio artifact). HUD or `DBG()` shows "audio device opened at 44100 Hz, 512 samples".

---

### Step 7 — First Sound: Single Hardcoded Voice Through Mixer

**Goal:** Hear audio. One hardcoded WAV plays immediately when transport starts.

**New files:**
- `Source/Audio/Mixer.h / .cpp` (MVP version: no spatial, no resampling, just read + accumulate)

**Steps:**
1. Load one WAV in `AudioEngine::initialise()` (hardcode path for now).
2. On `audioDeviceIOCallbackWithContext`: if transport is playing, activate one voice manually, run `Mixer::process()` on it.
3. `Mixer::process()` reads samples from the voice, copies to output buffer, advances `readPosition`.

**No spatialization yet. No resampling yet. Assume 44100 Hz WAV.**

**Success criterion:** Press Spacebar, hear the WAV play. Press again, pause. Clean audio, no clicks on start/stop (use `ParameterSmoother` for gain fade-in on activate and fade-out on deactivate).

---

### Step 8 — Resampling

**Goal:** `Mixer` handles WAVs of any sample rate correctly.

**Modify:** `Mixer::process()` — add linear interpolation resampler as described in 7.6.

**Test:** Load a 22050 Hz WAV and a 48000 Hz WAV. Verify they play at the correct pitch/speed.

**Success criterion:** Files of 22050, 44100, and 48000 Hz all play at correct tempo and pitch.

---

### Step 9 — SceneSnapshot and Full Pipeline

**Goal:** The snapshot system is live. Transport time drives block activation.

**New files:**
- `Source/Audio/SceneSnapshot.h`
- `Source/Audio/Scheduler.h / .cpp`
- `Source/Audio/SoundBlock.h`
- `Source/Audio/SoundScene.h / .cpp`

**Wire up:**
- `MainComponent` creates a `SoundScene` with one test `SoundBlock` (hardcoded WAV, `startTime = 0`).
- `MainComponent::renderOpenGL()` calls `audioEngine.pushSnapshot(scene.buildSnapshot(...))`.
- `AudioEngine::audioDeviceIOCallbackWithContext()` calls `Scheduler::processSnapshot()` using the latest snapshot.

**Success criterion:** The WAV plays when transport is playing and the block is in the active time window. Stopping/seeking correctly deactivates voices.

---

### Step 10 — Spatialization

**Goal:** Block positions affect audio panning and attenuation.

**New files:**
- `Source/Audio/Spatializer.h / .cpp`

**Modify:** `Mixer::process()` — call `Spatializer::computeGains()` per voice per buffer; ramp gains across buffer.

**Pass listener pose** through `SceneSnapshot` (camera position, forward, right).

**Test:** Place a block to the left of the origin. With camera at origin looking forward (+Z), the block should be louder in left ear. Move block far away, it should get quieter.

**Success criterion:** Panning and attenuation respond correctly to relative positions. No clicks when block position changes.

---

### Step 11 — SampleLibrary Background Loading

**Goal:** WAV loading is non-blocking on the UI thread.

**Modify:** `SampleLibrary` — add `juce::ThreadPool`, implement `requestLoad()` with async callback.

**Test:** Request loading a large WAV while the app is running. Verify UI and audio remain responsive during load. Verify clip is available after load completes.

**Success criterion:** Large file loads don't freeze the UI. HUD shows loading state.

---

### Step 12 — Multiple Blocks and Scale Testing

**Goal:** 10, 50, 100 simultaneous blocks play without dropout.

**Test scenario:** Create 100 `SoundBlock`s, all at `startTime = 0`, spread in a sphere around the origin. Press play. Verify no audio dropouts (no underruns in `AudioEngineStatus`). Verify CPU usage is acceptable.

**Success criterion:** 100 active voices, no underruns, CPU < 10% for audio thread.

---

### Step 13 — Save/Load Scene

**Goal:** `SoundScene` serializes to and from disk.

**Modify:** `SoundScene` — add `juce::ValueTree` serialization or simple XML/JSON via JUCE's `XmlElement`. Store file paths, not clip data. On load, request background loading of each referenced WAV.

**Test:** Create a scene with 5 blocks, save, reload, verify all blocks are restored with correct positions, start times, gains.

**Success criterion:** Round-trip save/load preserves all `SoundBlock` fields. WAVs reload and play correctly after load.

---

### Step 14 — Diagnostics HUD and Edge Cases

**Goal:** Audio engine status visible in UI; edge cases handled gracefully.

**Modify:** `MainComponent::paint()` — draw active voice count, output peak level, underrun count from `AudioEngineStatus`.

**Edge cases to handle:**
- Block deleted during playback → `Scheduler` deactivates voice at next snapshot update.
- Invalid WAV path → `SampleLibrary` logs error, block is silently skipped (no voice activated).
- Rapid seek → `Scheduler` handles correctly by deactivating all voices and recomputing.
- WAV not yet loaded when block activates → skip (voice not activated), retry next snapshot.

**Success criterion:** HUD shows live audio stats. All listed edge cases handled without crash or audio glitch.

---

### Step 15 — Stress Test and Final Validation

**Goal:** Confirm MVP meets all stated latency and correctness goals.

**Tests (see Section 11):** Run all stress tests. Fix any issues found.

**Success criterion:** All items in Section 13D checklist pass.

---

## 11. Verification and Testing Plan

### 11.1 Unit Tests (where feasible, using JUCE's `UnitTest` framework)

- `SPSCQueue`: push to capacity, pop all, verify FIFO order; overflow safety.
- `AtomicSnapshotBuffer`: write on one thread, read on another; verify latest is always visible.
- `ParameterSmoother`: verify convergence to target over N samples; verify no overshoot.
- `Spatializer::computeGains`: verify left block → more gain on L; distant block → less gain; gain sum ≤ 1.0 (no amplification for same-power law).
- `Mixer` (offline): feed a known sine wave into a voice with no resampling, verify output matches.
- `Scheduler`: mock snapshot with 3 blocks, transport at t=2.0, verify correct voices activate.
- `AudioClip`: load a real WAV, verify sample count and sample rate match known values.

### 11.2 Integration Tests

- Load WAV → SampleLibrary → SoundScene → SceneSnapshot → Scheduler → VoicePool → Mixer → output. Verify non-silence when transport plays.
- Seek to middle of a block → voice activates at correct sample offset.
- Block with `looping = true` wraps without click.

### 11.3 Stress Tests

- 100 simultaneous voices: run for 60 seconds, verify zero underruns.
- Rapid seek: press seek 100 times per second for 5 seconds. No crash, no corruption.
- Load 50 WAV files simultaneously on background thread while audio is playing. No stall.

### 11.4 Manual QA Scenarios

- Play a stereo scene, move the camera around, verify spatialization responds smoothly.
- Delete a block while it is playing. Verify it stops within 1–2 frames.
- Add a block mid-playback. Verify it starts at the correct point in its window.
- Save scene, close app, reopen, load scene, play. Verify identical behavior.
- Set block gain to very high, verify output doesn't clip (soft limiter engaging).

### 11.5 Real-Time Safety Checks

- Run under a debug-mode allocator that crashes on any heap allocation during the audio callback. Fix all violations. (Use JUCE's `JUCE_ENABLE_ALLOCATION_HOOKS` in debug mode.)
- Verify no `juce::CriticalSection` is entered on the audio thread path.
- Verify `std::vector`, `juce::String`, `juce::OwnedArray`, `std::shared_ptr` copy constructors are never called on the audio thread.

### 11.6 Sync/Latency Checks

- Measure time from `TransportCommand::PLAY` dispatch to first non-zero output sample. Should be ≤ 2 × buffer period (≤ 23 ms at 512/44100).
- Measure snapshot-to-audio lag: timestamp when `pushSnapshot()` is called, compare to when the audio thread consumes it. Should be ≤ 1 buffer period.

### 11.7 Movement/Spatial Correctness

- Move a block from left to right at constant velocity. Verify panning changes smoothly from L to R in real time. No jumps.
- Place block at distance `attenuationRadius`. Verify gain ≈ 0 (silence). At distance 0.5 × radius, verify audible.

### 11.8 Edge Cases

| Edge case | Expected behavior |
|---|---|
| Block deleted during playback | Voice deactivated within 1 snapshot cycle |
| WAV file missing | Block silently skipped, no crash |
| Corrupt WAV (truncated) | `AudioClip::loadFromFile` returns nullptr, block skipped |
| Transport seek beyond end of all blocks | All voices deactivated, silence |
| Two blocks with same start time | Both voices activate simultaneously |
| Block with `duration = 0` and short WAV | Plays to end, voice deactivates naturally |
| 128 voices, try to activate 129th | Pool returns nullptr, Scheduler logs warning, drops extra voice |

---

## 12. Risks and Mitigations

| Risk | Probability | Severity | Mitigation |
|---|---|---|---|
| **Audio thread allocation** (std::vector, shared_ptr copy, juce::String) | High | Critical | Use JUCE allocation hooks in debug. Review every code path on the audio thread. Keep `SceneSnapshot` POD-only. |
| **Lock contention** (CriticalSection on audio thread) | Medium | Critical | Audit every mutex in the codebase. Audio thread must touch only lock-free primitives. |
| **Clicks on parameter changes** (no smoothing) | High | Medium | Always use `ParameterSmoother` for gain changes. Always ramp gains across buffers. Never step-change a gain mid-buffer. |
| **Stale spatial state** (snapshot not updated) | Low | Low | `renderOpenGL()` always calls `pushSnapshot()` — as long as GL thread runs, snapshot is fresh. |
| **Clip unloaded while in flight** | Low | Critical | Never unload clips during playback. Only unload on project close when transport is stopped. Enforce in `SampleLibrary::unload()` with an assertion. |
| **Transport desync** (audio time vs UI time) | Medium | Medium | Audio thread is authoritative for time. UI reads `PlaybackTransport::getCurrentTime()` from atomic. Do not maintain a separate UI-side time counter. |
| **WAV format edge cases** (24-bit, 32-bit float, multichannel) | Medium | Low | Use JUCE's `AudioFormatReader` which normalizes to float. Test with 16-bit, 24-bit, and float WAV files. For >2 channels, downmix to stereo at load time. |
| **Overengineering** (adding reverb, HRTF, plugin hosting before MVP is solid) | Medium | Medium | Stick strictly to the phase plan in Section 6. Complete each phase's success criteria before moving on. |
| **Missing JUCE audio module** (not linked in CMake) | High (currently zero audio) | High | Step 1 explicitly addresses this. Verify build before proceeding. |
| **WASAPI exclusive mode conflict** (another app owns audio device) | Low | Low | Use WASAPI shared mode (JUCE default). Do not request exclusive mode. |
| **Poor separation of editor/runtime/audio state** (SoundBlock* passed to audio thread) | Medium | Critical | Enforce strictly: audio thread touches only `SceneSnapshot`, `Voice` pool, and `TransportCommand`. Code review every type that crosses thread boundaries. |

---

## 13. Deliverables for Opus Handoff

### A. Recommended Folder/File Structure

```
Source/
├── Main.cpp                    (existing, no change)
├── MainComponent.h / .cpp      (existing, extend with audioEngine + soundScene)
├── Camera.h / .cpp             (existing, no change)
├── Raycaster.h / .cpp          (existing, no change)
├── Renderer.h / .cpp           (existing, no change)
├── VoxelGrid.h                 (existing, no change)
├── MathUtils.h                 (existing, no change)
│
└── Audio/
    ├── AudioConfig.h           (Step 2)
    ├── LockFreeTypes.h         (Step 2)
    ├── AudioClip.h / .cpp      (Step 3)
    ├── SampleLibrary.h / .cpp  (Step 3, extended in Step 11)
    ├── ParameterSmoother.h     (Step 4)
    ├── VoicePool.h / .cpp      (Step 4)
    ├── TransportCommand.h      (Step 5)
    ├── PlaybackTransport.h / .cpp  (Step 5)
    ├── DeviceManagerWrapper.h / .cpp  (Step 6)
    ├── AudioEngine.h / .cpp    (Step 6)
    ├── Mixer.h / .cpp          (Step 7, extended in Step 8)
    ├── SceneSnapshot.h         (Step 9)
    ├── Scheduler.h / .cpp      (Step 9)
    ├── SoundBlock.h            (Step 9)
    ├── SoundScene.h / .cpp     (Step 9)
    └── Spatializer.h / .cpp    (Step 10)
```

### B. Recommended Milestones

| Milestone | Steps | Deliverable |
|---|---|---|
| M1: Build foundation | 1–2 | Audio modules linked, lock-free types ready |
| M2: First sound | 3–7 | Hear a WAV from Spacebar |
| M3: Correct playback | 8–9 | Any sample rate, snapshot-driven playback |
| M4: Spatial audio | 10 | Panning and attenuation correct |
| M5: Production-ready | 11–15 | Background loading, 100 voices, save/load, diagnostics |

### C. Must Verify in Codebase Before Coding

- [ ] JUCE is present at `C:\sime\JUCE\` — run `cmake --build` and confirm the project builds before any audio work.
- [ ] Check if a `juce::AudioDeviceManager` is already constructed anywhere in the existing code (it is not, per codebase scan, but confirm).
- [ ] Check for any `#include` of `juce_audio_*` headers anywhere (none expected per scan).
- [ ] Check `MathUtils.h` for the exact `Vec3f` API: confirm `dot()`, `cross()`, `normalized()`, `length()` all exist (they do per scan).
- [ ] Check `Camera.h` for `getPosition()`, `getForward()`, `getRight()` method signatures and return types (they exist per scan).
- [ ] Check `MainComponent.h` for the destructor — ensure it is `~MainComponent()` with no special virtual/override requirements.
- [ ] Check that `renderOpenGL()` is the correct place to call `pushSnapshot()` — confirm it runs on the GL thread and has access to current camera state and `dt`.
- [ ] Check whether `CMakeLists.txt` uses JUCE 7 or JUCE 8 API — `juce_add_gui_app` is JUCE 6+ API; `juce_audio_devices` module name is identical in both versions.
- [ ] Verify no conflicting branch state on `Nihar-Audio-Engine` that could create merge conflicts.

### D. "Done Means Done" Checklist for MVP Audio Engine

- [ ] App builds with JUCE audio modules. No warnings on audio-related code.
- [ ] App launches, opens WASAPI audio device at 44100 Hz / 512 samples.
- [ ] Spacebar starts and pauses playback correctly.
- [ ] `SoundBlock` with a WAV clip plays audio when transport is within the block's time window.
- [ ] Panning is correct: left-positioned block → louder in left ear; right-positioned → right ear.
- [ ] Distance attenuation is correct: farther block → quieter.
- [ ] Camera (listener) movement updates spatialization in real time, smoothly.
- [ ] Gain changes produce no clicks or zipper noise.
- [ ] 100 simultaneous blocks play without audio dropout for at least 60 seconds.
- [ ] Deleting a block during playback stops its audio within 1–2 frames, no crash.
- [ ] Loading an invalid WAV path produces a log warning and does not crash.
- [ ] Save/load round-trip preserves all scene data. Audio works correctly after load.
- [ ] JUCE allocation hook confirms zero heap allocations on audio thread during playback.
- [ ] No mutex is locked on the audio thread callback path.
- [ ] HUD displays active voice count and output peak level.
- [ ] Audio latency from play command to first sound ≤ 2 × buffer period (~23 ms).

---

*End of Master Execution Plan. This document is the single source of truth for audio engine implementation. All architectural decisions in this document are intentional. Deviations should be noted and justified in PR descriptions.*
