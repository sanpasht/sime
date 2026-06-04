#include "SynthComponent.h"
#include "SynthRenderer.h"

// ============================================================================
//  Palette
// ============================================================================
namespace
{
    const juce::Colour kBg        { 0xff070910 };
    const juce::Colour kPanel     { 0xff0f1320 };
    const juce::Colour kPanelHi   { 0xff141a2b };
    const juce::Colour kBorder    { 0xff222a3f };
    const juce::Colour kText      { 0xffd6def0 };
    const juce::Colour kTextDim   { 0xff6b7593 };
    const juce::Colour kRed       { 0xffff2e44 };
    const juce::Colour kCyan      { 0xff20e3e3 };
    const juce::Colour kPurple    { 0xff9b59f6 };
    const juce::Colour kGreen     { 0xff2ee6a0 };
    const juce::Colour kBlue      { 0xff3f8cff };

    int argbToInt(juce::Colour c) { return (int) c.getARGB(); }
}

// ============================================================================
//  SynthLookAndFeel
// ============================================================================
SynthLookAndFeel::SynthLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, kPanelHi);
    setColour(juce::ComboBox::textColourId,       kText);
    setColour(juce::ComboBox::outlineColourId,    kBorder);
    setColour(juce::ComboBox::arrowColourId,      kRed);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff10131d));
    setColour(juce::PopupMenu::textColourId,       kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kRed.withAlpha(0.30f));
    setColour(juce::Slider::textBoxTextColourId,   kText);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Colour SynthLookAndFeel::accentOf(const juce::Component& c, juce::Colour fallback)
{
    auto v = c.getProperties().getWithDefault("accent", argbToInt(fallback));
    return juce::Colour((juce::uint32) (int) v);
}

void SynthLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float pos, float startAngle, float endAngle,
                                        juce::Slider& s)
{
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const juce::Colour accent = accentOf(s, kRed);

    // Outer dished ring
    g.setColour(juce::Colour(0xff05070d));
    g.fillEllipse(juce::Rectangle<float>(radius * 2, radius * 2).withCentre(centre));

    // Track arc
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f,
                        0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff1b2030));
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Value arc with layered glow
    juce::Path val;
    val.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f,
                      0.0f, startAngle, angle, true);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour(accent.withAlpha(0.12f * (float) i));
        g.strokePath(val, juce::PathStrokeType(3.0f + i * 2.4f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
    g.setColour(accent);
    g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // Knob cap
    const float capR = radius * 0.62f;
    juce::ColourGradient cap(juce::Colour(0xff232a3c), centre.x, centre.y - capR,
                             juce::Colour(0xff0c0f18), centre.x, centre.y + capR, false);
    g.setGradientFill(cap);
    g.fillEllipse(juce::Rectangle<float>(capR * 2, capR * 2).withCentre(centre));
    g.setColour(accent.withAlpha(0.35f));
    g.drawEllipse(juce::Rectangle<float>(capR * 2, capR * 2).withCentre(centre), 1.0f);

    // Pointer
    juce::Point<float> tip(centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * capR,
                           centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * capR);
    // addCentredArc uses 0 at 12 o'clock; convert by rotating -90°.  Simpler: recompute.
    const float a2 = angle;
    juce::Point<float> p1(centre.x + std::sin(a2) * (capR * 0.30f),
                          centre.y - std::cos(a2) * (capR * 0.30f));
    juce::Point<float> p2(centre.x + std::sin(a2) * capR,
                          centre.y - std::cos(a2) * capR);
    g.setColour(accent.brighter(0.3f));
    g.drawLine({ p1, p2 }, 2.4f);
    g.setColour(accent);
    g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(p2));
    juce::ignoreUnused(tip);
}

void SynthLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
{
    const juce::Colour accent = accentOf(s, kRed);

    if (style == juce::Slider::LinearVertical)
    {
        auto track = juce::Rectangle<float>(x + w * 0.5f - 3.0f, (float) y, 6.0f, (float) h);
        g.setColour(juce::Colour(0xff161b29));
        g.fillRoundedRectangle(track, 3.0f);

        auto filled = track.withTop(sliderPos);
        for (int i = 2; i >= 1; --i)
        {
            g.setColour(accent.withAlpha(0.15f * (float) i));
            g.fillRoundedRectangle(filled.expanded((float) i * 1.2f, 0.0f), 3.0f);
        }
        g.setColour(accent);
        g.fillRoundedRectangle(filled, 3.0f);

        // Thumb
        auto thumb = juce::Rectangle<float>(16.0f, 8.0f)
                        .withCentre({ track.getCentreX(), sliderPos });
        g.setColour(juce::Colour(0xff2a3147));
        g.fillRoundedRectangle(thumb, 2.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(thumb, 2.0f, 1.0f);
        return;
    }

    LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, s);
}

void SynthLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const juce::Colour accent = accentOf(box, kRed);
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h);
    g.setColour(kPanelHi);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    arrow.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour(accent);
    g.fillPath(arrow);
}

juce::Font SynthLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(12.0f, juce::Font::bold);
}

// ============================================================================
//  SynthPanel
// ============================================================================
SynthPanel::SynthPanel(juce::String title, juce::Colour accent)
    : title_(std::move(title)), accent_(accent) {}

juce::Rectangle<int> SynthPanel::bodyBounds() const
{
    return getLocalBounds().reduced(10).withTrimmedTop(22);
}

void SynthPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient bg(kPanelHi, r.getCentreX(), r.getY(),
                            kPanel, r.getCentreX(), r.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 7.0f);

    g.setColour(kBorder);
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);

    // Header
    g.setColour(accent_);
    g.fillRoundedRectangle(juce::Rectangle<float>(r.getX() + 10, r.getY() + 10, 3.0f, 12.0f), 1.5f);
    g.setColour(accent_.withAlpha(0.95f));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(title_.toUpperCase(),
               juce::Rectangle<int>((int) r.getX() + 18, (int) r.getY() + 8, getWidth() - 24, 16),
               juce::Justification::centredLeft, true);

    // Accent underline glow
    g.setColour(accent_.withAlpha(0.25f));
    g.fillRect(juce::Rectangle<float>(r.getX() + 10, r.getY() + 26, r.getWidth() - 20, 1.0f));
}

// ============================================================================
//  KnobCell / BarCell
// ============================================================================
KnobCell::KnobCell(juce::String captionText, juce::Colour accent,
                   double lo, double hi, double def, double skewMid)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setRange(lo, hi, 0.0001);
    if (skewMid > lo && skewMid < hi)
        slider.setSkewFactorFromMidPoint(skewMid);
    slider.setValue(def, juce::dontSendNotification);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set("accent", argbToInt(accent));
    addAndMakeVisible(slider);

    caption.setText(captionText.toUpperCase(), juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    caption.setFont(juce::Font(9.5f, juce::Font::bold));
    caption.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(caption);
}

void KnobCell::resized()
{
    auto b = getLocalBounds();
    caption.setBounds(b.removeFromBottom(13));
    slider.setBounds(b);
}

BarCell::BarCell(juce::String captionText, juce::Colour accent,
                 double lo, double hi, double def)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setRange(lo, hi, 0.0001);
    slider.setValue(def, juce::dontSendNotification);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.getProperties().set("accent", argbToInt(accent));
    addAndMakeVisible(slider);

    caption.setText(captionText, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    caption.setFont(juce::Font(9.5f, juce::Font::bold));
    caption.setColour(juce::Label::textColourId, kTextDim);
    addAndMakeVisible(caption);
}

void BarCell::resized()
{
    auto b = getLocalBounds();
    caption.setBounds(b.removeFromBottom(13));
    slider.setBounds(b);
}

// ============================================================================
//  WaveThumb
// ============================================================================
void WaveThumb::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    juce::Path p;
    const int n = 80;
    const float midY = r.getCentreY();
    const float amp  = r.getHeight() * 0.34f;
    juce::Random rng(1234);

    for (int i = 0; i <= n; ++i)
    {
        const float t = i / (float) n;
        const float ph = t * juce::MathConstants<float>::twoPi * 2.0f;
        float y = 0.0f;
        switch (shape_)
        {
            case Shape::Sine:     y = std::sin(ph); break;
            case Shape::Square:   y = std::sin(ph) >= 0 ? 1.0f : -1.0f; break;
            case Shape::Saw:      y = 2.0f * (std::fmod(t * 2.0f, 1.0f)) - 1.0f; break;
            case Shape::Triangle: y = 2.0f * std::abs(2.0f * std::fmod(t * 2.0f, 1.0f) - 1.0f) - 1.0f; break;
            case Shape::Random:   y = std::sin(ph) * 0.6f + (rng.nextFloat() - 0.5f) * 0.5f; break;
            case Shape::Noise:    y = (rng.nextFloat() * 2.0f - 1.0f); break;
        }
        const float px = r.getX() + t * r.getWidth();
        const float py = midY - y * amp;
        if (i == 0) p.startNewSubPath(px, py);
        else        p.lineTo(px, py);
    }

    g.setColour(accent_.withAlpha(0.25f));
    g.strokePath(p, juce::PathStrokeType(3.0f));
    g.setColour(accent_);
    g.strokePath(p, juce::PathStrokeType(1.4f));
}

// ============================================================================
//  EnvCurve
// ============================================================================
void EnvCurve::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 3.0f);

    juce::Array<float> v = getAdsr ? getAdsr() : juce::Array<float>{ 0.1f, 0.2f, 0.6f, 0.3f };
    const float a = v[0], d = v[1], s = v[2], rel = v[3];
    const float total = a + d + 1.0f + rel;   // 1.0 = sustain hold span

    auto xAt = [&](float t) { return r.getX() + (t / total) * r.getWidth(); };
    auto yAt = [&](float lvl) { return r.getBottom() - lvl * r.getHeight(); };

    juce::Path p;
    p.startNewSubPath(xAt(0), yAt(0));
    p.lineTo(xAt(a), yAt(1.0f));
    p.lineTo(xAt(a + d), yAt(s));
    p.lineTo(xAt(a + d + 1.0f), yAt(s));
    p.lineTo(xAt(total), yAt(0));

    auto fill = p;
    fill.lineTo(xAt(total), r.getBottom());
    fill.lineTo(xAt(0), r.getBottom());
    fill.closeSubPath();
    g.setColour(accent.withAlpha(0.14f));
    g.fillPath(fill);

    g.setColour(accent.withAlpha(0.3f));
    g.strokePath(p, juce::PathStrokeType(3.0f));
    g.setColour(accent);
    g.strokePath(p, juce::PathStrokeType(1.6f));
}

