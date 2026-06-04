// ─────────────────────────────────────────────────────────────────────────────
// HelpPopup.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "HelpPopup.h"

namespace
{
    constexpr juce::uint32 kBgColor     = 0xf012141cu;
    constexpr juce::uint32 kBorderColor = 0xff2c3550u;
    constexpr juce::uint32 kAccentColor = 0xff5b7ce6u;
    constexpr juce::uint32 kHeaderColor = 0xff5b7ce6u;
    constexpr juce::uint32 kKeyColor    = 0xffffc24c;
    constexpr juce::uint32 kTextColor   = 0xffe2e6f2u;
    constexpr juce::uint32 kDimColor    = 0xff8b94adu;

    constexpr int kPanelW = 580;
    constexpr int kPanelH = 640;
    constexpr int kPad    = 16;
    constexpr int kTitleH = 32;     // pixel band reserved for the title
    constexpr int kFooterH= 52;     // pixel band reserved for the Close row
}

// ─────────────────────────────────────────────────────────────────────────────
// HelpPopup::Body
// ─────────────────────────────────────────────────────────────────────────────

int HelpPopup::Body::measureHeight(int width)
{
    // Use a TextLayout so we know exactly how tall the AttributedString
    // will be at the given wrap width.  +12 trailing padding keeps the
    // last line clear of the bottom of the viewport.
    juce::TextLayout layout;
    layout.createLayout(body_, (float) width);
    return (int) std::ceil(layout.getHeight()) + 12;
}

void HelpPopup::Body::paint(juce::Graphics& g)
{
    body_.draw(g, getLocalBounds().toFloat());
}

// ─────────────────────────────────────────────────────────────────────────────
// HelpPopup
// ─────────────────────────────────────────────────────────────────────────────

