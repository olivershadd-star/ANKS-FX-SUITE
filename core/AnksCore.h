// ANKS FX — tronc commun de la suite d'effets ANKS Labs
// Licence MIT (voir LICENSE). JUCE reste sous sa propre licence.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace anks
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using APVTS = juce::AudioProcessorValueTreeState;
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;

//==============================================================================
// Paramètres : helpers pour des noms et des valeurs lisibles dans le rack Ableton
inline juce::AudioParameterFloatAttributes attrs (const juce::String& unit, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (unit)
        .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
}

inline void addFloat (Layout& l, const juce::String& id, const juce::String& name,
                      float min, float max, float def, float skew = 1.0f,
                      const juce::String& unit = {}, int decimals = 1, bool symmetric = false)
{
    juce::NormalisableRange<float> r (min, max, 0.0f, skew, symmetric);
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs (unit, decimals)));
}

inline void addFloatText (Layout& l, const juce::String& id, const juce::String& name,
                          float min, float max, float def, float skew,
                          std::function<juce::String (float, int)> toText)
{
    juce::NormalisableRange<float> r (min, max, 0.0f, skew);
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText))));
}

inline void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
}

inline void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}

inline void addInt (Layout& l, const juce::String& id, const juce::String& name, int min, int max, int def)
{
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, min, max, def));
}

/** Dry/Wet, Output et Bypass, identiques sur tous les effets de la suite. */
inline void addCommon (Layout& l, float defaultMix = 100.0f)
{
    addFloat (l, "mix", "Dry/Wet", 0.0f, 100.0f, defaultMix, 1.0f, "%", 0);
    addFloat (l, "output", "Output", -24.0f, 12.0f, 0.0f, 1.0f, "dB", 1);
    addBool (l, "bypass", "Bypass", false);
}

inline juce::String noteText (float v, int)
{
    auto n = juce::roundToInt (v);
    return juce::MidiMessage::getMidiNoteName (n, true, true, 3);
}

