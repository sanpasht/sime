// ─────────────────────────────────────────────────────────────────────────────
// MainComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "MainComponent.h"
#include "SceneFile.h"

MainComponent::MainComponent()
{
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
    addAndMakeVisible(violinBtn);
    addAndMakeVisible(pianoBtn);
    addAndMakeVisible(drumBtn);
    addAndMakeVisible(customBtn);
    addAndMakeVisible(listenerBtn);

    violinBtn  .onClick = [this] { setActiveBlockType(BlockType::Violin);   };
    pianoBtn   .onClick = [this] { setActiveBlockType(BlockType::Piano);    };
    drumBtn    .onClick = [this] { setActiveBlockType(BlockType::Drum);     };
    customBtn  .onClick = [this] { setActiveBlockType(BlockType::Custom);   };
    listenerBtn.onClick = [this] { setActiveBlockType(BlockType::Listener); };

    refreshToolbarColors();

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

// ─────────────────────────────────────────────────────────────────────────────

void MainComponent::setActiveBlockType(BlockType t)
{
    activeType_ = t;
    view.setActiveBlockType(t);
    refreshToolbarColors();
}

void MainComponent::refreshToolbarColors()
{
    auto style = [&](juce::TextButton& btn, BlockType t,
                     juce::Colour activeCol)
    {
        if (activeType_ == t)
        {
            btn.setColour(juce::TextButton::buttonColourId,  activeCol);
            btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        else
        {
            btn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff222233));
            btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888899));
        }
    };

    style(violinBtn,   BlockType::Violin,   juce::Colour(0xffc03528));
    style(pianoBtn,    BlockType::Piano,    juce::Colour(0xff3366cc));
    style(drumBtn,     BlockType::Drum,     juce::Colour(0xff2eaa44));
    style(customBtn,   BlockType::Custom,   juce::Colour(0xff666688));
    style(listenerBtn, BlockType::Listener, juce::Colour(0xffdd7010));  // orange

    violinBtn  .repaint();
    pianoBtn   .repaint();
    drumBtn    .repaint();
    customBtn  .repaint();
    listenerBtn.repaint();
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
    auto area = getLocalBounds();

    // Sidebar — full height, left side
    const int sidebarWidth = sidebar.isCollapsed() ? 50 : 220;
    sidebar.setBounds(area.removeFromLeft(sidebarWidth));

    // Transport bar — bottom
    transportBar.setBounds(area.removeFromBottom(TransportBarComponent::kHeight));

    // Block type toolbar — top of viewport area
    auto toolbarArea = area.removeFromTop(kToolbarH);
    const int btnW = 80;
    const int gap  = 4;
    int tx = toolbarArea.getX() + 8;
    int ty = toolbarArea.getY() + (kToolbarH - 26) / 2;
    violinBtn  .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    pianoBtn   .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    drumBtn    .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    customBtn  .setBounds(tx, ty, btnW, 26);  tx += btnW + gap;
    listenerBtn.setBounds(tx, ty, btnW, 26);

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