HelpPopup::HelpPopup()
{
    setSize(kPanelW, kPanelH);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(closeBtn_);
    closeBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kAccentColor));
    closeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    closeBtn_.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    // ── Build the AttributedString body ─────────────────────────────────────
    const juce::Font hdrFont (juce::Font("Public Sans", 13.5f, juce::Font::bold));
    const juce::Font keyFont (juce::Font("Public Sans", 12.5f, juce::Font::bold));
    const juce::Font txtFont (juce::Font("Public Sans", 12.5f, juce::Font::plain));
    const juce::Font dimFont (juce::Font("Public Sans", 11.5f, juce::Font::italic));

    juce::AttributedString s;
    s.setLineSpacing(2.f);
    s.setWordWrap(juce::AttributedString::WordWrap::byWord);

    auto hdr = [&](const juce::String& t)
    {
        s.append("\n" + t + "\n", hdrFont, juce::Colour(kHeaderColor));
    };
    auto row = [&](const juce::String& keys, const juce::String& what)
    {
        s.append("  " + keys + "  ", keyFont, juce::Colour(kKeyColor));
        s.append(what + "\n",         txtFont, juce::Colour(kTextColor));
    };
    auto note = [&](const juce::String& t)
    {
        s.append("    " + t + "\n", dimFont, juce::Colour(kDimColor));
    };

    // ── Section: Mouse & navigation ─────────────────────────────────────────
    hdr("Mouse & navigation");
    row("RMB (drag)",         "Rotate camera / look around (free-fly)");
    row("W / A / S / D",      "Move forward / left / back / right (level)");
    row("Space",              "Move up");
    row("Ctrl",               "Move down");
    row("Mouse wheel",        "Zoom / dolly");
    row("Shift + Wheel",      "Raise / lower the mid-air placement plane");
    row("Home",               "Reset camera to (8, 8, 8)");
    row("Front / Back / Left / Right",
                              "View-gizmo buttons (top-right) snap to axis-aligned views");

    // ── Section: Placing & editing blocks ───────────────────────────────────
    hdr("Placing & editing blocks");
    row("LMB",                "Place a block at the hovered cell");
    row("Shift + LMB",        "Place a block in mid-air (Y plane set by Shift+Wheel)");
    row("RMB click (no drag)","Remove the hovered block");
    row("Backspace",          "Remove the block under the cursor");
    row("C",                  "Clear the whole scene (asks for confirmation)");
    row("Tab",                "Toggle Edit Mode");
    note("In Edit Mode the scene tints dark-red and blocks glow yellow.");
    row("RMB (edit mode)",    "Open the Edit popup for the clicked block");
    row("LMB (edit mode)",    "Select a block (sidebar opens its Info panel)");
    row("LMB drag (edit)",    "Rubber-band multi-select (cyan)");
    row("Shift + LMB (edit)", "Toggle a block in / out of the selection");
    row("Alt + LMB drag",     "Record movement keyframes by dragging the block");
    note("Multi-segment: move the playhead to a later time first, then Alt+drag again to");
    note("record an ADDITIONAL movement segment starting there (the prior path is kept and");
    note("the block holds between segments).  Align a segment with a scheduled sound's time");
    note("to give that sound its own movement.  Cancelling only drops the new segment.");
    note("Recording a movement no longer cuts the sound - the region is only extended to");
    note("cover the motion; set the sound duration yourself.");
    row("Drag a coloured arrow",
                              "Move the selected block along one axis (gizmo)");
    row("Esc",                "Clear the multi-selection");
    note("Block Edit popup (RMB in edit mode): pick a new sample (filterable by note,");
    note("dynamic, articulation), open the Position Keyframes editor, toggle visibility,");
    note("set loop / playback mode, or change start time / duration.");

    // ── Section: Selection clipboard & save ─────────────────────────────────
    hdr("Selection clipboard & save");
    row("Ctrl + C",           "Copy the current selection (primary + multi-select)");
    row("Ctrl + V",           "Paste at next free cell (translated +X)");
    row("Ctrl + A",           "Select every block in the scene (cyan highlight)");
    row("Ctrl + Z",           "Undo the last placement");
    row("Ctrl + S",           "Save the scene");
    note("Selected block highlights: orange = the primary selection, cyan = multi-select,");
    note("green pulse = currently emitting a sound (so you can tell selection vs playback).");
    note("File menu: New, Open, Save, Save As, Export Audio.  Scenes auto-save every");
    note("60 seconds to a recoverable backup.");

    // ── Section: Camera (listener) path ─────────────────────────────────────
    hdr("Camera (listener) path");
    row("R",                  "Toggle camera-path recording (works playing OR paused)");
    note("When paused, keyframe times start from the current playhead and advance with");
    note("wall-clock; play/pause/seek shows you a live camera preview from the path.");
    note("The path auto-follows during playback once it contains at least one keyframe;");
    note("recording always overrides following.  Toggle the toolbar Free Cam pill to");
    note("regain manual camera control without clearing the path.");
    note("Path editor (toolbar 'Path...'):");
    note("  + Hold @ cam now : append a Hold keyframe at the next free time slot");
    note("                     (= last keyframe's time + its HOLD (s) duration).");
    note("  HOLD (s) column  : how long a static Hold pose occupies the timeline.");
    note("  MODE column      : Hold = freeze + snap cut, Lerp = smooth interpolation.");
    note("  Capture every    : 0.1 / 0.25 / 0.5 / 1 / 2 / 5 s — recording sample rate.");
    note("  Clear All / X    : drop a row or wipe the draft (only commits on Apply).");

    // ── Section: Transport & audio ──────────────────────────────────────────
    hdr("Transport & audio");
    row("Play / Pause",       "Transport bar pill (left).  Space-bar shortcut.");
    row("Stop",               "Rewinds the playhead and snaps recorded paths back to start.");
    row("Speed dropdown",     "0.25x / 0.5x / 0.75x / 1x / 2x / 3x — change before or during play");
    row("Time field",         "Click + type a time (10 or 1:23 or 0:01:23), Enter to jump.  Esc cancels.");
    row("Timeline drag",      "Scrub the playhead; camera preview follows the path while you drag.");
    row("BPM field + Tap",    "Lay down a grid for visual subdivisions on the timeline.");
    note("Toolbar toggles: Doppler (pitch shift on moving voices),");
    note("                 Anchor (freeze listener at the current camera pose),");
    note("                 Free Cam (override camera-path follow, keep path data),");
    note("                 Freeze Move (all blocks hold position; un-freeze resumes");
    note("                              motion from the current time, audio unaffected).");
    note("Selected-block audition (need a block selected, in Edit Mode):");
    note("  Play   : preview the sound the block would be making at the current");
    note("           playhead time (picks the latest scheduled sound that has");
    note("           started, else the block's main sound).");
    note("  @Time  : move the playhead to the block's start time (does NOT play -");
    note("           press Play yourself; blocks + camera snap to that moment).");
    note("Toolbar menu:    Layers (Floor / Walls / Move arrows visibility).");
    note("Toolbar slider:  Spatial sensitivity 0.25 (gentle) ... 3.0 (aggressive).");

    // ── Section: Sidebar Info panel ─────────────────────────────────────────
    hdr("Sidebar Info panel (click a block to open)");
    note("BLOCKS / INFO tabs: scene list vs the per-block editor for the active selection.");
    note("Drag the sidebar's right edge to make it wider / narrower (like an IDE panel).");
    note("SPATIAL: live distance + dB for the selected block; click Distance... then click");
    note("another block to measure A->B (Euclidean metres + listener-relative dB diff).");
    note("MOVEMENT: enable / disable recorded path; 'Freeze this block' holds just this");
    note("block in place (independent of the global Freeze Move); 'Loop movement' loops");
    note("EACH recorded segment within its own window (teleport back to that segment's");
    note("start, replay until the next segment's time) - the only sanctioned teleport;");
    note("edit keyframes via the");
    note("Position Keyframes editor.  In that editor 'Snap times' RESAMPLES the path at");
    note("0.5 / 1 / 2 / 5 s spacing - it interpolates evenly along the trajectory so the");
    note("block steps through the path at that resolution instead of teleporting to the");
    note("end.  'Off' restores the exact recorded times.  Pick playback mode (Natural /");
    note("Loop / Stretch / Speed) and movement duration.");
    note("DURATION FIT: 'Match Duration to Sound' grows the region to the sample length.");
    note("The 'Fit sound / movement' dropdown reconciles a sound that's a different");
    note("length than the movement:");
    note("  Distort sound -> movement : speed/slow the AUDIO so it ends with the motion");
    note("                              (pitch/character changes - a warning confirms).");
    note("  Distort movement -> sound : stretch the MOTION to the sound length (audio kept).");
    note("  Hard cut at movement end  : play audio naturally, cut it when the motion stops.");
    note("SOUND SCHEDULE...: play extra sounds from this block at set times (e.g. note A");
    note("at 5s, note B at 45s).  Pick a row on the left, choose a sound (same instrument");
    note("type) on the right.  Each fires at its start and is cut at its end.  Toggle LOOP");
    note("on a row to repeat that sound across its window (GAP s between repeats).  The");
    note("timeline auto-extends to cover scheduled sounds, so later notes always play.");
    note("LOOP / MUTE: per-block loop buffer + length; per-block mute and scheduled");
    note("mute-windows for fade-in / fade-out sections without deleting blocks.");
    note("Apply with 2+ selected: bulk-pushes mute / hide / loop / mute-windows to all.");

    // ── Section: Per-type filters (toolbar menus) ───────────────────────────
    hdr("Per-type filters (toolbar menus)");
    note("Mute v menu : indefinitely mute a whole category (Synth, Strings, ...).");
    note("              Muted blocks still animate; they just go silent.  Transient —");
    note("              not saved to .sime and not baked into export.");
    note("View v menu : per-type visibility filter.  Hidden blocks are skipped by the");
    note("              renderer (mesh + highlights) but stay in the scene for sequencing.");

    // ── Section: Export ─────────────────────────────────────────────────────
    hdr("Export Audio (File menu)");
    note("Bounces the entire scene to WAV / AIFF.  Picks duration from the latest");
    note("block end-time (or the end of the camera path, whichever is later).");
    note("The 'Listener' header in the dialog tells you exactly what pose will bake:");
    note("  CAMERA PATH active (N keyframes, T0 -> T1 s) — exporter animates the listener.");
    note("  ANCHORED at (x, y, z) — exporter freezes at the anchor pose.");
    note("  Anchor not set — falls back to the current camera pose (warning shown).");

    // ── Section: Closing / dismissing ───────────────────────────────────────
    hdr("Closing this popup");
    row("Esc / Enter",        "Close the Help panel.");
    row("Mouse wheel",        "Scroll through the cheat-sheet.");
    row("PageUp / PageDown",  "Scroll a panel at a time.");
    row("Home / End",         "Jump to the top / bottom.");

    body_.setBody(std::move(s));
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&body_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
}