// ============================================================================
//  FilterCurve
// ============================================================================
void FilterCurve::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 3.0f);

    const float cut = getCutoffNorm ? getCutoffNorm() : 0.5f;
    const float res = getResNorm ? getResNorm() : 0.3f;
    const float kneeX = r.getX() + juce::jlimit(0.05f, 0.92f, cut) * r.getWidth();

    juce::Path p;
    p.startNewSubPath(r.getX(), r.getCentreY() - r.getHeight() * 0.12f);
    p.lineTo(kneeX - r.getWidth() * 0.06f, r.getCentreY() - r.getHeight() * 0.12f);
    // resonance bump
    p.quadraticTo(kneeX, r.getY() - res * 6.0f, kneeX + r.getWidth() * 0.02f,
                  r.getCentreY() - r.getHeight() * 0.12f - res * r.getHeight() * 0.25f);
    p.quadraticTo(kneeX + r.getWidth() * 0.10f, r.getBottom(),
                  r.getRight(), r.getBottom());

    g.setColour(accent.withAlpha(0.3f));
    g.strokePath(p, juce::PathStrokeType(3.0f));
    g.setColour(accent);
    g.strokePath(p, juce::PathStrokeType(1.6f));
}

// ============================================================================
//  StepBars
// ============================================================================
StepBars::StepBars()
{
    juce::Random rng(7);
    for (int i = 0; i < 16; ++i)
        steps_.add(0.25f + rng.nextFloat() * 0.7f);
}

void StepBars::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 3.0f);

    const float bw = r.getWidth() / steps_.size();
    for (int i = 0; i < steps_.size(); ++i)
    {
        const float h = steps_[i] * r.getHeight();
        auto bar = juce::Rectangle<float>(r.getX() + i * bw + 1.0f,
                                          r.getBottom() - h, bw - 2.0f, h);
        g.setColour(accent.withAlpha(0.30f + 0.5f * steps_[i]));
        g.fillRoundedRectangle(bar, 1.5f);
    }
}

// ============================================================================
//  XYPad
// ============================================================================
void XYPad::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    // crosshair
    g.setColour(juce::Colour(0xff1a2032));
    g.drawLine(r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
    g.drawLine(r.getX(), r.getCentreY(), r.getRight(), r.getCentreY());

    const float px = r.getX() + pos.x * r.getWidth();
    const float py = r.getY() + (1.0f - pos.y) * r.getHeight();
    for (int i = 3; i >= 1; --i)
    {
        g.setColour(accent.withAlpha(0.18f * i));
        g.fillEllipse(juce::Rectangle<float>(8.0f + i * 4, 8.0f + i * 4).withCentre({ px, py }));
    }
    g.setColour(accent);
    g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ px, py }));
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    pos.x = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    pos.y = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight());
    repaint();
}

// ============================================================================
//  LevelMeter
// ============================================================================
void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff080b13));
    g.fillRoundedRectangle(r, 2.0f);

    const int segs = 14;
    const float segH = r.getHeight() / segs;
    for (int i = 0; i < segs; ++i)
    {
        const float frac = (segs - i) / (float) segs;
        const bool on = frac <= level;
        juce::Colour c = frac > 0.85f ? kRed : (frac > 0.6f ? juce::Colour(0xfff0a020) : kGreen);
        g.setColour(on ? c : c.withAlpha(0.12f));
        g.fillRect(juce::Rectangle<float>(r.getX() + 1, r.getY() + i * segH + 1,
                                          r.getWidth() - 2, segH - 1.5f));
    }
}

// ============================================================================
//  MiniKeyboard
// ============================================================================
namespace { const int kWhiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 }; }

void MiniKeyboard::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / numWhite;

    // White keys
    for (int i = 0; i < numWhite; ++i)
    {
        const int note = startNote + (i / 7) * 12 + kWhiteSemis[i % 7];
        auto key = juce::Rectangle<float>(i * ww, 0, ww - 1.0f, r.getHeight());
        juce::ColourGradient grad(juce::Colour(0xffe9edf6), key.getCentreX(), key.getY(),
                                  juce::Colour(0xffbcc4d6), key.getCentreX(), key.getBottom(), false);
        g.setGradientFill(grad);
        if (note == pressed_) g.setColour(kRed.brighter(0.2f));
        g.fillRoundedRectangle(key, 2.0f);
        g.setColour(juce::Colour(0xff2a3045));
        g.drawRoundedRectangle(key, 2.0f, 0.8f);
    }

    // Black keys
    const float bh = r.getHeight() * 0.62f;
    const float bw = ww * 0.62f;
    for (int i = 0; i < numWhite; ++i)
    {
        const int idx = i % 7;
        const bool hasBlack = (idx == 0 || idx == 1 || idx == 3 || idx == 4 || idx == 5);
        if (!hasBlack || i == numWhite - 1) continue;
        const int note = startNote + (i / 7) * 12 + kWhiteSemis[idx] + 1;
        auto key = juce::Rectangle<float>((i + 1) * ww - bw * 0.5f, 0, bw, bh);
        g.setColour(note == pressed_ ? kRed : juce::Colour(0xff0b0e16));
        g.fillRoundedRectangle(key, 2.0f);
        g.setColour(juce::Colour(0xff2a3045));
        g.drawRoundedRectangle(key, 2.0f, 0.8f);
    }
}

int MiniKeyboard::noteAt(juce::Point<float> p) const
{
    const float ww = getWidth() / (float) numWhite;
    const float bh = getHeight() * 0.62f;
    const float bw = ww * 0.62f;

    for (int i = 0; i < numWhite - 1; ++i)
    {
        const int idx = i % 7;
        const bool hasBlack = (idx == 0 || idx == 1 || idx == 3 || idx == 4 || idx == 5);
        if (!hasBlack) continue;
        const float bx = (i + 1) * ww - bw * 0.5f;
        if (p.x >= bx && p.x < bx + bw && p.y < bh)
            return startNote + (i / 7) * 12 + kWhiteSemis[idx] + 1;
    }

    const int wi = juce::jlimit(0, numWhite - 1, (int) (p.x / ww));
    return startNote + (wi / 7) * 12 + kWhiteSemis[wi % 7];
}

