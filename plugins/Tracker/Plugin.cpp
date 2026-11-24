// ANKS Tracker — suiveur d'enveloppe qui joue un VCO (sub sous un kick, notes déclenchées par la batterie)
#include "AnksCore.h"

class Tracker final : public anks::Processor
{
public:
    static anks::Layout layout()
    {
        anks::Layout l;
        anks::addChoice (l, "mode", "Mode", { "Follow", "Trigger" }, 1);
        anks::addFloatText (l, "note", "Note", 24.0f, 96.0f, 36.0f, 1.0f, anks::noteText);
        anks::addChoice (l, "wave", "Wave", anks::waveNames(), 0);
        anks::addBool (l, "keyfollow", "Key Follow", false);
        anks::addFloat (l, "threshold", "Threshold", -60.0f, 0.0f, -24.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "attack", "Attack", 0.1f, 100.0f, 1.0f, 0.4f, "ms", 1);
        anks::addFloat (l, "release", "Release", 5.0f, 2000.0f, 200.0f, 0.4f, "ms", 0);
        anks::addFloat (l, "pitchenv", "Pitch Env", 0.0f, 48.0f, 12.0f, 1.0f, "st", 1);
        anks::addFloat (l, "pitchdecay", "Pitch Decay", 1.0f, 500.0f, 40.0f, 0.4f, "ms", 0);
        anks::addInt (l, "random", "Random Notes", 0, 24, 0);
        anks::addFloat (l, "tone", "Tone", 100.0f, 20000.0f, 4000.0f, 0.3f, "Hz", 0);
        anks::addFloat (l, "osclevel", "Osc Level", -48.0f, 12.0f, -6.0f, 1.0f, "dB", 1);
        anks::addFloat (l, "drylevel", "Dry Level", -60.0f, 6.0f, 0.0f, 1.0f, "dB", 1);
        anks::addCommon (l);
        return l;
    }

    Tracker() : Processor ("ANKS Tracker", "La boucle joue un oscillateur : sub sous le kick, bip tonal sur la caisse claire, notes au hasard.",
                           juce::Colour (0xff38b2ac), layout())
    {
        mode = p ("mode"); note = p ("note"); wave = p ("wave"); key = p ("keyfollow"); thr = p ("threshold");
        att = p ("attack"); rel = p ("release"); penv = p ("pitchenv"); pdec = p ("pitchdecay"); rnd = p ("random");
        tone = p ("tone"); oscLvl = p ("osclevel"); dryLvl = p ("drylevel");
        rng.seed (0x7EACu);
    }

    void prepareWet (double, int) override
    {
        env = amp = pitchOff = 0.0f; armed = true; phase = 0.0f; noteOff = 0;
        lp.reset(); dcb.prepare (sr);
        oscS.reset (sr, 0.02); dryS.reset (sr, 0.02);
        oscS.setCurrentAndTargetValue (anks::dbToGain (get (oscLvl))); dryS.setCurrentAndTargetValue (anks::dbToGain (get (dryLvl)));
    }
    double getTailLengthSeconds() const override { return 2.0; }

    void processWet (juce::AudioBuffer<float>& b, int start, int count, int numCh) override
    {
        const bool trigger = geti (mode) == 1;
        float base = get (note);
        if (getb (key) && notes.note() >= 0) base = (float) notes.note();
        const int w = geti (wave);
        const float thrLin = anks::dbToGain (get (thr));
        const float aC = 1.0f - std::exp (-1.0f / (get (att) * 0.001f * (float) sr));
        const float rC = 1.0f - std::exp (-1.0f / (get (rel) * 0.001f * (float) sr));
        const float pC = std::exp (-1.0f / (get (pdec) * 0.001f * (float) sr));
        const float decC = std::exp (-1.0f / (get (rel) * 0.001f * (float) sr));
        const float pAmt = get (penv); const int range = geti (rnd);
        const float fullScale = anks::dbToGain (get (thr) + 18.0f);
        lp.setHz (get (tone), sr);
        oscS.setTargetValue (anks::dbToGain (get (oscLvl)));
        dryS.setTargetValue (get (dryLvl) <= -59.9f ? 0.0f : anks::dbToGain (get (dryLvl)));

        for (int i = 0; i < count; ++i)
        {
            float level = 0.0f;
            for (int c = 0; c < numCh; ++c) level = juce::jmax (level, std::abs (b.getSample (c, start + i)));
            env += (level > env ? aC : rC) * (level - env);

            if (trigger)
            {
                if (armed && env > thrLin)
                {
                    armed = false; amp = juce::jmax (amp, 0.0f); attackPhase = 1;
                    pitchOff = pAmt; phase = 0.0f;
                    noteOff = range > 0 ? (int) std::round (rng.bi() * (float) range) : 0;
                }
                if (! armed && env < thrLin * 0.5f) armed = true;
                if (attackPhase) { amp += aC * (1.0f - amp); if (amp > 0.99f) attackPhase = 0; }
                else amp *= decC;
                pitchOff *= pC;
            }
            else
            {
                const float target = env > thrLin ? juce::jmin (1.0f, env / fullScale) : 0.0f;
                amp += (target > amp ? aC : rC) * (target - amp);
                pitchOff = 0.0f; noteOff = 0;
            }

            const float hz = juce::jlimit (5.0f, (float) sr * 0.45f, anks::mtof (base + (float) noteOff + pitchOff));
            const float dt = hz / (float) sr;
            const float o = dcb.process (lp.process (anks::oscWave (w, phase, dt))) * amp * oscS.getNextValue();
            phase += dt; if (phase >= 1.0f) phase -= 1.0f;
            const float dg = dryS.getNextValue();
            for (int c = 0; c < numCh; ++c) b.setSample (c, start + i, b.getSample (c, start + i) * dg + o);
        }
    }

private:
    std::atomic<float> *mode, *note, *wave, *key, *thr, *att, *rel, *penv, *pdec, *rnd, *tone, *oscLvl, *dryLvl;
    anks::Rng rng; anks::OnePole lp; anks::DcBlocker dcb;
    juce::SmoothedValue<float> oscS, dryS;
    float env = 0.0f, amp = 0.0f, pitchOff = 0.0f, phase = 0.0f;
    int noteOff = 0, attackPhase = 0; bool armed = true;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Tracker(); }
