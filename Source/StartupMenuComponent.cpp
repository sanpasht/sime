// ─────────────────────────────────────────────────────────────────────────────
// StartupMenuComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "StartupMenuComponent.h"

// ── Voxel S colour palette ────────────────────────────────────────────────────
static const juce::Colour kBlockColours[] =
{
    juce::Colour(0xffD93830), juce::Colour(0xff3D6FCC), juce::Colour(0xff33C050),
    juce::Colour(0xffEACC1F), juce::Colour(0xff2EC0C0),
    juce::Colour(0xffC060CC),
    juce::Colour(0xffD93830), juce::Colour(0xff3D6FCC), juce::Colour(0xff33C050),
    juce::Colour(0xffEACC1F),
    juce::Colour(0xff2EC0C0), juce::Colour(0xffC060CC),
    juce::Colour(0xffD93830), juce::Colour(0xff3D6FCC), juce::Colour(0xff33C050),
};

static const std::pair<int,int> kSBlocks[] =
{
    {0,1},{0,2},{0,3},
    {1,0},{1,1},
    {2,0},
    {3,1},{3,2},{3,3},
    {4,3},
    {5,2},{5,3},
    {6,0},{6,1},{6,2}
};

static const juce::Colour kSquareColours[] =
{
    juce::Colour(0xffD93830),
    juce::Colour(0xff3D6FCC),
    juce::Colour(0xff33C050),
    juce::Colour(0xffEACC1F),
};

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

