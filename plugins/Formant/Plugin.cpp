// ANKS Formant — filtre à voyelles morphable A E I O U
#include "AnksCore.h"

namespace
{
// F1, F2, F3 (Hz) et gains, voix adulte
const float vowelF[5][3] { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
const float vowelG[5][3] { { 1.0f, 0.50f, 0.25f }, { 1.0f, 0.40f, 0.30f }, { 1.0f, 0.25f, 0.30f }, { 1.0f, 0.60f, 0.15f }, { 1.0f, 0.30f, 0.10f } };
juce::String vowelText (float v, int)
{
    const char* names[] { "A", "E", "I", "O", "U" };
    const int i = juce::jlimit (0, 4, juce::roundToInt (v));
    return juce::String (names[i]) + " (" + juce::String (v, 2) + ")";
}
}

class Formant final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloatText (l, "vowel", "Vowel", 0.0f, 4.0f, 0.0f, 1.0f, vowelText);
        anks::addFloat (l, "shift", "Formant Shift", -12.0f, 12.0f, 0.0f, 1.0f, "st", 1);
        anks::addFloat (l, "reso", "Resonance", 2.0f, 30.0f, 10.0f, 0.6f, "Q", 1);
        anks::addFloat (l, "drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB", 1);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfovowel", "LFO > Vowel", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Formant() : Processor ("ANKS Formant", "Wah qui parle : trois filtres de voix glissent entre A, E, I, O, U.",
                           juce::Colour (0xfff56565), layout())
    {
        vowel = p ("vowel"); shift = p ("shift"); reso = p ("reso"); drive = p ("drive"); lfoAmt = p ("lfovowel");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        vowelS.reset (sr, 0.03); vowelS.setCurrentAndTargetValue (get (vowel));
        for (auto& ch : f) for (auto& s : ch) s.reset();
        counter = 0;
    }
    void resetWet() override { for (auto& ch : f) for (auto& s : ch) s.reset(); }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        vowelS.setTargetValue (get (vowel));
        const float ratio = std::pow (2.0f, get (shift) / 12.0f), q = get (reso);
        const float g = anks::dbToGain (get (drive)), amt = get (lfoAmt) * 0.01f * 2.0f;
        for (int i = 0; i < count; ++i)
        {
            const float v = juce::jlimit (0.0f, 4.0f, vowelS.getNextValue() + lfo.next() * amt);
            if (counter-- <= 0)
            {
                counter = 8;
                const int v0 = juce::jmin (3, (int) v); const float t = v - (float) v0;
                for (int k = 0; k < 3; ++k)
                {
                    const float hz = (vowelF[v0][k] + t * (vowelF[v0 + 1][k] - vowelF[v0][k])) * ratio;
                    gains[(size_t) k] = vowelG[v0][k] + t * (vowelG[v0 + 1][k] - vowelG[v0][k]);
                    for (int c = 0; c < 2; ++c) f[(size_t) c][(size_t) k].set (hz, q, sr);
                }
            }
            for (int c = 0; c < numCh; ++c)
            {
                const float x = g > 1.001f ? std::tanh (b.getSample (c, start + i) * g) : b.getSample (c, start + i);
                float y = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    auto& s = f[(size_t) c][(size_t) k];
                    float lp, bp, hp; s.tick (x, lp, bp, hp);
                    y += bp * s.k * gains[(size_t) k];
                }
                b.setSample (c, start + i, y * 3.0f);
            }
        }
    }

private:
    std::atomic<float> *vowel, *shift, *reso, *drive, *lfoAmt;
    anks::Lfo lfo;
    juce::SmoothedValue<float> vowelS;
    std::array<std::array<anks::Svf, 3>, 2> f;
    std::array<float, 3> gains { 1.0f, 0.5f, 0.25f };
    int counter = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Formant(); }
