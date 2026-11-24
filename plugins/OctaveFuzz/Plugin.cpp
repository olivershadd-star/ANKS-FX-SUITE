// ANKS Octave Fuzz — redresseur (octave au-dessus), fuzz, et sub une octave en dessous
#include "AnksCore.h"

class OctaveFuzz final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "drive", "Fuzz Drive", 0.0f, 48.0f, 24.0f, 1.0f, "dB", 1);
        anks::addChoice (l, "rect", "Rectifier", { "Full Wave", "Half Wave" }, 0);
        anks::addFloat (l, "fuzz", "Fuzz Level", -48.0f, 6.0f, 0.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "octave", "Octave Up Level", -48.0f, 6.0f, -6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "sub", "Sub Level", -48.0f, 6.0f, -48.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "gate", "Gate", -80.0f, -10.0f, -60.0f, 1.0f, "dB", 0);
        anks::addFloat (l, "bias", "Bias", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "tone", "Tone", 300.0f, 20000.0f, 5000.0f, 0.3f, "Hz", 0);
        anks::addCommon (l);
        return l;
    }

    OctaveFuzz() : Processor ("ANKS Octave Fuzz", "Fuzz a redresseur : une octave apparait au-dessus, un sub carre en dessous, et ca crachote.",
                              juce::Colour (0xff805ad5), layout())
    {
        drive = p ("drive"); rect = p ("rect"); fuzz = p ("fuzz"); octave = p ("octave"); sub = p ("sub");
        gate = p ("gate"); bias = p ("bias"); tone = p ("tone");
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        const double hi = sr * 2.0;
        for (auto& d : dc) d.prepare (hi);
        for (auto& d : dc2) d.prepare (hi);
        for (auto& f : lp) f.reset();
        for (auto& f : track) { f.setHz (250.0f, hi); f.reset(); }
        for (auto& f : subLp) { f.setHz (400.0f, hi); f.reset(); }
        for (auto& e : env) { e.set (1.0f, 60.0f, hi); e.env = 0.0f; }
        flip = { 1.0f, 1.0f }; prevSign = { 0, 0 };
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const double hi = sr * 2.0;
        const float g = anks::dbToGain (get (drive)), bi = get (bias) * 0.005f;
        const float lf = anks::dbToGain (get (fuzz)), lo = anks::dbToGain (get (octave));
        const float ls = get (sub) <= -47.9f ? 0.0f : anks::dbToGain (get (sub));
        const float gateLin = anks::dbToGain (get (gate));
        const bool full = geti (rect) == 0;
        for (auto& f : lp) f.setHz (get (tone), hi);
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    const float x = up.getSample ((int) c, (int) n);
                    const float e = env[c].process (std::abs (x));
                    const float open = juce::jlimit (0.0f, 1.0f, (e - gateLin) / (gateLin * 2.0f + 1.0e-6f));
                    // fuzz : écrêtage dur polarisé
                    const float fz = dc[c].process (juce::jlimit (-1.0f, 1.0f, x * g + bi));
                    // octave : redressement (|x| double la fréquence)
                    const float rx = full ? std::abs (x) : juce::jmax (0.0f, x);
                    const float oc = dc2[c].process (std::tanh (rx * g * 0.5f)) * 1.6f;
                    // sub : bascule à chaque passage par zéro montant -> carré une octave sous
                    const float tr = track[c].process (x);
                    const int sg = tr > 0.0f ? 1 : -1;
                    if (sg > 0 && prevSign[c] < 0) flip[c] = -flip[c];
                    prevSign[c] = sg;
                    const float sb = subLp[c].process (flip[c]) * juce::jmin (1.0f, e * 4.0f);
                    const float y = (fz * lf + oc * lo + sb * ls) * open;
                    up.setSample ((int) c, (int) n, lp[c].process (y) * 0.7f);
                }
        });
    }

private:
    std::atomic<float> *drive, *rect, *fuzz, *octave, *sub, *gate, *bias, *tone;
    anks::Oversampler os;
    std::array<anks::DcBlocker, 2> dc, dc2;
    std::array<anks::OnePole, 2> lp, track, subLp;
    std::array<anks::EnvFollower, 2> env;
    std::array<float, 2> flip { 1.0f, 1.0f }; std::array<int, 2> prevSign { 0, 0 };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OctaveFuzz(); }
