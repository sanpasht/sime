#pragma once
// ---------------------------------------------------------------------------
// SoundPickerComponent.h — Searchable, scrollable list of sounds for one
// BlockType.  Used inside BlockEditPopup.
//
// Layout
//   ┌─────────────────────────────────────┐
//   │ [ search query …             ]      │
//   ├─────────────────────────────────────┤
//   │ A3   /  0.25s  /  Forte  /  Arco    │
//   │ A3   /  0.5s   /  Forte  /  Arco    │   ← ListBox (virtualized)
//   │ A#3  /  1s     /  Piano  /  Arco    │
//   │ ...                                  │
//   └─────────────────────────────────────┘
// ---------------------------------------------------------------------------

#include <JuceHeader.h>
#include "BlockType.h"

class SoundLibrary;

class SoundPickerComponent : public juce::Component, public juce::ListBoxModel
{
public:
    SoundPickerComponent();

    void setLibrary(SoundLibrary* lib);
    void setBlockType(BlockType t);
    void setSelectedEntry(int entryIdx);    // entryIdx into the library, or -1
    int  getSelectedEntry() const noexcept  { return selectedEntry_; }

    /// Fired whenever the user clicks a row.  Argument is the library entry idx.
    std::function<void(int)> onSelectionChanged;

    // ── juce::Component ──────────────────────────────────────────────────
    void paint(juce::Graphics&) override;
    void resized() override;

    // ── juce::ListBoxModel ──────────────────────────────────────────────
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g,
                          int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;

private:
    SoundLibrary*       library_      = nullptr;
    BlockType           blockType_    = BlockType::Violin;
    juce::String        query_;
    std::vector<int>    visibleIndices_;        // entry indices currently shown
    int                 selectedEntry_ = -1;

    juce::TextEditor    searchField_;
    juce::ListBox       list_      { {}, this };
    juce::Label         hintLabel_;

    void rebuildVisible();
    int  rowToEntryIdx(int row) const;
    int  entryToRowIdx(int entryIdx) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPickerComponent)
};
