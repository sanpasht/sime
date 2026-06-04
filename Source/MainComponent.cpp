// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"
#include "SceneFile.h"
#include "ExportAudioDialog.h"
#include <algorithm>
// #include "MathUtils.h"
// #include "BlockEntry.h"

MainComponent::MainComponent()
{
    // ── Startup menu ─────────────────────────────────────────────────────────
    showingStartup_ = true;
    addAndMakeVisible(startupMenu_);

    startupMenu_.onNewScene = [this]
    {
        dismissStartupMenu();
        newScene();
    };
    startupMenu_.onOpenScene = [this]
    {
        dismissStartupMenu();
        openScene();
    };
    startupMenu_.onContinue = [this]
    {
        auto autosave = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("SIME")
                            .getChildFile("autosave.sime");
        dismissStartupMenu();
        loadSceneFromFile(autosave.getFullPathName());
    };
    startupMenu_.onRecentFile = [this](const juce::String& path)
    {
        dismissStartupMenu();
        loadSceneFromFile(path);
    };

    // Main UI starts hidden — shown when startup menu is dismissed
    addChildComponent(view);
    addChildComponent(sidebar);
    addChildComponent(transportBar);
    addChildComponent(sidebarResizer_);

    // ── Workspace tab strip + per-tab content ─────────────────────────────────
    auto setupTabButton = [this](juce::TextButton& b, WorkspaceTab t)
    {
        addChildComponent(b);
        b.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff1a1d2a));
        b.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff2a5298));
        b.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xff9aa3bd));
        b.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
        b.setClickingTogglesState(false);
        b.onClick = [this, t] { switchTab(t); };
    };
    setupTabButton(sceneTabBtn_,    WorkspaceTab::Scene);
    setupTabButton(timelineTabBtn_, WorkspaceTab::Timeline);
    setupTabButton(synthTabBtn_,    WorkspaceTab::Synth);

    addChildComponent(timelineTabBar_);
    timelineTabBar_.setCollapsible(false);   // always a full-screen timeline

    addChildComponent(synthPanel_);
    synthPanel_.onPreview = [this](const juce::AudioBuffer<float>& buf)
    {
        view.previewSynthBuffer(buf);
    };
    synthPanel_.onExport = [this](const juce::AudioBuffer<float>& buf, const juce::String& baseName)
    {
        return view.exportSynthBufferToWorkspace(buf, baseName);
    };

    sidebarResizer_.onDragStart = [this]
    {
        sidebarWidthAtDragStart_ = sidebarWidth_;
    };
    sidebarResizer_.onDrag = [this](int deltaX)
    {
        sidebarWidth_ = juce::jlimit(kSidebarMinW, kSidebarMaxW,
                                     sidebarWidthAtDragStart_ + deltaX);
        resized();
    };

    // ── Wire sidebar collapse ─────────────────────────────────────────────────
    view.setSidebarComponent(&sidebar);

    sidebar.setAudioAnalyzer([this](const BlockEntry& b)
    {
        return view.analyzeBlockAudio(b);
    });

    view.onBlockSelected = [this](int serial)
    {
        auto block = view.getBlockBySerial(serial);
        if (block)
        {
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
            refreshSpatialSidebarReadout();
        }
    };

    // Update the transport bar immediately when blocks are added / removed / loaded,
    // instead of waiting up to 33 ms for the next timerCallback tick.
    // Also drives the dirty / title-bar state, gated by suppressNextDirty_ so
    // that loads and new-scene operations do not falsely mark the scene as modified.
    view.onBlockListChanged = [this]()
    {
        refreshTimelines();
        if (suppressNextDirty_)
        {
            suppressNextDirty_ = false;
        }
        else
        {
            markDirty();
        }
    };

    
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed)
    {
        isSidebarCollapsed = isNowCollapsed;
        resized();
    };

    // ── Transport bar ─────────────────────────────────────────────────────────

    // Collapse only affects the Scene-tab transport bar's height.
    transportBar.onHeightChanged = [this]()
    {
        resized();
    };

    // Both the Scene-tab transport bar and the dedicated Timeline tab share the
    // exact same behaviour (same clock, same edits), so wire them identically.
    wireTimelineCallbacks(transportBar);
    wireTimelineCallbacks(timelineTabBar_);
    sidebar.onApplyBlockInfo = [this](int serial, Vec3i pos, double start, double duration,
                                      bool movementEnabled, uint8_t playbackMode,
                                      double movementDurationSec, int movementYOffset,
                                      bool isMuted, bool isHidden,
                                      double loopBufferSec,
                                      bool isLooping, double loopDurationSec,
                                      std::vector<MuteWindow> muteWindows)
    {
        const auto multi = view.getMultiSelectionCopy();
        const bool bulkApply = multi.size() > 1
            && std::find(multi.begin(), multi.end(), serial) != multi.end();

        if (!bulkApply)
        {
            view.applySidebarBlockInfo(serial, pos, start, duration, movementEnabled,
                                       playbackMode, movementDurationSec, movementYOffset,
                                       isMuted, isHidden, loopBufferSec,
                                       isLooping, loopDurationSec,
                                       muteWindows);
        }
        else
        {
            // Bulk: mute / hide / loop / mute-schedule go to every selected
            // block; position and timing fields stay per-block (only the
            // primary serial gets the values from the form).
            const auto windowsForAll = muteWindows;
            for (int s : multi)
            {
                Vec3i   applyPos = pos;
                double  applyStart = start;
                double  applyDur   = duration;
                bool    applyMovEn = movementEnabled;
                uint8_t applyMode  = playbackMode;
                double  applyMovDur = movementDurationSec;
                int     applyMovY   = movementYOffset;

                if (s != serial)
                {
                    if (auto b = view.getBlockBySerial(s))
                    {
                        applyPos    = b->pos;
                        applyStart  = b->startTimeSec;
                        applyDur    = b->durationSec;
                        applyMovEn  = b->movementEnabled;
                        applyMode   = static_cast<uint8_t>(b->playbackMode);
                        applyMovDur = b->movementDurationSec;
                        applyMovY   = b->movementYOffset;
                    }
                }

                view.applySidebarBlockInfo(s, applyPos, applyStart, applyDur,
                                           applyMovEn, applyMode,
                                           applyMovDur, applyMovY,
                                           isMuted, isHidden, loopBufferSec,
                                           isLooping, loopDurationSec,
                                           windowsForAll);
            }
        }

        // Optimistic sidebar refresh — getBlockBySerial reads a snapshot that
        // may lag one frame behind the GL-thread apply, so merge form values
        // explicitly and keep movement keyframes from the current panel.
        BlockEntry display;
        if (auto fromView = view.getBlockBySerial(serial))
            display = *fromView;
        else if (auto fromPanel = sidebar.getSelectedBlockCopy())
            display = *fromPanel;
        else
            display.serial = serial;

        if (auto fromPanel = sidebar.getSelectedBlockCopy())
        {
            if (fromPanel->serial == serial
                && fromPanel->recordedMovement.size() >= display.recordedMovement.size())
            {
                display.recordedMovement     = fromPanel->recordedMovement;
                display.hasRecordedMovement  = fromPanel->hasRecordedMovement;
            }
        }

        display.pos                 = pos;
        display.startTimeSec        = start;
        display.durationSec         = duration;
        display.movementEnabled     = movementEnabled;
        display.playbackMode        = static_cast<BlockPlaybackMode>(playbackMode);
        display.movementDurationSec = movementDurationSec;
        display.movementYOffset     = movementYOffset;
        display.isMuted             = isMuted;
        display.isHidden            = isHidden;
        display.loopBufferSec       = loopBufferSec;
        display.isLooping           = isLooping;
        display.loopDurationSec     = loopDurationSec;
        display.muteWindows         = std::move(muteWindows);
        if (!display.recordedMovement.empty())
            display.hasRecordedMovement = true;

        sidebar.showBlockInfo(display, view.displayNameForSerial(serial));

        transportBar.setBlocks(view.getBlockListCopy());
        markDirty();
    };

    sidebar.onMatchDurationToSound = [this](int serial)
    {
        view.matchBlockDurationToSound(serial);
        if (auto block = view.getBlockBySerial(serial))
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
        transportBar.setBlocks(view.getBlockListCopy());
        markDirty();
    };

    sidebar.onFreezeBlockMovement = [this](int serial, bool frozen)
    {
        view.setBlockMovementFrozenForSerial(serial, frozen);
    };

    sidebar.onSetMovementLoop = [this](int serial, bool loop)
    {
        view.setBlockMovementLoop(serial, loop);
        markDirty();
    };

    sidebar.onDurationSyncAction = [this](int serial, int action)
    {
        auto apply = [this, serial, action]
        {
            view.applyDurationSync(serial, static_cast<DurationSyncAction>(action));
            if (auto block = view.getBlockBySerial(serial))
                sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
            transportBar.setBlocks(view.getBlockListCopy());
            markDirty();
        };

        // Only the "distort sound" path changes the audio (pitch/speed); warn
        // the user first so they can back out.  The other actions are lossless.
        if (action == static_cast<int>(DurationSyncAction::DistortSoundToMovement))
        {
            juce::NativeMessageBox::showOkCancelBox(
                juce::AlertWindow::WarningIcon,
                "Audio may be affected",
                "Fitting the sound to the movement length speeds it up or slows it "
                "down, which changes its pitch/character.\n\nApply anyway?",
                nullptr,
                juce::ModalCallbackFunction::create(
                    [apply](int result) { if (result == 1) apply(); }));
        }
        else
        {
            apply();
        }
    };

    sidebar.onEditSoundSchedule = [this](int serial, juce::Point<int> screenPos)
    {
        auto block = view.getBlockBySerial(serial);
        if (!block) return;

        if (!soundSchedulePopup_)
        {
            soundSchedulePopup_ = std::make_unique<SoundSchedulePopup>();
            soundSchedulePopup_->onApply =
                [this](int s, std::vector<SoundEvent> schedule)
            {
                view.applySoundSchedule(s, std::move(schedule));
                if (auto b = view.getBlockBySerial(s))
                    sidebar.showBlockInfo(*b, view.displayNameForSerial(s));
                markDirty();
            };
            soundSchedulePopup_->onDismiss = [] {};
        }

        soundSchedulePopup_->setSchedule(
            serial,
            view.displayNameForSerial(serial),
            block->blockType,
            &view.soundLibrary(),
            [this](int entryIdx) { return view.ensureLibrarySoundLoaded(entryIdx); },
            block->soundSchedule);
        soundSchedulePopup_->showAt(screenPos);
    };

    sidebar.onApplyKeyframes = [this](int serial,
                                      std::vector<MovementKeyFrame> frames)
    {
        view.applyMovementKeyframes(serial, std::move(frames));
        // Sidebar refresh + transport rebuild happens via
        // view.onBlockPropertiesChanged once the GL thread drains the edit.
        transportBar.setBlocks(view.getBlockListCopy());
        markDirty();
    };

    view.onBlockPropertiesChanged = [this](int serial)
    {
        if (auto block = view.getBlockBySerial(serial))
        {
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
            refreshSpatialSidebarReadout();
        }
    };

    // ── Block type toolbar ────────────────────────────────────────────────────
    addChildComponent(typePill_);
    addChildComponent(blockTypeCombo);

    blockTypeCombo.setColour(juce::ComboBox::backgroundColourId,    juce::Colour(0xff181a24));
    blockTypeCombo.setColour(juce::ComboBox::textColourId,           juce::Colour(0xffe2e6f2));
    blockTypeCombo.setColour(juce::ComboBox::outlineColourId,        juce::Colour(0xff2f3447));
    blockTypeCombo.setColour(juce::ComboBox::arrowColourId,          juce::Colour(0xff8b94ad));
    blockTypeCombo.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xff5b7ce6));

    rebuildBlockTypeCombo();
    blockTypeCombo.onChange = [this]
    {
        int id = blockTypeCombo.getSelectedId();
        if (id > 0)
            setActiveBlockType(static_cast<BlockType>(id - 1));
    };
    syncComboToActive();

    // ── File menu ───────────────────────────────────────────────────────────
    addChildComponent(fileMenuBtn_);
    fileMenuBtn_.setButtonText(juce::String("File ") + juce::String::fromUTF8("\xe2\x96\xbe")); // "File ▾"
    fileMenuBtn_.setColour(juce::TextButton::buttonColourId,     juce::Colour(0xff252840));
    fileMenuBtn_.setColour(juce::TextButton::buttonOnColourId,   juce::Colour(0xff3a3f60));
    fileMenuBtn_.setColour(juce::TextButton::textColourOffId,    juce::Colour(0xffe2e6f2));
    fileMenuBtn_.setColour(juce::TextButton::textColourOnId,     juce::Colours::white);
    fileMenuBtn_.onClick = [this] { showFileMenu(); };

    // ── View menu (per-type visibility filter) ──────────────────────────────
    addChildComponent(viewMenuBtn_);
    viewMenuBtn_.setButtonText(juce::String("View ") + juce::String::fromUTF8("\xe2\x96\xbe"));
    viewMenuBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff252840));
    viewMenuBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3f60));
    viewMenuBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    viewMenuBtn_.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    viewMenuBtn_.onClick = [this] { showViewMenu(); };

    // ── Mute menu (per-type indefinite mute) ────────────────────────────────
    addChildComponent(muteMenuBtn_);
    muteMenuBtn_.setButtonText(juce::String("Mute ") + juce::String::fromUTF8("\xe2\x96\xbe"));
    muteMenuBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff252840));
    muteMenuBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3f60));
    muteMenuBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    muteMenuBtn_.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
    muteMenuBtn_.onClick = [this] { showMuteMenu(); };

    // ── View toggles (planes + arrows now live inside the Layers menu) ──────
    addChildComponent(layersMenuBtn_);
    layersMenuBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff252840));
    layersMenuBtn_.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff3a3f60));
    layersMenuBtn_.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffe2e6f2));
    layersMenuBtn_.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
    layersMenuBtn_.setTooltip("Show or hide the floor / walls / move arrows.");
    layersMenuBtn_.onClick = [this] { showLayersMenu(); };

    configureToggleButton(dopplerBtn_);
    configureToggleButton(anchorBtn_);
    configureToggleButton(freeCamBtn_);
    configureToggleButton(freezeMovBtn_);

    addChildComponent(pathEditBtn_);
    pathEditBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff252840));
    pathEditBtn_.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff3a3f60));
    pathEditBtn_.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffe2e6f2));
    pathEditBtn_.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
    pathEditBtn_.setTooltip("Open the camera-path editor (Hold / Lerp keyframes).");
    pathEditBtn_.onClick = [this] { showCameraPathPopup(); };

    addChildComponent(helpBtn_);
    helpBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff252840));
    helpBtn_.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff3a3f60));
    helpBtn_.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffe2e6f2));
    helpBtn_.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
    helpBtn_.setTooltip("Show keyboard and mouse controls.");
    helpBtn_.onClick = [this] { showHelpPopup(); };

    addChildComponent(spatialSensSlider_);
    spatialSensSlider_.setRange(0.25, 3.0, 0.05);
    spatialSensSlider_.setValue(1.0, juce::dontSendNotification);
    spatialSensSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    spatialSensSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 22);
    spatialSensSlider_.setTooltip("Spatial falloff sensitivity (higher = louder nearby, quieter far)");
    spatialSensSlider_.onValueChange = [this]
    {
        view.setSpatialSensitivity((float) spatialSensSlider_.getValue());
    };

    dopplerBtn_    .setToggleState(false, juce::dontSendNotification);
    anchorBtn_     .setToggleState(false, juce::dontSendNotification);
    freeCamBtn_    .setToggleState(false, juce::dontSendNotification);
    freeCamBtn_.setTooltip("Free Cam ON = user keeps manual control of the camera even when "
                           "a path is loaded.  Free Cam OFF = camera auto-follows the path.");

    freezeMovBtn_  .setToggleState(false, juce::dontSendNotification);
    freezeMovBtn_.setTooltip("Freeze Move ON = blocks hold their current position even if they "
                             "have a recorded path.  Turning it OFF resumes motion from the "
                             "current playhead time (e.g. freeze 0-5s, unfreeze, continue from 5s).");

    dopplerBtn_    .onClick = [this] { const bool v = !dopplerBtn_.getToggleState(); view.setDopplerEnabled(v); dopplerBtn_.setToggleState(v, juce::dontSendNotification); };
    anchorBtn_     .onClick = [this]
    {
        const bool v = !anchorBtn_.getToggleState();
        view.setAudioAnchorEnabled(v);
        anchorBtn_.setToggleState(v, juce::dontSendNotification);
    };
    freeCamBtn_    .onClick = [this]
    {
        const bool v = !freeCamBtn_.getToggleState();
        view.setFreeCameraOverride(v);
        freeCamBtn_.setToggleState(v, juce::dontSendNotification);
    };
    freezeMovBtn_  .onClick = [this]
    {
        const bool v = !freezeMovBtn_.getToggleState();
        view.setBlockMovementFrozen(v);
        freezeMovBtn_.setToggleState(v, juce::dontSendNotification);
    };

    // ── Selected-block audition buttons ──────────────────────────────────────
    auto styleAuditionBtn = [this](juce::TextButton& b, const juce::String& tip)
    {
        addChildComponent(b);
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1e3a2a));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd6f5e0));
        b.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
        b.setTooltip(tip);
    };
    styleAuditionBtn(auditionBtn_,
        "Play Sel: preview the selected block's sound once, spatialised from the "
        "current camera position (ignores the transport / other blocks).");
    styleAuditionBtn(auditionInTimeBtn_,
        "@Time: move the playhead to the selected block's start time (blocks + camera "
        "snap to that moment).  Does not start playback - press Play yourself.");

    auditionBtn_.onClick = [this]
    {
        if (auto block = sidebar.getSelectedBlockCopy())
            view.auditionBlock(block->serial);
    };
    auditionInTimeBtn_.onClick = [this] { playSelectedBlockInTime(); };

    // Camera-path always follows when a path is set unless Free Cam is on;
    // recording always overrides following.  See ViewPortComponent.
    view.setCameraPathFollowEnabled(true);

    view.onDistanceMeasured = [this](int aSer, int bSer,
                                     float dx, float dy, float dz,
                                     float distM, float dbAtB)
    {
        juce::ignoreUnused(aSer);
        const juce::String aName = view.displayNameForSerial(aSer);
        const juce::String bName = view.displayNameForSerial(bSer);
        sidebar.setBlockDistanceReadout(
            aName + " -> " + bName + ": "
            + juce::String(distM, 2) + " m"
            + "  (d " + juce::String(dx, 1) + ", "
            + juce::String(dy, 1) + ", " + juce::String(dz, 1) + ")"
            + "   level @ B: " + juce::String(dbAtB, 1) + " dB");
        sidebar.setDistancePickActive(false);
        refreshSpatialSidebarReadout();
    };

    sidebar.onRequestDistancePick = [this](int serial)
    {
        view.beginDistancePick(serial);
        sidebar.setDistancePickActive(true);
    };
    sidebar.onCancelDistancePick = [this]
    {
        view.cancelDistancePick();
    };

    // ── Wire edit popup ───────────────────────────────────────────────────────
    view.onRequestBlockEdit = [this](int serial, BlockType type,
                                     double start, double dur,
                                     int soundId, const juce::String& customFile,
                                     bool isLooping, double loopDur,
                                     juce::Point<int> posInView)
    {
        juce::Point<int> screenPos = view.localPointToGlobal(posInView);
        editPopup.showAt(serial, type, start, dur, soundId, customFile,
                         isLooping, loopDur, screenPos);
    };

    editPopup.onCommit = [this](int serial, double start, double dur,
                                int sid, const juce::String& customFile,
                                bool isLooping, double loopDur)
    {
        view.applyBlockEdit(serial, start, dur, sid, customFile,
                            isLooping, loopDur);
        markDirty();
    };

    editPopup.onCancel = [this]()
    {
        view.clearSelectedBlock();
    };

    // Wire the popup's sound picker to the library that lives in ViewPortComponent.
    editPopup.setSoundLibrary(&view.soundLibrary());

    view.onRequestMovementConfirm = 
        [this](int serial, double duration, 
               const std::vector<MovementKeyFrame>& keyframes,
               juce::Point<int> pos)
    {
        showMovementConfirmPopup(serial, duration, keyframes, pos);
    };

    // ── Poll transport state at 30 Hz ─────────────────────────────────────────
    startTimerHz(30);
}

