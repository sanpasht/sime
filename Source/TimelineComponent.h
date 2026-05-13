#pragma once
#include <JuceHeader.h>
#include "MathUtils.h"
#include "BlockEntry.h"
#include "BlockType.h"


enum class DragMode
{
    None,
    Move,
    ResizeLeft,
    ResizeRight
};



class TimelineComponent : public juce::Component
{
public:
    TimelineComponent();
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, 
                        const juce::MouseWheelDetails& w) override;
    
    /// Update blocks to display
    void setBlocks(const std::vector<BlockEntry>& blocks);
    
    /// Set current playback time (updates playhead position)
    void setCurrentTime(double timeSec);
    
    /// Set zoom level (pixels per second)
    void setZoom(double pixelsPerSecond);
    void setPlaying(bool playing);
    
    /// Callback when user clicks a block region
    std::function<void(int blockSerial)> onRectRegionClicked;
    
    /// Callback when user drags to edit a block's timing
    std::function<void(int serial, double startTime, double duration)> onBlockEdited;

    void setBpm(double newBpm);
    void setSnapToGrid(bool shouldSnap);
    void setSubdivision(int newSubdivision);
    
private:
    struct BlockRegion
    {
        int serial;
        BlockType type;
        double startTime;
        double duration;
        int trackIndex;  // Which row to draw in
        juce::String label;
    };
    // =========================
    // Rhythm / grid settings
    // =========================
    double bpm_ = 120.0;
    int beatsPerBar_ = 4;
    int subdivision_ = 16;     // 16 = sixteenth-note grid
    bool snapToGrid_ = true;


    // =========================
    // Time / pixel conversion
    // =========================
    float pixelsPerSecond_ = 100.0f;
    double viewStartTime_ = 0.0;


    double secondsPerBeat() const;
    double secondsPerSubdivision() const;

    float timeToX(double timeSeconds) const;
    double xToTime(float x) const;
    
    double snapTime(double timeSeconds) const;

    // =========================
    // Drawing helpers
    // =========================
    void drawBeatGrid(juce::Graphics& g);
    void drawTimelineBlocks(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);

    bool isPanningTimeline_ = false;
    bool isUserPanning_ = false;    

    int lastDragX_ = 0;
    int lastDragY_ = 0;

    int playheadAnchorX_ = 0;
    bool followPlayhead_ = true;
    void enableFollowPlayhead();

    bool isPlaying_ = false;
    bool userIsPanning_ = false;
    double dragStartViewStartTime_ = 10.0;
    double verticalScroll_ = 0.0;
    double dragStartVerticalScroll_ = 0.0;
    bool isVerticalDragging_ = false;

    int playheadX_ = getWidth() / 3;
    int selectedBlock_ = -1;
    DragMode dragMode_ = DragMode::None;
    double dragStartTime_ = 0.0;
    double originalStart_ = 0.0;
    double originalDuration_ = 0.0;
    
    std::vector<BlockRegion> regions_;
    
    double currentTime_ = 0.0;         // Playhead position
    double totalDuration_ = 10.0;      // Total timeline length
    
    static constexpr int kRulerHeight = 30;
    static constexpr int kTrackHeight = 50;
    static constexpr int kTrackGap = 2;

    
    void paintTimeRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void paintTracks(juce::Graphics& g, juce::Rectangle<int> area);
    void paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area);
    void paintBeatGrid(juce::Graphics& g, juce::Rectangle<int> tracksArea);
    
    juce::Colour getBlockColor(BlockType type, int soundId) const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};