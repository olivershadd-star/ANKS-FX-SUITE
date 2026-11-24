// ANKS Stutter — répétitions synchronisées déclenchées au hasard
#include "AnksCore.h"

namespace
{
const anks::Grid decide { "1 Bar", "1/2", "1/4", "1/8" };
const anks::Grid slices { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
}

class Stutter final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "grid", "Grid", decide.names, 2);
        anks::addChoice (l, "slice", "Slice", slices.names, 2);
        anks::addFloat (l, "chance", "Chance", 0.0f, 100.0f, 30.0f, 1.0f, "%", 0);
        anks::addBool (l, "hold", "Hold", false);
        anks::addFloat (l, "reverse", "Reverse", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "pitchstep", "Pitch Step", -12.0f, 12.0f, 0.0f, 1.0f, "st", 1);
        anks::addFloat (l, "fade", "Fade", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "gate", "Gate", 10.0f, 100.0f, 100.0f, 1.0f, "%", 0);
        anks::addFloat (l, "smooth", "Smooth", 0.5f, 20.0f, 2.0f, 0.5f, "ms", 1);
        anks::addCommon (l);
        return l;
    }

    Stutter() : Processor ("ANKS Stutter", "A chaque temps, une chance de repeter un petit morceau de la boucle : breaks hachés, accidents.",
                           juce::Colour (0xffecc94b), layout())
    {
        gridP = p ("grid"); sliceP = p ("slice"); chance = p ("chance"); hold = p ("hold"); rev = p ("reverse");
        pitchStep = p ("pitchstep"); fade = p ("fade"); gateP = p ("gate"); smooth = p ("smooth");
        rng.seed (0x5EEDu);
    }

    void prepareWet (double, int) override
    {
        int n = 1; while (n < (int) (sr * 8.0)) n <<= 1;
        for (auto& c : buf) c.assign ((size_t) n, 0.0f);
        mask = n - 1; absPos = 0; lastGrid = -1; active = false; mixEnv.reset(); mixEnv.setMs (2.0f, sr);
    }
    void resetWet() override { active = false; lastGrid = -1; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const double gBeats = decide.at (geti (gridP)), sBeats = slices.at (geti (sliceP));
        const float pc = get (chance) * 0.01f, pr = get (rev) * 0.01f, step = get (pitchStep);
        const float keep = 1.0f - get (fade) * 0.01f, gateFrac = get (gateP) * 0.01f;
        const float smoothS = juce::jmax (1.0f, get (smooth) * 0.001f * (float) sr);
        const bool always = getb (hold);
        const long long sliceLen = juce::jmax (32LL, (long long) (sBeats * 60.0 / transport.bpm * sr));

        for (int i = 0; i < count; ++i)
        {
            const double ppq = transport.ppq + i * transport.ppqPerSample;
            const long long gi = (long long) std::floor (ppq / gBeats);
            if (gi != lastGrid)
            {
                lastGrid = gi;
                active = always || rng.uni() < pc;
                reversed = rng.uni() < pr;
                startPos = absPos; len = sliceLen;
            }
            for (int c = 0; c < numCh; ++c) buf[(size_t) c][(size_t) (absPos & mask)] = b.getSample (c, start + i);

            const long long t = absPos - startPos;
            const long long rep = t / len;
            const bool repeating = active && rep >= 1;
            const float e = mixEnv.process (repeating ? 1.0f : 0.0f);
            if (e > 1.0e-4f)
            {
                const float within = (float) (t % len);
                const float rate = std::pow (2.0f, step * (float) rep / 12.0f);
                float rp = std::fmod (within * rate, (float) len);
                if (reversed) rp = (float) len - 1.0f - rp;
                const float gateLen = gateFrac * (float) len;
                float env = juce::jmin (1.0f, within / smoothS, (gateLen - within) / smoothS);
                env = juce::jmax (0.0f, env) * std::pow (keep, (float) (rep - 1));
                for (int c = 0; c < numCh; ++c)
                {
                    const float s = read (c, (double) startPos + rp) * env;
                    const float x = b.getSample (c, start + i);
                    b.setSample (c, start + i, x + e * (s - x));
                }
            }
            ++absPos;
        }
    }

private:
    float read (int c, double pos) const
    {
        const double fl = std::floor (pos);
        const long long i = (long long) fl;
        const float f = (float) (pos - fl);
        const auto& v = buf[(size_t) c];
        const float a = v[(size_t) (i & mask)];
        return a + f * (v[(size_t) ((i + 1) & mask)] - a);
    }

    std::atomic<float> *gridP, *sliceP, *chance, *hold, *rev, *pitchStep, *fade, *gateP, *smooth;
    anks::Rng rng; anks::OnePole mixEnv;
    std::array<std::vector<float>, 2> buf;
    long long mask = 0, absPos = 0, lastGrid = -1, startPos = 0, len = 1024;
    bool active = false, reversed = false;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Stutter(); }
