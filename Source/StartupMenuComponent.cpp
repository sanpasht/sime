// ─────────────────────────────────────────────────────────────────────────────
// StartupMenuComponent.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "StartupMenuComponent.h"

// ── Voxel S colour palette ────────────────────────────────────────────────────
// 15 blocks, colours assigned row-by-row to match the in-app block type colours.
static const juce::Colour kBlockColours[] =
{
    // Row 0 — col 1,2,3
    juce::Colour(0xffD93830),   // Violin red
    juce::Colour(0xff3D6FCC),   // Piano blue
    juce::Colour(0xff33C050),   // Drum green
    // Row 1 — col 0,1
    juce::Colour(0xffEACC1F),   // yellow
    juce::Colour(0xff2EC0C0),   // cyan
    // Row 2 — col 0
    juce::Colour(0xffC060CC),   // magenta
    // Row 3 — col 1,2,3
    juce::Colour(0xffD93830),
    juce::Colour(0xff3D6FCC),
    juce::Colour(0xff33C050),
    // Row 4 — col 3
    juce::Colour(0xffEACC1F),
    // Row 5 — col 2,3
    juce::Colour(0xff2EC0C0),
    juce::Colour(0xffC060CC),
    // Row 6 — col 0,1,2
    juce::Colour(0xffD93830),
    juce::Colour(0xff3D6FCC),
    juce::Colour(0xff33C050),
};

// S block positions — (row, col) in a 4-wide × 7-tall grid
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

// ─────────────────────────────────────────────────────────────────────────────

StartupMenuComponent::StartupMenuComponent()
{
    setWantsKeyboardFocus(false);
    refreshRecentFiles();
}

// ─────────────────────────────────────────────────────────────────────────────
// refreshRecentFiles
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::refreshRecentFiles()
{
    recent_.clear();

    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("SIME");

    if (!dir.isDirectory())
        return;

    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.sime");
    files.sort();

    // Sort newest-first by modification time
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() > b.getLastModificationTime(); });

    const auto now = juce::Time::getCurrentTime();

    // Dot colours cycle through block type colours
    const juce::Colour dots[] = {
        juce::Colour(0xff33C050),
        juce::Colour(0xff3D6FCC),
        juce::Colour(0xffD93830),
        juce::Colour(0xffEACC1F),
    };

    int count = 0;
    for (const auto& f : files)
    {
        if (count >= 5) break;  // show at most 5 recent files

        juce::String name = f.getFileName();

        // Human-readable age
        auto mod   = f.getLastModificationTime();
        int  days  = (int)(now - mod).inDays();
        juce::String age;
        if      (days == 0) age = "today";
        else if (days == 1) age = "yesterday";
        else                age = juce::String(days) + " days ago";

        // Skip autosave from the recent list — it's surfaced via Continue button
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
    // Total height of all content
    float h = 90.f;                               // logo row
    h += 36.f;                                    // divider
    h += 3.f * kBtnH + 2.f * kBtnGap;            // 3 buttons
    h += 36.f;                                    // divider
    h += 34.f;                                    // help link
    if (!recent_.empty())
    {
        h += 32.f;                                // "RECENT" header
        h += (float)recent_.size() * kRecentRowH;
    }
    return std::max(40.f, (getHeight() - h) / 2.f);
}

juce::Rectangle<float> StartupMenuComponent::newBtnRect() const
{
    float x = cardLeft();
    float y = cardTop() + 90.f + 36.f;
    return { x, y, kCardW, kBtnH };
}

juce::Rectangle<float> StartupMenuComponent::openBtnRect() const
{
    auto r = newBtnRect();
    return r.translated(0.f, kBtnH + kBtnGap);
}

juce::Rectangle<float> StartupMenuComponent::continueBtnRect() const
{
    auto r = openBtnRect();
    return r.translated(0.f, kBtnH + kBtnGap);
}

juce::Rectangle<float> StartupMenuComponent::helpLinkRect() const
{
    auto r = continueBtnRect();
    return { r.getX(), r.getBottom() + 36.f, kCardW, 34.f };
}

