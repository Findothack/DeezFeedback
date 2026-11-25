#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FeedbackCore.h"

class FeedbackStopperProcessor : public juce::AudioProcessor
{
public:
    FeedbackStopperProcessor();
    ~FeedbackStopperProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Feedback Stopper"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // for the editor
    const fbs::Core& getCore (int channel) const { return cores[juce::jlimit (0, 1, channel)]; }
    void requestClear() { clearFlag.store (true); }
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    fbs::Core cores[2];
    std::atomic<bool> clearFlag { false };

    std::atomic<float> *pSens, *pStep, *pMaxDepth, *pQ, *pHold, *pMaxNotches,
                       *pIn, *pOut, *pBypass, *pLimiter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackStopperProcessor)
};