void MainComponent::showMovementConfirmPopup(int serial, 
                                             double duration,
                                             const std::vector<MovementKeyFrame>& keyframes,
                                             juce::Point<int> position)
{
    auto* popup = new MovementConfirmPopup(serial, duration, keyframes);  // ← Pass keyframes
    
    popup->onConfirm = [this, serial, keyframes](int s, double d)
    {
        view.confirmMovementRecording(s, d);

        BlockEntry display;
        if (auto fromView = view.getBlockBySerial(s))
            display = *fromView;
        else if (auto fromPanel = sidebar.getSelectedBlockCopy())
            display = *fromPanel;
        else
            display.serial = s;

        display.recordedMovement    = keyframes;
        display.hasRecordedMovement = keyframes.size() >= 2;
        display.movementEnabled     = true;
        display.durationSec         = d;
        display.durationLocked      = true;

        sidebar.showBlockInfo(display, view.displayNameForSerial(s));
        view.highlightBlock(s);
    };
    
    popup->onCancel = [this, serial](int s)
    {
        view.cancelMovementRecording(s);
    };
    
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(popup);
    options.dialogTitle = "Confirm Movement Recording";
    options.dialogBackgroundColour = juce::Colour(0xff2a2a2a);
    options.escapeKeyTriggersCloseButton = false;  // Bug 5 fix: Escape would close without calling onCancel,
                                                    // leaving recordingBlockSerial stale. Force explicit button click.
    options.useNativeTitleBar = false;
    options.resizable = false;
    
    auto* dialog = options.launchAsync();
    
    if (dialog)
        dialog->centreWithSize(400, 300);
}

