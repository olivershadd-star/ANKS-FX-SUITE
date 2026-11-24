// ANKS PhaseMod — délai court modulé jusqu'à la vitesse audio (flanger -> phase modulation)
#include "AnksCore.h"

class PhaseMod final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "freq", "Mod Freq", 0.05f, 2000.0f, 2.0f, 0.22f, "Hz", 2);
        anks::addChoice (l, "wave", "Mod Wave", anks::waveNames(), 0);
        anks::addFloat (l, "depth", "Depth", 0.0f, 5.0f, 1.0f, 0.5f, "ms", 2);
        anks::addFloat (l, "center", "Center", 0.2f, 15.0f, 3.0f, 0.5f, "ms", 2);
        anks::addFloat (l, "feedback", "Feedback", -95.0f, 95.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "spread", "Stereo Spread", 0.0f, 100.0f, 25.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfofreq", "LFO > Freq", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l, 50.0f);
        return l;
    }

    PhaseMod() : Processor ("ANKS PhaseMod", "Un VCO fait vibrer un delai tres court : flanger lent, FM granuleuse en vitesse audio.",
                            juce::Colour (0xff63b3ed), layout())
    {
        freq = p ("freq"); wave = p ("wave"); depth = p ("depth"); center = p ("center"); fb = p ("feedback");
        spread = p ("spread"); lfoAmt = p ("lfofreq");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        for (auto& d : dl) d.prepare ((int) (sr * 0.025) + 8);
        freqS.reset (sr, 0.03); depthS.reset (sr, 0.03); centerS.reset (sr, 0.05); fbS.reset (sr, 0.03);
        freqS.setCurrentAndTargetValue (get (freq)); depthS.setCurrentAndTargetValue (get (depth));
        centerS.setCurrentAndTargetValue (get (center)); fbS.setCurrentAndTargetValue (get (fb) * 0.01f);
        last = { 0.0f, 0.0f }; phase = 0.0f;
    }
    void resetWet() override { for (auto& d : dl) d.clear(); last = { 0.0f, 0.0f }; }
    double getTailLengthSeconds() const override { return 0.2; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        freqS.setTargetValue (get (freq)); depthS.setTargetValue (get (depth));
        centerS.setTargetValue (get (center)); fbS.setTargetValue (get (fb) * 0.01f);
        const int w = geti (wave); const float sp = get (spread) * 0.005f, amt = get (lfoAmt) * 0.01f;
        const float msToS = (float) sr * 0.001f;
        for (int i = 0; i < count; ++i)
        {
            const float hz = juce::jlimit (0.01f, (float) sr * 0.4f, freqS.getNextValue() * std::pow (2.0f, lfo.next() * amt * 4.0f));
            const float dt = hz / (float) sr;
            const float dep = depthS.getNextValue(), cen = centerS.getNextValue(), g = fbS.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                float ph = phase + (c == 1 ? sp : 0.0f); if (ph >= 1.0f) ph -= 1.0f;
                const float o = anks::oscWave (w, ph, dt);
                const float x = b.getSample (c, start + i);
                dl[(size_t) c].push (x + g * last[(size_t) c]);
                const float y = dl[(size_t) c].read (juce::jmax (1.0f, (cen + dep * o) * msToS));
                last[(size_t) c] = std::tanh (y);
                b.setSample (c, start + i, y);
            }
            phase += dt; if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    std::atomic<float> *freq, *wave, *depth, *center, *fb, *spread, *lfoAmt;
    anks::Lfo lfo;
    std::array<anks::Delay, 2> dl;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqS;
    juce::SmoothedValue<float> depthS, centerS, fbS;
    std::array<float, 2> last { 0.0f, 0.0f };
    float phase = 0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaseMod(); }