void MiniKeyboard::mouseDown(const juce::MouseEvent& e)
{
    pressed_ = noteAt(e.position);
    repaint();
    if (onNoteOn) onNoteOn(pressed_);
}

void MiniKeyboard::mouseUp(const juce::MouseEvent&)
{
    pressed_ = -1;
    repaint();
}

// ============================================================================
//  SynthComponent
// ============================================================================
SynthComponent::SynthComponent()
{
    setOpaque(true);
    setLookAndFeel(&lnf_);

    // ── Header ────────────────────────────────────────────────────────────
    presetName_.setJustification(juce::Justification::centred);
    presetName_.setFont(juce::Font(15.0f, juce::Font::bold));
    presetName_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0b0f1a));
    presetName_.setColour(juce::TextEditor::textColourId, kCyan);
    presetName_.setColour(juce::TextEditor::outlineColourId, kBorder);
    presetName_.setColour(juce::TextEditor::focusedOutlineColourId, kCyan);
    presetName_.setTextToShowWhenEmpty("Enter File Name", kTextDim);
    presetName_.setText({}, juce::dontSendNotification);
    addAndMakeVisible(presetName_);

    auto styleActionBtn = [this](juce::TextButton& b, juce::Colour accent)
    {
        b.setColour(juce::TextButton::buttonColourId, accent.withAlpha(0.18f));
        b.setColour(juce::TextButton::textColourOffId, accent.brighter(0.3f));
        addAndMakeVisible(b);
    };
    previewBtn_.setButtonText("PREVIEW");
    exportBtn_.setButtonText("EXPORT WAV");
    styleActionBtn(previewBtn_, kCyan);
    styleActionBtn(exportBtn_, kGreen);
    previewBtn_.onClick = [this] { previewPatch(); };
    exportBtn_.onClick  = [this] { exportPatch(); };

    statusLabel_.setFont(juce::Font(11.0f));
    statusLabel_.setColour(juce::Label::textColourId, kTextDim);
    statusLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel_);

    addAndMakeVisible(masterMeter_);

    // ── Panels ──────────────────────────────────────────────────────────────
    addPanel("Oscillators", kRed);
    addPanel("Mixer",       kRed);
    addPanel("Amp Envelope",kRed);
    addPanel("Filter",      kRed);
    addPanel("Modulation",  kCyan);
    addPanel("Effects",     kPurple);
    addPanel("Voice",       kCyan);
    addPanel("Mod Matrix",  kRed);
    addPanel("Arp / Seq",   kRed);
    addPanel("Performance", kRed);
    addPanel("Master",      kRed);
    addPanel("Macro",       kPurple);

    // ── Oscillators (OSC1 functional waveform) ───────────────────────────────
    osc1Wave_.addItem("SINE", 1);
    osc1Wave_.addItem("SQUARE", 2);
    osc1Wave_.addItem("SAW", 3);
    osc1Wave_.addItem("TRIANGLE", 4);
    osc1Wave_.setSelectedId(3, juce::dontSendNotification);
    osc1Wave_.getProperties().set("accent", argbToInt(kRed));
    addAndMakeVisible(osc1Wave_);

    addWaveThumb(WaveThumb::Shape::Saw, kRed);       // osc1 thumb
    addKnob("Detune", kRed, 0, 1, 0.3);
    addKnob("Blend",  kRed, 0, 1, 0.5);

    addCombo({ "SQUARE", "SINE", "SAW", "TRIANGLE" }, 1);   // osc2
    addWaveThumb(WaveThumb::Shape::Square, kRed);
    addKnob("Detune", kRed, 0, 1, 0.4);
    addKnob("Blend",  kRed, 0, 1, 0.45);

    addCombo({ "TRIANGLE", "SINE", "SAW", "SQUARE" }, 1);   // osc3
    addWaveThumb(WaveThumb::Shape::Triangle, kRed);
    addKnob("Detune", kRed, 0, 1, 0.5);
    addKnob("Blend",  kRed, 0, 1, 0.35);

    addCombo({ "SINE", "SQUARE", "SAW" }, 1);               // sub
    addKnob("Level", kRed, 0, 1, 0.55);

    addKnob("Level", kPurple, 0, 1, 0.4);                   // noise level
    addKnob("Time",  kPurple, 0, 1, 0.25);                  // glide

    // ── Mixer ────────────────────────────────────────────────────────────────
    for (auto* nm : { "OSC 1", "OSC 2", "OSC 3", "SUB", "NOISE" })
        addKnob(nm, kRed, 0, 1, 0.6);

    // ── Amp envelope (functional ADSR) ───────────────────────────────────────
    for (auto* b : { &attackBar_, &decayBar_, &sustainBar_, &releaseBar_ })
    {
        addAndMakeVisible(b);
        b->slider.addListener(this);
    }
    envCurve_.accent = kRed;
    envCurve_.getAdsr = [this]
    {
        return juce::Array<float>{
            (float) (attackBar_.slider.getValue() / 2.0),
            (float) (decayBar_.slider.getValue() / 2.0),
            (float) sustainBar_.slider.getValue(),
            (float) (releaseBar_.slider.getValue() / 3.0) };
    };
    addAndMakeVisible(envCurve_);

    // ── Filter (functional cutoff + res) ──────────────────────────────────────
    addCombo({ "LOW PASS 24", "LOW PASS 12", "HIGH PASS", "BAND PASS" }, 1); // filter type

    cutoffKnob_ = std::make_unique<KnobCell>("Cutoff", kRed, 80.0, 12000.0, 4200.0, 1200.0);
    cutoffKnob_->slider.addListener(this);
    addAndMakeVisible(*cutoffKnob_);

    resKnob_ = std::make_unique<KnobCell>("Res", kRed, 0.0, 1.0, 0.35);
    resKnob_->slider.addListener(this);
    addAndMakeVisible(*resKnob_);

    addKnob("Drive", kRed, 0, 1, 0.2);

    filterCurve_.accent = kRed;
    filterCurve_.getCutoffNorm = [this]
    {
        return (float) ((cutoffKnob_->slider.getValue() - 80.0) / (12000.0 - 80.0));
    };
    filterCurve_.getResNorm = [this] { return (float) resKnob_->slider.getValue(); };
    addAndMakeVisible(filterCurve_);

    // ── Modulation: LFO1 / LFO2 ───────────────────────────────────────────────
    for (int lfo = 0; lfo < 2; ++lfo)
    {
        addCombo({ "SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM" }, 1);
        addWaveThumb(lfo == 0 ? WaveThumb::Shape::Sine : WaveThumb::Shape::Triangle, kCyan);
        addKnob("Rate", kCyan, 0, 1, 0.4);
        addKnob("Fade", kCyan, 0, 1, 0.3);
        addChip("SYNC", kCyan);
    }

    // ── Effects ────────────────────────────────────────────────────────────────
    {
        struct FX { const char* name; juce::Colour c; };
        const FX fx[] = { { "CHORUS", kPurple }, { "DELAY", kCyan },
                          { "REVERB", kCyan }, { "DISTORTION", juce::Colour(0xfff0c020) } };
        for (auto& f : fx)
        {
            addLabel(f.name, f.c, 12.0f, juce::Justification::centredLeft, true);
            addKnob("", f.c, 0, 1, 0.4);
        }
    }

    // ── Voice (LFO3) ───────────────────────────────────────────────────────────
    addWaveThumb(WaveThumb::Shape::Random, kCyan);
    addCombo({ "RANDOM", "SINE", "SAW", "S&H" }, 1);
    addKnob("Rate",   kCyan, 0, 1, 0.4);
    addKnob("Smooth", kCyan, 0, 1, 0.5);
    addKnob("Delay",  kCyan, 0, 1, 0.2);

    // ── Mod matrix rows ──────────────────────────────────────────────────────
    {
        const char* rows[] = { "LFO 1 \xe2\x86\x92 CUTOFF",
                               "LFO 2 \xe2\x86\x92 PITCH",
                               "LFO 3 \xe2\x86\x92 PAN",
                               "ENV 1 \xe2\x86\x92 VOLUME" };
        const char* vals[] = { "37%", "22%", "15%", "66%" };
        for (int i = 0; i < 4; ++i)
        {
            addLabel(rows[i], kText, 11.0f, juce::Justification::centredLeft);
            addLabel(vals[i], kRed,  11.0f, juce::Justification::centredRight, true);
        }
    }

    // ── Arp / Seq ──────────────────────────────────────────────────────────────
    stepBars_.accent = kRed;
    addAndMakeVisible(stepBars_);
    for (auto* nm : { "1/16", "UP", "1 OCT", "80%", "12%" })
        addLabel(nm, kTextDim, 10.0f, juce::Justification::centred);

    // ── Performance ──────────────────────────────────────────────────────────
    addBar("VEL",  kRed, 0.6);
    addBar("PRES", kRed, 0.5);
    xyPad_.accent = kPurple;
    addAndMakeVisible(xyPad_);

    // ── Master (functional gain) ──────────────────────────────────────────────
    masterKnob_ = std::make_unique<KnobCell>("Master", kRed, 0.0, 1.0, 0.75);
    masterKnob_->slider.addListener(this);
    addAndMakeVisible(*masterKnob_);

    // ── Macros ───────────────────────────────────────────────────────────────
    {
        const juce::Colour mc[] = { kRed, kBlue, kGreen, kPurple };
        for (int i = 0; i < 4; ++i)
            addKnob("MACRO " + juce::String(i + 1), mc[i], 0, 1, 0.5);
    }

    // ── Keyboard ───────────────────────────────────────────────────────────────
    keyboard_.onNoteOn = [this](int midi)
    {
        patch_.midiNote = midi;
        previewPatch();
    };
    addAndMakeVisible(keyboard_);

    // pitch / mod wheels
    addBar("PITCH", kRed, 0.5);
    addBar("MOD",   kRed, 0.0);

    syncPatchFromUi();
}