void MainComponent::dismissStartupMenu()
{
    showingStartup_ = false;
    startupMenu_.setVisible(false);

    // Workspace tab strip is always visible once the app is running.
    sceneTabBtn_.setVisible(true);
    timelineTabBtn_.setVisible(true);
    synthTabBtn_.setVisible(true);

    // Viewport stays attached (0×0 on non-Scene tabs) so the GL loop keeps the
    // clock, sequencer, and edit queues running without painting over tabs.
    view.setVisible(true);

    // Open on the Scene tab.
    activeTab_ = WorkspaceTab::Scene;
    setSceneChildrenVisible(true);
    timelineTabBar_.setVisible(false);
    synthPanel_.setVisible(false);
    updateTabButtonStates();

    resized();
}

void MainComponent::configureToggleButton(juce::TextButton& b)
{
    addChildComponent(b);
    b.setClickingTogglesState(false);   // we drive the toggle state manually
    b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1c1f2e));
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3f6fff));
    b.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xffa9b1c3));
    b.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::setActiveBlockType(BlockType t)
{
    activeType_ = t;
    view.setActiveBlockType(t);
    typePill_.setActive(t);
    syncComboToActive();
}

void MainComponent::rebuildBlockTypeCombo()
{
    blockTypeCombo.clear(juce::dontSendNotification);

    // Iterate categories in display order; for each, emit a section heading
    // followed by every type that lives in that category.  This avoids the
    // duplicated-header bug (Violin appearing alone in its own "Strings"
    // group) caused by traversing the enum in declaration order.
    const BlockCategory order[] = {
        BlockCategory::Synth,
        BlockCategory::Strings,
        BlockCategory::Woodwinds,
        BlockCategory::Brass,
        BlockCategory::Percussion,
        BlockCategory::Special,
    };

    bool firstSection = true;
    for (auto cat : order)
    {
        auto types = blockTypesByCategory(cat);
        if (types.empty()) continue;

        if (!firstSection)
            blockTypeCombo.addSeparator();
        firstSection = false;

        blockTypeCombo.addSectionHeading(blockCategoryName(cat));
        for (auto bt : types)
            blockTypeCombo.addItem(blockTypeDisplayName(bt), (int)bt + 1);
    }
}

