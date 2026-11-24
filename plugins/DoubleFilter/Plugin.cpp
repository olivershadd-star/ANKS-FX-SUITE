// ANKS Double Filter — deux filtres multimodes en série ou en parallèle, chacun modulé
#include "AnksCore.h"

class DoubleFilter final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "routing", "Routing", { "Serial", "Parallel" }, 0);
        anks::addFloat (l, "drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB", 1);
        anks::addChoice (l, "atype", "A Type", anks::filterNames(), 0);
        anks::addFloat (l, "acut", "A Cutoff", 20.0f, 20000.0f, 2000.0f, 0.25f, "Hz", 0);
        anks::addFloat (l, "areso", "A Resonance", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addChoice (l, "btype", "B Type", anks::filterNames(), 2);
        anks::addFloat (l, "bcut", "B Cutoff", 20.0f, 20000.0f, 200.0f, 0.25f, "Hz", 0);
        anks::addFloat (l, "breso", "B Resonance", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addFloat (l, "balance", "A/B Balance", 0.0f, 100.0f, 50.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfoa", "LFO > A", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lfob", "LFO > B", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "enva", "Env > A", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "envb", "Env > B", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "envatt", "Env Attack", 0.1f, 200.0f, 5.0f, 0.4f, "ms", 1);
        anks::addFloat (l, "envrel", "Env Release", 5.0f, 2000.0f, 150.0f, 0.4f, "ms", 0);
        anks::addCommon (l);
        return l;
    }

    DoubleFilter() : Processor ("ANKS Double Filter", "Deux filtres qui bougent l'un contre l'autre : en serie ils creusent une bande, en parallele ils se melangent.",
                                juce::Colour (0xff3182ce), layout())
    {
        routing = p ("routing"); drive = p ("drive"); balance = p ("balance");
        type[0] = p ("atype"); cut[0] = p ("acut"); reso[0] = p ("areso"); lfoAmt[0] = p ("lfoa"); envAmt[0] = p ("enva");
        type[1] = p ("btype"); cut[1] = p ("bcut"); reso[1] = p ("breso"); lfoAmt[1] = p ("lfob"); envAmt[1] = p ("envb");
        envAtt = p ("envatt"); envRel = p ("envrel");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        for (int f = 0; f < 2; ++f) { cutS[f].reset (sr, 0.03); cutS[f].setCurrentAndTargetValue (get (cut[f])); }
        for (auto& ch : svf) for (auto& f : ch) f.reset();
        follower.env = 0.0f; counter = 0;
    }
    void resetWet() override { for (auto& ch : svf) for (auto& f : ch) f.reset(); }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        follower.set (get (envAtt), get (envRel), sr);
        const bool parallel = geti (routing) == 1;
        const float g = anks::dbToGain (get (drive)), bal = get (balance) * 0.01f;
        int t[2]; float q[2], comp[2], la[2], ea[2];
        for (int f = 0; f < 2; ++f)
        {
            cutS[f].setTargetValue (get (cut[f]));
            t[f] = geti (type[f]); q[f] = anks::resoToQ (get (reso[f]));
            comp[f] = t[f] == 1 ? 1.0f : 1.0f / std::pow (juce::jmax (1.0f, q[f]), 0.4f);
            la[f] = get (lfoAmt[f]) * 0.04f; ea[f] = get (envAmt[f]) * 0.06f;
        }
        const float driveComp = 1.0f / std::sqrt (juce::jmax (1.0f, g));
        for (int i = 0; i < count; ++i)
        {
            float level = 0.0f;
            for (int c = 0; c < numCh; ++c) level = juce::jmax (level, std::abs (b.getSample (c, start + i)));
            const float e = juce::jmin (1.0f, follower.process (level) * 2.0f);
            const float l = lfo.next();
            const float c0 = cutS[0].getNextValue(), c1 = cutS[1].getNextValue();
            if (counter-- <= 0)
            {
                counter = 4;
                const float hz[2] { c0 * std::pow (2.0f, l * la[0] + e * ea[0]), c1 * std::pow (2.0f, l * la[1] + e * ea[1]) };
                for (int c = 0; c < 2; ++c) for (int f = 0; f < 2; ++f) svf[(size_t) c][(size_t) f].set (hz[f], q[f], sr);
            }
            for (int c = 0; c < numCh; ++c)
            {
                float x = b.getSample (c, start + i);
                if (g > 1.001f) x = std::tanh (x * g) * driveComp * 1.5f;
                float y;
                if (parallel)
                {
                    const float ya = anks::filterOut (svf[(size_t) c][0], t[0], x) * comp[0];
                    const float yb = anks::filterOut (svf[(size_t) c][1], t[1], x) * comp[1];
                    y = (1.0f - bal) * ya + bal * yb;
                    y *= 1.4f;
                }
                else
                {
                    const float ya = anks::filterOut (svf[(size_t) c][0], t[0], x) * comp[0];
                    y = anks::filterOut (svf[(size_t) c][1], t[1], ya) * comp[1];
                }
                b.setSample (c, start + i, y);
            }
        }
    }

private:
    std::atomic<float> *routing, *drive, *balance, *envAtt, *envRel;
    std::atomic<float>* type[2]; std::atomic<float>* cut[2]; std::atomic<float>* reso[2];
    std::atomic<float>* lfoAmt[2]; std::atomic<float>* envAmt[2];
    anks::Lfo lfo; anks::EnvFollower follower;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutS[2];
    std::array<std::array<anks::Svf, 2>, 2> svf;
    int counter = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DoubleFilter(); }
