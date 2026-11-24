// ANKS Grain — granulaire en temps réel sur un tampon de 4 s
#include "AnksCore.h"

class Grain final : public anks::Processor
{
public:
    static constexpr int maxGrains = 48, winSize = 1024;

    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "size", "Grain Size", 10.0f, 500.0f, 80.0f, 0.5f, "ms", 0);
        anks::addFloat (l, "density", "Density", 1.0f, 80.0f, 20.0f, 0.5f, "/s", 1);
        anks::addFloat (l, "position", "Position", 0.0f, 2000.0f, 100.0f, 0.4f, "ms", 0);
        anks::addFloat (l, "spray", "Spray", 0.0f, 1000.0f, 50.0f, 0.4f, "ms", 0);
        anks::addFloat (l, "pitch", "Pitch", -24.0f, 24.0f, 0.0f, 1.0f, "st", 1);
        anks::addFloat (l, "pitchrnd", "Pitch Random", 0.0f, 12.0f, 0.0f, 1.0f, "st", 1);
        anks::addFloat (l, "reverse", "Reverse", 0.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "spread", "Stereo Spread", 0.0f, 100.0f, 50.0f, 1.0f, "%", 0);
        anks::addFloat (l, "feedback", "Feedback", 0.0f, 90.0f, 0.0f, 1.0f, "%", 0);
        anks::addBool (l, "freeze", "Freeze", false);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfopos", "LFO > Position", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l, 50.0f);
        return l;
    }

    Grain() : Processor ("ANKS Grain", "Fait pleuvoir des petits grains de la boucle : textures, nuages, gel du son.",
                         juce::Colour (0xffb794f4), layout())
    {
        size = p ("size"); density = p ("density"); position = p ("position"); spray = p ("spray"); pitch = p ("pitch");
        pitchRnd = p ("pitchrnd"); reverse = p ("reverse"); spread = p ("spread"); fb = p ("feedback"); freeze = p ("freeze");
        lfoAmt = p ("lfopos");
        lfo.attach (state);
        for (int i = 0; i < winSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (anks::twoPi * (float) i / (float) (winSize - 1));
        rng.seed (0xBEEFu);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        int n = 1; while (n < (int) (sr * 4.0)) n <<= 1;
        for (auto& c : buf) c.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0; untilNext = 0;
        for (auto& g : grains) g.active = false;
    }
    void resetWet() override { for (auto& c : buf) std::fill (c.begin(), c.end(), 0.0f); for (auto& g : grains) g.active = false; }
    double getTailLengthSeconds() const override { return 2.5; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        const float sizeS = get (size) * 0.001f * (float) sr;
        const float dens = get (density), posMs = get (position), sprayMs = get (spray);
        const float pit = get (pitch), pr = get (pitchRnd), rev = get (reverse) * 0.01f, sp = get (spread) * 0.01f;
        const float g = get (fb) * 0.01f; const bool frozen = getb (freeze);
        const float amt = get (lfoAmt) * 0.01f;
        const float norm = 1.0f / std::sqrt (juce::jmax (1.0f, dens * sizeS / (float) sr));
        const float msToS = (float) sr * 0.001f;

        for (int i = 0; i < count; ++i)
        {
            const float l = lfo.next();
            if (--untilNext <= 0)
            {
                untilNext = juce::jmax (1, (int) ((float) sr / dens * (0.6f + 0.8f * rng.uni())));
                const float posNow = juce::jlimit (0.0f, 2000.0f, posMs + l * amt * 500.0f);
                spawn (sizeS, posNow * msToS + rng.uni() * sprayMs * msToS,
                       std::pow (2.0f, (pit + pr * rng.bi()) / 12.0f), rng.uni() < rev, sp * rng.bi());
            }
            float out[2] { 0.0f, 0.0f };
            for (auto& gr : grains)
            {
                if (! gr.active) continue;
                const float wv = window[(size_t) juce::jmin (winSize - 1, (int) ((float) gr.age / (float) gr.len * (winSize - 1)))];
                for (int c = 0; c < 2; ++c) out[c] += readBuf (c, gr.pos) * wv * gr.gain[c];
                gr.pos += gr.reverse ? -gr.rate : gr.rate;
                if (++gr.age >= gr.len) gr.active = false;
            }
            for (int c = 0; c < numCh; ++c)
            {
                const float x = b.getSample (c, start + i);
                const float y = (numCh == 1 ? 0.5f * (out[0] + out[1]) : out[c]) * norm;
                if (! frozen) buf[(size_t) c][(size_t) w] = x + g * std::tanh (y);
                b.setSample (c, start + i, y);
            }
            if (numCh == 1 && ! frozen) buf[1][(size_t) w] = buf[0][(size_t) w];
            if (! frozen) w = (w + 1) & mask;
        }
    }

private:
    struct G { bool active = false, reverse = false; double pos = 0.0; float rate = 1.0f; int len = 0, age = 0; float gain[2] { 1.0f, 1.0f }; };

    void spawn (float lenS, float delayS, float rate, bool rev, float pan)
    {
        for (auto& gr : grains)
        {
            if (gr.active) continue;
            gr.len = juce::jmax (16, (int) lenS);
            gr.rate = rate; gr.reverse = rev;
            // une tête de lecture plus rapide ne doit pas dépasser la tête d'écriture
            float minDelay = 4.0f;
            if (! rev && rate > 1.0f) minDelay += (rate - 1.0f) * (float) gr.len;
            const float d = juce::jlimit (minDelay, (float) mask - (float) gr.len * 4.0f - 8.0f, juce::jmax (minDelay, delayS));
            gr.pos = (double) w - (double) d;
            gr.gain[0] = std::sqrt (0.5f * (1.0f - pan)); gr.gain[1] = std::sqrt (0.5f * (1.0f + pan));
            gr.age = 0; gr.active = true;
            return;
        }
    }
    float readBuf (int c, double pos) const
    {
        const double fl = std::floor (pos);
        const int i = (int) ((long long) fl & mask);
        const float f = (float) (pos - fl);
        const auto& v = buf[(size_t) c];
        return v[(size_t) i] + f * (v[(size_t) ((i + 1) & mask)] - v[(size_t) i]);
    }

    std::atomic<float> *size, *density, *position, *spray, *pitch, *pitchRnd, *reverse, *spread, *fb, *freeze, *lfoAmt;
    anks::Lfo lfo; anks::Rng rng;
    std::array<std::vector<float>, 2> buf;
    std::array<float, winSize> window {};
    std::array<G, maxGrains> grains;
    int mask = 0, w = 0, untilNext = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Grain(); }
