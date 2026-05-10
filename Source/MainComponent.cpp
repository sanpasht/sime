// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"
#include "SceneFile.h"
// #include "MathUtils.h"
// #include "BlockEntry.h"

MainComponent::MainComponent()
{
    // ── Startup menu — DISABLED, jump straight to main UI ────────────────────
    showingStartup_ = false;
    startupMenu_.setVisible(false);

    addAndMakeVisible(view);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(transportBar);

    // ── Wire sidebar collapse ─────────────────────────────────────────────────
    view.setSidebarComponent(&sidebar);
    sidebar.onCollapsedChanged = [this](bool isNowCollapsed)
    {
        isSidebarCollapsed = isNowCollapsed;
        resized();
    };

    // ── Transport bar ─────────────────────────────────────────────────────────
    transportBar.onPlay = [this]
    {
        view.transportPlay();
        setPlaybackUiState(true, false, view.getTransportTime());
        transportBar.setTimelinePlaying(true);
    };
    transportBar.onPause = [this]
    {
        view.transportPause();
        setPlaybackUiState(false, true, view.getTransportTime());
        transportBar.setTimelinePlaying(false);
    };
    transportBar.onStop = [this]
    {
        stopPlaybackAndResetUi();
        transportBar.setTimelinePlaying(false);
    };
    transportBar.onBlockEdited = [this](int serial, double start, double duration)
    {
        view.updateBlockTiming(serial, start, duration);
    };

    transportBar.onTimelineBlockClicked = [this](int serial) { 
        view.highlightBlock(serial);
        auto block = view.getBlockBySerial(serial);
        if (block)
            sidebar.showBlockInfo(*block);

    };
    sidebar.onApplyBlockInfo = [this](int serial, Vec3i pos, double start, double duration, bool movementEnabled)
    {
        view.applySidebarBlockInfo(serial, pos, start, duration, movementEnabled);

        auto updated = view.getBlockBySerial(serial);
        if (updated)
            sidebar.showBlockInfo(*updated);

        transportBar.setBlocks(view.getBlockListCopy());
    };

    // ── Block type toolbar ────────────────────────────────────────────────────
    addAndMakeVisible(typePill_);
    addAndMakeVisible(blockTypeCombo);

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

    // ── File toolbar ────────────────────────────────────────────────────────
    for (auto* btn : { &newBtn, &openBtn, &saveBtn, &saveAsBtn })
    {
        addAndMakeVisible(*btn);
        btn->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1a1a2e));
        btn->setColour(juce::TextButton::textColourOffId,  juce::Colour(0xffccccdd));
    }
    newBtn  .onClick = [this] { newScene();  };
    openBtn .onClick = [this] { openScene(); };
    saveBtn .onClick = [this] { saveScene(); };
    saveAsBtn.onClick = [this] { currentFilePath_.clear(); saveScene(); };

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
    
    popup->onConfirm = [this, serial](int s, double d)
    {
        view.confirmMovementRecording(s, d);
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

    view           .setVisible(true);
    sidebar        .setVisible(true);
    transportBar   .setVisible(true);
    blockTypeCombo .setVisible(true);
    typePill_      .setVisible(true);
    newBtn         .setVisible(true);
    openBtn        .setVisible(true);
    saveBtn        .setVisible(true);
    saveAsBtn      .setVisible(true);

    resized();
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
    const double currentTime = view.getTransportTime();
    const double duration = view.getTransportDuration();

    const bool reachedEnd =
        view.isTransportPlaying()
        && duration > 0.0
        && currentTime >= duration;

    if (reachedEnd)
    {
        stopPlaybackAndResetUi();
        transportBar.setBlocks(view.getBlockListCopy());
        return;
    }

    setPlaybackUiState(
        view.isTransportPlaying(),
        view.isTransportPaused(),
        currentTime
    );

    transportBar.setBlocks(view.getBlockListCopy());
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1018));
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    // Startup menu always fills the whole window
    startupMenu_.setBounds(getLocalBounds());

    if (showingStartup_) return;  // don't lay out app components while startup is showing
    auto area = getLocalBounds();

    // Sidebar — full height, left side
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));

    // Transport bar — bottom
    transportBar.setBounds(area.removeFromBottom(transportBar.getCurrentHeight()));

    // Block type toolbar — top of viewport area
    auto toolbarArea = area.removeFromTop(kToolbarH);
    const int gap = 4;
    int ty = toolbarArea.getY() + (kToolbarH - 26) / 2;

    // Active type pill (color swatch + name) and full grouped ComboBox
    int tx = toolbarArea.getX() + 8;
    typePill_     .setBounds(tx, ty, 160, 26);  tx += 160 + gap;
    blockTypeCombo.setBounds(tx, ty, 200, 26);

    // File toolbar — right side of toolbar row
    const int fbtnW = 64;
    int fx = toolbarArea.getRight() - 8 - (4 * (fbtnW + gap));
    saveAsBtn.setBounds(fx + 3 * (fbtnW + gap), ty, fbtnW, 26);
    saveBtn  .setBounds(fx + 2 * (fbtnW + gap), ty, fbtnW, 26);
    openBtn  .setBounds(fx + 1 * (fbtnW + gap), ty, fbtnW, 26);
    newBtn   .setBounds(fx,                      ty, fbtnW, 26);

    // 3D viewport — whatever remains
    view.setBounds(area);

    if (movementPopup)
    {
        movementPopup->toFront(false);  // Don't give it keyboard focus
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene persistence
// ─────────────────────────────────────────────────────────────────────────────

MainComponent::~MainComponent()
{
    autoSave();
}

void MainComponent::newScene()
{
    view.clearScene();
    currentFilePath_.clear();
}

void MainComponent::saveScene(const juce::String& explicitPath)
{
    juce::String target = explicitPath.isNotEmpty() ? explicitPath : currentFilePath_;

    if (target.isNotEmpty())
    {
        auto blocks = view.getBlockListCopy();
        if (SceneFile::save(target.toStdString(), blocks))
        {
            currentFilePath_ = target;
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

            auto blocks = view.getBlockListCopy();
            if (SceneFile::save(path.toStdString(), blocks))
            {
                currentFilePath_ = path;
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
            std::vector<BlockEntry> loaded;
            if (SceneFile::load(path.toStdString(), loaded))
            {
                view.loadScene(std::move(loaded));
                currentFilePath_ = path;
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

    auto target = appData.getChildFile("autosave.sime");
    auto blocks = view.getBlockListCopy();
    if (!blocks.empty())
        SceneFile::save(target.getFullPathName().toStdString(), blocks);
}

void MainComponent::loadSceneFromFile(const juce::String& path)
{
    std::vector<BlockEntry> loaded;
    if (SceneFile::load(path.toStdString(), loaded))
    {
        view.loadScene(std::move(loaded));
        currentFilePath_ = path;
    }
}


void MainComponent::setPlaybackUiState(bool playing, bool paused, double currentTime)
{
    const double duration = view.getTransportDuration();

    transportBar.setTransportState(
        playing,
        paused,
        currentTime,
        duration
    );

    transportBar.setTimelinePlaying(playing);
}

void MainComponent::stopPlaybackAndResetUi()
{
    view.transportStop();

    setPlaybackUiState(false, false, 0.0);
}