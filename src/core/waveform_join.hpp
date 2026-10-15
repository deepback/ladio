#pragma once
#include "types.hpp"
#include <functional>
namespace ladio {
// Worker-only, bounded native-rate rejoin. Vocoder and original can have the
// same musical position but different waveform phase. A linear crossfade of
// those two versions creates a loudness valley. Align the fading contribution
// within 8 ms, then compensate measured local covariance, stereo-linked.
inline void rejoinNative(const std::filesystem::path&file,int channels,int sr,Frame frames,Frame first,Frame length,const std::function<float(Frame,int)>&native){
    first=std::max<Frame>(0,first);length=std::min(length,frames-first);
    if(length<2)return;
    const Frame margin=std::max(1,sr*8/1000),readFirst=std::max<Frame>(0,first-margin),readEnd=std::min(frames,first+length+margin);
    std::vector<float>source(size_t(readEnd-readFirst)*channels),original(size_t(length)*channels),shifted(original.size());
    std::fstream stream(file,std::ios::binary|std::ios::in|std::ios::out);stream.seekg(readFirst*channels*4);stream.read(reinterpret_cast<char*>(source.data()),source.size()*4);
    if(!stream)throw std::runtime_error("Cannot read native rejoin window");
    auto at=[&](double frame,int c){double p=clamp(frame-readFirst,0.,double(readEnd-readFirst-1));size_t k=size_t(p);double u=p-k;float a=source[k*channels+c],b=source[std::min(k+1,size_t(readEnd-readFirst-1))*channels+c];return a+float(u)*(b-a);};
    for(Frame f=0;f<length;f++)for(int c=0;c<channels;c++)original[size_t(f)*channels+c]=native(first+f,c);
    auto correlation=[&](Frame lag){double aa=0,bb=0,ab=0;for(Frame f=length/4;f<length*3/4;f+=8)for(int c=0;c<std::min(2,channels);c++){double a=at(first+f+lag,c),b=original[size_t(f)*channels+c];aa+=a*a;bb+=b*b;ab+=a*b;}return ab/std::sqrt(std::max(1e-20,aa*bb));};
    Frame lag=0;double initial=correlation(0),best=initial;
    for(Frame k=-margin;k<=margin;k+=4){double fit=correlation(k);if(fit>best){best=fit;lag=k;}}
    if(best<.25||best-initial<.10)lag=0;
    // The shift itself has zero initial slope and belongs only to the fading
    // vocoder voice. The original's source position never moves.
    std::vector<std::array<double,3>>prefix(size_t(length)+1);
    for(Frame f=0;f<length;f++){
        auto sums=prefix[size_t(f)];double offset=lag*smooth(double(f)/std::max<Frame>(1,length/4));
        for(int c=0;c<channels;c++){size_t i=size_t(f)*channels+c;double a=shifted[i]=at(first+f+offset,c),b=original[i];sums[0]+=a*a;sums[1]+=b*b;sums[2]+=a*b;}prefix[size_t(f)+1]=sums;
    }
    const Frame radius=std::max(1,sr*16/1000);
    for(Frame f=0;f<length;f++){
        Frame lo=std::max<Frame>(0,f-radius),hi=std::min(length,f+radius+1);auto a=prefix[size_t(lo)],b=prefix[size_t(hi)];
        double u=smooth(double(f)/(length-1)),x=1-u,ea=b[0]-a[0],eb=b[1]-a[1],cov=b[2]-a[2];
        double expected=x*ea+u*eb,mixed=x*x*ea+u*u*eb+2*x*u*cov;
        double gain=expected>1e-15?std::sqrt(expected/std::max(expected*.25,mixed)):1.;
        for(int c=0;c<channels;c++){size_t i=size_t(f)*channels+c;shifted[i]=float((x*shifted[i]+u*original[i])*gain);}
    }
    stream.seekp(first*channels*4);stream.write(reinterpret_cast<const char*>(shifted.data()),shifted.size()*4);
    if(!stream)throw std::runtime_error("Cannot save native rejoin window");
}
}
