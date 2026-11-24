// ANKS Ring — ring modulation / AM par un VCO
#include "AnksCore.h"

class Ring final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "mode", "Mode", { "Ring", "AM" }, 0);
        anks::addChoice (l, "wave", "Wave", anks::waveNames(), 0);
        anks::addFloat (l, "freq", "Frequency", 0.1f, 8000.0f, 220.0f, 0.25f, "Hz", 1);
        anks::addBool (l, "keyfollow", "Key Follow", false);
        anks::addInt (l, "octave", "Key Octave", -3, 3, 0);
        anks::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 100.0f, 1.0f, "%", 0);
        anks::addFloat (l, "spread", "Stereo Spread", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfofreq", "LFO > Freq", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Ring() : Processor ("ANKS Ring", "Ring modulation et AM : un VCO multiplie la boucle. Robot, cloche, tremolo.",
                        juce::Colour (0xff4fd1c5), layout())
    {
        mode = p ("mode"); wave = p ("wave"); freq = p ("freq"); key = p ("keyfollow"); oct = p ("octave");
        depth = p ("depth"); spread = p ("spread"); lfoAmt = p ("lfofreq");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        freqS.reset (sr, 0.03); depthS.reset (sr, 0.02); spreadS.reset (sr, 0.05);
        freqS.setCurrentAndTargetValue (get (freq)); depthS.setCurrentAndTargetValue (get (depth) * 0.01f);
        spreadS.setCurrentAndTargetValue (get (spread) * 0.005f);
        phase = 0.0f;
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        float f = get (freq);
        if (getb (key) && notes.note() >= 0) f = anks::mtof ((float) notes.note() + 12.0f * (float) geti (oct));
        freqS.setTargetValue (f);
        depthS.setTargetValue (get (depth) * 0.01f);
        spreadS.setTargetValue (get (spread) * 0.005f);
        const int w = geti (wave); const bool am = geti (mode) == 1;
        const float amt = get (lfoAmt) * 0.01f;

        for (int i = 0; i < count; ++i)
        {
            const float l = lfo.next();
            const float hz = juce::jlimit (0.01f, (float) sr * 0.45f, freqS.getNextValue() * std::pow (2.0f, l * amt * 4.0f));
            const float dt = hz / (float) sr;
            const float d = depthS.getNextValue(), sp = spreadS.getNextValue();
            for (int c = 0; c < numCh; ++c)
            {
                float ph = phase + (c == 1 ? sp : 0.0f); if (ph >= 1.0f) ph -= 1.0f;
                const float o = anks::oscWave (w, ph, dt);
                const float x = b.getSample (c, start + i);
                const float m = am ? 1.0f - d * 0.5f * (1.0f - o) : (1.0f - d) + d * o;
                b.setSample (c, start + i, x * m);
            }
            phase += dt; if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    std::atomic<float> *mode, *wave, *freq, *key, *oct, *depth, *spread, *lfoAmt;
    anks::Lfo lfo;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqS;
    juce::SmoothedValue<float> depthS, spreadS;
    float phase = 0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ring(); }
