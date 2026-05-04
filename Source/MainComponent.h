#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "TransportBarComponent.h"
#include "BlockType.h"
#include "MovementConfirmPopup.h"
#include "StartupMenuComponent.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void saveScene(const juce::String& path = {});
    void openScene();
    void newScene();
    void autoSave();
    void loadSceneFromFile(const juce::String& path);

private:
    // ── Startup menu ──────────────────────────────────────────────────────────
    StartupMenuComponent startupMenu_;
    bool                 showingStartup_ = true;
    void                 dismissStartupMenu();

    // ── Main app components ───────────────────────────────────────────────────
    ViewPortComponent     view;
    SidebarComponent      sidebar;
    BlockEditPopup        editPopup;
    TransportBarComponent transportBar;
    std::unique_ptr<MovementConfirmPopup> movementPopup;
    bool isSidebarCollapsed = false;

    // ── Block type toolbar ────────────────────────────────────────────────────
    // Single grouped ComboBox listing all 23 BlockTypes, plus a color "pill"
    // that visually echoes the active selection.
    juce::ComboBox blockTypeCombo;

    /// Compact color-swatch + active type name shown left of the combo.
    class TypePill : public juce::Component
    {
    public:
        void setActive(BlockType t) { type_ = t; repaint(); }
        void paint(juce::Graphics& g) override;
    private:
        BlockType type_ = BlockType::Violin;
    } typePill_;

    BlockType activeType_ = BlockType::Violin;
    void setActiveBlockType(BlockType t);
    void rebuildBlockTypeCombo();
    void syncComboToActive();

    // ── File toolbar ─────────────────────────────────────────────────────────
    juce::TextButton newBtn   { "New"  };
    juce::TextButton openBtn  { "Open" };
    juce::TextButton saveBtn  { "Save" };
    juce::TextButton saveAsBtn{ "Save As" };

    juce::String currentFilePath_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    static constexpr int kToolbarH = 34;
    void showMovementConfirmPopup(int serial, double duration,
                                  const std::vector<MovementKeyFrame>& keyframes,
                                  juce::Point<int> position);

    void setPlaybackUiState(bool playing, bool paused, double currentTime);
    void stopPlaybackAndResetUi();
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
