// ANKS Crush — réduction de bits et d'échantillonnage modulée par LFO
#include "AnksCore.h"

class Crush final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "bits", "Bits", 1.0f, 16.0f, 8.0f, 1.0f, "bit", 1);
        anks::addFloat (l, "rate", "Sample Rate", 200.0f, 48000.0f, 8000.0f, 0.3f, "Hz", 0);
        anks::addFloat (l, "jitter", "Jitter", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "tone", "Tone", 500.0f, 20000.0f, 20000.0f, 0.3f, "Hz", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfobits", "LFO > Bits", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lforate2", "LFO > Rate", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Crush() : Processor ("ANKS Crush", "Degradation numerique vivante : bits et frequence d'echantillonnage qui respirent avec le LFO.",
                         juce::Colour (0xfffc8181), layout())
    {
        bits = p ("bits"); rate = p ("rate"); jitter = p ("jitter"); drive = p ("drive"); tone = p ("tone");
        lfoBits = p ("lfobits"); lfoRate = p ("lforate2");
        lfo.attach (state); rng.seed (0xC0FFEEu);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        bitsS.reset (sr, 0.02); rateS.reset (sr, 0.02);
        bitsS.setCurrentAndTargetValue (get (bits)); rateS.setCurrentAndTargetValue (get (rate));
        for (auto& l : lp) l.reset();
        phase = 1.0f; hold = { 0.0f, 0.0f };
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        bitsS.setTargetValue (get (bits)); rateS.setTargetValue (get (rate));
        const float jit = get (jitter) * 0.01f, g = anks::dbToGain (get (drive));
        const float ab = get (lfoBits) * 0.01f * 8.0f, ar = get (lfoRate) * 0.01f * 3.0f;
        for (auto& l : lp) l.setHz (get (tone), sr);
        for (int i = 0; i < count; ++i)
        {
            const float l = lfo.next();
            const float bt = juce::jlimit (1.0f, 16.0f, bitsS.getNextValue() + l * ab);
            const float hz = juce::jlimit (50.0f, (float) sr, rateS.getNextValue() * std::pow (2.0f, l * ar));
            phase += hz / (float) sr;
            const bool sample = phase >= 1.0f;
            if (sample) phase -= 1.0f + jit * 0.9f * rng.uni();   // jitter : l'échantillon suivant arrive plus tard
            const float levels = std::pow (2.0f, bt - 1.0f);
            for (int c = 0; c < numCh; ++c)
            {
                if (sample)
                {
                    float x = b.getSample (c, start + i);
                    if (g > 1.001f) x = std::tanh (x * g);
                    hold[(size_t) c] = std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels;
                }
                b.setSample (c, start + i, lp[(size_t) c].process (hold[(size_t) c]));
            }
        }
    }

private:
    std::atomic<float> *bits, *rate, *jitter, *drive, *tone, *lfoBits, *lfoRate;
    anks::Lfo lfo; anks::Rng rng;
    juce::SmoothedValue<float> bitsS;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> rateS;
    std::array<anks::OnePole, 2> lp;
    float phase = 1.0f; std::array<float, 2> hold { 0.0f, 0.0f };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Crush(); }
