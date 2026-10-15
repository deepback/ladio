#pragma once
#include "types.hpp"
namespace ladio {
// Independent automation contract. All expensive interpolation/design work is
// performed while preparing a scene, never in its audio callback.
struct DJLane {
    static constexpr Frame stride=128;
    std::vector<float> values;
    Frame end=0,zeroFrom=-1,zeroUntil=-1;
    float constant=0;
    float at(Frame frame)const noexcept {
        if(zeroUntil>=0&&frame<=zeroUntil)return 0;
        if(zeroFrom>=0&&frame>=zeroFrom)return 0;
        if(values.empty())return constant;
        if(frame<=0)return values.front();if(frame>=end)return values.back();
        size_t i=size_t(frame/stride);Frame span=std::min(stride,end-Frame(i)*stride);
        return values[i]+(values[i+1]-values[i])*float(frame%stride)/float(span);
    }
};
struct DJDeckProgram {
    DJLane highpass,lowpass,filterWet,eqWet,flangerMix,gateDepth,delaySend,reverbSend,delayReturn,reverbReturn,delayFrames;
    double resonance=.707,flangerFeedback=.28,flangerQuarters=8,gateQuarters=.5,gateDuty=.65,delayFeedback=.36;
    double delayToReverb=0;
    double reverbLowSeconds=2.5,reverbHighSeconds=1.6,predelaySeconds=.025;
    std::array<double,5> eq{1,0,0,0,0};
    double tailHP=0,tailLP=1;
    bool sendPreFilter=false;
    // v2 separates input, dry output, serial auxiliary FX and the performance
    // reverb inside the remix path. Cutting a send must not erase its tail.
    bool serial=false,auxActive=false,auxReverbActive=false,roomActive=false,echoActive=false;
    DJLane inputGain,dryGain,auxSend,auxReturn,delayMix,reverbMix;
    DJLane roomSend,roomReturn,echoSend,echoReturn;
    double delayLowpass=3500,delayLowpassAlpha=1;
    double reverbMinSeconds=.022,reverbMaxSeconds=.093,reverbGain=1;
    double roomLowSeconds=6,roomHighSeconds=3.5;
    double flangerCenterSeconds=.004,flangerDepthSeconds=.002;
    double echoFeedback=.5;
};
struct DJProgram {bool enabled=false;std::array<DJDeckProgram,2> decks;};
DJLane compileDJLane(const Json&,Frame,double,double,double,bool,bool,int);
}
