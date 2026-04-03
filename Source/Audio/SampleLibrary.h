#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SampleLibrary.h  –  Central registry of loaded AudioClips.
//
// Supports both synchronous and async (background thread-pool) loading.
// ─────────────────────────────────────────────────────────────────────────────

#include "AudioClip.h"
#include <JuceHeader.h>
#include <unordered_map>
#include <memory>
#include <functional>

class SampleLibrary
{
public:
    SampleLibrary();
    ~SampleLibrary();

    std::shared_ptr<AudioClip> loadSync(const juce::File& file);

    using LoadCallback = std::function<void(std::shared_ptr<AudioClip>)>;
    void requestLoad(const juce::File& file, LoadCallback onDone = nullptr);

    std::shared_ptr<AudioClip> getClip(const juce::String& clipId) const;
    std::shared_ptr<AudioClip> getClipByPath(const juce::String& path) const;
    void unloadAll();
    int  getClipCount() const;
    bool isLoading() const;

private:
    std::unordered_map<juce::String, std::shared_ptr<AudioClip>> clips;
    std::unordered_map<juce::String, std::shared_ptr<AudioClip>> clipsByPath;
    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::ThreadPool> threadPool;
    std::atomic<int> pendingLoads { 0 };
};
