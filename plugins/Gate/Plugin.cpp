// ANKS Gate — gate rythmique euclidien synchronisé, avec hasard
#include "AnksCore.h"

namespace { const anks::Grid grid { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/8D", "1/16D" }; }

class Gate final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "rate", "Rate", grid.names, 2);
        anks::addInt (l, "steps", "Steps", 2, 16, 16);
        anks::addInt (l, "hits", "Hits", 1, 16, 7);
        anks::addInt (l, "rotate", "Rotate", 0, 15, 0);
        anks::addBool (l, "invert", "Invert", false);
        anks::addFloat (l, "chance", "Chance", 0.0f, 100.0f, 100.0f, 1.0f, "%", 0);
        anks::addFloat (l, "length", "Length", 5.0f, 100.0f, 60.0f, 1.0f, "%", 0);
        anks::addFloat (l, "swing", "Swing", 0.0f, 75.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "attack", "Attack", 0.1f, 50.0f, 1.0f, 0.4f, "ms", 1);
        anks::addFloat (l, "release", "Release", 1.0f, 500.0f, 30.0f, 0.4f, "ms", 0);
        anks::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 100.0f, 1.0f, "%", 0);
        anks::addCommon (l);
        return l;
    }

    Gate() : Processor ("ANKS Gate", "Decoupe la boucle en rythme euclidien cale sur le tempo. Chance < 100 % : des trous au hasard.",
                        juce::Colour (0xff48bb78), layout())
    {
        rate = p ("rate"); steps = p ("steps"); hits = p ("hits"); rot = p ("rotate"); inv = p ("invert");
        chance = p ("chance"); len = p ("length"); swing = p ("swing"); att = p ("attack"); rel = p ("release"); depth = p ("depth");
        rng.seed (0xA11CEu);
    }

    void prepareWet (double, int) override { env = 1.0f; lastStep = -1; open = true; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const double L = grid.at (geti (rate));
        const int n = geti (steps), h = juce::jmin (geti (hits), n), r = geti (rot);
        const bool invert = getb (inv);
        const float pChance = get (chance) * 0.01f, gateLen = get (len) * 0.01f, sw = get (swing) * 0.01f;
        const float floorGain = 1.0f - get (depth) * 0.01f;
        const float aCoef = 1.0f - std::exp (-1.0f / (get (att) * 0.001f * (float) sr));
        const float rCoef = 1.0f - std::exp (-1.0f / (get (rel) * 0.001f * (float) sr));

        for (int i = 0; i < count; ++i)
        {
            const double ppq = transport.ppq + i * transport.ppqPerSample;
            // swing : le pas impair de chaque paire démarre plus tard
            const double pair = std::floor (ppq / (2.0 * L));
            const double inPair = ppq - pair * 2.0 * L;
            const double boundary = L * (1.0 + sw * 0.5);
            const bool odd = inPair >= boundary;
            const long long step = (long long) pair * 2 + (odd ? 1 : 0);
            const double stepStart = odd ? boundary : 0.0;
            const double stepLen = odd ? 2.0 * L - boundary : boundary;
            const double frac = (inPair - stepStart) / stepLen;

            if (step != lastStep)
            {
                lastStep = step;
                const int idx = (int) (((step % n) + n) % n);
                bool hit = (((idx + r) % n) * h) % n < h;
                if (invert) hit = ! hit;
                open = hit && rng.uni() < pChance;
            }
            const bool on = open && frac < gateLen;
            const float target = on ? 1.0f : floorGain;
            env += (target > env ? aCoef : rCoef) * (target - env);
            for (int c = 0; c < numCh; ++c) b.setSample (c, start + i, b.getSample (c, start + i) * env);
        }
    }

private:
    std::atomic<float> *rate, *steps, *hits, *rot, *inv, *chance, *len, *swing, *att, *rel, *depth;
    anks::Rng rng;
    float env = 1.0f; long long lastStep = -1; bool open = true;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Gate(); }
