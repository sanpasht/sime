#pragma once

#include <JuceHeader.h>
#include "SceneAudioExporter.h"

// ---------------------------------------------------------------------------
// Small modal content: pick lossless / lossy container, then the host opens
// a save dialog and runs SceneAudioExporter.
// ---------------------------------------------------------------------------
class ExportAudioDialog : public juce::Component
{
public:
    ExportAudioDialog();

    void resized() override;

    /// Invoked when the user confirms; host should close the dialog and open
    /// a FileChooser (or call export directly).
    std::function<void(SceneAudioExporter::Format)> onExportChosen;

private:
    juce::Label       title_    { {}, "Format" };
    juce::ComboBox   formatBox_;
    juce::TextButton exportBtn_ { "Choose file and export" };
    juce::TextButton cancelBtn_ { "Cancel" };

    SceneAudioExporter::Format selectedFormat() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportAudioDialog)
};
