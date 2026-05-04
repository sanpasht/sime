// ---------------------------------------------------------------------------
// SoundPickerComponent.cpp
// ---------------------------------------------------------------------------

#include "SoundPickerComponent.h"
#include "SoundLibrary.h"

SoundPickerComponent::SoundPickerComponent()
{
    // ── Search field ─────────────────────────────────────────────────────
    searchField_.setTextToShowWhenEmpty("Search sounds…   (e.g. A3, forte, arco)",
                                        juce::Colour(0xff5d6378));
    searchField_.setColour(juce::TextEditor::backgroundColourId,        juce::Colour(0xff181a24));
    searchField_.setColour(juce::TextEditor::textColourId,              juce::Colour(0xfff0f2fa));
    searchField_.setColour(juce::TextEditor::outlineColourId,           juce::Colour(0xff2f3447));
    searchField_.setColour(juce::TextEditor::focusedOutlineColourId,    juce::Colour(0xff5b7ce6));
    searchField_.setColour(juce::TextEditor::highlightColourId,         juce::Colour(0xff3a5fbf));
    searchField_.setFont(juce::Font(13.0f));
    searchField_.setIndents(10, 4);
    searchField_.onTextChange = [this]
    {
        query_ = searchField_.getText();
        rebuildVisible();
    };
    addAndMakeVisible(searchField_);

    // ── Hint / count label ───────────────────────────────────────────────
    hintLabel_.setFont(juce::Font(11.0f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7a8298));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hintLabel_);

    // ── Sound list ───────────────────────────────────────────────────────
    list_.setColour(juce::ListBox::backgroundColourId,   juce::Colour(0xff10121b));
    list_.setColour(juce::ListBox::outlineColourId,      juce::Colour(0xff242838));
    list_.setOutlineThickness(1);
    list_.setRowHeight(40);
    addAndMakeVisible(list_);
}

// ---------------------------------------------------------------------------

void SoundPickerComponent::setLibrary(SoundLibrary* lib)
{
    library_ = lib;
    rebuildVisible();
}

void SoundPickerComponent::setBlockType(BlockType t)
{
    if (blockType_ == t) return;
    blockType_ = t;
    selectedEntry_ = -1;
    rebuildVisible();
}

void SoundPickerComponent::setSelectedEntry(int entryIdx)
{
    selectedEntry_ = entryIdx;
    int row = entryToRowIdx(entryIdx);
    if (row >= 0)
    {
        list_.selectRow(row, /*dontScroll*/false, /*deselectOthers*/true);
        list_.scrollToEnsureRowIsOnscreen(row);
    }
    else
    {
        list_.deselectAllRows();
    }
}

// ---------------------------------------------------------------------------

void SoundPickerComponent::rebuildVisible()
{
    visibleIndices_.clear();
    if (library_ != nullptr)
        visibleIndices_ = library_->search(blockType_, query_);

    list_.updateContent();

    // Re-select if the previous selection is still visible
    int row = entryToRowIdx(selectedEntry_);
    if (row >= 0)
        list_.selectRow(row, /*dontScroll*/true, /*deselectOthers*/true);
    else
        list_.deselectAllRows();

    juce::String count = juce::String((int)visibleIndices_.size())
                       + (visibleIndices_.size() == 1 ? " sound" : " sounds");
    if (library_ == nullptr || !library_->isLoaded())
        count = "Sound library not loaded — drop sound_library.csv into ./CSV/";
    hintLabel_.setText(count, juce::dontSendNotification);
}

int SoundPickerComponent::rowToEntryIdx(int row) const
{
    return (row >= 0 && row < (int)visibleIndices_.size())
        ? visibleIndices_[row] : -1;
}

int SoundPickerComponent::entryToRowIdx(int entryIdx) const
{
    if (entryIdx < 0) return -1;
    for (int i = 0; i < (int)visibleIndices_.size(); ++i)
        if (visibleIndices_[i] == entryIdx) return i;
    return -1;
}

// ---------------------------------------------------------------------------

int SoundPickerComponent::getNumRows()
{
    return (int)visibleIndices_.size();
}

void SoundPickerComponent::paintListBoxItem(int row, juce::Graphics& g,
                                             int width, int height, bool selected)
{
    if (library_ == nullptr) return;
    int idx = rowToEntryIdx(row);
    if (idx < 0) return;
    const auto& e = library_->at(idx);
    auto stripe = blockTypeColor(e.blockType);

    // Background
    if (selected)
    {
        g.setColour(juce::Colour(0xff263456));
        g.fillRect(0, 0, width, height);
        g.setColour(stripe.withAlpha(0.18f));
        g.fillRect(0, 0, width, height);
    }
    else if (row & 1)
    {
        g.setColour(juce::Colour(0xff14161f));
        g.fillRect(0, 0, width, height);
    }

    // Bottom hairline
    g.setColour(juce::Colour(0x14ffffff));
    g.fillRect(0, height - 1, width, 1);

    // Color stripe
    g.setColour(stripe);
    g.fillRect(0, 0, 4, height);
    if (selected)
    {
        g.setColour(stripe.withAlpha(0.7f));
        g.fillRect(4, 0, 1, height);
    }

    // Two-line text
    const int textX = 14;
    const int textW = width - textX - 8;

    g.setColour(selected ? juce::Colour(0xfff8faff) : juce::Colour(0xffe2e6f2));
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(e.displayName, textX, 5, textW, 16,
               juce::Justification::centredLeft, true);

    g.setColour(selected ? juce::Colour(0xffaab8de) : juce::Colour(0xff8189a0));
    g.setFont(juce::Font(11.0f));
    g.drawText(e.displaySub, textX, 22, textW, 14,
               juce::Justification::centredLeft, true);
}

void SoundPickerComponent::selectedRowsChanged(int lastRowSelected)
{
    int idx = rowToEntryIdx(lastRowSelected);
    if (idx == selectedEntry_) return;
    selectedEntry_ = idx;
    if (onSelectionChanged) onSelectionChanged(idx);
}

void SoundPickerComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    int idx = rowToEntryIdx(row);
    if (idx >= 0 && onDoubleClick) onDoubleClick(idx);
}

// ---------------------------------------------------------------------------

void SoundPickerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0c0e16));
}

void SoundPickerComponent::resized()
{
    auto area = getLocalBounds().reduced(2);

    auto searchRow = area.removeFromTop(28);
    searchField_.setBounds(searchRow);

    area.removeFromTop(6);
    hintLabel_.setBounds(area.removeFromTop(14));
    area.removeFromTop(2);

    list_.setBounds(area);
}
