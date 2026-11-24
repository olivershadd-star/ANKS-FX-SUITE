// ANKS Resonator — 4 filtres en peigne accordés (notes / accords) pour faire chanter les percussions
#include "AnksCore.h"

namespace
{
const float chords[8][4] { { 0.0f, 0.06f, -0.06f, 0.11f }, { 0, 12, 24, -12 }, { 0, 7, 12, 19 }, { 0, 4, 7, 12 },
                           { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 0, 3, 7, 10 }, { 0, 1, 2, 3 } };
}

class Resonator final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloatText (l, "note", "Note", 24.0f, 96.0f, 48.0f, 1.0f, anks::noteText);
        anks::addFloat (l, "fine", "Fine", -100.0f, 100.0f, 0.0f, 1.0f, "ct", 0);
        anks::addChoice (l, "chord", "Chord", { "Unison", "Octaves", "Fifth", "Major", "Minor", "Sus4", "Minor 7", "Cluster" }, 3);
        anks::addBool (l, "keyfollow", "Key Follow", false);
        anks::addFloat (l, "decay", "Decay", 0.05f, 10.0f, 1.5f, 0.4f, "s", 2);
        anks::addFloat (l, "damp", "Damp", 500.0f, 20000.0f, 6000.0f, 0.3f, "Hz", 0);
        anks::addFloat (l, "drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "spread", "Stereo Spread", 0.0f, 100.0f, 50.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfopitch", "LFO > Pitch", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addCommon (l, 50.0f);
        return l;
    }

    Resonator() : Processor ("ANKS Resonator", "Chaque coup fait sonner une note ou un accord : la batterie devient tubes et cordes.",
                             juce::Colour (0xffed8936), layout())
    {
        note = p ("note"); fine = p ("fine"); chord = p ("chord"); key = p ("keyfollow"); decay = p ("decay");
        damp = p ("damp"); drive = p ("drive"); spread = p ("spread"); lfoAmt = p ("lfopitch");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        for (auto& ch : comb) for (auto& d : ch) d.prepare ((int) (sr / 14.0) + 16);
        for (auto& ch : lp) for (auto& f : ch) f.reset();
        for (auto& d : dc) d.prepare (sr);
        pitchS.reset (sr, 0.04); pitchS.setCurrentAndTargetValue (get (note));
    }
    void resetWet() override { for (auto& ch : comb) for (auto& d : ch) d.clear(); }
    double getTailLengthSeconds() const override { return 10.0; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        float base = get (note);
        if (getb (key) && notes.note() >= 0) base = (float) notes.note();
        pitchS.setTargetValue (base + get (fine) * 0.01f);
        const int ch = juce::jlimit (0, 7, geti (chord));
        const float dec = get (decay), g = anks::dbToGain (get (drive)), sp = get (spread) * 0.01f;
        const float amt = get (lfoAmt) * 0.01f * 12.0f;
        for (auto& c : lp) for (auto& f : c) f.setHz (get (damp), sr);
        float pan[2][4];
        for (int v = 0; v < 4; ++v)
        {
            const float side = (v % 2 == 0 ? -1.0f : 1.0f) * sp;
            pan[0][v] = 1.0f - juce::jmax (0.0f, side); pan[1][v] = 1.0f + juce::jmin (0.0f, side);
        }

        for (int i = 0; i < count; ++i)
        {
            const float pitch = pitchS.getNextValue() + lfo.next() * amt;
            float len[4], fbk[4];
            for (int v = 0; v < 4; ++v)
            {
                const float hz = anks::mtof (pitch + chords[ch][v]);
                len[v] = juce::jlimit (2.0f, (float) sr / 15.0f, (float) sr / hz);
                fbk[v] = std::pow (10.0f, -3.0f * len[v] / (dec * (float) sr));
            }
            const float inMono = numCh > 1 ? 0.5f * (b.getSample (0, start + i) + b.getSample (1, start + i)) : b.getSample (0, start + i);
            for (int c = 0; c < numCh; ++c)
            {
                float x = numCh > 1 ? 0.5f * (b.getSample (c, start + i) + inMono) : inMono;
                if (g > 1.001f) x = std::tanh (x * g);
                float y = 0.0f;
                for (int v = 0; v < 4; ++v)
                {
                    auto& d = comb[(size_t) c][(size_t) v];
                    const float fbSample = lp[(size_t) c][(size_t) v].process (d.read (len[v] - 1.0f));
                    float s = x * 0.25f + fbk[v] * fbSample;
                    s = s / (1.0f + 0.1f * std::abs (s));
                    d.push (s);
                    y += s * pan[c][v];
                }
                b.setSample (c, start + i, dc[(size_t) c].process (y * 0.5f));
            }
        }
    }

private:
    std::atomic<float> *note, *fine, *chord, *key, *decay, *damp, *drive, *spread, *lfoAmt;
    anks::Lfo lfo;
    std::array<std::array<anks::Delay, 4>, 2> comb;
    std::array<std::array<anks::OnePole, 4>, 2> lp;
    std::array<anks::DcBlocker, 2> dc;
    juce::SmoothedValue<float> pitchS;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Resonator(); }