SynthComponent::~SynthComponent()
{
    setLookAndFeel(nullptr);
}

// ── Factory helpers ──────────────────────────────────────────────────────────
SynthPanel& SynthComponent::addPanel(juce::String title, juce::Colour accent)
{
    auto* p = new SynthPanel(std::move(title), accent);
    panels_.add(p);
    addAndMakeVisible(p);
    return *p;
}

KnobCell& SynthComponent::addKnob(juce::String caption, juce::Colour accent,
                                  double lo, double hi, double def, double skewMid)
{
    auto* k = new KnobCell(std::move(caption), accent, lo, hi, def, skewMid);
    knobs_.add(k);
    addAndMakeVisible(k);
    return *k;
}

juce::Label& SynthComponent::addLabel(juce::String text, juce::Colour colour, float fontSize,
                                      juce::Justification just, bool bold)
{
    auto* l = new juce::Label({}, text);
    l->setFont(juce::Font(fontSize, bold ? juce::Font::bold : juce::Font::plain));
    l->setColour(juce::Label::textColourId, colour);
    l->setJustificationType(just);
    labels_.add(l);
    addAndMakeVisible(l);
    return *l;
}

juce::ComboBox& SynthComponent::addCombo(std::initializer_list<const char*> items, int selected)
{
    auto* c = new juce::ComboBox();
    int id = 1;
    for (auto* it : items) c->addItem(it, id++);
    c->setSelectedId(selected, juce::dontSendNotification);
    c->getProperties().set("accent", argbToInt(kCyan));
    combos_.add(c);
    addAndMakeVisible(c);
    return *c;
}

