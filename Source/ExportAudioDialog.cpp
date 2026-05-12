#include "ExportAudioDialog.h"

ExportAudioDialog::ExportAudioDialog()
{
    addAndMakeVisible(title_);
    title_.setColour(juce::Label::textColourId, juce::Colour(0xffe2e6f2));
    title_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(formatBox_);
    formatBox_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff181a24));
    formatBox_.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe2e6f2));
    formatBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2f3447));
    formatBox_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff8b94ad));

    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Wav), 1);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Flac), 2);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Aiff), 3);
    formatBox_.addItem(SceneAudioExporter::formatDescription(SceneAudioExporter::Format::Ogg), 4);
    formatBox_.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(exportBtn_);
    exportBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a5298));
    exportBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    exportBtn_.onClick = [this]
    {
        const auto fmt = selectedFormat();
        if (onExportChosen)
            onExportChosen(fmt);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };

    addAndMakeVisible(cancelBtn_);
    cancelBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a3c));
    cancelBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e6f2));
    cancelBtn_.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    setSize(400, 130);
}

SceneAudioExporter::Format ExportAudioDialog::selectedFormat() const
{
    switch (formatBox_.getSelectedId())
    {
        case 2: return SceneAudioExporter::Format::Flac;
        case 3: return SceneAudioExporter::Format::Aiff;
        case 4: return SceneAudioExporter::Format::Ogg;
        default: return SceneAudioExporter::Format::Wav;
    }
}

void ExportAudioDialog::resized()
{
    auto r = getLocalBounds().reduced(16);
    title_.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);
    formatBox_.setBounds(r.removeFromTop(28));
    r.removeFromTop(16);
    auto row = r.removeFromTop(32);
    cancelBtn_.setBounds(row.removeFromRight(100));
    row.removeFromRight(8);
    exportBtn_.setBounds(row);
}