void MainComponent::syncComboToActive()
{
    blockTypeCombo.setSelectedId((int)activeType_ + 1, juce::dontSendNotification);
    typePill_.setActive(activeType_);
}

// ─────────────────────────────────────────────────────────────────────────────
// TypePill — color swatch + active type name shown left of the combo box
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::TypePill::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff14171f));
    g.fillRoundedRectangle(bounds, 4.f);

    auto swatch = bounds.withWidth(16.f).reduced(4.f);
    g.setColour(blockTypeColor(type_));
    g.fillRoundedRectangle(swatch, 2.f);

    g.setColour(juce::Colour(0xffe2e6f2));
    g.setFont(juce::Font(13.f, juce::Font::bold));
    g.drawText(blockTypeDisplayName(type_),
               bounds.withTrimmedLeft(20.f).withTrimmedRight(6.f),
               juce::Justification::centredLeft, true);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    // On Timeline / Synth tabs the viewport is parked off-screen at 1×1 so
    // OpenGL stays attached (JUCE requires w/h > 0) without covering the tab UI.
    if (activeTab_ != WorkspaceTab::Scene)
        view.nudgeEngineLoop();

    const double currentTime = view.getTransportTime();
    const double duration = view.getTransportDuration();

    const bool reachedEnd =
        view.isTransportPlaying()
        && duration > 0.0
        && currentTime >= duration;

    if (reachedEnd)
    {
        stopPlaybackAndResetUi();
        return;
    }

    setPlaybackUiState(
        view.isTransportPlaying(),
        view.isTransportPaused(),
        currentTime
    );

    // Keep the visible timeline bar's region list current.  Placement fires
    // onBlockListChanged mid-frame, before the GL thread refreshes its block
    // snapshot, so that callback can read a stale list.  A light per-tick push
    // of the active bar guarantees the new block appears within ~33 ms.
    auto blocks = view.getBlockListCopy();
    if (activeTab_ == WorkspaceTab::Timeline)
        timelineTabBar_.setBlocks(blocks);
    else if (activeTab_ == WorkspaceTab::Scene)
        transportBar.setBlocks(blocks);

    refreshSpatialSidebarReadout();

    // Audition buttons are only meaningful when a block is selected.  The
    // sidebar's selected block is the source of truth (set by both 3D-scene and
    // sidebar-list selection), so use it rather than the viewport atomic.
    const bool hasSel = sidebar.getSelectedBlockCopy().has_value();
    auditionBtn_.setEnabled(hasSel);
    auditionInTimeBtn_.setEnabled(hasSel);
}

void MainComponent::playSelectedBlockInTime()
{
    auto block = sidebar.getSelectedBlockCopy();
    if (!block) return;

    // Just move the playhead to the block's start time — do NOT start playback.
    // The block (and any moving blocks / camera path) snap to that moment so the
    // user can see where things are, then press Play themselves if they want.
    view.setSoloSerial(-1);
    view.seekTransportClock(block->startTimeSec);
    setPlaybackUiState(view.isTransportPlaying(),
                       view.isTransportPaused(),
                       view.getTransportTime());
}

