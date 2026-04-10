# SIME Bug & Issue Report

Generated from full codebase audit of all key source files.

---

## Editor / Lint — NOT Real Bugs

All 21 red-line errors in `AudioEngine.cpp` (and similar errors in other files) cascade from a single root cause:

```
'juce_audio_basics/juce_audio_basics.h' file not found
```

**Why:** Cursor's built-in clangd linter cannot find JUCE module headers because they are resolved through CMake at build time, not standard include paths. The actual MSVC build through CMake compiles fine.

**Fix:** Generate a `compile_commands.json` so clangd knows where JUCE headers live. Add this to the CMake configure step:

```powershell
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Then create a `.clangd` file in the project root pointing to it. This is purely an editor comfort fix — it does not affect the compiled application.

**Verdict: No code changes needed. The red lines are cosmetic.**

---

## Thread Safety Bugs (Real)

These are actual data races between the message thread and the GL/render thread.

### BUG-T1: `applyBlockEdit()` modifies GL-thread-owned data from message thread

**File:** `ViewPortComponent.h` line 61
**Severity:** High

`blockList` is documented as "owned and mutated exclusively on the GL thread." But `applyBlockEdit()` is called from `MainComponent`'s popup `onCommit` callback, which runs on the **message thread**. Meanwhile `renderOpenGL()` reads and writes `blockList` on the **GL thread** every frame.

This is an unguarded concurrent read/write — a data race.

### BUG-T2: `getTransportDuration()` iterates `blockList` from message thread

**File:** `ViewPortComponent.h` line 90
**Severity:** Medium

Called by `MainComponent::timerCallback()` at 30 Hz on the message thread. Iterates `blockList` which the GL thread is simultaneously mutating. Could read partially-written data or crash if the vector reallocates mid-iteration.

### BUG-T3: Transport control methods called cross-thread

**File:** `ViewPortComponent.h` lines 98–104
**Severity:** Low–Medium

`transportPlay()`, `transportPause()`, `transportStop()` are called from the message thread (via transport bar buttons) but modify `TransportClock` state that is read/updated by `renderOpenGL()` on the GL thread. `TransportClock` is documented as "not thread-safe." In practice the bool/double assignments are likely atomic on x86, but it's not formally safe.

### BUG-T4: `hasHit` / `currentHit` read cross-thread without lock

**File:** `ViewPortComponent.cpp` line 748, `ViewPortComponent.h` lines 179–181
**Severity:** Low

`keyPressed()` runs on the message thread and reads `hasHit` and `currentHit`, which are written exclusively on the GL thread in `doRaycast()`. No mutex protects this access. Unlikely to crash but could read stale or torn data.

---

## Audio Bugs

### BUG-A1: Test tone sample rate mismatch

**File:** `AudioEngine.cpp` line 48
**Severity:** Low

`generateTestTone()` uses hardcoded `44100.0` Hz. If the audio device runs at 48000 Hz (common on many Windows machines), the test tone will play ~9% higher in pitch than intended (e.g., 440 Hz tone sounds like ~480 Hz). The spatial Y-pitch mapping still works relatively, but absolute frequencies are off.

### BUG-A2: `activeVoices_.push_back()` can allocate on audio thread

**File:** `AudioEngine.cpp` line 229
**Severity:** Low (mitigated)

`handleStartEvent()` is called from the audio callback (via FIFO drain). If more than 32 voices are triggered simultaneously (exceeding the `reserve(32)` in `prepareToPlay`), `push_back` will trigger a heap allocation on the audio thread. Unlikely with < 32 blocks but violates strict real-time safety.

### BUG-A3: No voice deduplication on rapid stop/start

**File:** `AudioEngine.cpp` + `SequencerEngine.cpp`
**Severity:** Low

If the user rapidly stops and restarts transport, `resetAllBlocks()` clears `hasStarted` flags, so the next `update()` fires new Start events. But old voices from the previous playback may still be active (they get `stopping = true` and are removed next audio block, but there's a 1-block window where old and new voices overlap). For the MVP this causes a brief audio overlap, not a crash.

---

## Sequencer / Block Timing Bugs

### BUG-S1: All newly placed blocks start at time 0.0

**File:** `BlockEntry.h` line 29 + `ViewPortComponent.cpp` line 230
**Severity:** Medium (usability)

`BlockEntry::startTimeSec` defaults to `0.0` and is never auto-assigned when a block is placed. So if you place 10 blocks and hit Play, all 10 fire simultaneously at time 0. The user must manually edit each block's start time in the popup to stagger them.

This is technically "by design" but makes the app feel broken on first use — you place blocks, hit Play, and get a wall of simultaneous sound.

### BUG-S2: Pitch can only go UP, never down

**File:** `AudioEngine.cpp` line 219 + `ViewPortComponent.cpp` line 20
**Severity:** Low (design limitation)

The grid enforces `pos.y >= 0` (no blocks below the floor). Since pitch = `pow(2, Y/12)`, Y=0 is normal pitch and all higher positions are higher pitch. There is no way to pitch a block DOWN. This limits the musical range to "normal and above."

---

## UI / Polish Bugs

### BUG-U1: Debug alert dialogs on every startup

**File:** `Main.cpp` lines 19–21, 26–28
**Severity:** Low (annoyance)

Two `AlertWindow::showMessageBoxAsync` calls pop up every time the app launches:
- "initialise called"
- "window created"

These are leftover debug diagnostics and should be removed.

---

## Architecture / Code Quality Issues

### ISSUE-Q1: `BlockEntry.h` fragile include-order dependency

**File:** `BlockEntry.h`
**Severity:** Low (maintainability)

`BlockEntry.h` uses `Vec3i` but deliberately does not include `MathUtils.h`. Any file that includes `BlockEntry.h` without first including `MathUtils.h` will fail to compile. The header comment explains this, but it's a footgun for future development.

### ISSUE-Q2: `SidebarComponent::BlockEntry` shadows `::BlockEntry`

**File:** `SidebarComponent.h` line 9
**Severity:** Low

`SidebarComponent` defines its own nested `struct BlockEntry` with only `serial` and `pos` fields, separate from the top-level `BlockEntry`. This naming collision is confusing and requires explicit qualification everywhere.

---

## Summary Table

| ID | Category | Severity | File | One-line summary |
|----|----------|----------|------|-----------------|
| — | Editor lint | None | AudioEngine.cpp | Red lines from missing JUCE headers in clangd; not real errors |
| T1 | Thread safety | High | ViewPortComponent.h | `applyBlockEdit()` writes GL-owned data from message thread |
| T2 | Thread safety | Medium | ViewPortComponent.h | `getTransportDuration()` iterates GL-owned list from message thread |
| T3 | Thread safety | Low–Med | ViewPortComponent.h | Transport methods called cross-thread on non-thread-safe clock |
| T4 | Thread safety | Low | ViewPortComponent.cpp | `hasHit`/`currentHit` read cross-thread without lock |
| A1 | Audio | Low | AudioEngine.cpp | Test tone hardcoded at 44100; device may be 48000 |
| A2 | Audio | Low | AudioEngine.cpp | `push_back` on audio thread if >32 voices |
| A3 | Audio | Low | AudioEngine.cpp | Brief voice overlap on rapid transport stop/start |
| S1 | Sequencer | Medium | BlockEntry.h | All blocks start at time 0 — pile up on first beat |
| S2 | Sequencer | Low | AudioEngine.cpp | Pitch only goes up (Y >= 0), cannot go below normal |
| U1 | UI | Low | Main.cpp | Debug alert dialogs on every startup |
| Q1 | Code quality | Low | BlockEntry.h | Fragile include-order dependency |
| Q2 | Code quality | Low | SidebarComponent.h | Shadowed `BlockEntry` name |

---

## Recommended Fix Priority

1. **Remove debug alerts** (U1) — trivial, improves first impression
2. **Thread-safe block edits** (T1, T2) — highest risk; queue edits through the GL thread like placements already do
3. **Auto-stagger block start times** (S1) — or at least increment based on existing blocks
4. **Generate test tone at device sample rate** (A1) — query rate after `start()` or regenerate in `prepareToPlay`
5. Everything else is low priority for MVP
