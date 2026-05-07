#include "TimelineComponent.h"

TimelineComponent::TimelineComponent()
{
    setWantsKeyboardFocus(false);
}

void TimelineComponent::setBlocks(const std::vector<BlockEntry>& blocks)
{
    regions_.clear();
    
    // Organize blocks by serial to determine track index
    std::map<int, int> serialToTrack;
    int nextTrack = 0;
    
    for (const auto& block : blocks)
    {
        if (serialToTrack.find(block.serial) == serialToTrack.end())
            serialToTrack[block.serial] = nextTrack++;
        
        BlockRegion region;
        region.serial = block.serial;
        region.type = block.blockType;
        region.startTime = block.startTimeSec;
        region.duration = block.durationSec;
        region.trackIndex = serialToTrack[block.serial];
        region.label = "Block #" + juce::String(block.serial);
        
        regions_.push_back(region);
        
        // Update total duration
        totalDuration_ = std::max(totalDuration_, 
                                  block.startTimeSec + block.durationSec);
    }
    
    repaint();
}

void TimelineComponent::setZoom(double pixelsPerSecond)
{
    pixelsPerSecond_ = std::clamp(pixelsPerSecond, 20.0, 500.0);
    repaint();
}

void TimelineComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.fillAll(juce::Colour(0xff1a1a1a));

    auto rulerArea = bounds.removeFromTop(kRulerHeight);
    auto tracksArea = bounds;

    // Tracks are clipped so they can never draw over ruler
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(tracksArea);
        paintTracks(g, tracksArea);
    }

    // Draw ruler AFTER tracks so it always stays visible
    paintTimeRuler(g, rulerArea);

    // Draw playhead last
    paintPlayhead(g, getLocalBounds());
}


void TimelineComponent::paintTimeRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(area);
    
    g.setColour(juce::Colour(0xff444444));
    g.drawLine(area.getX(), area.getBottom() - 1, 
               area.getRight(), area.getBottom() - 1);
    
    // Draw time markers
    const double majorInterval = 2.0; // Every 2 seconds
    const double minorInterval = 0.5; // Every 0.5 seconds
    
    double startTime = std::floor(viewStartTime_ / majorInterval) * majorInterval;
    double endTime = viewStartTime_ + (getWidth() / pixelsPerSecond_);
    
    g.setFont(11.0f);
    
    for (double t = startTime; t <= endTime; t += minorInterval)
    {
        int x = (int)timeToX(t);
        
        bool isMajor = (std::fmod(t, majorInterval) < 0.01);
        
        if (isMajor)
        {
            // Major tick (every 2 sec)
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawVerticalLine(x, area.getY() + 20, area.getBottom());
            
            // Time label
            int minutes = (int)(t / 60.0);
            int seconds = (int)std::fmod(t, 60.0);
            juce::String label = juce::String::formatted("%d:%02d", minutes, seconds);
            
            g.drawText(label, x - 20, area.getY() + 2, 40, 16, 
                      juce::Justification::centred);
        }
        else
        {
            // Minor tick (every 0.5 sec)
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawVerticalLine(x, area.getY() + 25, area.getBottom());
        }
    }
}

