# Audio Library System

**Feature:** Searchable, lazy-loaded sound library covering 13,759 WAV
samples across 23 block types.

**Companion doc:** This file is the **detailed** write-up for **Task 3** (in-app
audio library). **Task 1** (batch MP3→WAV) and **Task 2** (rename + generate
`sound_library.csv`) are offline scripts / data on disk; this report focuses
on how the C++ app consumes the CSV and `Sounds/` folder.

**Related README sections:** [Controls](../README.md#controls), [Workflow](../README.md#workflow), [Project structure](../README.md#project-structure), [Sound Library (Where to change things)](../README.md#sound-library).

**Files touched:** `BlockType.h`, `SoundLibrary.{h,cpp}` (new),
`SoundPickerComponent.{h,cpp}` (new), `BlockEditPopup.{h,cpp}`,
`MainComponent.{h,cpp}`, `ViewPortComponent.{h,cpp}`, `CMakeLists.txt`.

---

## 1. Why this matters

After Tasks 1 & 2 we ended up with a flat `Sounds/` directory of **13,759
WAV files** (~4.4 GB) and a master index `CSV/sound_library.csv` describing
every entry's instrument, note, duration, dynamic, articulation, key, and BPM.

Naively wiring this into the app would have been catastrophic:

* loading 4.4 GB of audio into RAM at startup would freeze (and likely
  crash) the application
* the existing 3-item dropdown (Violin / Piano / Drum) cannot scale to
  ~1,500 violin samples, let alone 13,759 across categories
* the toolbar can not realistically hold 23 separate buttons

This task replaces the prototype scaffolding with a **lazy-loaded, indexed,
searchable** library that scales to the full content set without paying
any RAM cost up-front.

---

## 2. Performance principles

| Principle                    | How it is implemented                                  |
| ---------------------------- | ------------------------------------------------------ |
| **Index, don't decode**      | Startup parses the CSV (text only). No WAV is touched.|
| **Lazy WAV load**            | A sample is decoded and pushed into `AudioEngine` only when the user commits an edit assigning that sound to a block (via `SoundLibrary::ensureLoaded`).|
| **Cache once**               | Each sample is keyed by a runtime `soundId`; subsequent plays reuse the in-engine buffer. No duplicate loads.|
| **Bucket by BlockType**      | `byType_[BlockType]` -> vector of entry indices. Switching the picker between types is O(1).|
| **Cheap search**             | Substring match across pre-parsed metadata fields. ~1,500 items per category × 8 fields takes <1 ms per keystroke.|
| **Virtualized list**         | `juce::ListBox` only paints the visible rows (~10 of them).|

The net effect: opening the app, picking a sound, and playing the scene is
interactive at all times. A typical user only ever decodes a handful of
WAV files into RAM, regardless of library size.

---

## 3. New / changed code

### 3.1 `Source/BlockType.h`

Expanded the enum from **4 entries** (Violin, Piano, Drum, Custom) to
**24 entries** (the 4 originals plus 19 new instruments) with a sentinel
`_Count` that keeps loops bounded.

Helpers added:

* `blockTypeName(t)`            — internal tag used by `sound_library.csv`
* `blockTypeDisplayName(t)`     — friendly version (e.g. *Bass Clarinet*)
* `blockTypeFromName(s)`        — string -> enum (CSV reverse mapping)
* `blockTypeColor(t)`           — distinct color per type (used by the
  voxel renderer and the toolbar pill)
* `blockTypeCategory(t)`        — Synth / Strings / Woodwinds / Brass /
  Percussion / Special
* `blockCategoryName(c)`        — string for combo box section headers
* `blockTypesByCategory(cat)`   — **iterates types ordered by category**.
  This is what fixes the "Violin appears in its own Strings group" bug.

### 3.2 `Source/SoundLibrary.{h,cpp}` (new)

```cpp
struct SoundEntry {
    juce::String relativePath;     // "Instruments/violin/Violin_A3_…"
    juce::File   fullPath;          // resolved against Sounds/ root
    BlockType    blockType;
    juce::String instrument, note, duration, dynamic, articulation, key, bpm;
    juce::String displayName;       // pre-built primary line
    juce::String displaySub;        // pre-built secondary line
    int          soundId = -1;      // assigned on first ensureLoaded()
};

class SoundLibrary {
    bool load(const juce::File& csvFile, const juce::File& soundsRoot);
    const std::vector<int>& indicesFor(BlockType) const;
    std::vector<int> search(BlockType, const juce::String& query) const;
    int findByRelativePath(const juce::String& relPath) const;
    int ensureLoaded(int entryIdx, AudioEngine& engine);
};
```

* `load()` reads `CSV/sound_library.csv` once. CSV is parsed with a
  hand-rolled splitter that respects quoted fields (no JUCE CSV reader
  is available).
* The class owns three indices: by type, by relative path, and by runtime
  soundId (filled lazily as samples are decoded).
* `ensureLoaded()` is the single point that ever calls
  `AudioEngine::loadSample()`. If the engine already has the buffer it
  returns the existing id; otherwise it allocates a new id starting at
  10000 (well above the synth-fallback id range 100/200/300).

### 3.3 `Source/SoundPickerComponent.{h,cpp}` (new)

Compact `juce::Component` + `juce::ListBoxModel` wrapping:

* a `juce::TextEditor` for live search (placeholder: "Search sounds…")
* a small "*N sounds*" hint label
* a virtualized `juce::ListBox` with **40-pixel two-line rows**:
  * Line 1 (bold): NOTE  DYNAMIC  ARTICULATION
  * Line 2 (muted): duration · key · bpm · instrument
  * Color stripe in the block-type signature color along the left edge
  * Selected: full-width row highlight in accent blue plus a brighter stripe

`onSelectionChanged(entryIdx)` fires when the user clicks a row.
`onDoubleClick(entryIdx)` triggers immediate apply (the popup wires it
to `commit()`).

The picker forwards the entry index — never a raw soundId — so the WAV
is not decoded until the user actually commits the edit.

### 3.4 `Source/BlockEditPopup`

Bigger and cleaner than the prototype:

* 440 × 520 (was 260 × 260)
* Color-coded type badge in the header in the active block-type color
* Top accent stripe in the block-type color
* Start + Duration share a single row
* Picker fills the remaining vertical space
* Apply / Cancel pinned to the bottom of the popup

`commit()` passes the **absolute path** of the picked library WAV through
the existing `customFile` parameter; `ViewPortComponent::applyBlockEdit()`
detects whether that path lives under `Sounds/` and routes it through the
library cache (full path → relative → `library_.findByRelativePath()` →
`ensureLoaded()`).

### 3.5 `Source/MainComponent`

* Removed the four per-type buttons (Violin / Piano / Drum / Custom).
* Replaced with a single grouped `juce::ComboBox` that lists all 23
  block types under section headers (Synth, Strings, Woodwinds, Brass,
  Percussion, Special). The dropdown iterates **categories first**, then
  the types within each — eliminating the duplicate-section-header bug.
* Added a small **TypePill** Component that paints a color swatch + the
  active type's name, sitting to the left of the combo. Keeps the
  visual cue from the old colored buttons.
* All teammate transport-bar wiring is preserved
  (`onBlockEdited`, `setTimelinePlaying`, `setBlocks`, `getCurrentHeight`,
  `setPlaybackUiState`, `stopPlaybackAndResetUi`, `updateBlockTiming`).

### 3.6 `Source/ViewPortComponent`

* New member `SoundLibrary library_`. Loaded inside
  `newOpenGLContextCreated()` after the synth presets are generated.
* `soundLibrary()` accessor exposes it to `BlockEditPopup` so the popup
  can browse the index without touching audio internals.
* `applyBlockEdit()` now resolves library paths into a runtime soundId
  and stores the **relative** path on the block (so saves stay portable).
* `loadScene()` understands both flavors of `customFilePath`:
  absolute (legacy Custom WAV) or relative (library entry).
* `getBlockColor()` was simplified to delegate to `blockTypeColor()` for
  every type except `Custom`, which keeps its per-soundId palette.

---

## 4. Save/load compatibility

`BlockEntry::customFilePath` is still the only string field serialized by
`SceneFile`. The semantic was extended:

| Path form                                   | Means                       |
| ------------------------------------------- | --------------------------- |
| empty                                       | use the existing soundId    |
| relative ("Instruments/violin/Violin_A3…")  | library entry — resolved on load via `findByRelativePath`|
| absolute ("C:\\Users\\…\\my-sample.wav")    | user-imported WAV (legacy Custom flow)|

Existing `.sime` files still load. New saves are smaller because library
entries store ~50-byte relative paths instead of full Windows paths.

---

## 5. Threading

| Thread                  | What can run there                          |
| ----------------------- | ------------------------------------------- |
| message thread          | `SoundLibrary::*`, `ensureLoaded`, picker UI|
| GL thread               | per-frame edits applied via `pendingBlockEdit_` |
| audio thread            | reads `sampleLibrary_` only — never modifies|

Sample decoding happens on the **message thread** (inside `applyBlockEdit`
before queueing the GL-thread update). The audio callback never blocks;
if a soundId is ever missing from `sampleLibrary_` the callback simply
drops the event (safe fallback).

---

## 6. UX notes

* The toolbar shows: `[● Active Type Name] [▼ All 23 types]` and the
  existing file buttons on the right.
* The combo's section headers are rendered by JUCE without selectable
  items — clicking a heading does nothing, picking a type immediately
  switches the active block flavor.
* When the user clicks a placed block in edit mode, the popup opens with
  the picker pre-scrolled to the previously-selected sound (if any).
* Custom blocks still get their "Browse…" file picker — nothing was
  removed from the existing user-WAV flow.

---

## 7. Quick test recipe

1. Build & launch SIME (from VS, double-clicking the `.exe`, or a terminal — the app resolves the repo folder automatically if `Sounds/` + `CSV/` live above `build/`).
2. Open the **Block Type** combo at the top → pick *Strings → Banjo*.
3. Place a banjo block in the scene.
4. Hit `E` → right-click the new block.
5. The edit popup shows a scrollable list of every banjo sample. Type
   `A4` in the search box — the list shrinks to A4 entries instantly.
6. Pick one (or double-click to instantly apply), hit Apply, hit Play.
   The chosen WAV is decoded on the spot, future plays of that sample
   are cached.

---

## 8. Numbers

* **23** block types (was 4)
* **6** categories grouping the types in the dropdown
* **13,759** library entries indexed at startup
* **0** WAV files decoded until a block is assigned a sound
* Search across ~1,500 violin entries: **<1 ms** per keystroke
* CSV parse + indexing cost on cold launch: **well under 250 ms**

---

## 9. Content root path (why teammates saw an empty picker)

Older builds used **`getCurrentWorkingDirectory()`** only. That breaks when:

* Someone double-clicks **`SIME.exe`** (CWD is often `...\Debug\`, not the repo root).
* Visual Studio starts the debugger with CWD set to **`$(OutDir)`** (same problem).

The picker stays empty because **`CSV/sound_library.csv`** was never found, so `SoundLibrary::load` never ran — even if `Sounds/` existed elsewhere on disk.

**Fix (current code):** `resolveContentRoot()` in `ViewPortComponent.cpp` walks **up** from:

1. Current working directory, and  
2. The directory containing the executable,

until it finds a folder that contains **both** `Sounds/` (directory) and **`CSV/sound_library.csv`** (file). That folder becomes `contentRoot_`. Library loading and `applyBlockEdit()` library detection both use `contentRoot_/Sounds`.

### Troubleshooting (sound picker empty)

| Check | Action |
| ----- | ------ |
| Missing CSV | **Both** `Sounds/` **and** `CSV/sound_library.csv` must live beside each other (usually repo root). WAVs alone are not enough — the index comes from the CSV. |
| Wrong layout | Don’t put only `Sounds/` under `build/` unless you also copy `CSV/` there; normal layout keeps both under `sime/`. |
| Still broken | Run a **Debug** build and watch the IDE **Output** window — look for `SoundLibrary: content root = ...` and whether loading succeeded. |
| Stale exe | After pulling, **rebuild** (`cmake --build ...`). |
