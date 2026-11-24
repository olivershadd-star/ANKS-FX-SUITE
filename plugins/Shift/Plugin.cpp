// ANKS Shift — frequency shifter (Bode) à transformée de Hilbert
#include "AnksCore.h"

namespace
{
/** Paire de filtres passe-tout déphasés de 90° (coefficients d'Olli Niemitalo). */
struct Hilbert
{
    struct Ap { float a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
                float process (float x) { const float y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
    std::array<Ap, 4> a, b; float bDelay = 0.0f;
    Hilbert()
    {
        const float ca[] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        const float cb[] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { a[(size_t) i].a2 = ca[i] * ca[i]; b[(size_t) i].a2 = cb[i] * cb[i]; }
    }
    void reset() { for (auto& f : a) f = Ap { f.a2 }; for (auto& f : b) f = Ap { f.a2 }; bDelay = 0.0f; }
    void process (float x, float& re, float& im)
    {
        float ya = x, yb = x;
        for (auto& f : a) ya = f.process (ya);
        for (auto& f : b) yb = f.process (yb);
        re = ya; im = bDelay; bDelay = yb;
    }
};
}

class Shift final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addFloat (l, "shift", "Shift", -2000.0f, 2000.0f, 0.0f, 0.35f, "Hz", 1, true);
        anks::addFloat (l, "fine", "Fine", -10.0f, 10.0f, 0.0f, 1.0f, "Hz", 2);
        anks::addBool (l, "mirror", "Mirror R", false);
        anks::addFloat (l, "feedback", "Feedback", 0.0f, 95.0f, 0.0f, 1.0f, "%", 0);
        anks::Lfo::addParams (l);
        anks::addFloat (l, "lfoshift", "LFO > Shift", -100.0f, 100.0f, 0.0f, 1.0f, "%", 0);
        anks::addFloat (l, "lforange", "LFO Range", 1.0f, 2000.0f, 100.0f, 0.3f, "Hz", 0);
        anks::addCommon (l);
        return l;
    }

    Shift() : Processor ("ANKS Shift", "Decale toutes les frequences du meme nombre de Hz : metallique, inharmonique, spirales.",
                         juce::Colour (0xff9f7aea), layout())
    {
        shift = p ("shift"); fine = p ("fine"); mirror = p ("mirror"); fb = p ("feedback");
        lfoAmt = p ("lfoshift"); lfoRange = p ("lforange");
        lfo.attach (state);
    }

    void prepareWet (double, int) override
    {
        lfo.prepare (sr);
        shiftS.reset (sr, 0.03); fbS.reset (sr, 0.03);
        shiftS.setCurrentAndTargetValue (get (shift) + get (fine));
        fbS.setCurrentAndTargetValue (get (fb) * 0.01f);
        for (auto& h : hil) h.reset();
        last = { 0.0f, 0.0f }; phase = 0.0;
    }
    void resetWet() override { for (auto& h : hil) h.reset(); last = { 0.0f, 0.0f }; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        lfo.beginBlock (transport);
        shiftS.setTargetValue (get (shift) + get (fine));
        fbS.setTargetValue (get (fb) * 0.01f);
        const bool mir = getb (mirror);
        const float amt = get (lfoAmt) * 0.01f * get (lfoRange);
        for (int i = 0; i < count; ++i)
        {
            const float hz = shiftS.getNextValue() + lfo.next() * amt;
            const float g = fbS.getNextValue();
            const float cs = std::cos ((float) phase), sn = std::sin ((float) phase);
            for (int c = 0; c < numCh; ++c)
            {
                const float x = b.getSample (c, start + i) + g * last[(size_t) c];
                float re, im; hil[(size_t) c].process (x, re, im);
                const float s = (mir && c == 1) ? -1.0f : 1.0f;
                const float y = re * cs - s * im * sn;
                last[(size_t) c] = std::tanh (y);
                b.setSample (c, start + i, y);
            }
            phase += anks::twoPi * hz / sr;
            if (phase > anks::twoPi) phase -= anks::twoPi; else if (phase < 0.0) phase += anks::twoPi;
        }
    }

private:
    std::atomic<float> *shift, *fine, *mirror, *fb, *lfoAmt, *lfoRange;
    anks::Lfo lfo;
    juce::SmoothedValue<float> shiftS, fbS;
    std::array<Hilbert, 2> hil; std::array<float, 2> last { 0.0f, 0.0f };
    double phase = 0.0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Shift(); }