void TimelineComponent::paintTracks(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Determine max track index
    int maxTrack = 0;
    for (const auto& region : regions_)
        maxTrack = std::max(maxTrack, region.trackIndex);
    
    // Draw track backgrounds
    for (int i = 0; i <= maxTrack; ++i)
    {
        int y = area.getY() + i * (kTrackHeight + kTrackGap) - (int)verticalScroll_;
        
        juce::Colour trackBg = (i % 2 == 0) 
            ? juce::Colour(0xff1e1e1e) 
            : juce::Colour(0xff242424);
        
        g.setColour(trackBg);
        g.fillRect(area.getX(), y, area.getWidth(), kTrackHeight);
    }
    
    // Draw block regions
    for (const auto& region : regions_)
    {
        int x = (int)timeToX(region.startTime);
        int w = (int)(region.duration * pixelsPerSecond_);
        int y = area.getY() + region.trackIndex * (kTrackHeight + kTrackGap) + 2 - (int)verticalScroll_;
        int h = kTrackHeight - 4;
        
        // Skip if off-screen
        if (x + w < area.getX() || x > area.getRight())
            continue;
        
        juce::Rectangle<int> blockRect(x, y, w, h);
        
        // Block color
        juce::Colour color = getBlockColor(region.type, 0);
        g.setColour(color.withAlpha(0.8f));
        g.fillRoundedRectangle(blockRect.toFloat(), 3.0f);
        
        // Border
        g.setColour(color.brighter(0.3f));
        g.drawRoundedRectangle(blockRect.toFloat(), 3.0f, 1.5f);
        
        // Label
        if (w > 50) // Only show label if wide enough
        {
            g.setColour(juce::Colours::white);
            g.setFont(12.0f);
            g.drawText(region.label, blockRect.reduced(4), 
                      juce::Justification::centredLeft, true);
        }
    }
}

void TimelineComponent::paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area)
{
    int x = (int)timeToX(currentTime_);

    if (x > playheadAnchorX_)
        x = playheadAnchorX_;

    g.setColour(juce::Colours::yellow);
    g.drawLine(x, kRulerHeight, x, area.getBottom(), 2.0f);

    juce::Path triangle;
    triangle.addTriangle(x - 6, kRulerHeight - 2,
                         x + 6, kRulerHeight - 2,
                         x,     kRulerHeight + 6);

    g.fillPath(triangle);
}

int TimelineComponent::timeToX(double time) const
{
    return (int)((time - viewStartTime_) * pixelsPerSecond_);
}

double TimelineComponent::xToTime(int x) const
{
    return viewStartTime_ + (x / pixelsPerSecond_);
}

juce::Colour TimelineComponent::getBlockColor(BlockType type, int soundId) const
{
    switch (type)
    {
        case BlockType::Violin: return juce::Colour(0xffc03528);
        case BlockType::Piano:  return juce::Colour(0xff3366cc);
        case BlockType::Drum:   return juce::Colour(0xff2eaa44);
        case BlockType::Custom: return juce::Colour(0xff666688);
    }
    return juce::Colours::grey;
}

void TimelineComponent::resized()
{
    playheadAnchorX_ = getWidth() / 3;
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, 
                                       const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom
        double zoomFactor = 1.0 + (w.deltaY * 0.5);
        setZoom(pixelsPerSecond_ * zoomFactor);
    }
    else
    {
        const int totalContentHeight =
            (int)regions_.size() * (kTrackHeight + kTrackGap);

        const int visibleHeight = getHeight() - kRulerHeight;

        const double maxScroll =
            std::max(0, totalContentHeight - visibleHeight);

        // Trackpad: deltaY scrolls vertically, deltaX scrolls horizontally
        verticalScroll_ = std::clamp(
            verticalScroll_ - w.deltaY * 120.0,
            0.0,
            maxScroll
        );

        viewStartTime_ = std::max(0.0, viewStartTime_ - w.deltaX * 2.0);

        repaint();
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    // 1) Drag / resize block rectangles
    if (selectedBlock_ != -1 && dragMode_ != DragMode::None)
    {
        double currentMouseTime = xToTime(e.x);
        double delta = currentMouseTime - dragStartTime_;

        for (auto& r : regions_)
        {
            if (r.serial != selectedBlock_)
                continue;

            if (dragMode_ == DragMode::Move)
            {
                r.startTime = std::max(0.0, originalStart_ + delta);
            }
            else if (dragMode_ == DragMode::ResizeLeft)
            {
                double originalEnd = originalStart_ + originalDuration_;
                double newStart = std::max(0.0, originalStart_ + delta);
                double newDuration = originalEnd - newStart;

                if (newDuration >= 0.1)
                {
                    r.startTime = newStart;
                    r.duration = newDuration;
                }
            }
            else if (dragMode_ == DragMode::ResizeRight)
            {
                r.duration = std::max(0.1, originalDuration_ + delta);
            }

            if (onBlockEdited)
                onBlockEdited(r.serial, r.startTime, r.duration);

            break;
        }

        repaint();
        return;
    }

    // 2) Empty-space timeline panning
    if (isPanningTimeline_)
    {
        const int totalContentHeight =
            (int)regions_.size() * (kTrackHeight + kTrackGap);

        const int visibleHeight = getHeight() - kRulerHeight;

        const double maxScroll =
            std::max(0, totalContentHeight - visibleHeight);

            
        int dy = e.y - lastDragY_;
        int dx = e.x - lastDragX_;

        viewStartTime_ = std::max(
            0.0,
            viewStartTime_ - ((double)dx / pixelsPerSecond_)
        );

        verticalScroll_ = std::clamp(
            verticalScroll_ - (double)dy,
            0.0,
            maxScroll
        );

        lastDragX_ = e.x;
        lastDragY_ = e.y;

        repaint();
        return;
    }
}


