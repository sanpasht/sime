// ─────────────────────────────────────────────────────────────────────────────
// SampleLibrary.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "SampleLibrary.h"

SampleLibrary::SampleLibrary()
    : threadPool(std::make_unique<juce::ThreadPool>(2))
{
}

SampleLibrary::~SampleLibrary()
{
    threadPool.reset();
}

std::shared_ptr<AudioClip> SampleLibrary::loadSync(const juce::File& file)
{
    {
        juce::ScopedLock sl(lock);
        auto it = clipsByPath.find(file.getFullPathName());
        if (it != clipsByPath.end())
            return it->second;
    }

    auto clip = AudioClip::loadFromFile(file);
    if (clip == nullptr)
        return nullptr;

    juce::ScopedLock sl(lock);
    clips[clip->getClipId()] = clip;
    clipsByPath[file.getFullPathName()] = clip;
    return clip;
}

void SampleLibrary::requestLoad(const juce::File& file, LoadCallback onDone)
{
    {
        juce::ScopedLock sl(lock);
        auto it = clipsByPath.find(file.getFullPathName());
        if (it != clipsByPath.end())
        {
            if (onDone)
                onDone(it->second);
            return;
        }
    }

    pendingLoads.fetch_add(1, std::memory_order_relaxed);

    juce::File fileCopy = file;
    LoadCallback cb = onDone;

    threadPool->addJob([this, fileCopy, cb]()
    {
        auto clip = AudioClip::loadFromFile(fileCopy);

        if (clip != nullptr)
        {
            juce::ScopedLock sl(lock);
            clips[clip->getClipId()] = clip;
            clipsByPath[fileCopy.getFullPathName()] = clip;
        }

        pendingLoads.fetch_sub(1, std::memory_order_relaxed);

        if (cb)
        {
            juce::MessageManager::callAsync([cb, clip]()
            {
                cb(clip);
            });
        }
    });
}

std::shared_ptr<AudioClip> SampleLibrary::getClip(const juce::String& clipId) const
{
    juce::ScopedLock sl(lock);
    auto it = clips.find(clipId);
    if (it != clips.end())
        return it->second;
    return nullptr;
}

std::shared_ptr<AudioClip> SampleLibrary::getClipByPath(const juce::String& path) const
{
    juce::ScopedLock sl(lock);
    auto it = clipsByPath.find(path);
    if (it != clipsByPath.end())
        return it->second;
    return nullptr;
}

void SampleLibrary::unloadAll()
{
    juce::ScopedLock sl(lock);
    clips.clear();
    clipsByPath.clear();
}

int SampleLibrary::getClipCount() const
{
    juce::ScopedLock sl(lock);
    return static_cast<int>(clips.size());
}

bool SampleLibrary::isLoading() const
{
    return pendingLoads.load(std::memory_order_relaxed) > 0;
}
