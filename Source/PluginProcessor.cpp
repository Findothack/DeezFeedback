#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout FeedbackStopperProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using I = juce::AudioParameterInt;
    using B = juce::AudioParameterBool;
    using R = juce::NormalisableRange<float>;

    APVTS::ParameterLayout l;
    l.add (std::make_unique<I> (juce::ParameterID { "sens", 1 },       "Sensitivity",     1, 10, 5));
    l.add (std::make_unique<F> (juce::ParameterID { "step", 1 },       "Cut per hit",     R (2.0f, 12.0f, 1.0f), 5.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "maxdepth", 1 },   "Max cut",         R (6.0f, 40.0f, 1.0f), 24.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "q", 1 },          "Notch width Q",   R (8.0f, 60.0f, 1.0f), 25.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "hold", 1 },       "Release after",   R (5.0f, 120.0f, 1.0f), 30.0f));
    l.add (std::make_unique<I> (juce::ParameterID { "maxnotches", 1 }, "Max notches",     4, 20, 12));
    l.add (std::make_unique<F> (juce::ParameterID { "ingain", 1 },     "Input gain",      R (-24.0f, 24.0f, 0.1f), 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "outgain", 1 },    "Output gain",     R (-24.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<B> (juce::ParameterID { "bypass", 1 },     "Bypass",          false));
    l.add (std::make_unique<B> (juce::ParameterID { "limiter", 1 },    "Safety limiter",  true));
    return l;
}

FeedbackStopperProcessor::FeedbackStopperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pSens       = apvts.getRawParameterValue ("sens");
    pStep       = apvts.getRawParameterValue ("step");
    pMaxDepth   = apvts.getRawParameterValue ("maxdepth");
    pQ          = apvts.getRawParameterValue ("q");
    pHold       = apvts.getRawParameterValue ("hold");
    pMaxNotches = apvts.getRawParameterValue ("maxnotches");
    pIn         = apvts.getRawParameterValue ("ingain");
    pOut        = apvts.getRawParameterValue ("outgain");
    pBypass     = apvts.getRawParameterValue ("bypass");
    pLimiter    = apvts.getRawParameterValue ("limiter");

    for (auto& c : cores) c.prepare (44100.0);   // so display data is valid before prepareToPlay
}

void FeedbackStopperProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& c : cores) c.prepare (sampleRate);
}

bool FeedbackStopperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void FeedbackStopperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chs = juce::jmin (buffer.getNumChannels(), 2);
    for (int c = chs; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    fbs::Params p;
    p.bypass     = pBypass->load() > 0.5f;
    p.limiter    = pLimiter->load() > 0.5f;
    p.pnpr       = 28.0f - 2.0f * pSens->load();
    p.step       = pStep->load();
    p.maxDepth   = -pMaxDepth->load();
    p.q          = pQ->load();
    p.holdSec    = pHold->load();
    p.maxNotches = (int) pMaxNotches->load();

    const bool clear = clearFlag.exchange (false);
    const float ig = p.bypass ? 1.0f : juce::Decibels::decibelsToGain (pIn->load());
    const float og = p.bypass ? 1.0f : juce::Decibels::decibelsToGain (pOut->load());

    float inPk = 0.0f, outPk = 0.0f;
    for (int c = 0; c < chs; ++c)
    {
        float* d = buffer.getWritePointer (c);
        cores[c].setParams (p);
        if (clear) cores[c].clearNotches();

        for (int i = 0; i < n; ++i) { d[i] *= ig; inPk = juce::jmax (inPk, std::abs (d[i])); }
        cores[c].process (d, d, n);
        for (int i = 0; i < n; ++i) { d[i] *= og; outPk = juce::jmax (outPk, std::abs (d[i])); }
    }
    inPeak.store  (juce::jmax (inPk,  inPeak.load()  * 0.92f));
    outPeak.store (juce::jmax (outPk, outPeak.load() * 0.92f));
}

juce::AudioProcessorEditor* FeedbackStopperProcessor::createEditor()
{
    return new FeedbackStopperEditor (*this);
}

void FeedbackStopperProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FeedbackStopperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FeedbackStopperProcessor();
}