StartupMenuComponent::StartupMenuComponent()
{
    setWantsKeyboardFocus(false);
    refreshRecentFiles();

    for (int i = 0; i < kMaxSquares; ++i)
    {
        squares_[i].t      = rng_.nextFloat();
        squares_[i].active = false;
    }

    startTimerHz(30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timer — advances square lifecycles
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::timerCallback()
{
    const float dt = 1.f / 30.f;

    for (int i = 0; i < kMaxSquares; ++i)
    {
        AnimSquare& sq = squares_[i];

        if (!sq.active)
        {
            if (getWidth() > 0 && rng_.nextFloat() < 0.08f)
                initSquare(sq);
            continue;
        }

        sq.t += sq.speed * dt;

        if (sq.t >= 1.f)
            initSquare(sq);
    }

    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::initSquare(AnimSquare& sq)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    if (w <= 0.f || h <= 0.f) return;

    const float safeMargin = 60.f;
    const float safeL = w * 0.5f - kCardW * 0.5f - safeMargin;
    const float safeR = w * 0.5f + kCardW * 0.5f + safeMargin;

    float x;
    if (safeL > 20.f && rng_.nextBool())
        x = rng_.nextFloat() * (safeL - 10.f) + 5.f;
    else if (safeR < w - 20.f)
        x = safeR + rng_.nextFloat() * (w - safeR - 10.f);
    else
        x = rng_.nextFloat() * w;

    sq.x        = x;
    sq.y        = rng_.nextFloat() * h;
    sq.maxSize  = 20.f + rng_.nextFloat() * 50.f;
    sq.t        = 0.f;
    sq.speed    = 0.18f + rng_.nextFloat() * 0.22f;
    sq.rotation = rng_.nextFloat() * 30.f - 15.f;
    sq.colour   = kSquareColours[rng_.nextInt(4)];
    sq.active   = true;
}

void StartupMenuComponent::drawSquares(juce::Graphics& g) const
{
    for (int i = 0; i < kMaxSquares; ++i)
    {
        const AnimSquare& sq = squares_[i];
        if (!sq.active) continue;

        const float opacity = std::sin(sq.t * juce::MathConstants<float>::pi);

        float sizeFrac;
        if      (sq.t < 0.3f) sizeFrac = sq.t / 0.3f;
        else if (sq.t > 0.8f) sizeFrac = 1.f - (sq.t - 0.8f) / 0.2f;
        else                   sizeFrac = 1.f;
        sizeFrac = juce::jlimit(0.f, 1.f, sizeFrac);

        const float sz = sq.maxSize * sizeFrac;
        if (sz < 1.f) continue;

        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(
            sq.rotation * juce::MathConstants<float>::pi / 180.f,
            sq.x, sq.y));

        g.setColour(sq.colour.withAlpha(opacity * 0.55f));
        g.fillRoundedRectangle(sq.x - sz * 0.5f, sq.y - sz * 0.5f,
                               sz, sz, sz * 0.12f);

        g.setColour(sq.colour.brighter(0.4f).withAlpha(opacity * 0.30f));
        g.drawRoundedRectangle(sq.x - sz * 0.5f, sq.y - sz * 0.5f,
                               sz, sz, sz * 0.12f, 1.2f);

        g.restoreState();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// refreshRecentFiles
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::refreshRecentFiles()
{
    recent_.clear();

    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("SIME");
    if (!dir.isDirectory()) return;

    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.sime");

    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() > b.getLastModificationTime(); });

    const auto now = juce::Time::getCurrentTime();

    const juce::Colour dots[] = {
        juce::Colour(0xff33C050), juce::Colour(0xff3D6FCC),
        juce::Colour(0xffD93830), juce::Colour(0xffEACC1F),
    };

    int count = 0;
    for (const auto& f : files)
    {
        if (count >= 5) break;
        juce::String name = f.getFileName();
        int days = (int)(now - f.getLastModificationTime()).inDays();
        juce::String age = (days == 0) ? "today"
                         : (days == 1) ? "yesterday"
                                       : juce::String(days) + " days ago";
        if (name == "autosave.sime") { continueAvailable_ = true; continue; }
        recent_.push_back({ name, age, f.getFullPathName(), dots[count % 4] });
        ++count;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout helpers
// ─────────────────────────────────────────────────────────────────────────────

float StartupMenuComponent::cardTop() const
{
    float h = 90.f + 36.f + 3.f * kBtnH + 2.f * kBtnGap + 36.f + 34.f;
    if (!recent_.empty())
        h += 32.f + (float)recent_.size() * kRecentRowH;
    return std::max(40.f, (getHeight() - h) / 2.f);
}

juce::Rectangle<float> StartupMenuComponent::newBtnRect() const
{
    return { cardLeft(), cardTop() + 90.f + 36.f, kCardW, kBtnH };
}
juce::Rectangle<float> StartupMenuComponent::openBtnRect() const
{
    return newBtnRect().translated(0.f, kBtnH + kBtnGap);
}
juce::Rectangle<float> StartupMenuComponent::continueBtnRect() const
{
    return openBtnRect().translated(0.f, kBtnH + kBtnGap);
}
juce::Rectangle<float> StartupMenuComponent::helpLinkRect() const
{
    auto r = continueBtnRect();
    return { r.getX(), r.getBottom() + 36.f, kCardW, 34.f };
}
juce::Rectangle<float> StartupMenuComponent::recentRowRect(int i) const
{
    auto r = helpLinkRect();
    return { r.getX(), r.getBottom() + 32.f + (float)i * kRecentRowH,
             kCardW, kRecentRowH - 4.f };
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw helpers
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::drawVoxelS(juce::Graphics& g,
                                      float x, float y,
                                      float block, float gap) const
{
    const float step = block + gap;
    int ci = 0;
    for (auto [row, col] : kSBlocks)
    {
        float bx = x + (float)col * step;
        float by = y + (float)row * step;
        g.setColour(kBlockColours[ci]);
        g.fillRoundedRectangle(bx, by, block, block, block * 0.15f);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRoundedRectangle(bx, by, block, block * 0.22f, block * 0.15f);
        ++ci;
    }
}

void StartupMenuComponent::drawFileIcon(juce::Graphics& g,
                                        float cx, float cy,
                                        float alpha) const
{
    const float iw = 13.f, ih = 16.f, fold = 5.f;
    const float ix = cx - iw * 0.5f, iy = cy - ih * 0.5f;

    juce::Path body;
    body.startNewSubPath(ix,             iy + fold);
    body.lineTo         (ix + iw - fold, iy);
    body.lineTo         (ix + iw,        iy + fold);
    body.lineTo         (ix + iw,        iy + ih);
    body.lineTo         (ix,             iy + ih);
    body.closeSubPath();

    g.setColour(juce::Colours::white.withAlpha(alpha * 0.85f));
    g.fillPath(body);

    juce::Path ear;
    ear.startNewSubPath(ix + iw - fold, iy);
    ear.lineTo         (ix + iw,        iy + fold);
    ear.lineTo         (ix + iw - fold, iy + fold);
    ear.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(alpha * 0.25f));
    g.fillPath(ear);

    g.setColour(juce::Colour(0xff1a1d2e).withAlpha(alpha * 0.45f));
    const float lx1 = ix + 2.5f, lx2 = ix + iw - 2.5f;
    for (float ly : { iy + ih * 0.42f, iy + ih * 0.58f, iy + ih * 0.74f })
        g.drawLine(lx1, ly, lx2, ly, 1.2f);
}

void StartupMenuComponent::drawActionBtn(juce::Graphics& g,
                                         juce::Rectangle<float> r,
                                         const juce::String& icon,
                                         const juce::String& label,
                                         bool primary,
                                         bool enabled,
                                         bool hovered) const
{
    const float alpha = enabled ? 1.f : 0.35f;

    if (primary)
    {
        g.setColour((hovered ? juce::Colour(0xffcc2820)
                             : juce::Colour(0xffD93830)).withAlpha(alpha));
    }
    else
    {
        g.setColour((hovered ? juce::Colour(0xff252840)
                             : juce::Colour(0xff1a1d2e)).withAlpha(alpha));
    }
    g.fillRoundedRectangle(r, 10.f);

    if (!primary)
    {
        g.setColour(juce::Colour(0xff3a3d55).withAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(0.5f), 10.f, 1.f);
    }

    const float iconCx = r.getX() + 32.f;
    const float iconCy = r.getCentreY();

    if (icon == "FILE")
    {
        drawFileIcon(g, iconCx, iconCy, alpha);
    }
    else
    {
        g.setFont(juce::Font(16.f));
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawText(icon,
                   (int)r.getX() + 20, (int)r.getY(),
                   24, (int)r.getHeight(),
                   juce::Justification::centredLeft, false);
    }

    g.setFont(juce::Font(15.f, primary ? juce::Font::bold : juce::Font::plain));
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.drawText(label,
               (int)r.getX() + 52, (int)r.getY(),
               (int)r.getWidth() - 60, (int)r.getHeight(),
               juce::Justification::centredLeft, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// paint
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2235));

    drawSquares(g);

    const float cl = cardLeft();
    const float ct = cardTop();

    // Logo
    const float block = 9.f, gap = 1.5f;
    const float sW = 4.f * block + 3.f * gap;
    const float sH = 7.f * block + 6.f * gap;
    drawVoxelS(g, cl, ct + (90.f - sH) / 2.f, block, gap);

    const float textX = cl + sW + 16.f;
    const float textW = kCardW - sW - 16.f;

    g.setFont(juce::Font("Arial", 34.f, juce::Font::plain));
    g.setColour(juce::Colour(0xffeeeeee));
    g.drawText("SIME", (int)textX, (int)(ct + 10.f), (int)textW, 36,
               juce::Justification::centredLeft, false);

    juce::AttributedString sub;
    sub.setJustification(juce::Justification::centredLeft);
    sub.append("SPATIALLY INTERPRETED MUSIC ENGINE",
               juce::Font(10.f), juce::Colour(0xff7a8099));
    sub.draw(g, juce::Rectangle<float>(textX, ct + 50.f, textW, 20.f));

    // Divider
    g.setColour(juce::Colour(0xff2e3245));
    g.drawLine(cl + kCardW * 0.35f, ct + 90.f + 16.f,
               cl + kCardW * 0.65f, ct + 90.f + 16.f, 1.f);

    // Buttons
    drawActionBtn(g, newBtnRect(),      "+",    "New scene",
                  true,  true,               hoveredBtn_ == 0);
    drawActionBtn(g, openBtnRect(),     "FILE", "Open scene",
                  false, true,               hoveredBtn_ == 1);
    drawActionBtn(g, continueBtnRect(), "+",    "Continue autosave.sime",
                  false, continueAvailable_, hoveredBtn_ == 2);

    // Divider
    g.setColour(juce::Colour(0xff2e3245));
    g.drawLine(cl + kCardW * 0.35f, continueBtnRect().getBottom() + 18.f,
               cl + kCardW * 0.65f, continueBtnRect().getBottom() + 18.f, 1.f);

    // Controls & Help link
    auto hlr = helpLinkRect();
    g.setFont(juce::Font(13.f));
    g.setColour((hoveredBtn_ == 5) ? juce::Colour(0xff9aa0bb)
                                   : juce::Colour(0xff5a6078));
    g.drawText("Controls & Help", hlr.toNearestInt(),
               juce::Justification::centred, false);

    // Recent files
    if (!recent_.empty())
    {
        g.setFont(juce::Font(10.f, juce::Font::bold));
        g.setColour(juce::Colour(0xff4a5070));
        g.drawText("RECENT", (int)cl, (int)(hlr.getBottom() + 6.f),
                   (int)kCardW, 22, juce::Justification::centredLeft, false);

        for (int i = 0; i < (int)recent_.size(); ++i)
        {
            auto rr     = recentRowRect(i);
            bool rowHov = (hoveredBtn_ == 10 + i);

            g.setColour(rowHov ? juce::Colour(0xff252840) : juce::Colour(0xff1a1d2e));
            g.fillRoundedRectangle(rr, 8.f);
            g.setColour(juce::Colour(0xff2e3245));
            g.drawRoundedRectangle(rr.reduced(0.5f), 8.f, 0.5f);

            const float dotSize = 9.f;
            g.setColour(recent_[i].dot);
            g.fillRoundedRectangle(rr.getX() + 14.f,
                                   rr.getCentreY() - dotSize * 0.5f,
                                   dotSize, dotSize, 2.f);

            g.setFont(juce::Font(13.f));
            g.setColour(juce::Colour(0xffd0d4e8));
            g.drawText(recent_[i].name,
                       (int)(rr.getX() + 34.f), (int)rr.getY(),
                       (int)(kCardW - 110.f), (int)rr.getHeight(),
                       juce::Justification::centredLeft, true);

            g.setFont(juce::Font(11.f));
            g.setColour(juce::Colour(0xff4a5070));
            g.drawText(recent_[i].age,
                       (int)(rr.getRight() - 90.f), (int)rr.getY(),
                       80, (int)rr.getHeight(),
                       juce::Justification::centredRight, false);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// resized / Mouse
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::resized() {}

void StartupMenuComponent::mouseMove(const juce::MouseEvent& e)
{
    int prev = hoveredBtn_;
    if      (newBtnRect()     .contains(e.position)) hoveredBtn_ = 0;
    else if (openBtnRect()    .contains(e.position)) hoveredBtn_ = 1;
    else if (continueBtnRect().contains(e.position) && continueAvailable_) hoveredBtn_ = 2;
    else if (helpLinkRect()   .contains(e.position)) hoveredBtn_ = 5;
    else
    {
        hoveredBtn_ = -1;
        for (int i = 0; i < (int)recent_.size(); ++i)
            if (recentRowRect(i).contains(e.position)) { hoveredBtn_ = 10 + i; break; }
    }
    if (hoveredBtn_ != prev) repaint();
}

void StartupMenuComponent::mouseExit(const juce::MouseEvent&)
{
    if (hoveredBtn_ != -1) { hoveredBtn_ = -1; repaint(); }
}

void StartupMenuComponent::mouseDown(const juce::MouseEvent& e)
{
    if (newBtnRect().contains(e.position))
        { if (onNewScene)  onNewScene();  return; }
    if (openBtnRect().contains(e.position))
        { if (onOpenScene) onOpenScene(); return; }
    if (continueBtnRect().contains(e.position) && continueAvailable_)
        { if (onContinue)  onContinue();  return; }
    if (helpLinkRect().contains(e.position))
    {
        juce::URL("https://github.com/sanpasht/sime/blob/main/README.md")
            .launchInDefaultBrowser();
        return;
    }
    for (int i = 0; i < (int)recent_.size(); ++i)
        if (recentRowRect(i).contains(e.position))
            { if (onRecentFile) onRecentFile(recent_[i].fullPath); return; }
}
