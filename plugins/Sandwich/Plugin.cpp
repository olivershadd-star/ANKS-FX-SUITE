// ANKS Sandwich — Drive > filtre résonant > Drive : la résonance saturée hurle (acid)
#include "AnksCore.h"

class Sandwich final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "d1type", "Drive 1 Type", anks::driveNames(), 0);
        anks::addFloat (l, "d1drive", "Drive 1", 0.0f, 36.0f, 9.0f, 1.0f, "dB", 1);
        anks::addChoice (l, "ftype", "Filter Type", { "Low Pass", "Band Pass", "High Pass" }, 0);
        anks::addFloat (l, "cutoff", "Cutoff", 20.0f, 20000.0f, 800.0f, 0.25f, "Hz", 0);
        anks::addFloat (l, "reso", "Resonance", 0.0f, 100.0f, 70.0f, 1.0f, "%", 0);
        anks::addFloat (l, "envamt", "Env > Cutoff", -100.0f, 100.0f, 40.0f, 1.0f, "%", 0);
        anks::addFloat (l, "envatt", "Env Attack", 0.1f, 200.0f, 2.0f, 0.4f, "ms", 1);
        anks::addFloat (l, "envrel", "Env Release", 5.0f, 2000.0f, 120.0f, 0.4f, "ms", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfocut", "LFO > Cutoff", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addChoice (l, "d2type", "Drive 2 Type", anks::driveNames(), 3);
        anks::addFloat (l, "d2drive", "Drive 2", 0.0f, 36.0f, 6.0f, 1.0f, "dB", 1);
        anks::addCommon (l);
        return l;
    }

    Sandwich() : Processor ("ANKS Sandwich", "Distorsion, filtre resonant, distorsion : la resonance est saturee et se met a hurler. Tres acid.",
                            juce::Colour (0xffd53f8c), layout())
    {
        d1t = p ("d1type"); d1 = p ("d1drive"); ft = p ("ftype"); cut = p ("cutoff"); reso = p ("reso");
        envAmt = p ("envamt"); envAtt = p ("envatt"); envRel = p ("envrel"); lfoAmt = p ("lfocut");
        d2t = p ("d2type"); d2 = p ("d2drive");
        lfo.attach (state);
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        lfo.prepare (sr);
        hiRate = sr * 2.0;
        lfoBuf.assign ((size_t) maxBlock, 0.0f);
        cutS.reset (hiRate, 0.03); cutS.setCurrentAndTargetValue (get (cut));
        for (auto& f : svf) f.reset();
        for (auto& d : dc) d.prepare (hiRate);
        follower.env = 0.0f; counter = 0;
    }
    void resetWet() override { for (auto& f : svf) f.reset(); }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        for (int i = 0; i < count; ++i) lfoBuf[(size_t) i] = lfo.next();
        follower.set (get (envAtt), get (envRel), hiRate);
        cutS.setTargetValue (get (cut));
        const int t1 = geti (d1t), t2 = geti (d2t), f = geti (ft);
        const float g1 = anks::dbToGain (get (d1)), g2 = anks::dbToGain (get (d2));
        const float q = anks::resoToQ (get (reso)) * 1.5f;
        const float ea = get (envAmt) * 0.06f, la = get (lfoAmt) * 0.04f;
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                float level = 0.0f;
                for (size_t c = 0; c < up.getNumChannels(); ++c) level = juce::jmax (level, std::abs (up.getSample ((int) c, (int) n)));
                const float e = juce::jmin (1.0f, follower.process (level) * 2.0f);
                const float fc = cutS.getNextValue();
                if (counter-- <= 0)
                {
                    counter = 4;
                    const float hz = fc * std::pow (2.0f, lfoBuf[n / 2] * la + e * ea);
                    for (auto& s : svf) s.set (hz, q, hiRate);
                }
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    float x = anks::shape (t1, up.getSample ((int) c, (int) n) * g1);
                    x = anks::filterOut (svf[c], f, x);
                    x = anks::shape (t2, x * g2);
                    up.setSample ((int) c, (int) n, dc[c].process (x) * 0.8f);
                }
            }
        });
    }

private:
    std::atomic<float> *d1t, *d1, *ft, *cut, *reso, *envAmt, *envAtt, *envRel, *lfoAmt, *d2t, *d2;
    anks::Lfo lfo; anks::Oversampler os; anks::EnvFollower follower;
    std::vector<float> lfoBuf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutS;
    std::array<anks::Svf, 2> svf; std::array<anks::DcBlocker, 2> dc;
    double hiRate = 88200.0; int counter = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Sandwich(); }
