// ANKS Triple Drive — repris du depot TripleDist (DSP et parametres inchanges, projets existants compatibles)
#include "AnksCore.h"
#include <array>
#include <cmath>

class TripleDrive final : public juce::AudioProcessor
{
public:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        for (int s=0; s<3; ++s)
        {
            auto prefix=juce::String(s+1); auto name="Stage "+prefix+" ";
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix+"on",1},name+"On",s==0));
            p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix+"type",1},name+"Type",juce::StringArray{"Soft","Hard","Fold"},0));
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"drive",1},name+"Drive dB",juce::NormalisableRange<float>(0,36,0.01f),0));
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"tone",1},name+"Tone Hz",juce::NormalisableRange<float>(200,20000,1,0.35f),18000));
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"mix",1},name+"Mix %",juce::NormalisableRange<float>(0,100,0.1f),100));
            p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix+"level",1},name+"Level dB",juce::NormalisableRange<float>(-24,12,0.01f),0));
        }
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1},"Global Mix %",juce::NormalisableRange<float>(0,100,0.1f),100));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output dB",juce::NormalisableRange<float>(-36,6,0.01f),-6));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Global Bypass",false));
        return p;
    }
    juce::AudioProcessorValueTreeState state{*this,nullptr,"TripleDriveState",layout()};
    TripleDrive():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}
    const juce::String getName() const override {return "ANKS Triple Drive";}
    bool acceptsMidi() const override {return false;} bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 0;}
    int getNumPrograms() override {return 1;} int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override
    {return b.getMainInputChannelSet()==b.getMainOutputChannelSet() && (b.getMainOutputChannelSet()==juce::AudioChannelSet::mono() || b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
    float value(const juce::String& id) const {return state.getRawParameterValue(id)->load();}
    struct Stage
    {
        juce::SmoothedValue<float> drive,tone,mix,level,on;
        std::array<juce::SmoothedValue<float>,3> weights;
        float low[2]{}, dcIn[2]{}, dcOut[2]{};
    };
    std::array<Stage,3> stages;
    juce::SmoothedValue<float> globalMix,output,active;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> delayedDry, dryRing;
    int ringPos=0, capacity=0, latency=0; double highRate=0;
    void targets(bool initial)
    {
        auto set=[initial](auto& smooth,float v) {if(initial) smooth.setCurrentAndTargetValue(v); else smooth.setTargetValue(v);};
        for(int i=0;i<3;++i)
        {
            auto id=juce::String(i+1); auto& s=stages[(size_t)i];
            set(s.drive,juce::Decibels::decibelsToGain(value(id+"drive")));
            set(s.tone,value(id+"tone")); set(s.mix,value(id+"mix")*0.01f);
            set(s.level,juce::Decibels::decibelsToGain(value(id+"level")));
            set(s.on,value(id+"on"));
            for(int t=0;t<3;++t) set(s.weights[(size_t)t],juce::roundToInt(value(id+"type"))==t?1.0f:0.0f);
        }
        set(globalMix,value("mix")*0.01f); set(output,juce::Decibels::decibelsToGain(value("output")));
        set(active,value("bypass")>0.5f?0.0f:1.0f);
    }
    void prepareToPlay(double sr,int maxBlock) override
    {
        capacity=juce::jmax(1,maxBlock); highRate=sr*4.0;
        os=std::make_unique<juce::dsp::Oversampling<float>>((size_t)getTotalNumInputChannels(),2,juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true);
        os->initProcessing((size_t)capacity); os->reset();
        latency=juce::roundToInt(os->getLatencyInSamples()); setLatencySamples(latency);
        delayedDry.setSize(2,capacity); dryRing.setSize(2,latency+1); dryRing.clear(); ringPos=0;
        for(auto& s:stages)
        {
            for(auto* v:{&s.drive,&s.tone,&s.mix,&s.level,&s.on}) v->reset(highRate,0.02);
            for(auto& w:s.weights) w.reset(highRate,0.02);
            for(int c=0;c<2;++c) s.low[c]=s.dcIn[c]=s.dcOut[c]=0;
        }
        globalMix.reset(sr,0.02);output.reset(sr,0.02);active.reset(sr,0.02);targets(true);
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        if(!os) {buffer.clear();return;}
        targets(false);
        // Chunking avoids allocation even if a host delivers more than its advertised block size.
        for(int offset=0;offset<buffer.getNumSamples();offset+=capacity)
        {
            int count=juce::jmin(capacity,buffer.getNumSamples()-offset);
            juce::dsp::AudioBlock<float> whole(buffer); auto block=whole.getSubBlock((size_t)offset,(size_t)count);
            for(int n=0;n<count;++n)
            {
                int read=(ringPos+1)%(latency+1);
                for(int c=0;c<buffer.getNumChannels();++c)
                {
                    float input=block.getSample(c,n); if(!std::isfinite(input)) input=0;
                    block.setSample(c,n,input); dryRing.setSample(c,ringPos,input);
                    delayedDry.setSample(c,n,dryRing.getSample(c,read));
                }
                ringPos=read;
            }
            auto up=os->processSamplesUp(block);
            const float dc=std::exp(-2.0f*juce::MathConstants<float>::pi*10.0f/(float)highRate);
            for(auto& s:stages)
                for(size_t n=0;n<up.getNumSamples();++n)
                {
                    float d=s.drive.getNextValue(), a=1-std::exp(-2*juce::MathConstants<float>::pi*s.tone.getNextValue()/(float)highRate);
                    float m=s.mix.getNextValue(), l=s.level.getNextValue(), en=s.on.getNextValue();
                    float w[3]; for(int t=0;t<3;++t) w[t]=s.weights[(size_t)t].getNextValue();
                    for(size_t c=0;c<up.getNumChannels();++c)
                    {
                        float x=up.getSample((int)c,(int)n), z=juce::jlimit(-1.0e6f,1.0e6f,x*d);
                        float fold=(2.0f/juce::MathConstants<float>::pi)*std::asin(std::sin(z*juce::MathConstants<float>::halfPi));
                        float shaped=w[0]*std::tanh(z)+w[1]*juce::jlimit(-1.0f,1.0f,z)+w[2]*fold;
                        s.low[c]+=a*(shaped-s.low[c]);
                        float wet=s.low[c]-s.dcIn[c]+dc*s.dcOut[c]; s.dcIn[c]=s.low[c];s.dcOut[c]=wet;
                        up.setSample((int)c,(int)n,x+en*((x+m*(wet-x))*l-x));
                    }
                }
            os->processSamplesDown(block);
            for(int n=0;n<count;++n)
            {
                float mix=globalMix.getNextValue(), gain=output.getNextValue(), enabled=active.getNextValue();
                for(int c=0;c<buffer.getNumChannels();++c)
                {
                    float dry=delayedDry.getSample(c,n), wet=block.getSample(c,n);
                    float result=dry+enabled*((dry+mix*(wet-dry))*gain-dry);
                    block.setSample(c,n,std::isfinite(result)?result:0.0f);
                }
            }
        }
    }
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override {return state.getParameter("bypass");}
    void getStateInformation(juce::MemoryBlock& b) override {auto xml=state.copyState().createXml();copyXmlToBinary(*xml,b);}
    void setStateInformation(const void* data,int size) override
    {auto xml=getXmlFromBinary(data,size);if(xml && xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml));}
};

juce::AudioProcessorEditor* TripleDrive::createEditor()
{
    return new anks::PlateEditor (*this, "ANKS Triple Drive",
        "Trois distorsions en serie (Soft, Hard, Fold) : plusieurs petites doses plutot qu'une grosse.",
        juce::Colour (0xffef953e));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new TripleDrive();}
