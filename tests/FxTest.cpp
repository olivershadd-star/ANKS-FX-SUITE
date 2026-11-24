// Test hors ligne d'un effet ANKS : sons de test, paramètres aléatoires, mono/stéréo, recherche de NaN et d'explosions.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 124.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); return p;
    }
};

static float run (juce::AudioProcessor& fx, FakePlayHead& ph, int channels, double sr, int block, int seconds,
                  juce::Random& rnd, bool randomise, float& rmsOut, bool& bad)
{
    juce::AudioBuffer<float> buf (channels, block); juce::MidiBuffer midi;
    float peak = 0.0f; double sum = 0.0; long long n = 0; int t = 0;
    const int total = (int) (sr * seconds);
    for (int pos = 0; pos < total; pos += block)
    {
        if (randomise && (pos / block) % 200 == 0)
            for (auto* prm : fx.getParameters())
                if (prm != fx.getBypassParameter() && prm->getName (32) != "Output")
                    prm->setValueNotifyingHost (rnd.nextFloat());
        if ((pos / block) % 100 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + rnd.nextInt (36), (juce::uint8) 100), 0);
        for (int i = 0; i < block; ++i, ++t)
        {
            const double s = t / sr;
            float kick = std::fmod (s, 0.5) < 0.08 ? (float) std::sin (2 * juce::MathConstants<double>::pi * 55 * s) * 0.8f : 0.0f;
            float tone = 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 220 * s);
            float noise = (rnd.nextFloat() * 2.0f - 1.0f) * (std::fmod (s, 0.25) < 0.03 ? 0.5f : 0.02f);
            for (int c = 0; c < channels; ++c) buf.setSample (c, i, kick + tone + noise * (c ? -1.0f : 1.0f));
        }
        fx.processBlock (buf, midi); midi.clear();
        ph.ppq += block * ph.bpm / 60.0 / sr;
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) bad = true;
                peak = juce::jmax (peak, std::abs (v)); sum += (double) v * v; ++n;
            }
    }
    rmsOut = (float) std::sqrt (sum / (double) juce::jmax (1LL, n));
    return peak;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::Random rnd (42);
    bool anyBad = false;
    std::unique_ptr<juce::AudioProcessor> fx (createPluginFilter());
    FakePlayHead ph; fx->setPlayHead (&ph);
    std::printf ("%s : %d parametres, MIDI %s\n", fx->getName().toRawUTF8(), (int) fx->getParameters().size(), fx->acceptsMidi() ? "oui" : "non");
    if (fx->getParameters().size() > 64) { std::printf ("  ERREUR : plus de 64 parametres\n"); anyBad = true; }

    struct Case { int ch; double sr; int block; bool playing; bool randomise; };
    for (auto cs : { Case { 2, 48000.0, 512, true, false }, Case { 2, 44100.0, 64, false, true },
                     Case { 1, 96000.0, 1024, true, true }, Case { 2, 48000.0, 333, true, true } })
    {
        juce::AudioProcessor::BusesLayout layout;
        auto set = cs.ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.add (set); layout.outputBuses.add (set);
        if (! fx->setBusesLayout (layout)) { std::printf ("  layout %d refuse\n", cs.ch); anyBad = true; continue; }
        ph.playing = cs.playing;
        fx->setRateAndBufferSizeDetails (cs.sr, cs.block);
        fx->prepareToPlay (cs.sr, cs.block);
        bool bad = false; float rms = 0.0f;
        const float peak = run (*fx, ph, cs.ch, cs.sr, cs.block, 6, rnd, cs.randomise, rms, bad);
        std::printf ("  %dch %6.0f Hz bloc %4d %s : crete %.2f  rms %.3f  latence %d %s\n", cs.ch, cs.sr, cs.block,
                     cs.randomise ? "aleatoire" : "defaut   ", peak, rms, fx->getLatencySamples(), bad ? "NaN !" : "");
        if (bad || peak > 8.0f) anyBad = true;
    }
    // état : sauvegarde / rechargement
    juce::MemoryBlock mb; fx->getStateInformation (mb); fx->setStateInformation (mb.getData(), (int) mb.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> ed (fx->createEditorIfNeeded());
    std::printf ("  etat %d octets, plaque %dx%d\n", (int) mb.getSize(), ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
    ed.reset();
    std::printf ("  => %s\n", anyBad ? "ECHEC" : "OK");
    return anyBad ? 1 : 0;
}
