#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Live spectrum with the active notches drawn on top.
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (FeedbackStopperProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    FeedbackStopperProcessor& proc;
    std::array<float, fbs::Core::HALF + 1> smooth;
};

class FeedbackStopperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FeedbackStopperEditor (FeedbackStopperProcessor&);
    ~FeedbackStopperEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    FeedbackStopperProcessor& proc;
    SpectrumView spectrum;
    juce::ToggleButton bypassButton { "Bypass" }, limiterButton { "Safety limiter" };
    juce::TextButton clearButton { "Clear notches" };
    juce::Label meters;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach, limiterAttach;
    std::array<Knob, 8> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackStopperEditor)
};
