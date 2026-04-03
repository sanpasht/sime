// ─────────────────────────────────────────────────────────────────────────────
// SoundScene.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "SoundScene.h"
#include <algorithm>
#include <cmath>

uint64_t SoundScene::addBlock(const SoundBlock& block)
{
    SoundBlock b = block;
    b.id = nextId++;
    blocks.push_back(b);
    return b.id;
}

void SoundScene::removeBlock(uint64_t id)
{
    blocks.erase(
        std::remove_if(blocks.begin(), blocks.end(),
                       [id](const SoundBlock& b) { return b.id == id; }),
        blocks.end());
}

SoundBlock* SoundScene::getBlock(uint64_t id)
{
    for (auto& b : blocks)
    {
        if (b.id == id)
            return &b;
    }
    return nullptr;
}

SceneSnapshot SoundScene::buildSnapshot(const Vec3f& listenerPos,
                                         const Vec3f& listenerForward,
                                         const Vec3f& listenerRight,
                                         double transportTimeSec,
                                         bool isPlaying,
                                         const SampleLibrary& library) const
{
    SceneSnapshot snap {};
    snap.listenerPos     = listenerPos;
    snap.listenerForward = listenerForward;
    snap.listenerRight   = listenerRight;
    snap.transportTimeSec = transportTimeSec;
    snap.isPlaying       = isPlaying;
    snap.numBlocks       = 0;

    for (const auto& block : blocks)
    {
        if (!block.active)
            continue;
        if (snap.numBlocks >= AudioConfig::kMaxBlocks)
            break;

        auto clip = library.getClip(block.clipId);
        if (clip == nullptr)
            continue;

        auto& entry = snap.blocks[snap.numBlocks];
        entry.id               = block.id;
        entry.position         = block.position;
        entry.startTime        = block.startTime;
        entry.duration         = block.duration;
        entry.samplesL         = clip->getReadPointer(0);
        entry.samplesR         = clip->getNumChannels() > 1
                                     ? clip->getReadPointer(1)
                                     : clip->getReadPointer(0);
        entry.totalSamples     = clip->getNumSamples();
        entry.nativeSampleRate = clip->getSampleRate();
        entry.gainLinear       = block.getGainLinear();
        entry.attenuationRadius = block.attenuationRadius;
        entry.spread           = block.spread;
        entry.looping          = block.looping;

        ++snap.numBlocks;
    }

    return snap;
}

void SoundScene::clear()
{
    blocks.clear();
    nextId = 1;
}

bool SoundScene::saveToFile(const juce::File& file) const
{
    auto root = std::make_unique<juce::XmlElement>("SIMEScene");
    root->setAttribute("version", 1);

    for (const auto& block : blocks)
    {
        auto* elem = root->createNewChildElement("SoundBlock");
        elem->setAttribute("id", static_cast<int>(block.id));
        elem->setAttribute("posX", static_cast<double>(block.position.x));
        elem->setAttribute("posY", static_cast<double>(block.position.y));
        elem->setAttribute("posZ", static_cast<double>(block.position.z));
        elem->setAttribute("startTime", static_cast<double>(block.startTime));
        elem->setAttribute("duration", static_cast<double>(block.duration));
        elem->setAttribute("clipId", block.clipId);
        elem->setAttribute("gainDb", static_cast<double>(block.gainDb));
        elem->setAttribute("pitchSemitones", static_cast<double>(block.pitchSemitones));
        elem->setAttribute("looping", block.looping);
        elem->setAttribute("attenuationRadius", static_cast<double>(block.attenuationRadius));
        elem->setAttribute("spread", static_cast<double>(block.spread));
        elem->setAttribute("active", block.active);
    }

    return root->writeTo(file);
}

bool SoundScene::loadFromFile(const juce::File& file, SampleLibrary& library)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || xml->getTagName() != "SIMEScene")
    {
        DBG("SoundScene::loadFromFile — invalid scene file");
        return false;
    }

    clear();

    for (auto* elem : xml->getChildWithTagNameIterator("SoundBlock"))
    {
        SoundBlock block;
        block.position.x       = static_cast<float>(elem->getDoubleAttribute("posX"));
        block.position.y       = static_cast<float>(elem->getDoubleAttribute("posY"));
        block.position.z       = static_cast<float>(elem->getDoubleAttribute("posZ"));
        block.startTime        = static_cast<float>(elem->getDoubleAttribute("startTime"));
        block.duration         = static_cast<float>(elem->getDoubleAttribute("duration"));
        block.clipId           = elem->getStringAttribute("clipId");
        block.gainDb           = static_cast<float>(elem->getDoubleAttribute("gainDb"));
        block.pitchSemitones   = static_cast<float>(elem->getDoubleAttribute("pitchSemitones"));
        block.looping          = elem->getBoolAttribute("looping");
        block.attenuationRadius = static_cast<float>(elem->getDoubleAttribute("attenuationRadius", AudioConfig::kDefaultAttenRadius));
        block.spread           = static_cast<float>(elem->getDoubleAttribute("spread"));
        block.active           = elem->getBoolAttribute("active", true);

        addBlock(block);
    }

    DBG("Scene loaded: " + juce::String(getBlockCount()) + " blocks from " + file.getFileName());
    return true;
}