void MainComponent::refreshSpatialSidebarReadout()
{
    if (auto block = sidebar.getSelectedBlockCopy())
    {
        const auto r = view.measureSpatialAt((float) block->pos.x,
                                             (float) block->pos.y,
                                             (float) block->pos.z);
        sidebar.setListenerSpatialReadout(
            "From listener: " + juce::String(r.distanceMetres, 2) + " m,  "
            + juce::String(r.approxDb, 1) + " dB");
    }
    else
    {
        sidebar.setListenerSpatialReadout({});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// File menu + export
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::showMuteMenu()
{
    juce::PopupMenu m;

    constexpr int kMuteAllId = 1;
    constexpr int kUnmuteAllId = 2;
    constexpr int kTypeIdBase = 100;

    m.addItem(kUnmuteAllId, "Unmute All Types");
    m.addItem(kMuteAllId,   "Mute All Types");
    m.addSeparator();

    for (int c = 0; c < (int) BlockCategory::_Count; ++c)
    {
        const auto cat = static_cast<BlockCategory>(c);
        auto types = blockTypesByCategory(cat);
        if (types.empty()) continue;

        juce::PopupMenu sub;
        for (auto t : types)
        {
            // Tick = currently muted, so the user can tell at a glance
            // which categories are silenced.
            const bool muted = view.isBlockTypeMuted(t);
            sub.addItem(juce::PopupMenu::Item(blockTypeDisplayName(t))
                            .setID(kTypeIdBase + (int) t)
                            .setTicked(muted));
        }
        m.addSubMenu(blockCategoryName(cat), sub);
    }

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&muteMenuBtn_),
        [this](int result)
        {
            if (result == 0) return;

            if (result == 1)            // Mute All
            {
                for (int i = 0; i < (int) BlockType::_Count; ++i)
                    view.setBlockTypeMuted(static_cast<BlockType>(i), true);
                return;
            }
            if (result == 2)            // Unmute All
            {
                for (int i = 0; i < (int) BlockType::_Count; ++i)
                    view.setBlockTypeMuted(static_cast<BlockType>(i), false);
                return;
            }

            constexpr int kTypeIdBase = 100;
            const int typeIdx = result - kTypeIdBase;
            if (typeIdx >= 0 && typeIdx < (int) BlockType::_Count)
            {
                const auto t = static_cast<BlockType>(typeIdx);
                view.setBlockTypeMuted(t, !view.isBlockTypeMuted(t));
            }
        });
}

void MainComponent::showLayersMenu()
{
    juce::PopupMenu m;
    enum : int { kFloor = 1, kWallX, kWallZ, kArrows };

    m.addItem(juce::PopupMenu::Item("Floor")
                  .setID(kFloor).setTicked(view.getShowFloorPlane()));
    m.addItem(juce::PopupMenu::Item("YZ Wall")
                  .setID(kWallX).setTicked(view.getShowWallXPlane()));
    m.addItem(juce::PopupMenu::Item("XY Wall")
                  .setID(kWallZ).setTicked(view.getShowWallZPlane()));
    m.addItem(juce::PopupMenu::Item("Move arrows")
                  .setID(kArrows).setTicked(view.getShowArrows()));

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&layersMenuBtn_),
        [this](int result)
        {
            switch (result)
            {
                case kFloor:  view.setShowFloorPlane(!view.getShowFloorPlane()); break;
                case kWallX:  view.setShowWallXPlane(!view.getShowWallXPlane()); break;
                case kWallZ:  view.setShowWallZPlane(!view.getShowWallZPlane()); break;
                case kArrows: view.setShowArrows    (!view.getShowArrows());     break;
                default: break;
            }
        });
}

void MainComponent::showViewMenu()
{
    juce::PopupMenu m;

    // Bulk actions at the top.
    constexpr int kShowAllId = 1;
    constexpr int kHideAllId = 2;
    constexpr int kTypeIdBase = 100;  // ids kTypeIdBase + (int)BlockType

    m.addItem(kShowAllId, "Show All Types");
    m.addItem(kHideAllId, "Hide All Types");
    m.addSeparator();

    // Group by category so the menu mirrors the placement combo.
    for (int c = 0; c < (int) BlockCategory::_Count; ++c)
    {
        const auto cat = static_cast<BlockCategory>(c);
        auto types = blockTypesByCategory(cat);
        if (types.empty()) continue;

        juce::PopupMenu sub;
        for (auto t : types)
        {
            const bool on = view.isBlockTypeVisible(t);
            sub.addItem(juce::PopupMenu::Item(blockTypeDisplayName(t))
                            .setID(kTypeIdBase + (int) t)
                            .setTicked(on));
        }
        m.addSubMenu(blockCategoryName(cat), sub);
    }

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&viewMenuBtn_),
        [this](int result)
        {
            if (result == 0) return;

            if (result == 1)
            {
                for (int i = 0; i < (int) BlockType::_Count; ++i)
                    view.setBlockTypeVisible(static_cast<BlockType>(i), true);
                return;
            }
            if (result == 2)
            {
                for (int i = 0; i < (int) BlockType::_Count; ++i)
                    view.setBlockTypeVisible(static_cast<BlockType>(i), false);
                return;
            }

            constexpr int kTypeIdBase = 100;
            const int typeIdx = result - kTypeIdBase;
            if (typeIdx >= 0 && typeIdx < (int) BlockType::_Count)
            {
                const auto t = static_cast<BlockType>(typeIdx);
                view.setBlockTypeVisible(t, !view.isBlockTypeVisible(t));
            }
        });
}

void MainComponent::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "New Scene");
    m.addItem(2, juce::String("Open Scene") + juce::String::fromUTF8("\xe2\x80\xa6"));  // Open Scene…
    m.addSeparator();
    m.addItem(3, "Save");
    m.addItem(4, juce::String("Save As") + juce::String::fromUTF8("\xe2\x80\xa6"));     // Save As…
    m.addSeparator();
    m.addItem(5, juce::String("Export Audio") + juce::String::fromUTF8("\xe2\x80\xa6")); // Export Audio…

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&fileMenuBtn_),
        [this](int result) { handleFileMenu(result); });
}

void MainComponent::handleFileMenu(int result)
{
    if (result == 0)
        return;

    if (result == 1)
        newScene();
    else if (result == 2)
        openScene();
    else if (result == 3)
        saveScene();
    else if (result == 4)
    {
        currentFilePath_.clear();
        saveScene();
    }
    else if (result == 5)
        showExportAudioDialog();
}