juce::TextButton& SynthComponent::addChip(juce::String text, juce::Colour accent)
{
    auto* b = new juce::TextButton(text);
    b->setClickingTogglesState(true);
    b->setColour(juce::TextButton::buttonColourId, kPanelHi);
    b->setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.4f));
    b->setColour(juce::TextButton::textColourOffId, kTextDim);
    b->setColour(juce::TextButton::textColourOnId, accent.brighter(0.4f));
    chips_.add(b);
    addAndMakeVisible(b);
    return *b;
}

WaveThumb& SynthComponent::addWaveThumb(WaveThumb::Shape s, juce::Colour accent)
{
    auto* w = new WaveThumb(s, accent);
    waveThumbs_.add(w);
    addAndMakeVisible(w);
    return *w;
}

BarCell& SynthComponent::addBar(juce::String caption, juce::Colour accent, double def)
{
    auto* b = new BarCell(std::move(caption), accent, 0.0, 1.0, def);
    bars_.add(b);
    addAndMakeVisible(b);
    return *b;
}

// ── Painting ──────────────────────────────────────────────────────────────────
void SynthComponent::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    // subtle grid glow at top
    auto header = getLocalBounds().removeFromTop(94);
    juce::ColourGradient hg(juce::Colour(0xff10131f), header.getCentreX(), 0,
                            kBg, header.getCentreX(), (float) header.getBottom(), false);
    g.setGradientFill(hg);
    g.fillRect(header);

    // Logo
    g.setColour(kCyan);
    g.setFont(juce::Font(30.0f, juce::Font::bold));
    g.drawText("SIME", 22, 30, 160, 32, juce::Justification::centredLeft);
    g.setColour(kTextDim);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("SPATIALLY-INTERPRETED", 24, 64, 200, 12, juce::Justification::topLeft);
    g.drawText("MUSIC ENGINE",          24, 75, 200, 12, juce::Justification::topLeft);

    // Title — vertically centred on the control row
    g.setColour(kRed);
    g.setFont(juce::Font(21.0f, juce::Font::bold));
    g.drawText("SYNTHESIZER", 214, 45, 230, 30, juce::Justification::centredLeft);

    g.setColour(kTextDim);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("MASTER", getWidth() - 150, 16, 60, 14, juce::Justification::centredLeft);
}

