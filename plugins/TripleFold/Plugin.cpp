// ANKS Triple Fold — trois wavefolders sinusoïdaux en série (suréchantillonnage x4)
#include "AnksCore.h"

class TripleFold final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        for (int s = 1; s <= 3; ++s)
        {
            const juce::String id (s), name = "Stage " + id + " ";
            anks::addBool (l, id + "on", name + "On", s == 1);
            anks::addFloat (l, id + "fold", name + "Fold", 0.0f, 24.0f, 6.0f, 1.0f, "dB", 1);
            anks::addFloat (l, id + "sym", name + "Symmetry", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
            anks::addFloat (l, id + "tone", name + "Tone", 200.0f, 20000.0f, 12000.0f, 0.3f, "Hz", 0);
        }
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfofold", "LFO > Fold", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lfosym", "LFO > Symmetry", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    TripleFold() : Processor ("ANKS Triple Fold", "Trois wavefolders en serie : l'onde se replie sur elle-meme, son metallique de modulaire.",
                              juce::Colour (0xffd69e2e), layout())
    {
        for (int s = 0; s < 3; ++s)
        {
            const juce::String id (s + 1);
            on[s] = p (id + "on"); fold[s] = p (id + "fold"); sym[s] = p (id + "sym"); tone[s] = p (id + "tone");
        }
        lfoFold = p ("lfofold"); lfoSym = p ("lfosym");
        lfo.attach (state);
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 2);
        setWetLatency (os.latency());
        const double hi = sr * 4.0;
        lfo.prepare (sr);
        lfoBuf.assign ((size_t) maxBlock, 0.0f);
        for (int s = 0; s < 3; ++s)
        {
            foldS[s].reset (hi, 0.02); symS[s].reset (hi, 0.02); onS[s].reset (hi, 0.02);
            foldS[s].setCurrentAndTargetValue (anks::dbToGain (get (fold[s])));
            symS[s].setCurrentAndTargetValue (get (sym[s]) * 0.01f);
            onS[s].setCurrentAndTargetValue (getb (on[s]) ? 1.0f : 0.0f);
            for (auto& d : dc[s]) d.prepare (hi);
            for (auto& f : lp[s]) f.reset();
        }
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        for (int i = 0; i < count; ++i) lfoBuf[(size_t) i] = lfo.next();
        for (int s = 0; s < 3; ++s)
        {
            foldS[s].setTargetValue (anks::dbToGain (get (fold[s])));
            symS[s].setTargetValue (get (sym[s]) * 0.01f);
            onS[s].setTargetValue (getb (on[s]) ? 1.0f : 0.0f);
            for (auto& f : lp[s]) f.setHz (get (tone[s]), sr * 4.0);
        }
        const float lf = get (lfoFold) * 0.01f, ls = get (lfoSym) * 0.01f;
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                const float l = lfoBuf[n / 4];
                const float foldMod = std::pow (2.0f, l * lf * 2.0f);
                float g[3], bias[3], en[3];
                for (int s = 0; s < 3; ++s)
                {
                    g[s] = foldS[s].getNextValue() * foldMod;
                    bias[s] = juce::jlimit (-1.0f, 1.0f, symS[s].getNextValue() + l * ls) * 0.5f;
                    en[s] = onS[s].getNextValue();
                }
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    float x = up.getSample ((int) c, (int) n);
                    for (int s = 0; s < 3; ++s)
                    {
                        const float y = std::sin (anks::pi * 0.5f * (x * g[s] + bias[s]));
                        const float w = lp[s][c].process (dc[s][c].process (y));
                        x += en[s] * (w - x);
                    }
                    up.setSample ((int) c, (int) n, x);
                }
            }
        });
    }

private:
    std::atomic<float>* on[3]; std::atomic<float>* fold[3]; std::atomic<float>* sym[3]; std::atomic<float>* tone[3];
    std::atomic<float> *lfoFold, *lfoSym;
    anks::Lfo lfo; anks::Oversampler os;
    std::vector<float> lfoBuf;
    juce::SmoothedValue<float> foldS[3], symS[3], onS[3];
    std::array<anks::DcBlocker, 2> dc[3];
    std::array<anks::OnePole, 2> lp[3];
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TripleFold(); }