inline float mtof (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

//==============================================================================
// Divisions musicales (en temps = noires)
inline const juce::StringArray& syncNames()
{
    static const juce::StringArray n { "Free", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32",
                                       "1/2T", "1/4T", "1/8T", "1/16T", "1/4D", "1/8D", "1/16D" };
    return n;
}
inline double syncBeats (int index)
{
    static const double b[] { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                              4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75, 0.375 };
    return b[juce::jlimit (0, 15, index)];
}

/** Sous-liste de divisions pour les effets rythmiques (sans "Free"). */
struct Grid
{
    juce::StringArray names;
    std::vector<double> beats;
    Grid (std::initializer_list<const char*> items)
    {
        for (auto* s : items)
        {
            names.add (s);
            beats.push_back (syncBeats (syncNames().indexOf (s)));
        }
    }
    double at (int i) const { return beats[(size_t) juce::jlimit (0, (int) beats.size() - 1, i)]; }
};

//==============================================================================
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

struct Transport
{
    double bpm = 120.0, ppq = 0.0, ppqPerSample = 0.0;
    bool playing = false;
};

//==============================================================================
// Briques DSP
struct OnePole
{
    float z = 0.0f, a = 1.0f;
    void setHz (float hz, double sr) { a = 1.0f - std::exp (-twoPi * juce::jlimit (1.0f, (float) sr * 0.49f, hz) / (float) sr); }
    void setMs (float ms, double sr) { a = 1.0f - std::exp (-1.0f / (juce::jmax (0.01f, ms) * 0.001f * (float) sr)); }
    float process (float x) { z += a * (x - z); return z; }
    void reset (float v = 0.0f) { z = v; }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
    void prepare (double sr) { r = std::exp (-twoPi * 10.0f / (float) sr); x1 = y1 = 0.0f; }
    float process (float x) { float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

/** Filtre d'état variable TPT (Cytomic / Andrew Simper). */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    void set (float hz, float q, double sr)
    {
        hz = juce::jlimit (10.0f, (float) sr * 0.45f, hz);
        const float g = std::tan (pi * hz / (float) sr);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
    void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

/** Ligne de délai circulaire, lecture fractionnaire Hermite. */
struct Delay
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float at (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }   // d = 0 : dernier échantillon écrit
    float read (float d) const
    {
        d = juce::jlimit (1.0f, (float) (mask - 4), d);
        const int i = (int) d; const float f = d - (float) i;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/** Oscillateur à forme d'onde (0 Sine, 1 Triangle, 2 Saw, 3 Square), anti-repliement polyBLEP. */
inline float oscWave (int wave, float ph, float dt)
{
    dt = juce::jlimit (1.0e-6f, 0.5f, dt);
    switch (wave)
    {
        case 1:  return 1.0f - 4.0f * std::abs (ph - 0.5f);
        case 2:  return 2.0f * ph - 1.0f - polyBlep (ph, dt);
        case 3:
        {
            float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
            return (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (p2, dt);
        }
        default: return std::sin (twoPi * ph);
    }
}
inline const juce::StringArray& waveNames() { static const juce::StringArray n { "Sine", "Triangle", "Saw", "Square" }; return n; }

//==============================================================================
// Distorsions partagées par la suite
inline const juce::StringArray& driveNames()
{
    static const juce::StringArray n { "Soft", "Hard", "Fold", "Tube", "Tape", "Transistor" };
    return n;
}
/** Applique une saturation (entrée déjà amplifiée). */
inline float shape (int type, float x)
{
    switch (type)
    {
        case 1:  return juce::jlimit (-1.0f, 1.0f, x);
        case 2:  { x = juce::jlimit (-1.0e5f, 1.0e5f, x); return (2.0f / pi) * std::asin (std::sin (x * pi * 0.5f)); }
        case 3:  return x >= 0.0f ? std::tanh (x) : std::tanh (x * 0.6f) / 0.6f * 0.7f;     // asymétrique : harmoniques paires
        case 4:  return (2.0f / pi) * std::atan (x * pi * 0.5f);
        case 5:  { x = juce::jlimit (-1.5f, 1.5f, x); return x - (4.0f / 27.0f) * x * x * x; }
        default: return std::tanh (x);
    }
}

/** Suréchantillonnage x2 / x4 autour d'un traitement par échantillon. */
class Oversampler
{
public:
    void prepare (int maxBlock, int factorLog2)
    {
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) factorLog2,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        os->initProcessing ((size_t) maxBlock); os->reset();
        factor = 1 << factorLog2;
    }
    int latency() const { return os ? juce::roundToInt (os->getLatencyInSamples()) : 0; }
    int getFactor() const { return factor; }
    void reset() { if (os) os->reset(); }
    template <typename Fn>
    void process (juce::AudioBuffer<float>& b, int start, int count, int numCh, Fn&& fn)
    {
        juce::dsp::AudioBlock<float> whole (b);
        auto block = whole.getSubsetChannelBlock (0, (size_t) numCh).getSubBlock ((size_t) start, (size_t) count);
        auto up = os->processSamplesUp (block);
        fn (up);
        os->processSamplesDown (block);
    }
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int factor = 1;
};

/** Suiveur d'enveloppe (attaque / relâchement). */
struct EnvFollower
{
    float env = 0.0f, aC = 1.0f, rC = 1.0f;
    void set (float attackMs, float releaseMs, double sr)
    {
        aC = 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, attackMs) * 0.001f * (float) sr));
        rC = 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, releaseMs) * 0.001f * (float) sr));
    }
    float process (float level) { env += (level > env ? aC : rC) * (level - env); return env; }
};

/** Filtre multimode (LP, BP, HP, Notch) basé sur le SVF. */
inline const juce::StringArray& filterNames() { static const juce::StringArray n { "Low Pass", "Band Pass", "High Pass", "Notch" }; return n; }
inline float resoToQ (float percent) { return 0.5f * std::pow (40.0f, juce::jlimit (0.0f, 100.0f, percent) * 0.01f); }
inline float filterOut (Svf& f, int type, float x)
{
    float lp, bp, hp; f.tick (x, lp, bp, hp);
    switch (type) { case 1: return bp * f.k; case 2: return hp; case 3: return lp + hp; default: return lp; }
}