juce::Rectangle<float> StartupMenuComponent::recentRowRect(int i) const
{
    auto r = helpLinkRect();
    float y = r.getBottom() + 32.f + (float)i * kRecentRowH;
    return { r.getX(), y, kCardW, kRecentRowH - 4.f };
}

// ─────────────────────────────────────────────────────────────────────────────
// drawVoxelS
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

        // Top-face highlight
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRoundedRectangle(bx, by, block, block * 0.22f, block * 0.15f);

        ++ci;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawActionBtn
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::drawActionBtn(juce::Graphics& g,
                                         juce::Rectangle<float> r,
                                         const juce::String& icon,
                                         const juce::String& label,
                                         bool primary,
                                         bool enabled,
                                         bool hovered) const
{
    const float alpha = enabled ? 1.f : 0.35f;

    // Background
    if (primary)
    {
        auto col = hovered ? juce::Colour(0xffcc2820) : juce::Colour(0xffD93830);
        g.setColour(col.withAlpha(alpha));
    }
    else
    {
        auto col = hovered ? juce::Colour(0xff252840) : juce::Colour(0xff1a1d2e);
        g.setColour(col.withAlpha(alpha));
    }
    g.fillRoundedRectangle(r, 10.f);

    // Border (secondary only)
    if (!primary)
    {
        g.setColour(juce::Colour(0xff3a3d55).withAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(0.5f), 10.f, 1.f);
    }

    // Icon
    g.setFont(juce::Font(16.f));
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.drawText(icon, (int)r.getX() + 20, (int)r.getY(), 24, (int)r.getHeight(),
               juce::Justification::centredLeft, false);

    // Label
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
    // ── Background ─────────────────────────────────────────────────────────────
    g.fillAll(juce::Colour(0xff1e2235));

    const float cl  = cardLeft();
    const float ct  = cardTop();

    // ── Logo row ───────────────────────────────────────────────────────────────
    // Voxel S: block=9, gap=1.5, step=10.5
    // Width: 4*9+3*1.5=40.5  Height: 7*9+6*1.5=72
    const float block = 9.f;
    const float gap   = 1.5f;
    const float sW    = 4.f * block + 3.f * gap;   // ~40px
    const float sH    = 7.f * block + 6.f * gap;   // ~72px

    // Center S vertically in the 90px logo row
    const float sX = cl;
    const float sY = ct + (90.f - sH) / 2.f;
    drawVoxelS(g, sX, sY, block, gap);

    // Title text to the right of the S
    const float textX = cl + sW + 16.f;
    const float textW = kCardW - sW - 16.f;

    g.setFont(juce::Font("Arial", 34.f, juce::Font::plain));
    g.setColour(juce::Colour(0xffeeeeee));
    g.drawText("SIME", (int)textX, (int)(ct + 10.f), (int)textW, 36,
               juce::Justification::centredLeft, false);

    g.setFont(juce::Font(10.5f, juce::Font::plain));
    g.setColour(juce::Colour(0xff7a8099));
    // Custom letter-spacing effect via drawing char-by-char not needed —
    // JUCE doesn't have letter-spacing, so use a slightly expanded font.
    juce::AttributedString sub;
    sub.setJustification(juce::Justification::centredLeft);
    sub.append("SPATIALLY INTERPRETED MUSIC ENGINE",
               juce::Font(10.f), juce::Colour(0xff7a8099));
    sub.draw(g, juce::Rectangle<float>(textX, ct + 50.f, textW, 20.f));

    // ── Divider ─────────────────────────────────────────────────────────────────
    const float div1Y = ct + 90.f + 16.f;
    g.setColour(juce::Colour(0xff2e3245));
    g.drawLine(cl + kCardW * 0.35f, div1Y, cl + kCardW * 0.65f, div1Y, 1.f);

    // ── Buttons ─────────────────────────────────────────────────────────────────
    drawActionBtn(g, newBtnRect(),      "+",  "New scene",
                  /*primary=*/true,  /*enabled=*/true,  hoveredBtn_ == 0);
    drawActionBtn(g, openBtnRect(),     "\xF0\x9F\x93\x84",  "Open scene",
                  false, true,  hoveredBtn_ == 1);
    drawActionBtn(g, continueBtnRect(), "\xe2\x86\xba",
                  "Continue \xe2\x80\x94 autosave.sime",
                  false, continueAvailable_, hoveredBtn_ == 2);

    // ── Divider ─────────────────────────────────────────────────────────────────
    const float div2Y = continueBtnRect().getBottom() + 18.f;
    g.setColour(juce::Colour(0xff2e3245));
    g.drawLine(cl + kCardW * 0.35f, div2Y, cl + kCardW * 0.65f, div2Y, 1.f);

    // ── Help link ────────────────────────────────────────────────────────────────
    auto hlr = helpLinkRect();
    bool hlHov = (hoveredBtn_ == 5);
    g.setFont(juce::Font(13.f));
    g.setColour(hlHov ? juce::Colour(0xff9aa0bb) : juce::Colour(0xff5a6078));
    g.drawText("Controls & help guide \xe2\x86\x97",
               hlr.toNearestInt(), juce::Justification::centred, false);

    // ── Recent files ─────────────────────────────────────────────────────────────
    if (!recent_.empty())
    {
        float recHeaderY = hlr.getBottom() + 6.f;
        g.setFont(juce::Font(10.f, juce::Font::bold));
        g.setColour(juce::Colour(0xff4a5070));
        g.drawText("RECENT", (int)cl, (int)recHeaderY, (int)kCardW, 22,
                   juce::Justification::centredLeft, false);

        for (int i = 0; i < (int)recent_.size(); ++i)
        {
            auto rr = recentRowRect(i);
            bool rowHov = (hoveredBtn_ == 10 + i);

            // Row background
            g.setColour(rowHov ? juce::Colour(0xff252840) : juce::Colour(0xff1a1d2e));
            g.fillRoundedRectangle(rr, 8.f);

            g.setColour(juce::Colour(0xff2e3245));
            g.drawRoundedRectangle(rr.reduced(0.5f), 8.f, 0.5f);

            // Colour dot
            const float dotSize = 9.f;
            const float dotX = rr.getX() + 14.f;
            const float dotY = rr.getCentreY() - dotSize * 0.5f;
            g.setColour(recent_[i].dot);
            g.fillRoundedRectangle(dotX, dotY, dotSize, dotSize, 2.f);

            // Filename
            g.setFont(juce::Font(13.f));
            g.setColour(juce::Colour(0xffd0d4e8));
            g.drawText(recent_[i].name,
                       (int)(rr.getX() + 34.f), (int)rr.getY(),
                       (int)(kCardW - 110.f), (int)rr.getHeight(),
                       juce::Justification::centredLeft, true);

            // Timestamp
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
// resized
// ─────────────────────────────────────────────────────────────────────────────

void StartupMenuComponent::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse
// ─────────────────────────────────────────────────────────────────────────────

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
    {
        if (onNewScene) onNewScene();
        return;
    }
    if (openBtnRect().contains(e.position))
    {
        if (onOpenScene) onOpenScene();
        return;
    }
    if (continueBtnRect().contains(e.position) && continueAvailable_)
    {
        if (onContinue) onContinue();
        return;
    }
    if (helpLinkRect().contains(e.position))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Controls & Help",
            "Navigation\n"
            "  RMB drag  Look around\n"
            "  W/A/S/D   Move\n"
            "  E         Up\n"
            "  Q         Down\n"
            "  R         Reset camera\n"
            "\n"
            "Placing blocks\n"
            "  LMB         Place block\n"
            "  RMB click   Remove block\n"
            "  Shift+LMB   Air placement\n"
            "  Shift+Scroll Raise/lower plane\n"
            "  C           Clear all (with confirm)\n"
            "\n"
            "Edit Mode (Tab to toggle)\n"
            "  Click block   Edit timing\n"
            "  Alt+drag      Record movement\n"
            "\n"
            "Transport\n"
            "  Space   Play / Pause\n"
            "  Stop button resets to 0");
        return;
    }

    for (int i = 0; i < (int)recent_.size(); ++i)
    {
        if (recentRowRect(i).contains(e.position))
        {
            if (onRecentFile) onRecentFile(recent_[i].fullPath);
            return;
        }
    }
}
