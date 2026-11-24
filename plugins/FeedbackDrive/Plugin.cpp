// ANKS Feedback Drive — une distorsion qui se renvoie en elle-même par un délai court : siffle, s'emballe
#include "AnksCore.h"

class FeedbackDrive final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "type", "Drive Type", anks::driveNames(), 0);
        anks::addFloat (l, "drive", "Drive", 0.0f, 36.0f, 12.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "feedback", "Feedback", 0.0f, 120.0f, 60.0f, 1.0f, "%", 0);
        anks::addFloat (l, "time", "Time", 0.1f, 50.0f, 3.0f, 0.4f, "ms", 2);
        anks::addFloat (l, "looptone", "Loop Tone", 200.0f, 20000.0f, 6000.0f, 0.3f, "Hz", 0);
        anks::addFloat (l, "lowcut", "Loop Low Cut", 20.0f, 2000.0f, 80.0f, 0.4f, "Hz", 0);
        anks::addFloat (l, "duck", "Duck", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfotime", "LFO > Time", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l, 70.0f);
        return l;
    }

    FeedbackDrive() : Processor ("ANKS Feedback Drive", "La distorsion se renvoie dans elle-meme : larsen, sifflements, ca s'emballe. Duck calme le jeu.",
                                 juce::Colour (0xffe53e3e).withRotatedHue (0.05f), layout())
    {
        type = p ("type"); drive = p ("drive"); fb = p ("feedback"); time = p ("time"); loopTone = p ("looptone");
        lowCut = p ("lowcut"); duck = p ("duck"); lfoAmt = p ("lfotime");
        lfo.attach (state);
    }

    void prepareWet (double, int maxBlock) override
    {
        os.prepare (maxBlock, 1);
        setWetLatency (os.latency());
        hiRate = sr * 2.0;
        lfo.prepare (sr);
        lfoBuf.assign ((size_t) maxBlock, 0.0f);
        for (auto& d : dl) d.prepare ((int) (hiRate * 0.1) + 8);
        timeS.reset (hiRate, 0.05); timeS.setCurrentAndTargetValue (get (time));
        fbS.reset (hiRate, 0.03); fbS.setCurrentAndTargetValue (get (fb) * 0.01f);
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& d : dc) d.prepare (hiRate);
        follower.env = 0.0f;
    }
    void resetWet() override { for (auto& d : dl) d.clear(); }
    double getTailLengthSeconds() const override { return 1.0; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        for (int i = 0; i < count; ++i) lfoBuf[(size_t) i] = lfo.next();
        timeS.setTargetValue (get (time)); fbS.setTargetValue (get (fb) * 0.01f);
        for (auto& f : lp) f.setHz (get (loopTone), hiRate);
        for (auto& f : hp) f.setHz (get (lowCut), hiRate);
        follower.set (1.0f, 150.0f, hiRate);
        const int t = geti (type);
        const float g = anks::dbToGain (get (drive)), dk = get (duck) * 0.01f, la = get (lfoAmt) * 0.01f;
        const float msToS = (float) hiRate * 0.001f;
        os.process (b, start, count, numCh, [&] (juce::dsp::AudioBlock<float>& up)
        {
            for (size_t n = 0; n < up.getNumSamples(); ++n)
            {
                float level = 0.0f;
                for (size_t c = 0; c < up.getNumChannels(); ++c) level = juce::jmax (level, std::abs (up.getSample ((int) c, (int) n)));
                const float e = juce::jmin (1.0f, follower.process (level) * 3.0f);
                const float k = fbS.getNextValue() * (1.0f - dk * e);     // duck : moins de larsen quand la boucle joue
                const float d = juce::jmax (1.0f, timeS.getNextValue() * std::pow (2.0f, lfoBuf[n / 2] * la * 2.0f) * msToS);
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    const float x = up.getSample ((int) c, (int) n);
                    float back = dl[c].read (d);
                    back = lp[c].process (back);
                    back -= hp[c].process (back);
                    const float y = anks::shape (t, (x + k * back) * g);
                    dl[c].push (y);
                    up.setSample ((int) c, (int) n, dc[c].process (y) * 0.8f);
                }
            }
        });
    }

private:
    std::atomic<float> *type, *drive, *fb, *time, *loopTone, *lowCut, *duck, *lfoAmt;
    anks::Lfo lfo; anks::Oversampler os; anks::EnvFollower follower;
    std::vector<float> lfoBuf;
    std::array<anks::Delay, 2> dl;
    juce::SmoothedValue<float> timeS, fbS;
    std::array<anks::OnePole, 2> lp, hp; std::array<anks::DcBlocker, 2> dc;
    double hiRate = 88200.0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FeedbackDrive(); }