void TimelineComponent::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);

    isPanningTimeline_ = false;
    isUserPanning_ = false;

    selectedBlock_ = -1;
    dragMode_ = DragMode::None;
}

void TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.y < kRulerHeight)
    {
        isPanningTimeline_ = false;
        isUserPanning_ = false;
        selectedBlock_ = -1;
        dragMode_ = DragMode::None;
        return;
    }

    isPanningTimeline_ = false;

    for (auto& r : regions_)
    {
        int trackY = kRulerHeight
                   + r.trackIndex * (kTrackHeight + kTrackGap)
                   - (int)verticalScroll_;

        int x = timeToX(r.startTime);
        int width = (int)(r.duration * pixelsPerSecond_);

        juce::Rectangle<int> rect(x, trackY, width, kTrackHeight);

        if (rect.contains(e.getPosition()))
        {
            if (onRectRegionClicked)
                onRectRegionClicked(r.serial);

            selectedBlock_ = r.serial;
            dragStartTime_ = xToTime(e.x);
            originalStart_ = r.startTime;
            originalDuration_ = r.duration;

            const int edgeThreshold = 5;

            if (std::abs(e.x - rect.getX()) < edgeThreshold)
                dragMode_ = DragMode::ResizeLeft;
            else if (std::abs(e.x - rect.getRight()) < edgeThreshold)
                dragMode_ = DragMode::ResizeRight;
            else
                dragMode_ = DragMode::Move;

            return;
        }else{
            if (onRectRegionClicked)
                onRectRegionClicked(-1);

        }
    }

    selectedBlock_ = -1;
    dragMode_ = DragMode::None;

    if (selectedBlock_ == -1)
    {
        followPlayhead_ = false;

        isPanningTimeline_ = true;
        isUserPanning_ = true;

        lastDragX_ = e.x;
        lastDragY_ = e.y;
    }
}

void TimelineComponent::setCurrentTime(double time)
{
    currentTime_ = time;

    if (isPlaying_ && followPlayhead_ && !isUserPanning_)
    {
        const double anchorTime = playheadAnchorX_ / pixelsPerSecond_;

        if (currentTime_ <= anchorTime)
            viewStartTime_ = 0.0;
        else
            viewStartTime_ = currentTime_ - anchorTime;
    }

    repaint();
}


void TimelineComponent::enableFollowPlayhead()
{
    followPlayhead_ = true;
}

void TimelineComponent::setPlaying(bool playing)
{
    isPlaying_ = playing;

    if (playing)
    {
        followPlayhead_ = true;
        isUserPanning_ = false;
        isPanningTimeline_ = false;
    }
}


