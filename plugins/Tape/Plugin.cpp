// ANKS Tape — cassette usée : wow, flutter, dérive, saturation, souffle, décrochages
#include "AnksCore.h"

class Tape final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "wow", "Wow", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addFloat (l, "wowrate", "Wow Rate", 0.1f, 3.0f, 0.6f, 0.6f, "Hz", 2);
        anks::addFloat (l, "flutter", "Flutter", 0.0f, 100.0f, 20.0f, 1.0f, "%", 0);
        anks::addFloat (l, "flutrate", "Flutter Rate", 4.0f, 20.0f, 9.0f, 1.0f, "Hz", 1);
        anks::addFloat (l, "drift", "Drift", 0.0f, 100.0f, 20.0f, 1.0f, "%", 0);
        anks::addFloat (l, "sat", "Saturation", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addFloat (l, "age", "Age", 0.0f, 100.0f, 20.0f, 1.0f, "%", 0);
        anks::addFloat (l, "noise", "Noise", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "dropouts", "Dropouts", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "stereo", "Stereo", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Tape() : Processor ("ANKS Tape", "Vieille cassette : le son ondule, sature, souffle et decroche de temps en temps.",
                        juce::Colour (0xffa0aec0), layout())
    {
        wow = p ("wow"); wowRate = p ("wowrate"); flutter = p ("flutter"); flutRate = p ("flutrate"); drift = p ("drift");
        sat = p ("sat"); age = p ("age"); noise = p ("noise"); drop = p ("dropouts"); stereo = p ("stereo");
        rng.seed (0x7A9Eu);
    }

    void prepareWet (double, int) override
    {
        for (auto& d : dl) d.prepare ((int) (sr * 0.03) + 8);
        for (auto& f : driftLp) { f.setHz (0.5f, sr); f.reset(); }
        driftTarget = { 0.0f, 0.0f }; driftCount = 0;
        dropEnv.setMs (6.0f, sr); dropEnv.reset (1.0f);
        for (auto& h : hp) { h.reset(); }
        for (auto& l : lp) l.reset();
        wowPh = flutPh = 0.0; dropLeft = 0;
    }
    void resetWet() override { for (auto& d : dl) d.clear(); }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const float msToS = (float) sr * 0.001f;
        const float wowD = get (wow) * 0.01f * 4.0f * msToS, fluD = get (flutter) * 0.01f * 0.3f * msToS;
        const float driftD = get (drift) * 0.01f * 3.0f * msToS;
        const float drive = 1.0f + get (sat) * 0.04f;
        const float a = get (age) * 0.01f;
        const float noiseG = get (noise) > 0.0f ? anks::dbToGain (-66.0f + get (noise) * 0.36f) : 0.0f;
        const float dropP = get (drop) * 0.01f * 1.5f / (float) sr;
        const float st = get (stereo) * 0.01f;
        for (auto& l : lp) l.setHz (20000.0f * std::pow (0.15f, a), sr);
        for (auto& h : hp) h.setHz (15.0f + 160.0f * a, sr);
        const double wInc = get (wowRate) / sr, fInc = get (flutRate) / sr;
        const float base = 8.0f * msToS;

        for (int i = 0; i < count; ++i)
        {
            if (--driftCount <= 0)
            {
                driftCount = (int) ((0.4f + 1.6f * rng.uni()) * (float) sr);
                driftTarget[0] = rng.bi();
                driftTarget[1] = driftTarget[0] + st * (rng.bi() - driftTarget[0]);
            }
            if (dropLeft <= 0 && rng.uni() < dropP) { dropLeft = (int) ((0.03f + 0.12f * rng.uni()) * (float) sr); dropDepth = 0.2f + 0.4f * rng.uni(); }
            const float dg = dropEnv.process (dropLeft > 0 ? dropDepth : 1.0f);
            if (dropLeft > 0) --dropLeft;
            for (int c = 0; c < numCh; ++c)
            {
                const double off = c == 1 ? st * 0.25 : 0.0;
                const float wv = std::sin (anks::twoPi * (float) (wowPh + off));
                const float fl = std::sin (anks::twoPi * (float) (flutPh + off * 2.0));
                const float dr = driftLp[(size_t) c].process (driftTarget[(size_t) c]);
                const float x = b.getSample (c, start + i);
                dl[(size_t) c].push (x);
                float y = dl[(size_t) c].read (base + wowD * wv + fluD * fl + driftD * juce::jlimit (-1.0f, 1.0f, dr));
                y = std::tanh (y * drive) / std::sqrt (drive);
                y = lp[(size_t) c].process (y);
                y -= hp[(size_t) c].process (y);
                y = (y + noiseG * rng.bi()) * dg;
                b.setSample (c, start + i, y);
            }
            wowPh += wInc; if (wowPh >= 1.0) wowPh -= 1.0;
            flutPh += fInc; if (flutPh >= 1.0) flutPh -= 1.0;
        }
    }

private:
    std::atomic<float> *wow, *wowRate, *flutter, *flutRate, *drift, *sat, *age, *noise, *drop, *stereo;
    anks::Rng rng;
    std::array<anks::Delay, 2> dl;
    std::array<anks::OnePole, 2> driftLp, lp, hp;
    anks::OnePole dropEnv;
    double wowPh = 0.0, flutPh = 0.0;
    int dropLeft = 0, driftCount = 0; float dropDepth = 1.0f;
    std::array<float, 2> driftTarget { 0.0f, 0.0f };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Tape(); }
