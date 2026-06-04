#pragma once

#include <JuceHeader.h>
#include "SynthPatch.h"

// ============================================================================
//  Cyberpunk / dark-techno synthesizer UI for the Synthesizer workspace tab.
//  Core controls (oscillator waveform, ADSR, filter cutoff/res, master gain)
//  drive the real SynthRenderer; the remaining panels are styled performance
//  surfaces that complete the instrument's look and feel.
// ============================================================================

// ── Custom look & feel: glowing rotary knobs + dark combo/slider styling ────
class SynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SynthLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool down,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;

    static juce::Colour accentOf(const juce::Component& c, juce::Colour fallback);
};

// ── Dark panel with a header strip + accent underline ───────────────────────
class SynthPanel : public juce::Component
{
public:
    SynthPanel(juce::String title, juce::Colour accent);
    void paint(juce::Graphics&) override;
    juce::Rectangle<int> bodyBounds() const;   ///< inner area (local coords)

    juce::String title_;
    juce::Colour accent_;
};

// ── Rotary knob + caption ───────────────────────────────────────────────────
class KnobCell : public juce::Component
{
public:
    KnobCell(juce::String caption, juce::Colour accent,
             double lo, double hi, double def,
             double skewMidpoint = 0.0);
    void resized() override;

    juce::Slider slider;
    juce::Label  caption;
};

// ── Vertical bar slider + caption (ADSR) ────────────────────────────────────
class BarCell : public juce::Component
{
public:
    BarCell(juce::String caption, juce::Colour accent,
            double lo, double hi, double def);
    void resized() override;

    juce::Slider slider;
    juce::Label  caption;
};

// ── Small waveform thumbnail ────────────────────────────────────────────────
class WaveThumb : public juce::Component
{
public:
    enum class Shape { Sine, Square, Saw, Triangle, Random, Noise };
    explicit WaveThumb(Shape s, juce::Colour accent) : shape_(s), accent_(accent) {}
    void setShape(Shape s) { shape_ = s; repaint(); }
    void paint(juce::Graphics&) override;
private:
    Shape shape_;
    juce::Colour accent_;
};

// ── ADSR envelope curve preview ─────────────────────────────────────────────
class EnvCurve : public juce::Component
{
public:
    std::function<juce::Array<float>()> getAdsr;   ///< returns {a,d,s,r} normalised
    juce::Colour accent { 0xffff3b4e };
    void paint(juce::Graphics&) override;
};

// ── Low-pass filter response preview ────────────────────────────────────────
class FilterCurve : public juce::Component
{
public:
    std::function<float()> getCutoffNorm;   ///< 0..1
    std::function<float()> getResNorm;      ///< 0..1
    juce::Colour accent { 0xffff3b4e };
    void paint(juce::Graphics&) override;
};

// ── Arp/sequencer step bars ─────────────────────────────────────────────────
class StepBars : public juce::Component
{
public:
    StepBars();
    void paint(juce::Graphics&) override;
    juce::Colour accent { 0xffff3b4e };
private:
    juce::Array<float> steps_;
};

// ── XY performance pad ──────────────────────────────────────────────────────
class XYPad : public juce::Component
{
public:
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override;
    juce::Point<float> pos { 0.5f, 0.5f };
    juce::Colour accent { 0xff9b59f6 };
};

// ── Vertical level meter (decorative) ───────────────────────────────────────
class LevelMeter : public juce::Component
{
public:
    void paint(juce::Graphics&) override;
    float level = 0.7f;
};

// ── On-screen keyboard ──────────────────────────────────────────────────────
class MiniKeyboard : public juce::Component
{
public:
    std::function<void(int midiNote)> onNoteOn;
    int startNote = 48;     ///< C3
    int numWhite  = 21;     ///< 3 octaves

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    int noteAt(juce::Point<float>) const;
    int pressed_ = -1;
};

// ============================================================================

class SynthComponent : public juce::Component,
                       private juce::Slider::Listener
{
public:
    SynthComponent();
    ~SynthComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    SynthPatch getPatch() const;

    std::function<void(const juce::AudioBuffer<float>&)> onPreview;
    std::function<juce::String(const juce::AudioBuffer<float>&, const juce::String& baseName)> onExport;

    void setStatusText(const juce::String& text);

private:
    void sliderValueChanged(juce::Slider*) override;
    void syncPatchFromUi();
    void previewPatch();
    void exportPatch();

    KnobCell& addKnob(juce::String caption, juce::Colour accent,
                      double lo, double hi, double def, double skewMid = 0.0);
    SynthPanel& addPanel(juce::String title, juce::Colour accent);

    SynthLookAndFeel lnf_;

    // Header
    juce::TextEditor presetName_;
    juce::TextButton previewBtn_;
    juce::TextButton exportBtn_;
    juce::Label      statusLabel_;
    LevelMeter       masterMeter_;

    // Panels (owned, drawn behind controls)
    juce::OwnedArray<SynthPanel> panels_;

    // Functional controls
    juce::ComboBox   osc1Wave_;
    std::unique_ptr<KnobCell> masterKnob_;
    std::unique_ptr<KnobCell> cutoffKnob_;
    std::unique_ptr<KnobCell> resKnob_;

    BarCell attackBar_  { "A", juce::Colour(0xffff3b4e), 0.001, 2.0, 0.01 };
    BarCell decayBar_   { "D", juce::Colour(0xffff3b4e), 0.001, 2.0, 0.15 };
    BarCell sustainBar_ { "S", juce::Colour(0xffff3b4e), 0.0,   1.0, 0.65 };
    BarCell releaseBar_ { "R", juce::Colour(0xffff3b4e), 0.001, 3.0, 0.35 };

    EnvCurve    envCurve_;
    FilterCurve filterCurve_;

    // Decorative controls
    juce::OwnedArray<KnobCell>  knobs_;
    juce::OwnedArray<WaveThumb> waveThumbs_;
    juce::OwnedArray<juce::ComboBox> combos_;
    juce::OwnedArray<juce::Label> labels_;
    juce::OwnedArray<juce::TextButton> chips_;
    juce::OwnedArray<BarCell> bars_;

    StepBars stepBars_;
    XYPad    xyPad_;
    MiniKeyboard keyboard_;

    SynthPatch patch_;

    juce::Label& addLabel(juce::String text, juce::Colour colour, float fontSize,
                          juce::Justification just = juce::Justification::centredLeft,
                          bool bold = false);
    juce::ComboBox& addCombo(std::initializer_list<const char*> items, int selected = 1);
    juce::TextButton& addChip(juce::String text, juce::Colour accent);
    WaveThumb& addWaveThumb(WaveThumb::Shape s, juce::Colour accent);
    BarCell& addBar(juce::String caption, juce::Colour accent, double def);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthComponent)
};
