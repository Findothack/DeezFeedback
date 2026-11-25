#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      { 0xff0f1216 };
const juce::Colour kPlot    { 0xff0b0e11 };
const juce::Colour kGrid    { 0xff1d252d };
const juce::Colour kDim     { 0xff6b7783 };
const juce::Colour kAccent  { 0xff4cc2ff };
const juce::Colour kNotch   { 0xffffb454 };

juce::String freqText (float f)
{
    return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (f)) + " Hz";
}
}

//==============================================================================
SpectrumView::SpectrumView (FeedbackStopperProcessor& p) : proc (p)
{
    smooth.fill (-120.0f);
    startTimerHz (30);
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPlot);
    g.fillRoundedRectangle (r, 6.0f);

    const float padL = 34.0f, padB = 18.0f, top = 6.0f, right = 6.0f;
    auto plot = juce::Rectangle<float> (r.getX() + padL, r.getY() + top,
                                        r.getWidth() - padL - right, r.getHeight() - top - padB);
    const float fmin = 20.0f, fmax = 20000.0f, dbMin = -100.0f, dbMax = 0.0f;
    const float lmin = std::log10 (fmin), lmax = std::log10 (fmax);

    auto X = [&] (float f) { return plot.getX() + plot.getWidth() * (std::log10 (f) - lmin) / (lmax - lmin); };
    auto Y = [&] (float d) { return plot.getY() + plot.getHeight() * (1.0f - (d - dbMin) / (dbMax - dbMin)); };

    g.setFont (11.0f);
    for (int d : { -20, -40, -60, -80 })
    {
        g.setColour (kGrid);
        g.drawHorizontalLine ((int) Y ((float) d), plot.getX(), plot.getRight());
        g.setColour (kDim);
        g.drawText (juce::String (d), 0, (int) Y ((float) d) - 6, (int) padL - 4, 12, juce::Justification::right);
    }
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (kGrid);
        g.drawVerticalLine ((int) X (f), plot.getY(), plot.getBottom());
        g.setColour (kDim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    (int) X (f) - 15, (int) plot.getBottom() + 3, 30, 12, juce::Justification::centred);
    }

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (plot.toNearestInt());

    const auto& core = proc.getCore (0);
    const float* spec = core.spectrumDb();
    const float binHz = core.sampleRate() / (float) fbs::Core::N;

    for (int k = 0; k <= fbs::Core::HALF; ++k)
        smooth[(size_t) k] = smooth[(size_t) k] * 0.55f + spec[k] * 0.45f;

    juce::Path path;
    bool started = false;
    float lastX = plot.getX();
    for (int k = 1; k <= fbs::Core::HALF; ++k)
    {
        const float f = (float) k * binHz;
        if (f < fmin || f > fmax) continue;
        const float x = X (f), y = Y (juce::jlimit (dbMin, dbMax, smooth[(size_t) k]));
        if (! started) { path.startNewSubPath (x, y); started = true; }
        else path.lineTo (x, y);
        lastX = x;
    }
    if (started)
    {
        juce::Path fill (path);
        fill.lineTo (lastX, plot.getBottom());
        fill.lineTo (X (fmin), plot.getBottom());
        fill.closeSubPath();
        g.setColour (kAccent.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (kAccent);
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }

    for (int i = 0; i < core.numDisplayNotches(); ++i)
    {
        float f, d;
        if (! core.displayNotch (i, f, d) || f < fmin || f > fmax) continue;
        const float x = X (f), a = juce::jmin (1.0f, std::abs (d) / 20.0f);
        g.setColour (kNotch.withAlpha (0.4f + 0.5f * a));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 2.0f);
        const auto label = freqText (f) + " " + juce::String (d, 0);
        const int tw = 90;
        g.setColour (kNotch);
        g.drawText (label, (int) juce::jmin (x + 4.0f, plot.getRight() - (float) tw), (int) plot.getY() + 2, tw, 12,
                    juce::Justification::left);
    }
}

//==============================================================================
FeedbackStopperEditor::FeedbackStopperEditor (FeedbackStopperProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setSize (860, 520);
    addAndMakeVisible (spectrum);

    addAndMakeVisible (bypassButton);
    addAndMakeVisible (limiterButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (meters);
    meters.setJustificationType (juce::Justification::centredRight);
    meters.setColour (juce::Label::textColourId, kDim);

    bypassAttach  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bypass",  bypassButton);
    limiterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "limiter", limiterButton);
    clearButton.onClick = [this] { proc.requestClear(); };

    struct Def { const char* id; const char* name; const char* suffix; };
    const Def defs[8] = {
        { "sens",       "Sensitivity",   ""      },
        { "step",       "Cut per hit",   " dB"   },
        { "maxdepth",   "Max cut",       " dB"   },
        { "q",          "Notch width Q", ""      },
        { "hold",       "Release after", " s"    },
        { "maxnotches", "Max notches",   ""      },
        { "ingain",     "Input gain",    " dB"   },
        { "outgain",    "Output gain",   " dB"   },
    };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setTextValueSuffix (defs[i].suffix);
        k.label.setText (defs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, kDim);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, defs[i].id, k.slider);
    }
    startTimerHz (10);
}

void FeedbackStopperEditor::timerCallback()
{
    auto db = [] (float v) { return v > 1.0e-5f ? juce::String (juce::Decibels::gainToDecibels (v), 1) : juce::String ("-inf"); };
    meters.setText ("In " + db (proc.inPeak.load()) + " dB    Out " + db (proc.outPeak.load()) + " dB",
                    juce::dontSendNotification);
}

void FeedbackStopperEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
}

void FeedbackStopperEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (28);
    bypassButton.setBounds (top.removeFromLeft (90));
    limiterButton.setBounds (top.removeFromLeft (130));
    clearButton.setBounds (top.removeFromLeft (120).reduced (0, 2));
    meters.setBounds (top);

    area.removeFromTop (6);
    auto knobRow = area.removeFromBottom (140);
    spectrum.setBounds (area.withTrimmedBottom (8));

    const int w = knobRow.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto col = knobRow.removeFromLeft (w);
        k.label.setBounds (col.removeFromTop (18));
        k.slider.setBounds (col);
    }
}
