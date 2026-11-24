// ANKS Shaper — la table d'onde comme fonction de transfert (waveshaping morphable)
#include "AnksCore.h"

class Shaper final : public anks::Processor
{
public:
    static constexpr int tableSize = 2048, frames = 8, banks = 4;

    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "drive", "Drive", 0.0f, 36.0f, 6.0f, 1.0f, "dB", 1);
        anks::addChoice (l, "bank", "Table", { "Sines", "Cheby", "Folds", "Steps" }, 0);
        anks::addFloat (l, "position", "Position", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "bias", "Bias", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "tone", "Tone", 200.0f, 20000.0f, 20000.0f, 0.3f, "Hz", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfopos", "LFO > Position", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Shaper() : Processor ("ANKS Shaper", "La boucle lit une table d'onde : distorsion qui change de grain en balayant la table.",
                          juce::Colour (0xffe8a33d), layout())
    {
        drive = p ("drive"); bank = p ("bank"); pos = p ("position"); bias = p ("bias"); tone = p ("tone"); lfoAmt = p ("lfopos");
        lfo.attach (state);
        buildTables();
    }

    void prepareWet (double, int maxBlock) override
    {
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        os->initProcessing ((size_t) maxBlock); os->reset();
        setWetLatency (juce::roundToInt (os->getLatencyInSamples()));
        const double hi = sr * 2.0;
        lfo.prepare (sr);
        for (auto* s : { &driveS, &posS, &biasS }) s->reset (hi, 0.02);
        driveS.setCurrentAndTargetValue (anks::dbToGain (get (drive)));
        posS.setCurrentAndTargetValue (get (pos) * 0.01f);
        biasS.setCurrentAndTargetValue (get (bias) * 0.005f);
        for (auto& d : dc) d.prepare (hi);
        for (auto& t : lp) t.reset();
        lfoBuf.assign ((size_t) maxBlock, 0.0f);
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        for (int i = 0; i < count; ++i) lfoBuf[(size_t) i] = lfo.next();
        driveS.setTargetValue (anks::dbToGain (get (drive)));
        posS.setTargetValue (get (pos) * 0.01f);
        biasS.setTargetValue (get (bias) * 0.005f);
        const int bk = juce::jlimit (0, banks - 1, geti (bank));
        const float amt = get (lfoAmt) * 0.01f;
        for (auto& t : lp) t.setHz (get (tone), sr * 2.0);

        juce::dsp::AudioBlock<float> whole (b);
        auto block = whole.getSubsetChannelBlock (0, (size_t) numCh).getSubBlock ((size_t) start, (size_t) count);
        auto up = os->processSamplesUp (block);
        for (size_t n = 0; n < up.getNumSamples(); ++n)
        {
            const float g = driveS.getNextValue(), bi = biasS.getNextValue();
            const float position = juce::jlimit (0.0f, 1.0f, posS.getNextValue() + lfoBuf[n / 2] * amt);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                const float x = up.getSample ((int) c, (int) n);
                const float y = lookup (bk, position, juce::jlimit (-1.0f, 1.0f, x * g + bi));
                up.setSample ((int) c, (int) n, lp[c].process (dc[c].process (y)));
            }
        }
        os->processSamplesDown (block);
    }

private:
    void buildTables()
    {
        tables.assign ((size_t) (banks * frames * tableSize), 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            for (int i = 0; i < tableSize; ++i)
            {
                const float x = -1.0f + 2.0f * (float) i / (float) (tableSize - 1);
                // Sines : sinus impairs de plus en plus repliés
                at (0, f, i) = std::sin (anks::pi * 0.5f * (float) (2 * f + 1) * x);
                // Cheby : somme de polynômes de Tchebychev (harmoniques ajoutées une à une)
                float s = 0.0f; const float a = std::acos (x);
                for (int n = 1; n <= f + 2; ++n) s += std::cos ((float) n * a) * (n % 2 ? 1.0f : 0.6f) / (float) n;
                at (1, f, i) = s;
                // Folds : repliement en triangle de plus en plus serré
                const float z = x * (1.0f + (float) f * 1.5f);
                at (2, f, i) = (2.0f / anks::pi) * std::asin (std::sin (z * anks::pi * 0.5f));
                // Steps : saturation quantifiée, de 30 à 2 niveaux
                const float levels = (float) (2 + (frames - 1 - f) * 4);
                at (3, f, i) = std::round (std::tanh (3.0f * x) * levels) / levels;
            }
            for (int bk = 0; bk < banks; ++bk)
            {
                float mx = 1.0e-6f;
                for (int i = 0; i < tableSize; ++i) mx = juce::jmax (mx, std::abs (at (bk, f, i)));
                for (int i = 0; i < tableSize; ++i) at (bk, f, i) /= mx;
            }
        }
    }
    float& at (int bk, int f, int i) { return tables[(size_t) ((bk * frames + f) * tableSize + i)]; }
    float lookup (int bk, float position, float x)
    {
        const float fp = position * (frames - 1);
        const int f0 = juce::jmin (frames - 2, (int) fp); const float ff = fp - (float) f0;
        const float ip = (x * 0.5f + 0.5f) * (tableSize - 1);
        const int i0 = juce::jmin (tableSize - 2, (int) ip); const float fi = ip - (float) i0;
        auto rd = [&] (int f) { const float a = at (bk, f, i0); return a + fi * (at (bk, f, i0 + 1) - a); };
        const float a = rd (f0);
        return a + ff * (rd (f0 + 1) - a);
    }

    std::atomic<float> *drive, *bank, *pos, *bias, *tone, *lfoAmt;
    std::vector<float> tables, lfoBuf;
    anks::Lfo lfo;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::SmoothedValue<float> driveS, posS, biasS;
    std::array<anks::DcBlocker, 2> dc; std::array<anks::OnePole, 2> lp;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Shaper(); }