void MainComponent::showCameraPathPopup()
{
    if (!cameraPathPopup_)
    {
        cameraPathPopup_ = std::make_unique<CameraPathPopup>();
        cameraPathPopup_->onApply = [this](std::vector<CameraKeyframe> path)
        {
            view.applyCameraPath(std::move(path));
            markDirty();
        };
        cameraPathPopup_->onDismiss = [] {};
        cameraPathPopup_->onCommitDraft = [this](std::vector<CameraKeyframe> path)
        {
            view.applyCameraPath(std::move(path));
            markDirty();
        };
        cameraPathPopup_->onRecordToggle = [this]
        {
            view.toggleCameraPathRecording();
        };
        cameraPathPopup_->isRecording = [this]
        {
            return view.isCameraPathRecording();
        };
        cameraPathPopup_->fetchLivePath = [this]
        {
            return view.getCameraPathCopy();
        };
        cameraPathPopup_->getCurrentCamPose = [this]
        {
            return view.getCurrentCameraPose();
        };
        cameraPathPopup_->getCurrentPlayheadSec = [this]
        {
            return view.getTransportTime();
        };
        cameraPathPopup_->getCaptureIntervalSec = [this]
        {
            return view.getCameraRecordIntervalSec();
        };
        cameraPathPopup_->onCaptureIntervalChanged = [this](double s)
        {
            view.setCameraRecordIntervalSec(s);
        };
    }

    cameraPathPopup_->setPath(view.getCameraPathCopy());

    juce::Point<int> screenPos = pathEditBtn_.localPointToGlobal(
        juce::Point<int>(pathEditBtn_.getWidth() / 2,
                         pathEditBtn_.getHeight() + 6));
    cameraPathPopup_->showAt(screenPos);
}

void MainComponent::showHelpPopup()
{
    auto* panel = new HelpPopup();

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(panel);
    opt.dialogTitle = "Help";
    opt.dialogBackgroundColour = juce::Colour(0xff2a2a2a);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;

    if (auto* w = opt.launchAsync())
        w->centreWithSize(panel->getWidth() + 24, panel->getHeight() + 40);
}

void MainComponent::showExportAudioDialog()
{
    auto* panel = new ExportAudioDialog();

    {
        const auto info = view.getExportListenerInfo();
        const auto path = view.getCameraPathCopy();
        if (!path.empty())
        {
            panel->setListenerPathInfo((int) path.size(),
                                        path.front().timeSec,
                                        path.back().timeSec);
        }
        else
        {
            const float yawDeg   = std::atan2(info.forward.x, info.forward.z)
                                       * 180.0f / juce::MathConstants<float>::pi;
            const float pitchDeg = std::asin(juce::jlimit(-1.0f, 1.0f, info.forward.y))
                                       * 180.0f / juce::MathConstants<float>::pi;
            panel->setListenerInfo(info.anchored,
                                   info.pos.x, info.pos.y, info.pos.z,
                                   yawDeg, pitchDeg);
        }
    }

    panel->onExportChosen = [this](SceneAudioExporter::Format fmt)
    {
        juce::MessageManager::callAsync([this, fmt]()
        {
            launchExportSaveChooser(fmt);
        });
    };

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(panel);
    opt.dialogTitle = "Export Audio";
    opt.dialogBackgroundColour = juce::Colour(0xff2a2a2a);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;

    if (auto* w = opt.launchAsync())
        w->centreWithSize(panel->getWidth() + 24, panel->getHeight() + 40);
}

void MainComponent::launchExportSaveChooser(SceneAudioExporter::Format format)
{
    const juce::String wildcard = SceneAudioExporter::formatWildcard(format);
    const juce::String ext = juce::String(SceneAudioExporter::formatFileSuffix(format)).substring(1);
    const juce::String defaultName = juce::String("SIME-export.") + ext;

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Audio As",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(defaultName),
        wildcard);

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, format, ext](const juce::FileChooser& fc)
        {
            juce::File result = fc.getResult();
            if (result == juce::File{})
                return;

            result = result.withFileExtension(ext);

            juce::String err;
            if (view.exportSceneAudioToFile(result, format, err))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Export complete",
                    "Audio saved to:\n" + result.getFullPathName());
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Export failed",
                    err.isNotEmpty() ? err : juce::String("Unknown error."));
            }
        });
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1018));
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    startupMenu_.setBounds(getLocalBounds());

    if (showingStartup_)
        return;

    auto area = getLocalBounds();

    // ── Workspace tab strip (always at the very top) ──────────────────────────
    auto tabStrip = area.removeFromTop(kTabStripH);
    {
        const int tabW = 110;
        int txb       = tabStrip.getX() + 6;
        const int ty  = tabStrip.getY() + 4;
        const int th  = kTabStripH - 6;
        sceneTabBtn_   .setBounds(txb, ty, tabW, th); txb += tabW + 2;
        timelineTabBtn_.setBounds(txb, ty, tabW, th); txb += tabW + 2;
        synthTabBtn_   .setBounds(txb, ty, tabW, th); txb += tabW + 2;
    }

    // ── Non-Scene tabs fill the whole area below the tab strip ────────────────
    // JUCE OpenGL composites above normal components AND detaches when w/h is 0,
    // so park the viewport off-screen at 1×1: the GL loop keeps running (clock,
    // sequencer, edit queues) without covering or intercepting the tab UI.
    if (activeTab_ != WorkspaceTab::Scene)
    {
        view.setInterceptsMouseClicks(false, false);
        view.setBounds(-1000, -1000, 1, 1);

        if (activeTab_ == WorkspaceTab::Timeline)
        {
            timelineTabBar_.setBounds(area);
            timelineTabBar_.setVisible(true);
            timelineTabBar_.toFront(false);
        }
        else // Synth
        {
            synthPanel_.setBounds(area);
            synthPanel_.setVisible(true);
            synthPanel_.toFront(false);
        }

        // Keep the tab strip clickable above everything.
        sceneTabBtn_.toFront(false);
        timelineTabBtn_.toFront(false);
        synthTabBtn_.toFront(false);
        return;
    }

    view.setInterceptsMouseClicks(true, true);

    // ── Scene tab ─────────────────────────────────────────────────────────────
    const bool collapsed = sidebar.isCollapsed();
    const int sidebarWidth = collapsed ? 50 : sidebarWidth_;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));

    // Drag handle sits flush against the sidebar's right edge (expanded only).
    sidebarResizer_.setVisible(!collapsed && !showingStartup_);
    if (!collapsed)
    {
        constexpr int kHandleW = 5;
        sidebarResizer_.setBounds(sidebarWidth - kHandleW + 1,
                                  sidebar.getY(),
                                  kHandleW,
                                  sidebar.getHeight());
        sidebarResizer_.toFront(false);
    }

    // Transport bar fixed at bottom
    auto transportArea = area.removeFromBottom(transportBar.getPreferredHeight());
    transportBar.setBounds(transportArea);

    // Toolbar stays at top
    auto toolbarArea = area.removeFromTop(kToolbarH);

    const int gap = 4;
    int ty = toolbarArea.getY() + (kToolbarH - 26) / 2;

    int tx = toolbarArea.getX() + 8;
    typePill_.setBounds(tx, ty, 110, 26);
    tx += 110 + gap;

    blockTypeCombo.setBounds(tx, ty, 180, 26);
    tx += 180 + gap;

    // Selected-block audition (grouped with the block-type selector).
    const int audSelW = 46;
    const int audTimeW = 58;
    auditionBtn_      .setBounds(tx, ty, audSelW,  26); tx += audSelW  + gap;
    auditionInTimeBtn_.setBounds(tx, ty, audTimeW, 26); tx += audTimeW + gap;

    // ── Compact toolbar: Layers menu, audio pills, path/free-cam, slider.
    const int toggleW = 64;
    const int menuW   = 80;
    const int freeW   = 78;
    layersMenuBtn_ .setBounds(tx, ty, menuW,   26); tx += menuW   + gap;
    dopplerBtn_    .setBounds(tx, ty, toggleW, 26); tx += toggleW + gap;
    anchorBtn_     .setBounds(tx, ty, toggleW, 26); tx += toggleW + gap;
    pathEditBtn_   .setBounds(tx, ty, toggleW, 26); tx += toggleW + gap;
    freeCamBtn_    .setBounds(tx, ty, freeW,   26); tx += freeW   + gap;
    const int freezeW = 88;
    freezeMovBtn_  .setBounds(tx, ty, freezeW, 26); tx += freezeW + gap;
    spatialSensSlider_.setBounds(tx, ty, 90, 26);

    const int fbtnW = 72;
    const int helpW = 56;
    helpBtn_    .setBounds(toolbarArea.getRight() - 8 - helpW,                          ty, helpW, 26);
    fileMenuBtn_.setBounds(toolbarArea.getRight() - 8 - helpW - 6 - fbtnW,              ty, fbtnW, 26);
    viewMenuBtn_.setBounds(toolbarArea.getRight() - 8 - helpW - 6 - fbtnW * 2 - 6,      ty, fbtnW, 26);
    muteMenuBtn_.setBounds(toolbarArea.getRight() - 8 - helpW - 6 - fbtnW * 3 - 12,     ty, fbtnW, 26);

    // Viewport gets remaining area above transport bar
    view.setBounds(area);

    transportBar.toFront(false);

    if (movementPopup)
        movementPopup->toFront(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene persistence
// ─────────────────────────────────────────────────────────────────────────────

MainComponent::~MainComponent()
{
    stopTimer();   // prevent timerCallback from firing during teardown
    autoSave();
}

void MainComponent::newScene()
{
    suppressNextDirty_ = true;   // the upcoming clear won't mark the scene dirty
    hasUnsavedChanges_ = false;
    currentFilePath_.clear();
    view.clearScene();
    sidebar.clearSelectedBlock();   // drop stale info from the previous scene
    updateWindowTitle();
}

void MainComponent::saveScene(const juce::String& explicitPath)
{
    juce::String target = explicitPath.isNotEmpty() ? explicitPath : currentFilePath_;

    if (target.isNotEmpty())
    {
        auto blocks = view.getBlockListCopy();
        auto camPath = view.getCameraPathCopy();
        if (SceneFile::save(target.toStdString(), blocks, camPath))
        {
            currentFilePath_   = target;
            hasUnsavedChanges_ = false;
            updateWindowTitle();
            DBG("Scene saved: " << target);
        }
        return;
    }

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Scene",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.sime");

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;

            juce::String path = result.getFullPathName();
            if (!path.endsWithIgnoreCase(".sime"))
                path += ".sime";

            auto blocks  = view.getBlockListCopy();
            auto camPath = view.getCameraPathCopy();
            if (SceneFile::save(path.toStdString(), blocks, camPath))
            {
                currentFilePath_   = path;
                hasUnsavedChanges_ = false;
                updateWindowTitle();
                DBG("Scene saved: " << path);
            }
        });
}

