// ANKS Crush Drive — distorsion > bitcrush > distorsion : dégradation numérique sale mais grasse
#include "AnksCore.h"

class CrushDrive final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "d1type", "Drive 1 Type", anks::driveNames(), 0);
        anks::addFloat (l, "d1drive", "Drive 1", 0.0f, 36.0f, 6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "bits", "Bits", 1.0f, 16.0f, 6.0f, 1.0f, "bit", 1);
        anks::addFloat (l, "rate", "Sample Rate", 200.0f, 48000.0f, 11025.0f, 0.3f, "Hz", 0);
        anks::addFloat (l, "jitter", "Jitter", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addChoice (l, "d2type", "Drive 2 Type", anks::driveNames(), 3);
        anks::addFloat (l, "d2drive", "Drive 2", 0.0f, 36.0f, 6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "tone", "Tone", 500.0f, 20000.0f, 12000.0f, 0.3f, "Hz", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfobits", "LFO > Bits", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lforate2", "LFO > Rate", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    CrushDrive() : Processor ("ANKS Crush Drive", "Distorsion, bitcrush, distorsion : le numerique casse, mais avec du gras autour.",
                              juce::Colour (0xffc53030), layout())
    {
        d1t = p ("d1type"); d1 = p ("d1drive"); bits = p ("bits"); rate = p ("rate"); jitter = p ("jitter");
        d2t = p ("d2type"); d2 = p ("d2drive"); tone = p ("tone"); lfoBits = p ("lfobits"); lfoRate = p ("lforate2");
        lfo.attach (state); rng.seed (0xD15Du);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        bitsS.reset (sr, 0.02); rateS.reset (sr, 0.02);
        bitsS.setCurrentAndTargetValue (get (bits)); rateS.setCurrentAndTargetValue (get (rate));
        for (auto& l : lp) l.reset();
        for (auto& d : dc) d.prepare (sr);
        phase = 1.0f; hold = { 0.0f, 0.0f };
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        bitsS.setTargetValue (get (bits)); rateS.setTargetValue (get (rate));
        const int t1 = geti (d1t), t2 = geti (d2t);
        const float g1 = anks::dbToGain (get (d1)), g2 = anks::dbToGain (get (d2));
        const float comp = 1.0f / std::sqrt (juce::jmax (1.0f, g2));
        const float jit = get (jitter) * 0.01f, ab = get (lfoBits) * 0.08f, ar = get (lfoRate) * 0.03f;
        for (auto& l : lp) l.setHz (get (tone), sr);
        for (int i = 0; i < count; ++i)
        {
            const float l = lfo.next();
            const float bt = juce::jlimit (1.0f, 16.0f, bitsS.getNextValue() + l * ab);
            const float hz = juce::jlimit (50.0f, (float) sr, rateS.getNextValue() * std::pow (2.0f, l * ar));
            phase += hz / (float) sr;
            const bool sample = phase >= 1.0f;
            if (sample) phase -= 1.0f + jit * 0.9f * rng.uni();
            const float levels = std::pow (2.0f, bt - 1.0f);
            for (int c = 0; c < numCh; ++c)
            {
                if (sample)
                {
                    const float x = anks::shape (t1, b.getSample (c, start + i) * g1);
                    hold[(size_t) c] = std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels;
                }
                const float y = anks::shape (t2, hold[(size_t) c] * g2) * comp * 1.3f;
                b.setSample (c, start + i, lp[(size_t) c].process (dc[(size_t) c].process (y)));
            }
        }
    }

private:
    std::atomic<float> *d1t, *d1, *bits, *rate, *jitter, *d2t, *d2, *tone, *lfoBits, *lfoRate;
    anks::Lfo lfo; anks::Rng rng;
    juce::SmoothedValue<float> bitsS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> rateS;
    std::array<anks::OnePole, 2> lp; std::array<anks::DcBlocker, 2> dc;
    float phase = 1.0f; std::array<float, 2> hold { 0.0f, 0.0f };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CrushDrive(); }