//==============================================================================
/** LFO commun : libre en Hz ou calé sur le tempo (phase verrouillée sur la grille quand Live joue). */
class Lfo
{
public:
    static void addParams (Layout& l)
    {
        addFloat (l, "lforate", "LFO Rate", 0.01f, 40.0f, 1.0f, 0.3f, "Hz", 2);
        addChoice (l, "lfosync", "LFO Sync", syncNames(), 0);
        addChoice (l, "lfoshape", "LFO Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd" }, 0);
    }
    void attach (APVTS& s)
    {
        rate = s.getRawParameterValue ("lforate");
        sync = s.getRawParameterValue ("lfosync");
        shape = s.getRawParameterValue ("lfoshape");
    }
    void prepare (double sampleRate) { sr = sampleRate; smooth.setMs (1.5f, sr); rng.seed (0x1234567u); }
    void beginBlock (const Transport& t)
    {
        const int idx = juce::roundToInt (sync->load());
        if (idx > 0)
        {
            const double beats = syncBeats (idx);
            inc = t.bpm / 60.0 / beats / sr;
            if (t.playing)
            {
                double p = t.ppq / beats;
                phase = p - std::floor (p);
            }
        }
        else inc = (double) rate->load() / sr;
        shapeIndex = juce::roundToInt (shape->load());
    }
    float next()
    {
        const float ph = (float) phase;
        float v;
        switch (shapeIndex)
        {
            case 1:  v = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
            case 2:  v = 2.0f * ph - 1.0f; break;
            case 3:  v = 1.0f - 2.0f * ph; break;
            case 4:  v = ph < 0.5f ? 1.0f : -1.0f; break;
            case 5:  v = held; break;
            case 6:  v = prev + (target - prev) * (0.5f - 0.5f * std::cos (pi * ph)); break;
            default: v = std::sin (twoPi * ph); break;
        }
        phase += inc;
        if (phase >= 1.0) { phase -= std::floor (phase); held = rng.bi(); prev = target; target = rng.bi(); }
        return smooth.process (v);
    }
private:
    std::atomic<float>* rate = nullptr; std::atomic<float>* sync = nullptr; std::atomic<float>* shape = nullptr;
    double sr = 44100.0, phase = 0.0, inc = 0.0;
    int shapeIndex = 0;
    float held = 0.0f, prev = 0.0f, target = 0.0f;
    OnePole smooth; Rng rng;
};

/** Suivi des notes MIDI reçues (pour les effets accordables). */
struct NoteTracker
{
    std::vector<int> held;
    int last = -1;
    NoteTracker() { held.reserve (128); }
    void handle (const juce::MidiBuffer& midi)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                held.erase (std::remove (held.begin(), held.end(), m.getNoteNumber()), held.end());
                if (held.size() < 128) held.push_back (m.getNoteNumber());
                last = m.getNoteNumber();
            }
            else if (m.isNoteOff())
            {
                held.erase (std::remove (held.begin(), held.end(), m.getNoteNumber()), held.end());
                if (! held.empty()) last = held.back();
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) held.clear();
        }
    }
    int note() const { return held.empty() ? last : held.back(); }
};

