#pragma once
#include "types.hpp"
#include "dj_program.hpp"
#include "dj_catalog.hpp"
#include <optional>
namespace ladio {
inline bool independentFamily(const std::string&f){return djStyle(f)||f=="phrase_blend_v1"||f=="spectral_bridge_v1"||f=="echo_bridge_v1"||f=="pulse_bridge_v1"||f=="gentle_overlap_v1"||f=="repeat_bridge_v1";}
inline bool sceneFamily(const std::string&f){return independentFamily(f)||f=="groove_weave"||f=="loop_lift"||f=="vocal_relay"||f=="tease_reveal"||f=="pulse_bridge";}
// Equal gain/filter lanes can share the original-minus-selected remainder.
inline Json requiredRoles(const Json&voice){
    Json result=Json::array();if(voice.value("original_only",false))return result;
    auto filters=[&](const std::string&role){Json f=Json::array();for(auto v:voice.value("soft_filters",Json::array()))if(v.at("role")==role){v.erase("role");f.push_back(v);}return f;};
    const auto&base=voice.at("roles").at("residual");
    for(auto role:{"vocals","drums","bass","other"})if(voice.at("roles").at(role)!=base||filters(role)!=filters("residual")||(std::string(role)=="other"&&voice.contains("texture_filter")))result.push_back(role);
    return result;
}
inline std::vector<int> preparedChannels(const Json&voice){
    std::vector<int> channels{0,1};for(const auto&role:requiredRoles(voice)){int k=role=="vocals"?0:role=="drums"?1:role=="bass"?2:3;channels.push_back(2+k*2);channels.push_back(3+k*2);}return channels;
}
struct Knot {Frame frame=0;double value=0;};
struct Envelope {
    std::vector<Knot> knots;
    double at(Frame f)const noexcept {
        if(knots.empty())return 0;if(f<=knots.front().frame)return knots.front().value;
        for(size_t k=1;k<knots.size();k++)if(f<knots[k].frame){auto&a=knots[k-1];auto&b=knots[k];return a.value+(b.value-a.value)*smooth(double(f-a.frame)/double(b.frame-a.frame));}
        return knots.back().value;
    }
};
// Separation is needed only while a component differs from the reconstructing
// remainder. Equal lanes read the original exactly, even outside stem windows.
inline Json roleSourceRanges(const Json&voice,int sr){
    Json result=Json::array();if(voice.value("original_only",false))return result;
    auto envelope=[](const Json&j){Envelope e;for(const auto&p:j)e.knots.push_back({p.at(0),p.at(1)});return e;};
    auto filters=[&](const std::string&role){Json f=Json::array();for(auto v:voice.value("soft_filters",Json::array()))if(v.at("role")==role){v.erase("role");f.push_back(v);}return f;};
    const auto base=envelope(voice.at("roles").at("residual"));const auto remainderFilters=filters("residual");
    const Frame begin=voice.at("start_frame"),end=voice.at("end_frame");const auto map=TimeMap::from(voice.at("time_map"));
    const double source=voice.at("source_start_seconds");
    for(auto role:{"vocals","drums","bass","other"}){
        const auto lane=envelope(voice.at("roles").at(role));const auto selectedFilters=filters(role);
        std::vector<Frame> edges{begin,end};for(const auto&e:{base,lane})for(const auto&k:e.knots)if(k.frame>begin&&k.frame<end)edges.push_back(k.frame);
        for(const auto&fs:{selectedFilters,remainderFilters})for(const auto&f:fs)for(const auto&p:f.at("wet")){Frame at=p.at(0);if(at>begin&&at<end)edges.push_back(at);}
        std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
        std::vector<std::pair<Frame,Frame>> active;
        for(size_t k=1;k<edges.size();k++){
            bool needed=false;for(double u:{0.,.25,.5,.75,1.}){Frame at=edges[k-1]+std::llround(u*(edges[k]-edges[k-1]));double a=lane.at(at),b=base.at(at);needed|=abs(a-b)>1e-10;
                if(selectedFilters!=remainderFilters){for(const auto&fs:{selectedFilters,remainderFilters})for(const auto&f:fs)needed|=std::max(a,b)>1e-10&&envelope(f.at("wet")).at(at)>1e-10;}
                if(std::string(role)=="other"&&voice.contains("texture_filter"))needed|=a>1e-10;
            }
            if(!needed)continue;if(!active.empty()&&active.back().second==edges[k-1])active.back().second=edges[k];else active.push_back({edges[k-1],edges[k]});
        }
        for(auto [first,last]:active){double lo=source+map.sourceAt(double(first-begin)/sr),hi=source+map.sourceAt(double(last-begin)/sr);
            // A repeated voice consumes its small unique clip, not virtual time.
            if(voice.value("loop_frames",Frame(0))>0||voice.contains("source_loop")){lo=voice.at("source_start_seconds");hi=voice.at("source_end_seconds");}
            result.push_back({{"role",role},{"start",lo},{"end",hi}});
        }
    }return result;
}
// Cutoff curves are compiled on the preparation worker, in log Hz. The audio
// worker only interpolates bounded coefficients: no exp/log or allocation.
struct FilterAutomation {
    static constexpr Frame stride=256;
    Frame first=0,last=0;std::vector<float> values;
    float at(Frame f)const noexcept {
        if(f<=first)return values.front();if(f>=last)return values.back();
        Frame offset=f-first;size_t k=size_t(offset/stride);
        return values[k]+(values[k+1]-values[k])*float(offset%stride)/float(std::min(stride,last-first-Frame(k)*stride));
    }
};
// Source PCM seconds, source quarters, scene quarters and output samples are
// separate units. Endpoints are rounded from the absolute accumulated map.
struct MusicalTransport {
    struct Segment {double seconds=0,quarter=0,bpm=120;int numerator=4,denominator=4;double endBpm=120,rampSeconds=0;bool smoothRamp=false;
        double quarters(double t)const noexcept{double r=std::min(std::max(0.,t),rampSeconds),u=rampSeconds>0?r/rampSeconds:0;double integral=smoothRamp?rampSeconds*(u*u*u-.5*u*u*u*u):r*u*.5;return rampSeconds>0?(bpm*r+(endBpm-bpm)*integral+std::max(0.,t-rampSeconds)*endBpm)/60:t*bpm/60;}};
    int sr=44100;std::vector<Segment> segments;
    double quarterAt(Frame f)const noexcept {double s=double(f)/sr;const Segment*part=&segments.front();for(auto&v:segments)if(v.seconds<=s)part=&v;return part->quarter+part->quarters(s-part->seconds);}
    Frame frameAt(double q)const noexcept {const Segment*part=&segments.front();for(auto&v:segments)if(v.quarter<=q)part=&v;double lo=0,hi=(q-part->quarter)*60/std::min(part->bpm,part->endBpm)+1;for(int k=0;k<52;k++){double mid=(lo+hi)*.5;if(part->quarters(mid)<q-part->quarter)lo=mid;else hi=mid;}return std::llround((part->seconds+(lo+hi)*.5)*sr);}
    static double quartersPerBar(int n,int d){return double(n)*4/d;}
};
struct SceneVoice {
    struct RoleFilter {float hpAlpha=0,lpAlpha=0;int stages=1;Envelope wet;FilterAutomation hp,lp;};
    std::string id,mode;int deck=0,channels=12;Frame start=0,end=0,offset=0,loop=0,rollStart=0;bool roll=false,originalOnly=false;
    double sourceStart=0,sourceEnd=0,gain=1,loopPeriod=0,rollFoundationMix=0;TimeMap map;std::array<Envelope,5> roles; // V,D,B,O,residual
    double sourceLoopStart=-1,sourceLoopEnd=-1,sourceLoopActivate=-1; // Loop expanded before time-stretch.
    std::vector<std::pair<double,double>> sourceRepeatStages;
    std::array<double,4> rollPeriods{};
    float hpAlpha=0,lpAlpha=0;Frame filterStart=0,filterEnd=0;
    std::array<RoleFilter,5> softFilters;
    int rollStage(Frame f)const noexcept{double u=double(f-rollStart)/std::max<Frame>(1,end-rollStart);return u<.5?0:u<.75?1:u<.875?2:3;}
    double cycleAt(Frame f,int stem)const noexcept{double cycle=loopPeriod>0?loopPeriod:double(loop);return roll&&stem==1&&f>=rollStart?rollPeriods[rollStage(f)]:cycle;}
    double position(Frame f,int stem)const noexcept {
        double x=double(f-start);if(loop>0){if(roll&&stem==1&&f>=rollStart){constexpr double phase[]={0.,.5,.75,.875};Frame stageStart=rollStart+Frame(std::ceil(phase[rollStage(f)]*(end-rollStart)));x=double(f-stageStart);}double cycle=cycleAt(f,stem);return offset+std::fmod(std::fmod(x,cycle)+cycle,cycle);}return offset+x;
    }
    double virtualSourceSeconds(Frame f,int sr)const noexcept {
        // Slip advances the virtual source while its audible loop repeats.
        // Hold keeps the source in the held range until an explicit release.
        double x=mode=="hold"?position(f,2):double(offset+f-start);
        double source=sourceStart+map.sourceAt(x/sr);
        if(!sourceRepeatStages.empty()&&source>=sourceRepeatStages.front().first){
            size_t k=0;while(k+1<sourceRepeatStages.size()&&source>=sourceRepeatStages[k+1].first)++k;
            source=sourceLoopStart+std::fmod(source-sourceRepeatStages[k].first,sourceRepeatStages[k].second);
        }else if(sourceLoopEnd>sourceLoopStart&&sourceLoopStart>=0&&source>=sourceLoopActivate){double n=sourceLoopEnd-sourceLoopStart;source=sourceLoopStart+std::fmod(std::fmod(source-sourceLoopStart,n)+n,n);}
        return source;
    }
};
struct Scene {
    int version=1,sr=44100;Frame frames=0,arrival=0;double bpm=120,voicePhraseEnd=0,voiceRelease=0,fxEnd=0;bool fx=false,reset=false;std::string accent;
    std::string family;MusicalTransport transport;std::vector<SceneVoice> voices;Json contract;
    struct EffectChain {
        bool enabled=false;int sourceDeck=0;Envelope capture,wet;
        double delayBeats=.5,delayWet=0,feedback=0,reverbWet=0,tailBpm=120;
        std::array<double,5> eq{1,0,0,0,0};
        double tailHP=0,tailLP=1,flangerWet=0,flangerFeedback=0,flangerQuarters=8,gateDepth=0,gateQuarters=.5,gateDuty=.65;
    } effects;
    DJProgram dj;
    static Scene from(const Json&j,bool compileFilters=true);
};
std::optional<Plan> directScene(const Track&,const Track&,int,int,const Policy&,uint64_t,double,const std::vector<std::string>&,Json&,double maxBSourceSeconds=-1);
}
