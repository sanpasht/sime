#pragma once
#include <JuceHeader.h>
#include "ViewPortComponent.h"
#include "SidebarComponent.h"
#include "BlockEditPopup.h"
#include "TransportBarComponent.h"
#include "BlockType.h"
#include "MovementConfirmPopup.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Save current scene to the given path (or prompt if empty).
    void saveScene(const juce::String& path = {});
    /// Open a .sime file and load it.
    void openScene();
    /// Start a new (empty) scene.
    void newScene();
    /// Auto-save to the last-used file (called on app close).
    void autoSave();
    /// Load a scene directly from a file path (used for auto-load).
    void loadSceneFromFile(const juce::String& path);

private:
    ViewPortComponent view;
    SidebarComponent  sidebar;
    BlockEditPopup    editPopup;
    TransportBarComponent transportBar;
    std::unique_ptr<MovementConfirmPopup> movementPopup;
    bool isSidebarCollapsed = false;

    // ── Block type toolbar ────────────────────────────────────────────────────
    juce::TextButton violinBtn { "Violin" };
    juce::TextButton pianoBtn  { "Piano"  };
    juce::TextButton drumBtn   { "Drum"   };
    juce::TextButton customBtn { "Custom" };
    BlockType activeType_ = BlockType::Violin;
    void setActiveBlockType(BlockType t);
    void refreshToolbarColors();

    // ── File toolbar ─────────────────────────────────────────────────────────
    juce::TextButton newBtn   { "New"  };
    juce::TextButton openBtn  { "Open" };
    juce::TextButton saveBtn  { "Save" };
    juce::TextButton saveAsBtn{ "Save As" };

    juce::String currentFilePath_;   ///< Path of the currently loaded .sime file
    std::unique_ptr<juce::FileChooser> fileChooser_;

    static constexpr int kToolbarH = 34;
    void showMovementConfirmPopup(int serial, double duration, 
                              const std::vector<MovementKeyFrame>& keyframes,
                              juce::Point<int> position);
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