//==============================================================================
/** Plaque minimale : le VST3 impose une fenêtre, mais tout se règle dans le rack Ableton. */
class PlateEditor final : public juce::AudioProcessorEditor
{
public:
    PlateEditor (juce::AudioProcessor& p, juce::String t, juce::String d, juce::Colour c)
        : AudioProcessorEditor (p), title (std::move (t)), desc (std::move (d)), accent (c)
    { setSize (380, 130); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff121418));
        g.setColour (accent); g.fillRect (0, 0, 6, getHeight());
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (title, 22, 12, getWidth() - 40, 30, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("ANKS Labs", 22, 40, getWidth() - 40, 18, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (desc, 22, 62, getWidth() - 40, 34, juce::Justification::topLeft, 2);
        g.setColour (accent.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Tous les reglages sont dans le rack Ableton", 22, getHeight() - 24, getWidth() - 40, 16, juce::Justification::centredLeft);
    }
private:
    juce::String title, desc; juce::Colour accent;
};

//==============================================================================
/** Base de tous les effets : bus mono/stéréo, Dry/Wet compensé en latence, Output, Bypass, état, transport. */
class Processor : public juce::AudioProcessor
{
public:
    Processor (juce::String name, juce::String description, juce::Colour accentColour, Layout layout)
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "ANKSFX", std::move (layout)),
          pluginName (std::move (name)), pluginDesc (std::move (description)), accent (accentColour)
    {
        mixP = p ("mix"); outP = p ("output"); bypassP = p ("bypass");
    }

    APVTS state;

    std::atomic<float>* p (const juce::String& id) { auto* v = state.getRawParameterValue (id); jassert (v != nullptr); return v; }
    static float get (std::atomic<float>* v) { return v->load(); }
    static int geti (std::atomic<float>* v) { return juce::roundToInt (v->load()); }
    static bool getb (std::atomic<float>* v) { return v->load() > 0.5f; }

    // --- à implémenter par chaque effet
    virtual void prepareWet (double sampleRate, int maxBlock) = 0;
    virtual void processWet (juce::AudioBuffer<float>& buffer, int start, int count, int numChannels) = 0;
    virtual void resetWet() {}

    // --- utilitaires pour les effets
    void setWetLatency (int samples)
    {
        latency = juce::jmax (0, samples);
        setLatencySamples (latency);
        dryRing.setSize (2, latency + 1); dryRing.clear(); ringPos = 0;
    }
    Transport transport;
    NoteTracker notes;
    double sr = 44100.0;

    // --- AudioProcessor
    const juce::String getName() const override { return pluginName; }
    bool acceptsMidi() const override
    {
       #if JucePlugin_WantsMidiInput
        return true;
       #else
        return false;
       #endif
    }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new PlateEditor (*this, pluginName, pluginDesc, accent); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter ("bypass"); }

    bool isBusesLayoutSupported (const BusesLayout& b) const override
    {
        const auto& out = b.getMainOutputChannelSet();
        return b.getMainInputChannelSet() == out
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double sampleRate, int maxBlock) override
    {
        sr = sampleRate;
        capacity = juce::jmax (32, maxBlock);
        dry.setSize (2, capacity);
        latency = 0; setLatencySamples (0);
        dryRing.setSize (2, 1); dryRing.clear(); ringPos = 0;
        mixS.reset (sr, 0.02); outS.reset (sr, 0.02); activeS.reset (sr, 0.02);
        mixS.setCurrentAndTargetValue (get (mixP) * 0.01f);
        outS.setCurrentAndTargetValue (dbToGain (get (outP)));
        activeS.setCurrentAndTargetValue (getb (bypassP) ? 0.0f : 1.0f);
        freePpq = 0.0;
        prepareWet (sr, capacity);
    }
    void releaseResources() override {}
    void reset() override { resetWet(); }
    using juce::AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
        if (numCh == 0 || capacity == 0) return;

        notes.handle (midi);
        updateTransport (buffer.getNumSamples());

        mixS.setTargetValue (get (mixP) * 0.01f);
        outS.setTargetValue (dbToGain (get (outP)));
        activeS.setTargetValue (getb (bypassP) ? 0.0f : 1.0f);

        const double blockPpq = transport.ppq;
        for (int start = 0; start < buffer.getNumSamples(); start += capacity)
        {
            const int count = juce::jmin (capacity, buffer.getNumSamples() - start);
            transport.ppq = blockPpq + start * transport.ppqPerSample;

            for (int c = 0; c < numCh; ++c)
            {
                auto* d = buffer.getWritePointer (c, start);
                for (int i = 0; i < count; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
            }
            // signal sec, retardé de la latence de l'effet pour un Dry/Wet sans décalage
            for (int i = 0; i < count; ++i)
            {
                const int read = (ringPos + 1) % (latency + 1);
                for (int c = 0; c < numCh; ++c)
                {
                    dryRing.setSample (c, ringPos, buffer.getSample (c, start + i));
                    dry.setSample (c, i, dryRing.getSample (c, read));
                }
                ringPos = read;
            }

            processWet (buffer, start, count, numCh);

            for (int i = 0; i < count; ++i)
            {
                const float m = mixS.getNextValue(), g = outS.getNextValue(), a = activeS.getNextValue();
                for (int c = 0; c < numCh; ++c)
                {
                    const float d = dry.getSample (c, i);
                    float w = buffer.getSample (c, start + i);
                    if (! std::isfinite (w)) w = 0.0f;
                    const float y = d + a * ((d + m * (w - d)) * g - d);
                    buffer.setSample (c, start + i, std::isfinite (y) ? y : 0.0f);
                }
            }
        }
    }

    void getStateInformation (juce::MemoryBlock& b) override
    {
        if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, b);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (state.state.getType()))
                state.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    void updateTransport (int numSamples)
    {
        bool hostPlaying = false; double hostPpq = 0.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) if (*b > 1.0) transport.bpm = *b;
                if (auto q = pos->getPpqPosition()) hostPpq = *q;
                hostPlaying = pos->getIsPlaying();
            }
        transport.ppqPerSample = transport.bpm / 60.0 / sr;
        transport.playing = hostPlaying;
        if (hostPlaying) freePpq = hostPpq;
        transport.ppq = freePpq;
        freePpq += numSamples * transport.ppqPerSample;   // à l'arrêt, horloge interne au tempo de Live
    }

    juce::String pluginName, pluginDesc; juce::Colour accent;
    std::atomic<float>* mixP = nullptr; std::atomic<float>* outP = nullptr; std::atomic<float>* bypassP = nullptr;
    juce::SmoothedValue<float> mixS, outS, activeS;
    juce::AudioBuffer<float> dry, dryRing;
    int capacity = 0, latency = 0, ringPos = 0;
    double freePpq = 0.0;
};

} // namespace anks
