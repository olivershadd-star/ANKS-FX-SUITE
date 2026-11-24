// ANKS Filter — filtre multimode avec LFO et suiveur d'enveloppe
#include "AnksCore.h"

class Filter final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "type", "Type", { "Low Pass", "Band Pass", "High Pass", "Notch" }, 0);
        anks::addFloat (l, "cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f, 0.25f, "Hz", 0);
        anks::addFloat (l, "reso", "Resonance", 0.0f, 100.0f, 20.0f, 1.0f, "%", 0);
        anks::addFloat (l, "drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "envamt", "Env > Cutoff", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "envatt", "Env Attack", 0.1f, 200.0f, 5.0f, 0.4f, "ms", 1);
        anks::addFloat (l, "envrel", "Env Release", 5.0f, 2000.0f, 150.0f, 0.4f, "ms", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfocut", "LFO > Cutoff", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Filter() : Processor ("ANKS Filter", "Filtre classique qui bouge tout seul : LFO cale sur le tempo et suiveur d'enveloppe.",
                          juce::Colour (0xff4299e1), layout())
    {
        type = p ("type"); cutoff = p ("cutoff"); reso = p ("reso"); drive = p ("drive"); envAmt = p ("envamt");
        envAtt = p ("envatt"); envRel = p ("envrel"); lfoAmt = p ("lfocut");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        cutS.reset (sr, 0.03); cutS.setCurrentAndTargetValue (get (cutoff));
        for (auto& f : svf) f.reset();
        env = 0.0f; counter = 0;
    }
    void resetWet() override { for (auto& f : svf) f.reset(); }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        cutS.setTargetValue (get (cutoff));
        const int t = geti (type);
        const float q = 0.5f * std::pow (40.0f, get (reso) * 0.01f);
        const float g = anks::dbToGain (get (drive));
        const float comp = 1.0f / std::pow (juce::jmax (1.0f, q), 0.4f) / std::sqrt (juce::jmax (1.0f, g));   // garde le niveau sous contrôle
        const float ea = get (envAmt) * 0.01f * 6.0f, la = get (lfoAmt) * 0.01f * 4.0f;
        const float aC = 1.0f - std::exp (-1.0f / (get (envAtt) * 0.001f * (float) sr));
        const float rC = 1.0f - std::exp (-1.0f / (get (envRel) * 0.001f * (float) sr));
        for (int i = 0; i < count; ++i)
        {
            float level = 0.0f;
            for (int c = 0; c < numCh; ++c) level = juce::jmax (level, std::abs (b.getSample (c, start + i)));
            env += (level > env ? aC : rC) * (level - env);
            const float l = lfo.next();
            const float fc = cutS.getNextValue();
            if (counter-- <= 0)
            {
                counter = 4;
                const float hz = fc * std::pow (2.0f, l * la + juce::jmin (1.0f, env * 2.0f) * ea);
                for (auto& f : svf) f.set (hz, q, sr);
            }
            for (int c = 0; c < numCh; ++c)
            {
                float x = b.getSample (c, start + i);
                if (g > 1.001f) x = std::tanh (x * g);
                float lp, bp, hp; svf[(size_t) c].tick (x, lp, bp, hp);
                const float y = t == 0 ? lp : t == 1 ? bp * svf[(size_t) c].k : t == 2 ? hp : lp + hp;
                b.setSample (c, start + i, (t == 1 ? y : y * comp));
            }
        }
    }

private:
    std::atomic<float> *type, *cutoff, *reso, *drive, *envAmt, *envAtt, *envRel, *lfoAmt;
    anks::Lfo lfo;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutS;
    std::array<anks::Svf, 2> svf;
    float env = 0.0f; int counter = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Filter(); }
