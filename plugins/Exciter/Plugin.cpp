// ANKS Exciter — ne sature que les aigus et les ajoute au signal : brillance sans toucher au corps
#include "AnksCore.h"

class Exciter final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "freq", "Frequency", 1000.0f, 12000.0f, 3000.0f, 0.4f, "Hz", 0);
        anks::addChoice (l, "harm", "Harmonics", { "Odd", "Even", "Both" }, 2);
        anks::addFloat (l, "drive", "Drive", 0.0f, 36.0f, 12.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "amount", "Amount", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addFloat (l, "air", "Air", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lowboost", "Low Warmth", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Exciter() : Processor ("ANKS Exciter", "Sature seulement les aigus et les ajoute : du brillant et du mordant sans abimer le corps du son.",
                           juce::Colour (0xfff6e05e), layout())
    {
        freq = p ("freq"); harm = p ("harm"); drive = p ("drive"); amount = p ("amount"); air = p ("air"); low = p ("lowboost");
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        const double hi = sr * 2.0;
        for (auto& f : split) f.reset();
        for (auto& f : post) f.reset();
        for (auto& f : airLp) f.reset();
        for (auto& f : lowLp) f.reset();
        for (auto& d : dc) d.prepare (hi);
        amtS.reset (hi, 0.03); amtS.setCurrentAndTargetValue (get (amount) * 0.01f);
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const double hi = sr * 2.0;
        for (auto& f : split) f.setHz (get (freq), hi);
        for (auto& f : post) f.setHz (get (freq) * 0.7f, hi);
        for (auto& f : airLp) f.setHz (10000.0f, hi);
        for (auto& f : lowLp) f.setHz (150.0f, hi);
        amtS.setTargetValue (get (amount) * 0.01f);
        const int h = geti (harm);
        const float g = anks::dbToGain (get (drive)), comp = 1.0f / std::sqrt (juce::jmax (1.0f, g));
        const float ar = get (air) * 0.01f, lw = get (low) * 0.01f;
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                const float a = amtS.getNextValue();
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    const float x = up.getSample ((int) c, (int) n);
                    const float highs = x - split[c].process (x);
                    const float z = highs * g;
                    float s = h == 0 ? std::tanh (z) : h == 1 ? std::tanh (std::abs (z)) : 0.5f * (std::tanh (z) + std::tanh (z * 0.7f + 0.3f) - std::tanh (0.3f));
                    s = dc[c].process (s) * comp;
                    s -= post[c].process (s);                         // on garde seulement les nouvelles harmoniques aiguës
                    const float airBand = x - airLp[c].process (x);
                    const float lowBand = lowLp[c].process (x);
                    up.setSample ((int) c, (int) n, x + a * 1.2f * s + ar * 1.0f * airBand + lw * 0.8f * std::tanh (lowBand * 2.0f));
                }
            }
        });
    }

private:
    std::atomic<float> *freq, *harm, *drive, *amount, *air, *low;
    anks::Oversampler os;
    std::array<anks::OnePole, 2> split, post, airLp, lowLp; std::array<anks::DcBlocker, 2> dc;
    juce::SmoothedValue<float> amtS;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Exciter(); }
