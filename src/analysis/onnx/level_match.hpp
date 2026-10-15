#pragma once
#include "audio.hpp"
namespace ladio::mobile {
// Offline stereo-linked safety: anticipate peaks, release slowly, never clip
// the first kick. Small gain knots keep memory independent of song duration.
struct ScenePeakGuard {
    static constexpr Frame stride=64;
    std::vector<double> gain;double minimum=1;Frame limited=0;
    explicit ScenePeakGuard(const PCMFile&pcm,int sr){
        const size_t n=size_t((pcm.frames()+stride-1)/stride);std::vector<double> groups(n,1);
        for(size_t k=0;k<n;k++){double peak=0;for(Frame f=Frame(k)*stride;f<std::min(pcm.frames(),Frame(k+1)*stride);f++)for(int ch=0;ch<2;ch++)peak=std::max(peak,double(std::abs(pcm.sample(f,ch))));groups[k]=peak>.99?.99/peak:1.;}
        const double attack=std::exp(-double(stride)/(sr*.004)),release=std::exp(-double(stride)/(sr*.080));
        for(size_t k=n;k-->1;)groups[k-1]=std::min(groups[k-1],1-(1-groups[k])*attack);
        for(size_t k=1;k<n;k++)groups[k]=std::min(groups[k],1-(1-groups[k-1])*release);
        gain.resize(n+1);gain[0]=groups[0];for(size_t k=1;k<n;k++)gain[k]=std::min(groups[k-1],groups[k]);gain[n]=groups.back();
        for(size_t k=0;k<n;k++){minimum=std::min(minimum,gain[k]);if(gain[k]<.999)limited+=std::min(stride,pcm.frames()-Frame(k)*stride);}
    }
    double at(Frame f)const{size_t k=size_t(f/stride);return gain[k]+(gain[k+1]-gain[k])*smooth(double(f%stride)/stride);}
};
inline double originalLevelGain(double aLUFS,double bLUFS,double seconds,double from,double arrival){
    double u=smooth((seconds-from)/std::max(.1,arrival-from));return db((1-u)*(aLUFS+14)+u*(bLUFS+14));
}
}
