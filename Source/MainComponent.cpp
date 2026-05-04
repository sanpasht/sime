// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"
#include "SceneFile.h"

MainComponent::MainComponent()
{
    // ── Startup menu ──────────────────────────────────────────────────────────
    addAndMakeVisible(startupMenu_);
    startupMenu_.setAlwaysOnTop(true);

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
                            .getChildFile("SIME").getChildFile("autosave.sime");
        dismissStartupMenu();
        loadSceneFromFile(autosave.getFullPathName());
    };

    startupMenu_.onRecentFile = [this](const juce::String& path)
    {
        dismissStartupMenu();
        loadSceneFromFile(path);
    };

    // Hide main app components until the user dismisses the startup screen
    view           .setVisible(false);
    sidebar        .setVisible(false);
    transportBar   .setVisible(false);
    blockTypeCombo .setVisible(false);
    typePill_      .setVisible(false);
    newBtn         .setVisible(false);
    openBtn        .setVisible(false);
    saveBtn        .setVisible(false);
    saveAsBtn      .setVisible(false);
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
    transportBar.onPlay  = [this] { view.transportPlay(); };
    transportBar.onPause = [this] { view.transportPause(); };
    transportBar.onStop  = [this]
    {
        view.transportStop();
        transportBar.setTransportState(false, false, 0.0,
                                       view.getTransportDuration());
    };

    // ── Block type toolbar ────────────────────────────────────────────────────
    addAndMakeVisible(typePill_);
    addAndMakeVisible(blockTypeCombo);
    blockTypeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1a1a2e));
    blockTypeCombo.setColour(juce::ComboBox::textColourId,        juce::Colour(0xffeeeeff));
    blockTypeCombo.setColour(juce::ComboBox::outlineColourId,     juce::Colour(0xff3344aa));
    blockTypeCombo.setColour(juce::ComboBox::arrowColourId,       juce::Colour(0xff8899bb));
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
                                     juce::Point<int> posInView)
    {
        juce::Point<int> screenPos = view.localPointToGlobal(posInView);
        editPopup.showAt(serial, type, start, dur, soundId, customFile, screenPos);
    };

    editPopup.onCommit = [this](int serial, double start, double dur,
                                int sid, const juce::String& customFile)
    {
        view.applyBlockEdit(serial, start, dur, sid, customFile);
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

    // Group items by category.  ComboBox uses int IDs > 0 (we use enum + 1).
    BlockCategory current = BlockCategory(-1);
    for (int i = 0; i < (int)BlockType::_Count; ++i)
    {
        auto bt  = static_cast<BlockType>(i);
        auto cat = blockTypeCategory(bt);

        if (cat != current)
        {
            blockTypeCombo.addSeparator();
            blockTypeCombo.addSectionHeading(blockCategoryName(cat));
            current = cat;
        }
        blockTypeCombo.addItem(blockTypeDisplayName(bt), i + 1);
    }
}

void MainComponent::syncComboToActive()
{
    blockTypeCombo.setSelectedId((int)activeType_ + 1, juce::dontSendNotification);
    typePill_.setActive(activeType_);
}

// ─────────────────────────────────────────────────────────────────────────────
// TypePill — small color swatch + active type name shown left of the combo box
// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::TypePill::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff14171f));
    g.fillRoundedRectangle(bounds, 4.f);

    // Color swatch on the left
    auto swatch = bounds.withWidth(18.f).reduced(5.f);
    g.setColour(blockTypeColor(type_));
    g.fillRoundedRectangle(swatch, 2.f);

    // Type name
    g.setColour(juce::Colour(0xffe6e8f5));
    g.setFont(juce::Font(13.f, juce::Font::bold));
    g.drawText(blockTypeDisplayName(type_),
               bounds.withTrimmedLeft(20.f),
               juce::Justification::centredLeft, true);
}

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    const double currentTime = view.getTransportTime();
    const double duration    = view.getTransportDuration();

    if (view.isTransportPlaying() && duration > 0.0 && currentTime >= duration)
    {
        view.transportStop();
        transportBar.setTransportState(false, false, 0.0, duration);
        return;
    }

    transportBar.setTransportState(
        view.isTransportPlaying(),
        view.isTransportPaused(),
        currentTime,
        duration);
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
    transportBar.setBounds(area.removeFromBottom(TransportBarComponent::kHeight));

    // Block type toolbar — top of viewport area
    auto toolbarArea = area.removeFromTop(kToolbarH);
    const int gap = 4;
    int ty = toolbarArea.getY() + (kToolbarH - 26) / 2;

    // Active-type pill (color swatch + name) and ComboBox listing all 23 types
    int tx = toolbarArea.getX() + 8;
    typePill_     .setBounds(tx, ty, 150, 26);  tx += 150 + gap;
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