void MainComponent::openScene()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open Scene",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.sime");

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{}) return;

            juce::String path = result.getFullPathName();
            std::vector<BlockEntry>     loaded;
            std::vector<CameraKeyframe> loadedCamPath;
            if (SceneFile::load(path.toStdString(), loaded, loadedCamPath))
            {
                suppressNextDirty_ = true;
                hasUnsavedChanges_ = false;
                view.loadScene(std::move(loaded));
                view.applyCameraPath(std::move(loadedCamPath));
                sidebar.clearSelectedBlock();   // drop stale info from the previous scene
                currentFilePath_ = path;
                updateWindowTitle();
                DBG("Scene loaded: " << path << "  (" << (int)view.getBlockListCopy().size() << " blocks)");
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Load Error",
                    "Could not open \"" + result.getFileName() + "\".\nThe file may be corrupted or an unsupported version.");
            }
        });
}

void MainComponent::autoSave()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("SIME");
    appData.createDirectory();

    auto target  = appData.getChildFile("autosave.sime");
    auto blocks  = view.getBlockListCopy();
    auto camPath = view.getCameraPathCopy();
    if (!blocks.empty() || !camPath.empty())
        SceneFile::save(target.getFullPathName().toStdString(), blocks, camPath);
}

void MainComponent::loadSceneFromFile(const juce::String& path)
{
    std::vector<BlockEntry>     loaded;
    std::vector<CameraKeyframe> loadedCamPath;
    if (SceneFile::load(path.toStdString(), loaded, loadedCamPath))
    {
        suppressNextDirty_ = true;   // the upcoming load won't mark the scene dirty
        hasUnsavedChanges_ = false;
        view.loadScene(std::move(loaded));
        view.applyCameraPath(std::move(loadedCamPath));
        sidebar.clearSelectedBlock();   // previous-scene selection is no longer valid
        currentFilePath_ = path;
        updateWindowTitle();
    }
}


void MainComponent::setPlaybackUiState(bool playing, bool paused, double currentTime)
{
    const double duration = view.getTransportDuration();

    transportBar.setTransportState(playing, paused, currentTime, duration);
    transportBar.setTimelinePlaying(playing);

    timelineTabBar_.setTransportState(playing, paused, currentTime, duration);
    timelineTabBar_.setTimelinePlaying(playing);

    // Keep the visible transport bar repainting smoothly during playback.
    if (activeTab_ == WorkspaceTab::Timeline)
        timelineTabBar_.repaint();
    else if (activeTab_ == WorkspaceTab::Scene)
        transportBar.repaint();
}

