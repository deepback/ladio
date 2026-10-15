#pragma once
#include "audio.hpp"
#include "harmony.hpp"
#include <complex>
#include <numeric>
#include <soxr.h>
namespace ladio::mobile {
inline void fft(std::vector<std::complex<double>>&a,bool inverse=false){size_t n=a.size();for(size_t i=1,j=0;i<n;i++){size_t b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j)std::swap(a[i],a[j]);}for(size_t len=2;len<=n;len<<=1){auto wlen=std::polar(1.,(inverse?2:-2)*pi/len);for(size_t i=0;i<n;i+=len){std::complex<double>w=1;for(size_t j=0;j<len/2;j++){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}if(inverse)for(auto&v:a)v/=n;}
inline int reflected(int n,int length){if(length<=1)return 0;int cycle=2*(length-1);n=(n%cycle+cycle)%cycle;return n<length?n:cycle-n;}
// scipy.ndimage's half-sample symmetric boundary (different from Torch reflect).
inline int symmetric(int n,int length){int cycle=2*length;n=(n%cycle+cycle)%cycle;return n<length?n:cycle-1-n;}
inline Json keyInfo(const std::array<double,12>&c){
    auto estimate=estimateKey(c);const auto&best=estimate.candidates.front();Json alternatives=Json::array();
    for(int k=0;k<3;k++){const auto&v=estimate.candidates[k];alternatives.push_back({{"tonic",v.tonic},{"mode",v.mode?"minor":"major"},{"score",v.score}});}
    constexpr const char*names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return {{"tonic",estimate.known?best.tonic:-1},{"mode",estimate.known?(best.mode?"minor":"major"):"unknown"},
        {"name",estimate.known?std::string(names[best.tonic])+(best.mode?"m":""):"unknown"},
        {"confidence",estimate.confidence},{"status",estimate.known?"estimated":"insufficient_tonal_evidence"},
        {"method","hpss-chroma-profile-v2"},{"correlation",best.score},{"chroma",c},{"alternatives",alternatives}};
}
inline double median(std::vector<double> v){if(v.empty())return 0;size_t n=v.size()/2;std::nth_element(v.begin(),v.begin()+n,v.end());double r=v[n];if(v.size()%2==0)r=(r+*std::max_element(v.begin(),v.begin()+n))*.5;return r;}
// Exact fixed-width median. HPSS advances one sample at a time; preserving
// the sorted window avoids two heap allocations and nth_element per FFT bin.
class Median31 {
    std::array<float,31> values{};
public:
    template<class Sample> void reset(Sample sample){for(int i=0;i<31;i++)values[i]=sample(i-15);std::sort(values.begin(),values.end());}
    float value()const{return values[15];}
    void advance(float outgoing,float incoming){
        int at=int(std::lower_bound(values.begin(),values.end(),outgoing)-values.begin());
        if(at==31||values[at]!=outgoing)throw std::runtime_error("Invalid HPSS median window");
        for(int i=at;i<30;i++)values[i]=values[i+1];
        at=int(std::lower_bound(values.begin(),values.begin()+30,incoming)-values.begin());
        for(int i=30;i>at;i--)values[i]=values[i-1];values[at]=incoming;
    }
};
inline double mean(const std::vector<double>&v){return v.empty()?0:std::accumulate(v.begin(),v.end(),0.)/v.size();}
inline std::vector<float> monoResampled(const PCMFile&pcm,double rate=22050,const std::function<bool()>&cancel={}){
    // Keep SoXR's FIFOs bounded. Feeding a whole song to oneshot grows its
    // output FIFO a few kilobytes at a time; Android realloc then repeatedly
    // copies the already converted song. Chunking retains the same HQ filter
    // and its continuous state, without retaining a second whole-track input.
    auto spec=soxr_io_spec(SOXR_FLOAT32_I,SOXR_FLOAT32_I);
    auto quality=soxr_quality_spec(SOXR_HQ,0);soxr_error_t error=nullptr;
    auto raw=soxr_create(44100,rate,1,&error,&spec,&quality,nullptr);
    std::unique_ptr<std::remove_pointer_t<soxr_t>,decltype(&soxr_delete)> converter(raw,&soxr_delete);
    if(error||!raw)throw std::runtime_error(error?error:"Cannot create analysis resampler");
    std::array<float,8192> in{},converted{};
    std::vector<float> out;out.reserve(size_t(std::ceil(pcm.frames()*rate/44100))+64);
    for(Frame offset=0;offset<pcm.frames();){
        if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");
        size_t count=size_t(std::min<Frame>(in.size(),pcm.frames()-offset));
        for(size_t i=0;i<count;i++)in[i]=(pcm.sample(offset+Frame(i),0)+pcm.sample(offset+Frame(i),1))*.5f;
        size_t consumed=0;
        while(consumed<count){
            size_t used=0,written=0;
            error=soxr_process(raw,in.data()+consumed,count-consumed,&used,converted.data(),converted.size(),&written);
            if(error)throw std::runtime_error(error);
            if(!used&&!written)throw std::runtime_error("Analysis resampler stalled");
            out.insert(out.end(),converted.begin(),converted.begin()+written);consumed+=used;
        }
        offset+=Frame(count);
    }
    for(;;){
        size_t written=0;
        error=soxr_process(raw,nullptr,0,nullptr,converted.data(),converted.size(),&written);
        if(error)throw std::runtime_error(error);
        out.insert(out.end(),converted.begin(),converted.begin()+written);
        if(!written)break;
    }
    return out;
}
inline std::vector<float> mono22050(const PCMFile&pcm){return monoResampled(pcm);}
inline std::vector<float> readFloats(const std::filesystem::path&p){size_t n=std::filesystem::file_size(p);if(n%4)throw std::runtime_error("Invalid float file");std::vector<float> v(n/4);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(v.data()),n);if(!f)throw std::runtime_error("Cannot read float file");return v;}
inline void writeFloats(const std::filesystem::path&p,const std::vector<float>&v){auto tmp=p;tmp+=".pending";std::ofstream f(tmp,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size()*4);if(!f)throw std::runtime_error("Cannot write PCM");f.close();std::filesystem::rename(tmp,p);}
inline void check(const std::function<bool()>&cancel){if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");}
}
