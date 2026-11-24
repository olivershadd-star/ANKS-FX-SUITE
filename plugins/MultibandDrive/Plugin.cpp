// ANKS Multiband Drive — trois bandes (graves, médiums, aigus), une distorsion différente sur chacune
#include "AnksCore.h"

class MultibandDrive final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "xlow", "Low / Mid", 40.0f, 1000.0f, 200.0f, 0.4f, "Hz", 0);
        anks::addFloat (l, "xhigh", "Mid / High", 1000.0f, 12000.0f, 3000.0f, 0.4f, "Hz", 0);
        const char* bands[] { "Low", "Mid", "High" };
        const int defType[] { 0, 2, 1 };
        for (int k = 0; k < 3; ++k)
        {
            const juce::String id = juce::String (bands[k]).toLowerCase(), name = juce::String (bands[k]) + " ";
            anks::addChoice (l, id + "type", name + "Type", anks::driveNames(), defType[k]);
            anks::addFloat (l, id + "drive", name + "Drive", 0.0f, 36.0f, k == 0 ? 3.0f : 9.0f, 1.0f, "dB", 1);
            anks::addFloat (l, id + "level", name + "Level", -48.0f, 12.0f, 0.0f, 1.0f, "dB", 1);
            anks::addBool (l, id + "clean", name + "Clean", false);
        }
        anks::addCommon (l);
        return l;
    }

    MultibandDrive() : Processor ("ANKS Multiband Drive", "Graves, mediums et aigus distordus separement : ecrase le milieu, garde des graves propres.",
                                  juce::Colour (0xffe53e3e), layout())
    {
        xLow = p ("xlow"); xHigh = p ("xhigh");
        const char* ids[] { "low", "mid", "high" };
        for (int k = 0; k < 3; ++k)
        {
            const juce::String id (ids[k]);
            type[k] = p (id + "type"); drive[k] = p (id + "drive"); level[k] = p (id + "level"); clean[k] = p (id + "clean");
        }
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        const double hi = sr * 2.0;
        juce::dsp::ProcessSpec spec { hi, (juce::uint32) (maxBlock * 2), 2 };
        for (auto* f : { &lowSplit, &highSplit, &lowAllpass }) { f->prepare (spec); f->reset(); }
        lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        for (int k = 0; k < 3; ++k)
        {
            driveS[k].reset (hi, 0.02); levelS[k].reset (hi, 0.02);
            driveS[k].setCurrentAndTargetValue (anks::dbToGain (get (drive[k])));
            levelS[k].setCurrentAndTargetValue (anks::dbToGain (get (level[k])));
            for (auto& d : dc[k]) d.prepare (hi);
        }
    }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lowSplit.setCutoffFrequency (get (xLow));
        const float fh = juce::jmax (get (xHigh), get (xLow) * 1.5f);
        highSplit.setCutoffFrequency (fh); lowAllpass.setCutoffFrequency (fh);
        int t[3]; bool cl[3];
        for (int k = 0; k < 3; ++k)
        {
            driveS[k].setTargetValue (anks::dbToGain (get (drive[k])));
            levelS[k].setTargetValue (anks::dbToGain (get (level[k])));
            t[k] = geti (type[k]); cl[k] = getb (clean[k]);
        }
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                float g[3], lv[3];
                for (int k = 0; k < 3; ++k) { g[k] = driveS[k].getNextValue(); lv[k] = levelS[k].getNextValue(); }
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    const int ch = (int) c;
                    float low, rest, mid, high;
                    lowSplit.processSample (ch, up.getSample (ch, (int) n), low, rest);
                    highSplit.processSample (ch, rest, mid, high);
                    low = lowAllpass.processSample (ch, low);
                    float band[3] { low, mid, high }, y = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        float v = band[k];
                        if (! cl[k]) v = dc[k][c].process (anks::shape (t[k], v * g[k])) / std::sqrt (juce::jmax (1.0f, g[k])) * 0.8f;
                        y += v * lv[k];
                    }
                    up.setSample (ch, (int) n, y);
                }
            }
        });
    }

private:
    std::atomic<float> *xLow, *xHigh;
    std::atomic<float>* type[3]; std::atomic<float>* drive[3]; std::atomic<float>* level[3]; std::atomic<float>* clean[3];
    anks::Oversampler os;
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowAllpass;
    juce::SmoothedValue<float> driveS[3], levelS[3];
    std::array<anks::DcBlocker, 2> dc[3];
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultibandDrive(); }
