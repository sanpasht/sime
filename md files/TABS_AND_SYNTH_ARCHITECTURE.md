# Workspace Tabs & Synthesizer — Architecture & Details

A dedicated deep-dive into Ambitious Feature **E**: the workspace **tab system**
(Scene / Timeline / Synthesizer), the full-screen **Timeline** tab, and the
**Synthesizer** tab (subtractive synth → WAV → new **Synth** block type).

This document is implementation-level. For the user-facing walkthrough see the
[README](../README.md); for the chronological change log see
[SESSION_2026-06-03_REPORT.md](SESSION_2026-06-03_REPORT.md).

---

## Table of Contents

1. [Overview](#1-overview)
2. [Workspace tab system](#2-workspace-tab-system)
3. [The OpenGL "off-screen park" trick](#3-the-opengl-off-screen-park-trick)
4. [Timeline tab](#4-timeline-tab)
5. [Synthesizer — data & DSP](#5-synthesizer--data--dsp)
6. [Synthesizer — UI](#6-synthesizer--ui)
7. [Synth block type](#7-synth-block-type)
8. [End-to-end data flow](#8-end-to-end-data-flow)
9. [Threading model](#9-threading-model)
10. [File / symbol index](#10-file--symbol-index)
11. [Extension guide](#11-extension-guide)
12. [Known limitations](#12-known-limitations)

---

## 1. Overview

Before this feature the app was a single screen: a 3D viewport with a left
sidebar and a bottom transport/timeline bar. Feature E turns that into a
**three-tab workspace**:

| Tab | Purpose |
|-----|---------|
| **Scene** | The original 3D voxel editor (sidebar + toolbar + viewport + collapsible transport bar). |
| **Timeline** | A full-screen, FL/Ableton-style timeline — the same transport + region editing as the Scene bar, blown up to fill the window. |
| **Synthesizer** | A dark-techno subtractive synth that designs sounds, previews them, and exports WAVs that can be assigned to **Synth** blocks. |

Design goals:

- **One source of truth.** Both timelines drive the *same* transport clock and
  edit queues — no duplicated playback state.
- **Engine never stops.** Switching tabs must not interrupt playback or drop
  queued edits.
- **Offline synthesis.** The synth must not add real-time DSP load or risk to
  the audio thread; it renders buffers offline and reuses the proven
  sample-playback path.
- **Reuse the Custom-block pipeline** for assigning synth WAVs.

---

## 2. Workspace tab system

Owned entirely by `MainComponent`.

```cpp
enum class WorkspaceTab { Scene = 0, Timeline = 1, Synth = 2 };
WorkspaceTab activeTab_ = WorkspaceTab::Scene;

juce::TextButton sceneTabBtn_, timelineTabBtn_, synthTabBtn_;   // top strip
TransportBarComponent timelineTabBar_;   // full-screen Timeline tab
SynthComponent        synthPanel_;       // Synthesizer tab
```

### `switchTab(WorkspaceTab t)`

1. `activeTab_ = t`.
2. `setSceneChildrenVisible(t == Scene)` — toggles the Scene toolbar buttons,
   sidebar, sidebar resizer, and bottom transport bar.
3. `timelineTabBar_.setVisible(t == Timeline)`, `synthPanel_.setVisible(t == Synth)`.
4. When switching **to** Timeline: `refreshTimelines()` + `setPlaybackUiState(...)`
   so the big timeline immediately reflects current blocks and playhead.
5. `updateTabButtonStates()` (highlights the active tab) → `resized()` → `repaint()`.

### `resized()` per-tab layout

- A fixed-height tab strip is laid out at the very top every frame.
- **Non-Scene tabs:** the viewport is parked off-screen (see §3); the active
  tab's content (`timelineTabBar_` or `synthPanel_`) fills the area below the
  strip and is brought to front.
- **Scene tab:** the original layout (sidebar + resizer + bottom transport +
  toolbar + viewport) is restored.

---

## 3. The OpenGL "off-screen park" trick

**This is the single most important detail of the feature.**

`ViewPortComponent::renderOpenGL()` is not just rendering — it is the heartbeat:

```
renderOpenGL():
    transportClock.update(dt)              // advances time
    ... drain pending edit/clipboard/path/freeze queues ...
    sequencer.update(...) → audioEngine.processEvents(...)   // fires audio
    blockListSnapshot_ = blockList         // publishes a message-thread copy
```

The loop runs via `openGLContext.setContinuousRepainting(true)`. Two JUCE facts
make hiding the viewport on other tabs dangerous:

1. **OpenGL composites *above* normal components.** You cannot cover the 3D view
   by placing the timeline/synth UI in front of it.
2. **JUCE detaches the GL context when the target component's width or height is
   0** (`canBeAttached()` requires `w > 0 && h > 0 && isShowing`). A detached
   context stops calling `renderOpenGL()` — which **freezes the transport clock,
   the sequencer, and the edit queues**.

So both "hide it" and "size it 0×0" break playback and editing on the
Timeline/Synth tabs. The fix:

```cpp
// resized(), non-Scene tabs:
view.setInterceptsMouseClicks(false, false);
view.setBounds(-1000, -1000, 1, 1);   // attached (1×1) but off-screen & unseen
```

Plus a steady nudge so the loop keeps a regular cadence even when nothing else
triggers a repaint:

```cpp
// timerCallback(), non-Scene tabs:
if (activeTab_ != WorkspaceTab::Scene)
    view.nudgeEngineLoop();             // openGLContext.triggerRepaint()
```

Result: the engine keeps ticking on every tab, the 3D scene is neither drawn nor
clickable off-tab, and playback stays perfectly in sync across tab switches.

---

## 4. Timeline tab

The Timeline tab is a **second `TransportBarComponent`** (`timelineTabBar_`),
deliberately reusing the Scene bar's component so behaviour is identical.

### Forcing full-screen

```cpp
timelineTabBar_.setCollapsible(false);   // hides collapse button, locks expanded
```

`TransportBarComponent::setCollapsible(false)` hides the collapse toggle and
pins `isCollapsed_ = false`, so the embedded `TimelineComponent` always shows.

### Shared wiring

Both bars are wired through one helper so they are interchangeable:

```cpp
wireTimelineCallbacks(transportBar);     // Scene bar
wireTimelineCallbacks(timelineTabBar_);  // Timeline tab
```

`wireTimelineCallbacks(bar)` routes every control to shared logic:

| Callback | Routed to |
|----------|-----------|
| `onPlay/onPause/onStop` | `doTransportPlay/Pause/Stop()` (one clock) |
| `onSpeedChanged` | `view.setPlaybackRate()` |
| `onPlayheadMoved` | `view.seekTransportClock()` |
| `onBlockEdited` | `view.updateBlockTiming()` + refresh |
| `onRegionEdited` | `view.updateBlockTimeRange()` + refresh |
| `onRegionDuplicated` | `view.addTimeRangeToBlock()` + refresh |
| `onDeleteBlockOrRegion` | `view.deleteBlockOrRegion()` + refresh |
| `onTimelineBlockClicked` | `view.highlightBlock()` + sidebar info |

### Keeping both bars in sync

- `setPlaybackUiState(playing, paused, time)` pushes `(state, time, duration)` to
  **both** bars every timer tick and repaints whichever tab is visible, so the
  playhead animates smoothly and a mid-song tab switch is seamless.
- The active bar's `setBlocks()` is refreshed each tick (see the stale-snapshot
  bug in §9 of the session report) so newly placed/edited regions show promptly.

### Deleting regions

Region delete is the **right-click → Delete** item in `TimelineComponent`
(`onDeleteBlockOrRegion`). It works identically on both bars.

---

## 5. Synthesizer — data & DSP

The synth is **offline-first**: it renders an audio buffer from parameters, then
either auditions it through the existing engine or writes it to disk. No new
code runs on the audio thread.

### `SynthPatch` (`SynthPatch.h`)

A plain parameter struct:

```cpp
struct SynthPatch {
    enum class Waveform { Sine, Square, Saw, Triangle };
    Waveform waveform     = Waveform::Saw;
    int      midiNote     = 60;       // middle C
    double   durationSec  = 2.0;      // held length before release
    float    attackSec, decaySec, sustainLevel, releaseSec;   // ADSR
    float    filterCutoffHz, filterResonance;                 // low-pass
    float    masterGain;
    static float midiToHz(int midi);  // 440 * 2^((n-69)/12)
};
```

### `SynthRenderer` (`SynthRenderer.cpp/h`)

Pure DSP, no component dependencies.

**`render(patch, sampleRate) → juce::AudioBuffer<float>` (mono):**

1. **Oscillator** — per-sample sine / square / saw / triangle at `midiToHz`.
2. **ADSR amp envelope** — a sample-accurate state machine
   (`Attack → Decay → Sustain → Release → Done`). It renders the held
   `durationSec`, then the release tail; the buffer is trimmed the instant the
   envelope hits zero, and a short fade is applied to avoid an end click.
3. **Filter** — a TPT (topology-preserving transform) 2-pole resonant low-pass;
   `filterResonance` (0..1) maps to Q ≈ 0.707…8.
4. Output is hard-limited to [-1, 1].

**`writeWav(buffer, file, sampleRate) → bool:**

16-bit PCM via `juce::WavAudioFormat::createWriterFor` over a
`juce::FileOutputStream` + `writeFromAudioSampleBuffer`. Creates the parent
directory if needed.

### Engine hook

```cpp
void AudioEngine::setSampleBuffer(int soundId, juce::AudioBuffer<float> buffer);
```

Registers an in-memory buffer directly in the sample library (no file decode).
Used for preview under a reserved id (`ViewPortComponent::kSynthPreviewSoundId
= 9998`).

`ViewPortComponent`:

- `previewSynthBuffer(buf)` — `setSampleBuffer(9998, buf)` then `processEvents`
  a `Start` event at the origin → audible immediately.
- `exportSynthBufferToWorkspace(buf, baseName)` — `SynthRenderer::writeWav` to
  `workspaceAudios/<name>.wav` (dedup via `getNonexistentChildFile`), refresh the
  sidebar Audio list, return the portable relative path.

---

## 6. Synthesizer — UI

`SynthComponent` is a full-window dark-techno surface modelled on a hardware
mockup. It is composed from small, reusable, custom-painted classes.

### Building blocks

| Class | Role |
|-------|------|
| `SynthLookAndFeel` | Glowing layered-arc rotary knobs, neon vertical bar sliders, dark combo styling. Per-control **accent colour** is read from a `"accent"` component property, so one L&F serves red/cyan/purple/green knobs. |
| `SynthPanel` | Dark rounded panel with an accent header strip + underline; pure chrome (controls are siblings drawn on top). |
| `KnobCell` | Rotary `Slider` + caption label. |
| `BarCell` | Vertical bar `Slider` + caption (ADSR, wheels, performance). |
| `WaveThumb` | Mini waveform preview (sine/square/saw/triangle/random/noise). |
| `EnvCurve` | Live ADSR curve drawn from the four amp sliders. |
| `FilterCurve` | Live low-pass response drawn from cutoff + resonance. |
| `StepBars` | Arp/seq step visualiser. |
| `XYPad` | Performance / aftertouch pad. |
| `LevelMeter` | Segmented master meter. |
| `MiniKeyboard` | 3-octave click keyboard; `onNoteOn(midi)` callback. |

### Layout

Every panel and control is positioned from **reference coordinates** (a
1024×600 design space) scaled by `getWidth()/1024 × getHeight()/600`. So the
arrangement is preserved at any window size. Panels are added first (painted
behind); interactive controls are siblings placed on top.

Panels present: **Oscillators** (OSC 1/2/3, Sub, Noise), **Mixer**,
**Amp Envelope**, **Filter**, **Modulation** (LFO 1/2), **Effects**
(Chorus/Delay/Reverb/Distortion), **Voice** (LFO 3), **Mod Matrix**,
**Arp/Seq**, **Performance**, **Master** (+meter), **Macro**, plus pitch/mod
wheels and a header (SIME logo, SYNTHESIZER title, **Enter File Name** field,
**Preview**, **Export WAV**, status line).

### Functional vs decorative

To keep the renderer focused, a subset of controls drives real sound; the rest
are styled, interactive performance surfaces (a deliberate extension point).

| Wired to DSP | Decorative (visual only, for now) |
|--------------|-----------------------------------|
| OSC 1 waveform | OSC 2 / 3, Sub, Noise |
| Amp envelope A / D / S / R | Mixer knobs |
| Filter Cutoff + Resonance | LFO 1–3, Mod Matrix |
| Master level | Effects, Macros |
| On-screen keyboard (sets note → preview) | Arp/Seq, Performance, wheels |

`sliderValueChanged()` calls `syncPatchFromUi()` and repaints `EnvCurve` /
`FilterCurve` / `LevelMeter` live.

### Header note

The reference mockup's `SYNTH / FX / MATRIX / PRESETS` tabs were intentionally
**removed** — they were decorative and there are no such sub-screens (effects and
the mod matrix already live inline; there is no preset database). The header now
simply reads **SYNTHESIZER**.

---

## 7. Synth block type

`BlockType::Synth` is appended **before `_Count`** so existing `.sime` files
(which store `blockType` as a `uint8`) remain loadable — **no `SceneFile`
version bump** was required.

| Concern | Value |
|---------|-------|
| `blockTypeName` / `blockTypeDisplayName` | `"Synth"` |
| `blockTypeColor` | synth purple (`0xff9b59f6`) |
| `blockTypeCategory` | `BlockCategory::Synth` (shows in toolbar dropdown) |
| `blockTypeDefaultSoundId` | `-1` (silent until assigned) |
| `SoundLibrary::defaultSoundForBlockType` | returns `-1`, like Custom |
| `BlockEditPopup` | treats Synth like Custom: hides the library picker, shows **Browse File** |
| `BlockEntry::getBlockColor` | per-`soundId` palette variation (distinct blocks per patch) |

The assign flow is identical to Custom: the chosen WAV is copied into
`workspaceAudios/`, the portable `customFilePath` is stored, and the sequencer
plays it like any other sample with full spatial audio.

---

## 8. End-to-end data flow

```
[Synthesizer tab]
   user tweaks knobs / clicks a key
        │  syncPatchFromUi()
        ▼
   SynthPatch ──► SynthRenderer::render() ──► AudioBuffer<float> (mono)
        │                                        │
        │ Preview                                │ Export WAV
        ▼                                        ▼
   onPreview(buf)                          onExport(buf, name)
        │                                        │
   view.previewSynthBuffer(buf)            view.exportSynthBufferToWorkspace(buf,name)
        │                                        │
   AudioEngine.setSampleBuffer(9998,buf)   SynthRenderer::writeWav → workspaceAudios/<name>.wav
   processEvents(Start@origin)             refreshWorkspaceAudioPanel()  (sidebar Audio list)
        │                                        │
   audible preview                         file ready to assign
                                                 │
[Scene tab]                                      ▼
   place a Synth block → RMB → Browse File → pick the exported WAV
        │
   ViewPortComponent::applyBlockEdit() → copyAudioToWorkspace + loadSample
        │
   sequencer plays the block's WAV with spatial audio, movement, scheduling, etc.
```

---

## 9. Threading model

- **Message thread:** all UI (tabs, synth controls), `SynthRenderer::render` /
  `writeWav` (offline, synchronous on the click), `setSampleBuffer`, and edit
  callbacks. `setSampleBuffer` is only ever called before firing the preview
  event, mirroring the existing "load samples on the message thread" rule.
- **GL thread (`renderOpenGL`):** transport clock, sequencer, audio-event
  dispatch, edit-queue draining, and publishing `blockListSnapshot_`.
- **Audio thread:** unchanged — mixes pre-rendered buffers from the sample
  library. The synth adds **no** audio-thread code; preview just plays a buffer
  like any other sample.

Cross-tab safety relies on the off-screen-park trick (§3) so the GL thread keeps
running regardless of the active tab.

---

## 10. File / symbol index

| File | Key symbols |
|------|-------------|
| `MainComponent.h/.cpp` | `WorkspaceTab`, `switchTab`, `setSceneChildrenVisible`, `wireTimelineCallbacks`, `doTransportPlay/Pause/Stop`, `refreshTimelines`, `setPlaybackUiState`, `timelineTabBar_`, `synthPanel_` |
| `TransportBarComponent.h/.cpp` | `setCollapsible()` |
| `ViewPortComponent.h/.cpp` | `nudgeEngineLoop`, `previewSynthBuffer`, `exportSynthBufferToWorkspace`, `kSynthPreviewSoundId`, `copyAudioToWorkspace`, `refreshWorkspaceAudioPanel` |
| `AudioEngine.h/.cpp` | `setSampleBuffer` |
| `SynthPatch.h` | `SynthPatch`, `Waveform`, `midiToHz` |
| `SynthRenderer.h/.cpp` | `render`, `writeWav` |
| `SynthComponent.h/.cpp` | `SynthLookAndFeel`, `SynthPanel`, `KnobCell`, `BarCell`, `WaveThumb`, `EnvCurve`, `FilterCurve`, `StepBars`, `XYPad`, `LevelMeter`, `MiniKeyboard`, `SynthComponent` |
| `BlockType.h` | `BlockType::Synth` and all its switch arms |
| `BlockEditPopup.cpp` | Custom/Synth file-browse branch |
| `SoundLibrary.cpp` | `defaultSoundForBlockType` (Synth → -1) |
| `CMakeLists.txt` | adds `SynthRenderer.cpp`, `SynthComponent.cpp` |

---

## 11. Extension guide

### Wire a decorative synth panel into the sound

1. Add the parameter(s) to `SynthPatch`.
2. Read them in `SynthRenderer::render` (e.g. mix a 2nd oscillator, add an LFO
   that modulates cutoff per sample, apply a delay line after the filter).
3. In `SynthComponent::syncPatchFromUi`, copy the relevant slider/combo values
   into `patch_`; if it should update a live preview/curve, repaint in
   `sliderValueChanged`.

### Add another workspace tab

1. Extend `WorkspaceTab`, add a tab button + a content component.
2. Handle it in `switchTab`, `setSceneChildrenVisible` (leave Scene children
   hidden), and `resized` (park the viewport off-screen like the other non-Scene
   tabs so the engine keeps running).

### Persist synth patches

Serialise `SynthPatch` (it's a flat struct) to a small file or into the scene,
and add a preset browser. This would justify reinstating a "Presets" concept.

---

## 12. Known limitations

- **Mono, single-voice render.** No polyphony or stereo in the rendered WAV.
- **Decorative panels** (extra oscillators, LFOs, effects, mod matrix, macros,
  performance, wheels) are not yet routed into the DSP.
- **No patch save/load** — designed sounds live only as exported WAVs.
- **Preview is one-shot** — clicking a key plays the full envelope; there is no
  hold-to-sustain or release-on-mouse-up yet.
- **Sample rate** — the renderer defaults to 44.1 kHz; on a 48 kHz device a
  preview buffer plays back at the engine's rate (same caveat as the other
  procedurally generated tones).
