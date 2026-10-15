#pragma once
#include "types.hpp"
#include "scene.hpp"
#include "tail_colour.hpp"
#include "dj_dsp.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace ladio {
/* Disk access lives on preparation/render-worker threads, never the hardware callback. */
class PCMFile {
    const float *samples_=nullptr; size_t size_=0; int channels_=2;std::vector<int> layout_;
#if defined(__unix__) || defined(__APPLE__)
    void *mapping_=MAP_FAILED;
#else
    std::vector<float> storage_;
#endif
public:
    PCMFile(const std::filesystem::path&p,int channels=2,std::vector<int> layout={}):channels_(channels),layout_(std::move(layout)){
        if(!layout_.empty()){if(layout_.size()<2||layout_.size()!=size_t(channels)||layout_[0]!=0||layout_[1]!=1)throw std::runtime_error("Invalid sparse PCM map");std::vector<int> seen;for(int c:layout_){if(c<0||c>=10||std::find(seen.begin(),seen.end(),c)!=seen.end())throw std::runtime_error("Invalid sparse PCM channel");seen.push_back(c);}}
        if(channels<1||channels>32)throw std::runtime_error("Invalid PCM layout");
        size_=std::filesystem::file_size(p);if(size_%(sizeof(float)*channels)!=0||size_==0)throw std::runtime_error("Truncated PCM: "+p.string());
#if defined(__unix__) || defined(__APPLE__)
        int fd=::open(p.c_str(),O_RDONLY);if(fd<0)throw std::runtime_error("Cannot open PCM");mapping_=mmap(nullptr,size_,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(mapping_==MAP_FAILED)throw std::runtime_error("Cannot map PCM");samples_=static_cast<const float*>(mapping_);
#else
        storage_.resize(size_/4);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(storage_.data()),size_);if(!f)throw std::runtime_error("PCM read failed");samples_=storage_.data();
#endif
        for(size_t i=0;i<size_/sizeof(float);i++)if(!std::isfinite(samples_[i])){
#if defined(__unix__) || defined(__APPLE__)
            if(mapping_!=MAP_FAILED){munmap(mapping_,size_);mapping_=MAP_FAILED;}
#endif
            throw std::runtime_error("Nonfinite prepared/source PCM");
        }
    }
    ~PCMFile(){
#if defined(__unix__) || defined(__APPLE__)
        if(mapping_!=MAP_FAILED)munmap(mapping_,size_);
#endif
    }
    PCMFile(const PCMFile&)=delete;PCMFile&operator=(const PCMFile&)=delete;
    Frame frames()const{return Frame(size_/4/channels_);}
    float sample(Frame i,int c)const noexcept{if(i<0||i>=frames()||c<0)return 0;if(!layout_.empty()){if(c>=10&&c<12){float value=sample(i,c%2);for(int stem=0;stem<4;stem++)value-=sample(i,2+stem*2+c%2);return value;}auto at=std::find(layout_.begin(),layout_.end(),c);if(at==layout_.end())return 0;c=int(at-layout_.begin());}if(c>=channels_)return 0;float v=samples_[size_t(i)*channels_+c];return std::isfinite(v)?v:0;}
};
struct Source {
    std::shared_ptr<PCMFile> original;std::array<std::shared_ptr<PCMFile>,4> stems;
    struct Window {Frame start=0,end=0;std::array<std::shared_ptr<PCMFile>,4> stems;};std::vector<Window> windows;
    float value(Frame frame,int channel)const noexcept{
        if(channel<2)return original->sample(frame,channel);
        if(channel>=10){float r=original->sample(frame,channel%2);for(int k=0;k<4;k++)r-=value(frame,2+k*2+channel%2);return r;}
        int stem=(channel-2)/2;
        if(stems[stem])return stems[stem]->sample(frame,channel%2);
        double result=0,total=0;for(auto&w:windows)if(frame>=w.start&&frame<w.end){double weight=std::min(1.,double(std::min(frame-w.start+1,w.end-frame))/5292.);if(w.stems[stem]){result+=weight*w.stems[stem]->sample(frame-w.start,channel%2);total+=weight;}}
        return total>0?float(result/total):0;
    }
    bool covered(Frame from,Frame to)const {if(stems[0])return true;Frame reached=std::max<Frame>(0,from);for(auto&w:windows){if(w.start>reached)break;reached=std::max(reached,w.end);}return reached>=std::min(to,original->frames());}
    bool coveredRole(Frame from,Frame to,int role)const{if(stems[role])return true;Frame reached=std::max<Frame>(0,from);for(const auto&w:windows)if(w.stems[role]){if(w.start>reached)break;reached=std::max(reached,w.end);}return reached>=std::min(to,original->frames());}
};
struct Compiled {Plan plan;std::shared_ptr<PCMFile> a,b,rendered;std::optional<Scene> scene;std::vector<std::shared_ptr<PCMFile>> voices;};
struct Span {Frame start=0,length=0,source=0;int track=0,transition=-1;};
struct Session {
    int sr=44100;Frame frames=0,loopStart=0;bool loop=true;Json manifest;std::vector<Track> tracks;std::vector<Source> sources;std::vector<Compiled> transitions;std::vector<Span> spans;
    static std::shared_ptr<Session> load(const std::filesystem::path&);
    size_t spanAt(Frame f)const noexcept{size_t i=0;while(i+1<spans.size()&&f>=spans[i+1].start)i++;return i;}
};
class OnePole {
    std::array<float,2> z_{};
public:
    float low(float x,int c,float alpha)noexcept{z_[c]+=alpha*(x-z_[c]);if(abs(z_[c])<1e-20f)z_[c]=0;return z_[c];}
    void reset()noexcept{z_={};}
};
class Delay {
    std::vector<std::array<float,2>> ring_;size_t pos_=0;std::array<float,2> lo_{},hi_{};
    std::array<float,3> highpass_,lowpass_;
public:
    explicit Delay(int sr):ring_(size_t(sr)*3+1){for(int k=0;k<3;k++){const double hp[]={180,320,550},lp[]={5800,3400,2800};highpass_[k]=float(1-std::exp(-2*pi*hp[k]/sr));lowpass_[k]=float(1-std::exp(-2*pi*lp[k]/sr));}}
    void clear(){std::fill(ring_.begin(),ring_.end(),std::array<float,2>{});pos_=0;lo_={};hi_={};}
    std::array<float,2> process(std::array<float,2> in,double length,float feedback,float wet,int style=0)noexcept{
        length=std::clamp(length,1.,double(ring_.size()-2));size_t delay=size_t(length);float fraction=float(length-delay);
        size_t read=(pos_+ring_.size()-delay)%ring_.size(),previous=(read+ring_.size()-1)%ring_.size();std::array<float,2> y{},filtered{};style=std::clamp(style,0,2);
        for(int c=0;c<2;c++){float v=ring_[read][c]*(1-fraction)+ring_[previous][c]*fraction;lo_[c]+=highpass_[style]*(v-lo_[c]);hi_[c]+=lowpass_[style]*(v-lo_[c]-hi_[c]);filtered[c]=hi_[c];y[c]=filtered[c]*wet;}
        for(int c=0;c<2;c++)ring_[pos_][c]=std::clamp(in[c]+(filtered[c]*.72f+filtered[1-c]*.28f)*std::clamp(feedback,0.f,.55f),-1.5f,1.5f);
        pos_=(pos_+1)%ring_.size();return y;
    }
};
class Reverb {
    std::array<std::vector<std::array<float,2>>,4>lines_;std::array<size_t,4>positions_{};std::array<std::array<float,2>,4> damping_{};OnePole inputLow_,inputHigh_;float hp_,lp_,damp_;
public:
    explicit Reverb(int sr){const double delays[]={.0371,.0533,.0677,.0799};for(int k=0;k<4;k++)lines_[k].resize(size_t(sr*delays[k])+1);hp_=float(1-std::exp(-2*pi*210/sr));lp_=float(1-std::exp(-2*pi*6000/sr));damp_=float(1-std::exp(-2*pi*4200/sr));}
    void clear(){for(auto&l:lines_)std::fill(l.begin(),l.end(),std::array<float,2>{});positions_={};damping_={};inputLow_.reset();inputHigh_.reset();}
    std::array<float,2>process(std::array<float,2>in)noexcept{
        std::array<std::array<float,2>,4>v{};std::array<float,2>out{};
        for(int c=0;c<2;c++){float low=inputLow_.low(in[c],c,hp_);in[c]=inputHigh_.low(in[c]-low,c,lp_);}
        for(int k=0;k<4;k++)for(int c=0;c<2;c++){float read=lines_[k][positions_[k]][c];damping_[k][c]+=damp_*(read-damping_[k][c]);v[k][c]=damping_[k][c];out[c]+=read*.35f;}
        static constexpr int signs[4][4]={{1,1,1,1},{1,-1,1,-1},{1,1,-1,-1},{1,-1,-1,1}};
        for(int k=0;k<4;k++){for(int c=0;c<2;c++){float mixed=0;for(int j=0;j<4;j++)mixed+=signs[k][j]*v[j][c]*.5f;lines_[k][positions_[k]][c]=in[c]*.38f+mixed*.84f;}positions_[k]=(positions_[k]+1)%lines_[k].size();}return out;
    }
};
class Engine {
    std::shared_ptr<Session> session_;Frame cursor_=0;int transition_=-1;OnePole bassA_,bassB_,sweep_,sweepHigh_;Delay delay_;Reverb reverb_;double duck_=0,limiter_=1;float bassAlpha_;double limiterRelease_,duckAttack_,duckRelease_;
    std::array<float,2> mix(const Compiled&,Frame local);
    std::array<float,2> mixScene(const Compiled&,Frame local);
    std::array<std::array<float,2>,3> buses_{};
    std::array<std::array<std::array<float,2>,5>,2> roleBuses_{};
    std::array<OnePole,20> textureHigh_,textureLow_;
    std::array<OnePole,20> roleHigh_,roleLow_,roleHigh2_,roleLow2_;
    TailColour tailColour_;
    std::array<DJDeckDSP,2> djDecks_;
    void resetSceneFilters()noexcept{tailColour_.clear();for(auto&d:djDecks_)d.clear();for(auto&f:textureHigh_)f.reset();for(auto&f:textureLow_)f.reset();for(auto&f:roleHigh_)f.reset();for(auto&f:roleLow_)f.reset();for(auto&f:roleHigh2_)f.reset();for(auto&f:roleLow2_)f.reset();}
    double inputPeak_=0,minSafetyGain_=1;uint64_t nonfiniteFrames_=0,limitedFrames_=0,renderedFrames_=0;
public:
    explicit Engine(std::shared_ptr<Session>s):session_(std::move(s)),delay_(session_->sr),reverb_(session_->sr),tailColour_(session_->sr),djDecks_{DJDeckDSP(session_->sr),DJDeckDSP(session_->sr)}{bassAlpha_=float(1-std::exp(-2*pi*145/session_->sr));limiterRelease_=1-std::exp(-1/(session_->sr*.120));duckAttack_=1-std::exp(-1/(session_->sr*.035));duckRelease_=1-std::exp(-1/(session_->sr*.260));}
    void seek(Frame frame);
    // Optional worker-only diagnostic sink: post-routing A, B, FX with the
    // same master safety gain as the sum. Never allocate in render().
    void render(float *left,float *right,uint32_t frames,float *buses=nullptr,float *roles=nullptr);
    Frame cursor()const{return cursor_;}
    double inputPeak()const{return inputPeak_;}double minSafetyGain()const{return minSafetyGain_;}
    uint64_t nonfiniteFrames()const{return nonfiniteFrames_;}
    double limitedFraction()const{return double(limitedFrames_)/std::max<uint64_t>(1,renderedFrames_);}
    std::shared_ptr<Session> session()const{return session_;}
};
Plan planPair(const Json&,const Policy&,uint64_t,double minimumSourceSeconds=0,const std::vector<std::string>&history={});
Json preparePair(Json,const std::filesystem::path&base,const std::filesystem::path&directory,uint64_t,double minimumSourceSeconds=0,const std::vector<std::string>&history={},const std::function<bool()>&cancel={});
void prepareLibrary(const std::filesystem::path&,const std::filesystem::path&,const Policy&,uint64_t,const std::function<bool()>&cancel={},const std::function<void(float)>&progress={});
// Mobile hosts can prepare an original stereo voice with the same transport,
// latency compensation and native-rate join used by compiled stem scenes.
void prepareOriginalVoice(const Source&,double,const TimeMap&,double,int,const std::filesystem::path&,
                          const std::function<bool()>&,double=-1,double=-1,double=-1);
void freezeSession(const std::filesystem::path&);
void renderWav(const std::filesystem::path&,const std::filesystem::path&,const std::filesystem::path&);
void renderDiagnostics(const std::filesystem::path&,const std::filesystem::path&);
}
