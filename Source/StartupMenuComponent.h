#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StartupMenuComponent.h
// Full-screen startup overlay shown when the app launches.
// Matches the dark-blue card design with voxel S logo.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <vector>

class StartupMenuComponent : public juce::Component
{
public:
    StartupMenuComponent();

    // ── Callbacks wired by MainComponent ─────────────────────────────────────
    std::function<void()>                    onNewScene;
    std::function<void()>                    onOpenScene;
    std::function<void()>                    onContinue;
    std::function<void(const juce::String&)> onRecentFile;

    /// Scans AppData/SIME for .sime files and populates the Recent list.
    void refreshRecentFiles();

    // ── juce::Component ──────────────────────────────────────────────────────
    void paint    (juce::Graphics&)          override;
    void resized  ()                         override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    // ── State ─────────────────────────────────────────────────────────────────
    bool continueAvailable_ = false;
    int  hoveredBtn_        = -1;   // 0=New, 1=Open, 2=Continue, 10+N=Recent row N

    struct RecentFile
    {
        juce::String name;      ///< filename without path
        juce::String age;       ///< "today", "yesterday", "3 days ago" …
        juce::String fullPath;
        juce::Colour dot;
    };
    std::vector<RecentFile> recent_;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr float kCardW     = 360.f;
    static constexpr float kBtnH      = 52.f;
    static constexpr float kBtnGap    = 10.f;
    static constexpr float kRecentRowH = 44.f;

    // ── Layout helpers ────────────────────────────────────────────────────────
    float cardLeft()    const { return (getWidth()  - kCardW) / 2.f; }
    float cardTop()     const;

    juce::Rectangle<float> newBtnRect()       const;
    juce::Rectangle<float> openBtnRect()      const;
    juce::Rectangle<float> continueBtnRect()  const;
    juce::Rectangle<float> helpLinkRect()     const;
    juce::Rectangle<float> recentRowRect(int i) const;

    // ── Draw helpers ──────────────────────────────────────────────────────────
    void drawVoxelS   (juce::Graphics& g, float x, float y,
                       float block, float gap) const;
    void drawActionBtn(juce::Graphics& g, juce::Rectangle<float> r,
                       const juce::String& icon, const juce::String& label,
                       bool primary, bool enabled, bool hovered) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StartupMenuComponent)
};