// ─────────────────────────────────────────────────────────────────────────────

void HelpPopup::resized()
{
    auto r = getLocalBounds().reduced(kPad);

    // Footer reserved for the Close button.
    auto btnRow = r.removeFromBottom(kFooterH);
    closeBtn_.setBounds(btnRow.removeFromRight(120).withTrimmedTop(16));

    // Title strip lives in the top kTitleH — handled in paint().  The
    // viewport occupies everything in between.
    auto topStrip = r.removeFromTop(kTitleH);   // visually reserved for title
    juce::ignoreUnused(topStrip);

    viewport_.setBounds(r);

    // Re-measure the body for the new viewport width (subtract the scrollbar
    // gutter so words don't get clipped under the scrollbar thumb).
    const int innerW = viewport_.getMaximumVisibleWidth();
    const int h      = body_.measureHeight(innerW);
    body_.setBounds(0, 0, innerW, h);
}

void HelpPopup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(kBgColor));
    g.fillRoundedRectangle(bounds, 10.f);
    g.setColour(juce::Colour(kBorderColor));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.f, 1.f);

    g.setColour(juce::Colour(kAccentColor));
    g.fillRoundedRectangle(bounds.withHeight(3.f).reduced(2.f, 0.f), 1.5f);

    g.setFont(juce::Font("Public Sans", 16.f, juce::Font::bold));
    g.setColour(juce::Colour(0xfff0f2fa));
    g.drawText("SIME - Controls", kPad, 8, getWidth() - 2 * kPad, 28,
               juce::Justification::centredLeft);

    // Divider under the title.
    g.setColour(juce::Colour(0xff222a3e));
    g.fillRect(kPad, kPad + kTitleH - 2, getWidth() - 2 * kPad, 1);
}