// ── Layout ──────────────────────────────────────────────────────────────────
void SynthComponent::resized()
{
    const float sx = getWidth()  / 1024.0f;
    const float sy = getHeight() / 600.0f;
    auto R = [&](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>((int) (x * sx), (int) (y * sy),
                                    (int) (w * sx), (int) (h * sy));
    };

    // ── Header controls ──────────────────────────────────────────────────────
    // File-name + Preview + Export group, pulled left toward the title.
    presetName_.setBounds (R(360, 36, 210, 28));
    previewBtn_.setBounds (R(580, 36, 76,  28));
    exportBtn_.setBounds  (R(664, 36, 92,  28));
    statusLabel_.setBounds(R(360, 66, 396, 16));
    masterMeter_.setBounds  (R(984, 18, 16, 72));
    masterKnob_->setBounds  (R(900, 14, 78, 80));

    // ── Panels ────────────────────────────────────────────────────────────────
    // 0 Osc 1 Mixer 2 Amp 3 Filter 4 Modulation 5 Effects
    // 6 Voice 7 ModMatrix 8 Arp 9 Performance 10 Master(header) 11 Macro
    panels_[0]->setBounds(R(8,   104, 250, 414));   // Oscillators (tall)
    panels_[1]->setBounds(R(266, 104, 78,  266));   // Mixer
    panels_[2]->setBounds(R(352, 104, 146, 266));   // Amp Envelope
    panels_[3]->setBounds(R(506, 104, 122, 266));   // Filter
    panels_[4]->setBounds(R(636, 104, 236, 266));   // Modulation
    panels_[5]->setBounds(R(880, 104, 136, 414));   // Effects (tall)
    panels_[6]->setBounds(R(266, 378, 150, 140));   // Voice
    panels_[7]->setBounds(R(424, 378, 170, 140));   // Mod Matrix
    panels_[8]->setBounds(R(602, 378, 150, 140));   // Arp / Seq
    panels_[9]->setBounds(R(760, 378, 112, 140));   // Performance
    panels_[10]->setBounds(R(884, 8, 132, 88));     // Master (header frame)
    panels_[11]->setBounds(R(714, 526, 302, 66));   // Macro

    // master panel sits behind header master controls — push to back
    panels_[10]->toBack();

    // ── Oscillators interior ───────────────────────────────────────────────────
    // knobs_ index tracker
    int ki = 0, ci = 0, wi = 0;
    auto osc = panels_[0]->getBounds();
    {
        int rowY = osc.getY() + 30;
        const int rowH = (osc.getHeight() - 36) / 5;
        auto place3 = [&](juce::ComboBox* combo, WaveThumb* thumb, int kA, int kB)
        {
            auto row = juce::Rectangle<int>(osc.getX() + 10, rowY, osc.getWidth() - 20, rowH).reduced(0, 4);
            auto left = row.removeFromLeft((int)(row.getWidth() * 0.42f));
            if (combo) combo->setBounds(left.removeFromTop(24));
            if (thumb) thumb->setBounds(left.withTrimmedTop(4));
            auto kw = row.getWidth() / 2;
            knobs_[kA]->setBounds(row.removeFromLeft(kw).reduced(2));
            knobs_[kB]->setBounds(row.reduced(2));
            rowY += rowH;
        };

        // OSC1 uses functional combo
        {
            auto row = juce::Rectangle<int>(osc.getX() + 10, rowY, osc.getWidth() - 20, rowH).reduced(0, 4);
            auto left = row.removeFromLeft((int)(row.getWidth() * 0.42f));
            osc1Wave_.setBounds(left.removeFromTop(24));
            waveThumbs_[wi++]->setBounds(left.withTrimmedTop(4));
            auto kw = row.getWidth() / 2;
            knobs_[ki++]->setBounds(row.removeFromLeft(kw).reduced(2)); // detune
            knobs_[ki++]->setBounds(row.reduced(2));                    // blend
            rowY += rowH;
        }
        place3(combos_[ci++], waveThumbs_[wi++], ki, ki + 1); ki += 2;  // OSC2
        place3(combos_[ci++], waveThumbs_[wi++], ki, ki + 1); ki += 2;  // OSC3

        // SUB row: combo + level
        {
            auto row = juce::Rectangle<int>(osc.getX() + 10, rowY, osc.getWidth() - 20, rowH).reduced(0, 4);
            combos_[ci++]->setBounds(row.removeFromLeft((int)(row.getWidth() * 0.42f)).removeFromTop(24));
            knobs_[ki++]->setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2)); // sub level
            rowY += rowH;
        }
        // NOISE row: level + glide
        {
            auto row = juce::Rectangle<int>(osc.getX() + 10, rowY, osc.getWidth() - 20, rowH).reduced(0, 4);
            row.removeFromLeft((int)(row.getWidth() * 0.42f));
            knobs_[ki++]->setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2)); // noise level
            knobs_[ki++]->setBounds(row.reduced(2));                                    // glide
        }
    }

    // ── Mixer interior (5 small knobs) ──────────────────────────────────────────
    {
        auto b = panels_[1]->bodyBounds().translated(panels_[1]->getX(), panels_[1]->getY());
        const int n = 5;
        const int rh = b.getHeight() / n;
        for (int i = 0; i < n; ++i)
            knobs_[ki++]->setBounds(juce::Rectangle<int>(b.getX(), b.getY() + i * rh, b.getWidth(), rh).reduced(4, 2));
    }

    // ── Amp envelope (4 vertical bars + curve) ──────────────────────────────────
    {
        auto b = panels_[2]->bodyBounds().translated(panels_[2]->getX(), panels_[2]->getY());
        auto curve = b.removeFromBottom((int)(b.getHeight() * 0.34f));
        envCurve_.setBounds(curve.reduced(2, 4));
        const int bw = b.getWidth() / 4;
        attackBar_.setBounds (juce::Rectangle<int>(b.getX() + 0 * bw, b.getY(), bw, b.getHeight()).reduced(4, 2));
        decayBar_.setBounds  (juce::Rectangle<int>(b.getX() + 1 * bw, b.getY(), bw, b.getHeight()).reduced(4, 2));
        sustainBar_.setBounds(juce::Rectangle<int>(b.getX() + 2 * bw, b.getY(), bw, b.getHeight()).reduced(4, 2));
        releaseBar_.setBounds(juce::Rectangle<int>(b.getX() + 3 * bw, b.getY(), bw, b.getHeight()).reduced(4, 2));
    }

    // ── Filter interior ──────────────────────────────────────────────────────────
    {
        auto b = panels_[3]->bodyBounds().translated(panels_[3]->getX(), panels_[3]->getY());
        combos_[ci++]->setBounds(b.removeFromTop(26));
        auto curve = b.removeFromBottom((int)(b.getHeight() * 0.30f));
        filterCurve_.setBounds(curve.reduced(2, 4));
        cutoffKnob_->setBounds(b.removeFromTop((int)(b.getHeight() * 0.6f)).reduced(6));
        auto rr = b;
        resKnob_->setBounds(rr.removeFromLeft(rr.getWidth() / 2).reduced(2));
        knobs_[ki++]->setBounds(rr.reduced(2));   // drive
    }

    // ── Modulation (LFO1 / LFO2) ──────────────────────────────────────────────────
    {
        auto b = panels_[4]->bodyBounds().translated(panels_[4]->getX(), panels_[4]->getY());
        const int half = b.getWidth() / 2;
        for (int lfo = 0; lfo < 2; ++lfo)
        {
            auto col = juce::Rectangle<int>(b.getX() + lfo * half, b.getY(), half, b.getHeight()).reduced(4);
            combos_[ci++]->setBounds(col.removeFromTop(24));
            col.removeFromTop(4);
            waveThumbs_[wi++]->setBounds(col.removeFromTop((int)(col.getHeight() * 0.34f)));
            col.removeFromTop(4);
            auto chip = col.removeFromBottom(22);
            chips_[lfo]->setBounds(chip.reduced((int)(chip.getWidth() * 0.18f), 0));
            auto kw = col.getWidth() / 2;
            knobs_[ki++]->setBounds(col.removeFromLeft(kw).reduced(2)); // rate
            knobs_[ki++]->setBounds(col.reduced(2));                    // fade
        }
    }

    // ── Effects (4 rows) ───────────────────────────────────────────────────────────
    {
        auto b = panels_[5]->bodyBounds().translated(panels_[5]->getX(), panels_[5]->getY());
        const int rh = b.getHeight() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto row = juce::Rectangle<int>(b.getX(), b.getY() + i * rh, b.getWidth(), rh).reduced(2, 6);
            knobs_[ki++]->setBounds(row.removeFromRight(rh).reduced(2)); // fx knob (square)
            labels_[i]->setBounds(row);   // first 4 labels are FX names
        }
    }

    // ── Voice (LFO3) ─────────────────────────────────────────────────────────────
    {
        auto b = panels_[6]->bodyBounds().translated(panels_[6]->getX(), panels_[6]->getY());
        auto top = b.removeFromTop((int)(b.getHeight() * 0.45f));
        waveThumbs_[wi++]->setBounds(top.removeFromLeft((int)(top.getWidth() * 0.6f)).reduced(2));
        combos_[ci++]->setBounds(top.withSizeKeepingCentre(top.getWidth() - 4, 24));
        const int kw = b.getWidth() / 3;
        knobs_[ki++]->setBounds(juce::Rectangle<int>(b.getX() + 0 * kw, b.getY(), kw, b.getHeight()).reduced(2));
        knobs_[ki++]->setBounds(juce::Rectangle<int>(b.getX() + 1 * kw, b.getY(), kw, b.getHeight()).reduced(2));
        knobs_[ki++]->setBounds(juce::Rectangle<int>(b.getX() + 2 * kw, b.getY(), kw, b.getHeight()).reduced(2));
    }

    // ── Mod matrix (4 rows of label + value) — labels_ indices 4..11 ───────────────
    {
        auto b = panels_[7]->bodyBounds().translated(panels_[7]->getX(), panels_[7]->getY());
        const int rh = b.getHeight() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto row = juce::Rectangle<int>(b.getX(), b.getY() + i * rh, b.getWidth(), rh);
            labels_[4 + i * 2]->setBounds(row.removeFromLeft((int)(row.getWidth() * 0.72f)));
            labels_[4 + i * 2 + 1]->setBounds(row);
        }
    }

    // ── Arp / Seq (step bars + 5 small labels) — labels indices 12..16 ─────────────
    {
        auto b = panels_[8]->bodyBounds().translated(panels_[8]->getX(), panels_[8]->getY());
        stepBars_.setBounds(b.removeFromTop((int)(b.getHeight() * 0.6f)));
        const int lw = b.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
            labels_[12 + i]->setBounds(juce::Rectangle<int>(b.getX() + i * lw, b.getY(), lw, b.getHeight()));
    }

    // ── Performance (VEL/PRES bars + XY pad) — bars_ indices 0,1 ──────────────────
    {
        auto b = panels_[9]->bodyBounds().translated(panels_[9]->getX(), panels_[9]->getY());
        auto bars = b.removeFromLeft((int)(b.getWidth() * 0.42f));
        bars_[0]->setBounds(bars.removeFromLeft(bars.getWidth() / 2).reduced(2));
        bars_[1]->setBounds(bars.reduced(2));
        xyPad_.setBounds(b.reduced(2));
    }

    // ── Macros (4 knobs) — last 4 knobs ────────────────────────────────────────────
    {
        auto b = panels_[11]->bodyBounds().translated(panels_[11]->getX(), panels_[11]->getY());
        const int kw = b.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            knobs_[ki++]->setBounds(juce::Rectangle<int>(b.getX() + i * kw, b.getY(), kw, b.getHeight()).reduced(4, 2));
    }

    // ── Keyboard + pitch/mod wheels (bars indices 2,3) ───────────────────────────
    keyboard_.setBounds(R(120, 526, 584, 66));
    bars_[2]->setBounds(R(10, 526, 46, 66));
    bars_[3]->setBounds(R(62, 526, 46, 66));
}

