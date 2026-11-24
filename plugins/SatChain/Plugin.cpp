// ANKS Saturation Chain — lampe > bande > transistor : trois saturations douces en série
#include "AnksCore.h"

class SatChain final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addBool (l, "tubeon", "Tube On", true);
        anks::addFloat (l, "tube", "Tube Drive", 0.0f, 24.0f, 6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "tubebias", "Tube Bias", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addBool (l, "tapeon", "Tape On", true);
        anks::addFloat (l, "tape", "Tape Drive", 0.0f, 24.0f, 6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "tapetone", "Tape Tone", 2000.0f, 20000.0f, 14000.0f, 0.3f, "Hz", 0);
        anks::addBool (l, "transon", "Transistor On", false);
        anks::addFloat (l, "trans", "Transistor Drive", 0.0f, 24.0f, 6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "tilt", "Tilt", -6.0f, 6.0f, 0.0f, 1.0f, "dB", 1);
        anks::addBool (l, "autogain", "Auto Gain", true);
        anks::addCommon (l);
        return l;
    }

    SatChain() : Processor ("ANKS Saturation Chain", "Lampe, bande, transistor en serie : colore et epaissit sans detruire.",
                            juce::Colour (0xffdd6b20), layout())
    {
        on[0] = p ("tubeon"); drv[0] = p ("tube"); on[1] = p ("tapeon"); drv[1] = p ("tape"); on[2] = p ("transon"); drv[2] = p ("trans");
        bias = p ("tubebias"); tapeTone = p ("tapetone"); tilt = p ("tilt"); autoGain = p ("autogain");
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        const double hi = sr * 2.0;
        for (int s = 0; s < 3; ++s)
        {
            gS[s].reset (hi, 0.02); onS[s].reset (hi, 0.02);
            gS[s].setCurrentAndTargetValue (anks::dbToGain (get (drv[s])));
            onS[s].setCurrentAndTargetValue (getb (on[s]) ? 1.0f : 0.0f);
        }
        for (auto& d : dc) d.prepare (hi);
        for (auto& f : tapeLp) f.reset();
        for (auto& f : tiltLp) f.reset();
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const double hi = sr * 2.0;
        for (int s = 0; s < 3; ++s)
        {
            gS[s].setTargetValue (anks::dbToGain (get (drv[s])));
            onS[s].setTargetValue (getb (on[s]) ? 1.0f : 0.0f);
        }
        for (auto& f : tapeLp) f.setHz (get (tapeTone), hi);
        for (auto& f : tiltLp) f.setHz (800.0f, hi);
        const float bi = get (bias) * 0.004f;
        const float tl = anks::dbToGain (get (tilt)), th = 1.0f / tl;
        const bool ag = getb (autoGain);
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                float g[3], en[3];
                for (int s = 0; s < 3; ++s) { g[s] = gS[s].getNextValue(); en[s] = onS[s].getNextValue(); }
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    float x = up.getSample ((int) c, (int) n);
                    // lampe : asymétrique, polarisée
                    float y = (std::tanh (x * g[0] + bi) - std::tanh (bi)) / (ag ? std::sqrt (g[0]) : 1.0f);
                    x += en[0] * (dc[c].process (y) - x);
                    // bande : arrondie, aigus adoucis
                    y = tapeLp[c].process ((2.0f / anks::pi) * std::atan (x * g[1] * anks::pi * 0.5f)) / (ag ? std::sqrt (g[1]) : 1.0f);
                    x += en[1] * (y - x);
                    // transistor : coude plus dur
                    const float z = juce::jlimit (-1.5f, 1.5f, x * g[2]);
                    y = (z - (4.0f / 27.0f) * z * z * z) / (ag ? std::sqrt (g[2]) : 1.0f);
                    x += en[2] * (y - x);
                    // tilt autour de 800 Hz
                    const float low = tiltLp[c].process (x);
                    up.setSample ((int) c, (int) n, low * tl + (x - low) * th);
                }
            }
        });
    }

private:
    std::atomic<float>* on[3]; std::atomic<float>* drv[3];
    std::atomic<float> *bias, *tapeTone, *tilt, *autoGain;
    anks::Oversampler os;
    juce::SmoothedValue<float> gS[3], onS[3];
    std::array<anks::DcBlocker, 2> dc; std::array<anks::OnePole, 2> tapeLp, tiltLp;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SatChain(); }