bool HelpPopup::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey
        || k == juce::KeyPress::returnKey)
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
        return true;
    }

    // Keyboard scroll — the viewport already handles wheel scrolling for us,
    // we just need to translate keys into vertical position changes.
    const int step = 36;     // ~one row at a time
    const int page = juce::jmax(80, viewport_.getViewHeight() - 40);
    const int maxY = juce::jmax(0,
                                viewport_.getViewedComponent()->getHeight()
                                - viewport_.getViewHeight());

    if (k == juce::KeyPress::upKey)
    {
        viewport_.setViewPosition(0, juce::jmax(0, viewport_.getViewPositionY() - step));
        return true;
    }
    if (k == juce::KeyPress::downKey)
    {
        viewport_.setViewPosition(0, juce::jmin(maxY, viewport_.getViewPositionY() + step));
        return true;
    }
    if (k == juce::KeyPress::pageUpKey)
    {
        viewport_.setViewPosition(0, juce::jmax(0, viewport_.getViewPositionY() - page));
        return true;
    }
    if (k == juce::KeyPress::pageDownKey)
    {
        viewport_.setViewPosition(0, juce::jmin(maxY, viewport_.getViewPositionY() + page));
        return true;
    }
    if (k == juce::KeyPress::homeKey)
    {
        viewport_.setViewPosition(0, 0);
        return true;
    }
    if (k == juce::KeyPress::endKey)
    {
        viewport_.setViewPosition(0, maxY);
        return true;
    }

    return false;
}
