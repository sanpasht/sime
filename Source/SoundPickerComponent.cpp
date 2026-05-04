// ---------------------------------------------------------------------------
// SoundPickerComponent.cpp
// ---------------------------------------------------------------------------

#include "SoundPickerComponent.h"
#include "SoundLibrary.h"

SoundPickerComponent::SoundPickerComponent()
{
    // ── Search field ─────────────────────────────────────────────────────
    searchField_.setTextToShowWhenEmpty("search…  (e.g. A3, forte, arco)",
                                        juce::Colour(0xff667799));
    searchField_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e2030));
    searchField_.setColour(juce::TextEditor::textColourId,        juce::Colour(0xffeeeeff));
    searchField_.setColour(juce::TextEditor::outlineColourId,     juce::Colour(0xff3344aa));
    searchField_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff6677ff));
    searchField_.setFont(juce::Font(13.f));
    searchField_.onTextChange = [this]
    {
        query_ = searchField_.getText();
        rebuildVisible();
    };
    addAndMakeVisible(searchField_);

    // ── Hint / count label ───────────────────────────────────────────────
    hintLabel_.setFont(juce::Font(11.f));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8899bb));
    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hintLabel_);

    // ── Sound list ───────────────────────────────────────────────────────
    list_.setColour(juce::ListBox::backgroundColourId,   juce::Colour(0xff15171f));
    list_.setColour(juce::ListBox::outlineColourId,      juce::Colour(0xff3344aa));
    list_.setOutlineThickness(1);
    list_.setRowHeight(22);
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

    // Re-select if previous selection is still in the visible set
    int row = entryToRowIdx(selectedEntry_);
    if (row >= 0)
        list_.selectRow(row, /*dontScroll*/true, /*deselectOthers*/true);
    else
        list_.deselectAllRows();

    hintLabel_.setText(juce::String(visibleIndices_.size()) + " sounds",
                       juce::dontSendNotification);
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

    if (selected)
        g.fillAll(juce::Colour(0xff2a3a6c));
    else if (row & 1)
        g.fillAll(juce::Colour(0xff1a1c26));

    // Color stripe in the block's signature color
    g.setColour(blockTypeColor(e.blockType));
    g.fillRect(0, 0, 3, height);

    g.setColour(selected ? juce::Colours::white : juce::Colour(0xffd0d8e8));
    g.setFont(juce::Font(12.0f));
    g.drawText(e.displayName, 10, 0, width - 14, height,
               juce::Justification::centredLeft, true);
}

void SoundPickerComponent::selectedRowsChanged(int lastRowSelected)
{
    int idx = rowToEntryIdx(lastRowSelected);
    if (idx == selectedEntry_) return;
    selectedEntry_ = idx;
    if (onSelectionChanged) onSelectionChanged(idx);
}

// ---------------------------------------------------------------------------

void SoundPickerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10121a));
}

void SoundPickerComponent::resized()
{
    auto area = getLocalBounds().reduced(2);
    searchField_.setBounds(area.removeFromTop(24));
    area.removeFromTop(2);
    hintLabel_.setBounds(area.removeFromTop(14));
    list_.setBounds(area);
}