void MainComponent::stopPlaybackAndResetUi()
{
    view.transportStop();
    view.setSoloSerial(-1);   // clear any "Play @Time" solo

    setPlaybackUiState(false, false, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Workspace tabs + shared transport
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::doTransportPlay()
{
    view.setSoloSerial(-1);   // normal play hears everything
    view.transportPlay();
    if (activeTab_ != WorkspaceTab::Scene)
        view.nudgeEngineLoop();   // kick the engine immediately on Timeline tab
    setPlaybackUiState(true, false, view.getTransportTime());
}

void MainComponent::doTransportPause()
{
    view.transportPause();
    setPlaybackUiState(false, true, view.getTransportTime());
}

void MainComponent::doTransportStop()
{
    stopPlaybackAndResetUi();
}

void MainComponent::refreshTimelines()
{
    auto blocks = view.getBlockListCopy();
    transportBar.setBlocks(blocks);
    timelineTabBar_.setBlocks(blocks);
}

void MainComponent::wireTimelineCallbacks(TransportBarComponent& bar)
{
    bar.onPlay  = [this] { doTransportPlay();  };
    bar.onPause = [this] { doTransportPause(); };
    bar.onStop  = [this] { doTransportStop();  };

    bar.onBlockEdited = [this](int serial, int occurrenceIndex, double start, double duration)
    {
        view.updateBlockTiming(serial, occurrenceIndex, start, duration);
        refreshTimelines();
        if (auto block = view.getBlockBySerial(serial))
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
        timerCallback();   // refresh transport readout immediately
        markDirty();
    };

    bar.onDeleteBlockOrRegion = [this](int serial, int timeIndex)
    {
        if (view.deleteBlockOrRegion(serial, timeIndex))
        {
            refreshTimelines();
            sidebar.clearSelectedBlockIfSerial(serial);
            timerCallback();
            markDirty();
        }
    };

    bar.onRegionDuplicated = [this](int serial, double start, double duration)
    {
        view.addTimeRangeToBlock(serial, start, duration);
        refreshTimelines();
        if (auto block = view.getBlockBySerial(serial))
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
        timerCallback();
        markDirty();
    };

    bar.onRegionEdited = [this](int serial, int timeIndex, double start, double duration)
    {
        view.updateBlockTimeRange(serial, timeIndex, start, duration);
        refreshTimelines();
        if (auto block = view.getBlockBySerial(serial))
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
        timerCallback();
        markDirty();
    };

    bar.onTimelineBlockClicked = [this](int serial)
    {
        view.highlightBlock(serial);
        if (auto block = view.getBlockBySerial(serial))
            sidebar.showBlockInfo(*block, view.displayNameForSerial(serial));
    };

    bar.onPlayheadMoved = [this](double newTimeSec) { view.seekTransportClock(newTimeSec); };
    bar.onSpeedChanged  = [this](double rate)       { view.setPlaybackRate(rate); };
}

void MainComponent::updateTabButtonStates()
{
    sceneTabBtn_   .setToggleState(activeTab_ == WorkspaceTab::Scene,    juce::dontSendNotification);
    timelineTabBtn_.setToggleState(activeTab_ == WorkspaceTab::Timeline, juce::dontSendNotification);
    synthTabBtn_   .setToggleState(activeTab_ == WorkspaceTab::Synth,    juce::dontSendNotification);
}

void MainComponent::setSceneChildrenVisible(bool v)
{
    // `view` visibility is managed in resized(): full size on Scene, 1×1
    // off-screen on other tabs so OpenGL stays attached without covering tabs.
    sidebar.setVisible(v);
    transportBar.setVisible(v);
    typePill_.setVisible(v);
    blockTypeCombo.setVisible(v);
    auditionBtn_.setVisible(v);
    auditionInTimeBtn_.setVisible(v);
    layersMenuBtn_.setVisible(v);
    dopplerBtn_.setVisible(v);
    anchorBtn_.setVisible(v);
    pathEditBtn_.setVisible(v);
    freeCamBtn_.setVisible(v);
    freezeMovBtn_.setVisible(v);
    spatialSensSlider_.setVisible(v);
    helpBtn_.setVisible(v);
    fileMenuBtn_.setVisible(v);
    viewMenuBtn_.setVisible(v);
    muteMenuBtn_.setVisible(v);
    // sidebarResizer_ visibility is finalised in resized() (depends on collapse).
    if (!v)
        sidebarResizer_.setVisible(false);
}

void MainComponent::switchTab(WorkspaceTab t)
{
    if (showingStartup_)
        return;

    activeTab_ = t;
    const bool scene = (t == WorkspaceTab::Scene);

    setSceneChildrenVisible(scene);
    timelineTabBar_.setVisible(t == WorkspaceTab::Timeline);
    synthPanel_.setVisible(t == WorkspaceTab::Synth);

    // Keep the timeline tab in sync the moment it's shown.
    if (t == WorkspaceTab::Timeline)
    {
        refreshTimelines();
        setPlaybackUiState(view.isTransportPlaying(),
                           view.isTransportPaused(),
                           view.getTransportTime());
    }

    updateTabButtonStates();
    resized();
    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// Title bar + dirty tracking
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::updateWindowTitle()
{
    // em dash U+2014 = \xe2\x80\x94
    const juce::String dash = juce::String::fromUTF8("\xe2\x80\x94");
    juce::String title = "SIME";

    if (currentFilePath_.isNotEmpty())
        title += " " + dash + " " + juce::File(currentFilePath_).getFileName();
    else if (hasUnsavedChanges_)
        title += " " + dash + " Untitled";

    if (hasUnsavedChanges_)
        title += " *";

    if (auto* tlc = getTopLevelComponent())
        tlc->setName(title);
}

void MainComponent::markDirty()
{
    hasUnsavedChanges_ = true;
    updateWindowTitle();
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard shortcuts  (Ctrl+S = Save,  Ctrl+Z = Undo last placement)
//
// ViewPortComponent.keyPressed() returns false for all Ctrl combos, so these
// bubble up from the focused viewport to here automatically.
// ─────────────────────────────────────────────────────────────────────────────

bool MainComponent::keyPressed(const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();

    // Bare R — toggle camera-path recording.  Routed here (in addition to
    // ViewPortComponent::keyPressed) so a focused popup / sidebar doesn't
    // eat the shortcut: any descendant that doesn't consume R lets it
    // bubble up to MainComponent.
    if (!mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAltDown())
    {
        const int code = k.getKeyCode();
        if (code == 'R' || code == 'r')
        {
            view.toggleCameraPathRecording();
            return true;
        }
    }

    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        const int code = k.getKeyCode();

        if (code == 'S' || code == 's')
        {
            saveScene();
            return true;
        }

        if (code == 'Z' || code == 'z')
        {
            view.requestUndo();
            return true;
        }

        // Standard editor shortcuts:
        //   Ctrl+C  → copy current selection (single block, or whatever
        //             is in the Ctrl+A multi-selection set) to the
        //             clipboard.
        //   Ctrl+V  → paste the clipboard back into the scene.  Pasted
        //             copies are offset along +X until they find a free
        //             cell, and become the new multi-selection so the
        //             user can immediately keep pasting / bulk-editing.
        //   Ctrl+A  → select every block in the scene.
        if (code == 'C' || code == 'c')
        {
            view.requestCopySelection();
            return true;
        }

        if (code == 'V' || code == 'v')
        {
            view.requestPasteSelection();
            return true;
        }

        if (code == 'A' || code == 'a')
        {
            view.requestSelectAll();
            return true;
        }
    }

    return false;
}