// ── Audio glue ──────────────────────────────────────────────────────────────────
void SynthComponent::sliderValueChanged(juce::Slider*)
{
    syncPatchFromUi();
    envCurve_.repaint();
    filterCurve_.repaint();
    masterMeter_.level = (float) masterKnob_->slider.getValue();
    masterMeter_.repaint();
}

void SynthComponent::syncPatchFromUi()
{
    switch (osc1Wave_.getSelectedId())
    {
        case 1: patch_.waveform = SynthPatch::Waveform::Sine; break;
        case 2: patch_.waveform = SynthPatch::Waveform::Square; break;
        case 4: patch_.waveform = SynthPatch::Waveform::Triangle; break;
        default: patch_.waveform = SynthPatch::Waveform::Saw; break;
    }

    patch_.attackSec      = (float) attackBar_.slider.getValue();
    patch_.decaySec       = (float) decayBar_.slider.getValue();
    patch_.sustainLevel   = (float) sustainBar_.slider.getValue();
    patch_.releaseSec     = (float) releaseBar_.slider.getValue();
    patch_.filterCutoffHz = (float) cutoffKnob_->slider.getValue();
    patch_.filterResonance= (float) resKnob_->slider.getValue();
    patch_.masterGain     = (float) masterKnob_->slider.getValue();
    patch_.durationSec    = 1.4;
}

void SynthComponent::previewPatch()
{
    syncPatchFromUi();
    auto buf = SynthRenderer::render(patch_);
    if (onPreview) onPreview(buf);
    setStatusText("Preview \xe2\x96\xb6  note " + juce::String(patch_.midiNote));
}

void SynthComponent::exportPatch()
{
    syncPatchFromUi();
    auto buf = SynthRenderer::render(patch_);

    juce::String base = presetName_.getText()
                            .removeCharacters(":").trim()
                            .replaceCharacter(' ', '_')
                            .retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    if (base.isEmpty()) base = "synth_patch";

    if (onExport)
    {
        const juce::String rel = onExport(buf, base);
        setStatusText(rel.isNotEmpty() ? "Exported: " + rel : "Export failed.");
    }
}

SynthPatch SynthComponent::getPatch() const { return patch_; }

void SynthComponent::setStatusText(const juce::String& text)
{
    statusLabel_.setText(text, juce::dontSendNotification);
}
