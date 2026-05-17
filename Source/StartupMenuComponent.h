#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// StartupMenuComponent.h
// Full-screen startup overlay shown when the app launches.
// Animated coloured squares drift in the background outside the card.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <vector>

class StartupMenuComponent : public juce::Component,
                              private juce::Timer
{
public:
    StartupMenuComponent();

    // ── Callbacks wired by MainComponent ─────────────────────────────────────
    std::function<void()>                    onNewScene;
    std::function<void()>                    onOpenScene;
    std::function<void()>                    onContinue;
    std::function<void(const juce::String&)> onRecentFile;

    void refreshRecentFiles();

    // ── juce::Component ──────────────────────────────────────────────────────
    void paint    (juce::Graphics&)          override;
    void resized  ()                         override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    // ── Animated background squares ───────────────────────────────────────────
    struct AnimSquare
    {
        float x        = 0.f;
        float y        = 0.f;
        float maxSize  = 40.f;
        float t        = 0.f;    ///< lifecycle 0 -> 1
        float speed    = 0.28f;
        float rotation = 0.f;
        juce::Colour colour;
        bool  active   = false;
    };

    static constexpr int kMaxSquares = 12;
    AnimSquare squares_[kMaxSquares];
    juce::Random rng_;

    void timerCallback() override;
    void initSquare (AnimSquare& sq);
    void drawSquares(juce::Graphics& g) const;

    // ── State ─────────────────────────────────────────────────────────────────
    bool continueAvailable_ = false;
    int  hoveredBtn_        = -1;

    struct RecentFile
    {
        juce::String name;
        juce::String age;
        juce::String fullPath;
        juce::Colour dot;
    };
    std::vector<RecentFile> recent_;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr float kCardW      = 360.f;
    static constexpr float kBtnH       = 52.f;
    static constexpr float kBtnGap     = 10.f;
    static constexpr float kRecentRowH = 44.f;

    // ── Layout helpers ────────────────────────────────────────────────────────
    float cardLeft() const { return (getWidth()  - kCardW) / 2.f; }
    float cardTop()  const;

    juce::Rectangle<float> newBtnRect()      const;
    juce::Rectangle<float> openBtnRect()     const;
    juce::Rectangle<float> continueBtnRect() const;
    juce::Rectangle<float> helpLinkRect()    const;
    juce::Rectangle<float> recentRowRect(int i) const;

    // ── Draw helpers ──────────────────────────────────────────────────────────
    void drawVoxelS   (juce::Graphics& g, float x, float y,
                       float block, float gap) const;
    void drawActionBtn(juce::Graphics& g, juce::Rectangle<float> r,
                       const juce::String& icon, const juce::String& label,
                       bool primary, bool enabled, bool hovered) const;
    void drawFileIcon (juce::Graphics& g, float cx, float cy,
                       float alpha) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StartupMenuComponent)
};
