#include "types.hpp"
#include "ladio/ladio.h"
#include "structure.hpp"
#include <complex>
#include <functional>
namespace ladio {
static void fft(std::vector<std::complex<double>>&a){size_t n=a.size();for(size_t i=1,j=0;i<n;i++){size_t b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j)std::swap(a[i],a[j]);}for(size_t len=2;len<=n;len<<=1){auto wlen=std::polar(1.,-2*pi/len);for(size_t i=0;i<n;i+=len){std::complex<double>w=1;for(size_t j=0;j<len/2;j++){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}}
Json nativeAnalyzeCancellable(const float*x,Frame frames,int sr,const char*id,const std::function<bool()>&cancel){
    if(!x||frames<sr||sr<8000||sr>192000)throw std::runtime_error("Invalid native analysis input");
    int stride=std::max(1,sr/22050),asr=sr/stride,hop=256;constexpr int nfft=2048;
    std::vector<std::complex<double>>buf(nfft);std::vector<double>last(nfft/2),on,low,rms;std::array<double,12>chroma{};std::vector<std::array<double,12>>localChroma;
    for(Frame p=0;p<frames;p+=Frame(hop)*stride){if(cancel&&cancel())throw std::runtime_error("Cancelled");double sum=0;for(int k=0;k<nfft;k++){Frame f=p+Frame(k)*stride;double v=f<frames?(x[f*2]+x[f*2+1])*.5:0;if(!std::isfinite(v))v=0;buf[k]=v*(.5-.5*std::cos(2*pi*k/(nfft-1)));sum+=v*v;}fft(buf);double flux=0,bass=0;std::array<double,12>local{};for(int k=1;k<nfft/2;k++){double m=std::log1p(abs(buf[k]));flux+=std::max(0.,m-last[k]);last[k]=m;double hz=double(k)*asr/nfft;if(hz<180)bass+=std::norm(buf[k]);if(hz>70&&hz<3000){int note=int(std::llround(69+12*std::log2(hz/440)));chroma[(note%12+12)%12]+=abs(buf[k]);local[(note%12+12)%12]+=abs(buf[k]);}}on.push_back(flux);low.push_back(bass);rms.push_back(std::sqrt(sum/nfft));double norm=0;for(auto v:local)norm+=v*v;for(auto &v:local)v/=std::sqrt(std::max(norm,1e-12));localChroma.push_back(local);}
    double dt=double(hop)*stride/sr;Json hypotheses=Json::array();double bpm=120,best=-1;for(int v=700;v<=1900;v++){if(cancel&&cancel())throw std::runtime_error("Cancelled");double period=60/(v*.1)/dt,score=0;for(int multiple:{1,2,3}){int lag=int(std::llround(period*multiple));double correlation=0;for(size_t k=lag;k<on.size();k++)correlation+=on[k]*on[k-lag];score+=correlation/multiple;}score*=.9+.1*std::exp(-std::pow(std::log2(v*.1/124),2));if(score>best){best=score;bpm=v*.1;}}
    double period=60/bpm/dt,phase=0,phaseBest=-1;for(double ph=0;ph<period;ph+=.5){double score=0;for(double k=ph;k<on.size();k+=period)score+=on[size_t(k)];if(score>phaseBest){phaseBest=score;phase=ph;}}
    std::vector<double>beats,down;Json cues=Json::array(),curve=Json::array();size_t beat=0;for(double k=phase;k<on.size();k+=period){double t=k*dt;beats.push_back(t);if(beat%4==0)down.push_back(t);if(beat%16==0&&t<double(frames)/sr-8)cues.push_back({{"time",t},{"frame",std::llround(t*sr)},{"confidence",.55},{"kind","phrase"},{"vocal",.5}});beat++;}
    for(size_t k=0;k<on.size();k+=17)curve.push_back({{"time",k*dt},{"rms",rms[k]},{"low_energy",low[k]},{"onset",on[k]},{"vocal",.5},{"novelty",k?std::min(1.,abs(std::log((rms[k]+1e-7)/(rms[k-1]+1e-7)))):0.},{"chroma",localChroma[k]}});
    double sum=0;for(double v:chroma)sum+=v*v;for(double&v:chroma)v/=std::sqrt(std::max(sum,1e-12));
    Json result={{"schema_version",1},{"id",id?id:"native"},{"title",id?id:"native"},{"source_hash","host-must-bind-content-hash"},{"revision","native-a1"},{"pcm_path","host-provided"},{"sample_rate",sr},{"frames",frames},{"duration",double(frames)/sr},{"channels",2},{"curve_step",17*dt},{"tempo_hypotheses",Json::array()},{"tempo",bpm},{"tempo_confidence",.55},{"downbeat_confidence",.4},{"downbeat_method","periodic-accent-A1"},{"meter",4},{"beat_unit","quarter"},{"beats",beats},{"downbeats",down},{"cues",cues},{"curves",curve},{"key",{{"chroma",chroma},{"confidence",.3}}},{"loudness_lufs",-14},{"loudness_method","host-must-measure-BS1770"},{"capabilities",{"pcm","energy","coarse_beats"}},{"analyzer_version","portable-fft-flux-structure-A1-v3"}};
    analyzeStructure(result);return result;
}
Json nativeAnalyze(const float*x,Frame frames,int sr,const char*id){return nativeAnalyzeCancellable(x,frames,sr,id,{});}
}
extern "C" int ladio_analyze_pcm(const float*x,int64_t frames,int32_t sr,const char*id,const char*out){try{ladio::writeJson(out,ladio::nativeAnalyze(x,frames,sr,id));return 1;}catch(...){return 0;}}
