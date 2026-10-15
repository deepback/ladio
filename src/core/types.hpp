#pragma once
#include "json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ladio {
using Json=nlohmann::json;
using Frame=int64_t;
inline constexpr double pi=std::numbers::pi;
inline double clamp(double v,double a=0,double b=1) { return std::clamp(v,a,b); }
inline double smooth(double u) { u=clamp(u); return u*u*(3-2*u); }
inline double db(double v) { return std::pow(10,v/20); }
inline uint64_t hash64(uint64_t x) { x+=0x9e3779b97f4a7c15ULL; x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL; x=(x^(x>>27))*0x94d049bb133111ebULL; return x^(x>>31); }
inline double random01(uint64_t seed) { return double(hash64(seed)>>11)*0x1.0p-53; }
inline Json readJson(const std::filesystem::path &p) { std::ifstream f(p); if(!f) throw std::runtime_error("Cannot read "+p.string()); Json j; f>>j; return j; }
inline void writeJson(const std::filesystem::path &p,const Json &j) {
    if(!p.parent_path().empty())std::filesystem::create_directories(p.parent_path()); auto tmp=p; tmp+=".pending";
    { std::ofstream f(tmp); if(!f) throw std::runtime_error("Cannot write "+p.string()); f<<j.dump(2)<<'\n'; if(!f)throw std::runtime_error("Write failed "+p.string()); }
#ifdef _WIN32
    if(!MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish "+p.string());
#else
    std::filesystem::rename(tmp,p);
#endif
}
inline std::filesystem::path resolve(const std::filesystem::path &base,const std::string &p) { std::filesystem::path v(p); return v.is_absolute()?v:base/v; }
struct Cue {
    double time=0,confidence=.4,novelty=0,energy=0,jump=0,vocal=.5,vocalAfter=.5;
    std::string kind="phrase"; std::array<double,12> chroma{};
    std::array<double,8> rhythm{}; std::array<double,4> timbre{};
    std::array<double,64> pulse{}; double pulseConfidence=0,pulseTime=-1;
    int bar=-1,phraseAnchor=-1; bool sectionBoundary=false,exitOnly=false; double boundaryStrength=0,exitSuitability=0,entrySuitability=0,structureConfidence=0,energyFall=0;
    double harmonicConfidence=0, drumJump=0, bassJump=0, attack=0, attackConfidence=0, vocalBefore=.5;
};
struct Interval { double start,end,confidence; };
struct TonalRole {std::array<double,12> chroma{};double rms=0,tonality=0,vocalCorrelation=0;};
struct RoleProfile {double start=0,end=0;std::array<TonalRole,4> roles;};
struct Track {
    std::string id,title,artist,revision,hash,pcm; int sampleRate=44100; Frame frames=0;
    double duration=0,bpm=120,beatConfidence=0,downbeatConfidence=0,lufs=-14;
    int meter=4,meterDenominator=4; bool stems=false,words=false; Json source;
    std::vector<double> beats,downbeats; std::vector<Cue> cues; std::vector<Interval> voices,wordRanges;
    std::vector<RoleProfile> roleProfiles;
};
inline Track trackFromJson(const Json &j) {
    Track t; t.source=j; t.id=j.at("id"); t.title=j.value("title",t.id); t.artist=j.value("artist",""); t.hash=j.at("source_hash");
    t.revision=j.value("revision",t.hash); t.pcm=j.at("pcm_path");
    t.sampleRate=j.at("sample_rate"); t.frames=j.at("frames"); t.duration=double(t.frames)/t.sampleRate;
    t.bpm=j.value("tempo",120.0); t.beatConfidence=j.value("tempo_confidence",.0); t.downbeatConfidence=j.value("downbeat_confidence",.0); t.lufs=j.value("loudness_lufs",-14.0); t.meter=j.value("meter",4);t.meterDenominator=j.value("meter_denominator",4);
    if(t.meter<1||t.meter>16||(t.meterDenominator!=2&&t.meterDenominator!=4&&t.meterDenominator!=8&&t.meterDenominator!=16))throw std::runtime_error("Invalid musical meter");
    if(!std::isfinite(t.bpm)||t.bpm<30||t.bpm>300||!std::isfinite(t.lufs)||t.frames<=0||t.sampleRate<8000||t.sampleRate>192000) throw std::runtime_error("Invalid track analysis");
    t.beats=j.value("beats",std::vector<double>{}); t.downbeats=j.value("downbeats",std::vector<double>{});
    if(j.contains("stems")) { auto &s=j["stems"]; t.stems=s.value("state","")=="ready"&&s.value("source_hash","")==t.hash&&s.value("sample_rate",0)==t.sampleRate&&s.value("frames",Frame(0))==t.frames&&s.value("qc",Json{}).value("finite",false)&&s.value("qc",Json{}).value("reconstruction_rms",1.0)<1e-5; }
    for(auto &w:j.value("stem_windows",Json::array()))if(w.value("state","")=="ready"&&w.value("source_hash","")==t.hash&&w.value("sample_rate",0)==t.sampleRate&&w.value("qc",Json{}).value("finite",false)&&w.value("qc",Json{}).value("reconstruction_rms",1.)<1e-5)t.stems=true;
    for(auto &c:j.value("cues",Json::array())) { Cue q; q.time=c.at("time"); q.confidence=c.value("confidence",.5); q.novelty=c.value("novelty",0.0); q.energy=c.value("energy",0.0); q.jump=c.value("energy_jump",0.0); q.kind=c.value("kind","phrase"); q.vocal=c.value("vocal",.5); q.vocalAfter=c.value("vocal_after",.5); if(c.contains("key")){q.chroma=c["key"].value("chroma",q.chroma);q.harmonicConfidence=c["key"].value("confidence",0.);}
        q.bar=c.value("bar",-1);q.phraseAnchor=c.value("phrase_anchor_bar",-1);q.exitOnly=c.value("exit_only",false);q.sectionBoundary=c.value("section_boundary",false);q.boundaryStrength=c.value("boundary_strength",0.);q.exitSuitability=c.value("exit_suitability",0.);q.entrySuitability=c.value("entry_suitability",0.);q.structureConfidence=c.value("structure_confidence",0.);q.energyFall=c.value("energy_fall",0.);
        q.rhythm=c.value("rhythm",q.rhythm);q.pulse=c.value("pulse_8quarters",q.pulse);q.pulseConfidence=c.value("pulse_confidence",0.);q.pulseTime=c.value("pulse_time",-1.);q.timbre=c.value("timbre",q.timbre);q.drumJump=c.value("drum_jump",0.);q.bassJump=c.value("bass_jump",0.);q.attack=c.value("attack_seconds",q.time);q.attackConfidence=c.value("attack_confidence",0.);q.vocalBefore=c.value("vocal_before",q.vocal);t.cues.push_back(q); }
    for(const auto&p:j.value("role_profiles",Json::array())){
        RoleProfile row;row.start=p.at("start");row.end=p.at("end");
        if(!std::isfinite(row.start)||!std::isfinite(row.end)||row.start<0||row.end<=row.start||row.end>t.duration+1./t.sampleRate)throw std::runtime_error("Invalid local role timeline");
        int k=0;for(auto n:{"vocals","drums","bass","other"}){auto&v=p.at("roles").at(n);auto&r=row.roles[k++];r.chroma=v.value("chroma",r.chroma);r.rms=v.value("rms",0.);r.tonality=v.value("tonality",0.);r.vocalCorrelation=v.value("vocal_correlation",0.);for(double c:r.chroma)if(!std::isfinite(c)||c<0)throw std::runtime_error("Invalid role chroma");if(!std::isfinite(r.rms)||r.rms<0||!std::isfinite(r.tonality)||r.tonality<0||r.tonality>1||!std::isfinite(r.vocalCorrelation)||r.vocalCorrelation<0||r.vocalCorrelation>1)throw std::runtime_error("Invalid role feature");}
        t.roleProfiles.push_back(row);
    }
    std::sort(t.roleProfiles.begin(),t.roleProfiles.end(),[](const auto&a,const auto&b){return a.start<b.start;});
    for(size_t k=1;k<t.roleProfiles.size();k++)if(t.roleProfiles[k-1].end>t.roleProfiles[k].start+1./t.sampleRate)throw std::runtime_error("Overlapping local role observations");
    for(auto &v:j.value("vocal_intervals",Json::array())) t.voices.push_back({v.at("start"),v.at("end"),v.value("confidence",.5)});
    for(auto &w:j.value("words",Json::array())) t.wordRanges.push_back({w.at("start"),w.at("end"),w.value("probability",.0)});
    t.words=!t.wordRanges.empty(); return t;
}
struct Policy { std::string mode="dj"; double maxWarp=.06; bool cues=true,loops=true,stems=true; double variety=.16; bool requestStems=false; };
inline Policy policyFor(std::string mode) {
    if(mode=="dj"||mode=="aggressive")return {"dj",.06,true,true,true,.18};
    if(mode=="smart")return {mode,.03,false,false,true,.08};
    if(mode=="gentle")return {mode,0,false,false,false,.02};
    if(mode=="off")return {mode,0,false,false,false,0};
    throw std::runtime_error("Unknown mode: "+mode);
}
struct TimeMap {
    double initialRate=1,steadyRate=1,rampIn=0,holdEnd=0,rampOut=0,finalRate=1;
    // An optional scene clock is shared by every stem, clip and rendered hold.
    // Offset is the voice's start on that clock, not a new local ramp origin.
    double clockStart=-1,clockEnd=-1,clockOffset=0;
    // Small measured bar corrections ride on the common tempo trajectory.
    // Smoothstep offsets keep the source rate continuous at every bar.
    std::vector<std::pair<double,double>> phaseCorrections;
    double phaseAt(double t,bool derivative=false)const noexcept {
        if(phaseCorrections.empty()||t<=phaseCorrections.front().first)return 0;
        if(t>=phaseCorrections.back().first)return derivative?0:phaseCorrections.back().second;
        auto hi=std::upper_bound(phaseCorrections.begin(),phaseCorrections.end(),t,[](double x,const auto&k){return x<k.first;});
        const auto&b=*hi;const auto&a=*(hi-1);double d=b.first-a.first,u=(t-a.first)/d;
        return derivative?(b.second-a.second)*6*u*(1-u)/d:a.second+(b.second-a.second)*smooth(u);
    }
    bool clocked()const noexcept{return clockStart>=0&&clockEnd>clockStart;}
    double clockIntegral(double t)const noexcept {
        double length=clockEnd-clockStart,x=clamp(t-clockStart,0.,length),u=x/length;
        return initialRate*t+(steadyRate-initialRate)*(length*(u*u*u-.5*u*u*u*u)+std::max(0.,t-clockEnd));
    }
    double baseSourceAt(double out) const noexcept {
        if(clocked())return clockIntegral(clockOffset+out)-clockIntegral(clockOffset);
        if(out<=0)return out*initialRate;
        double sum=0;
        if(rampIn>0){ double v=std::min(out,rampIn); sum=initialRate*v+(steadyRate-initialRate)*v*v/(2*rampIn); out-=v; if(out<=0)return sum; }
        double hold=std::max(0.0,holdEnd-rampIn),v=std::min(out,hold); sum+=steadyRate*v; out-=v; if(out<=0)return sum;
        if(rampOut>0){v=std::min(out,rampOut); sum+=steadyRate*v+(finalRate-steadyRate)*v*v/(2*rampOut); out-=v;}
        return sum+out*finalRate;
    }
    double baseRateAt(double t) const noexcept { if(clocked())return initialRate+(steadyRate-initialRate)*smooth((clockOffset+t-clockStart)/(clockEnd-clockStart));if(t<0)return initialRate; if(rampIn>0&&t<rampIn)return initialRate+(steadyRate-initialRate)*t/rampIn; if(t<holdEnd)return steadyRate; if(rampOut>0&&t<holdEnd+rampOut)return steadyRate+(finalRate-steadyRate)*(t-holdEnd)/rampOut; return finalRate; }
    double sourceAt(double t)const noexcept{return baseSourceAt(t)+phaseAt(t);}
    double rateAt(double t)const noexcept{return baseRateAt(t)+phaseAt(t,true);}
    Json json()const{Json j={{"initial_rate",initialRate},{"steady_rate",steadyRate},{"ramp_in_seconds",rampIn},{"hold_end_seconds",holdEnd},{"ramp_out_seconds",rampOut},{"final_rate",finalRate}};if(!phaseCorrections.empty())j["bar_phase_offsets"]=phaseCorrections;if(clocked())j["scene_clock"]={{"start",clockStart},{"end",clockEnd},{"offset",clockOffset},{"curve","smoothstep"}};return j;}
    static TimeMap from(const Json &j){TimeMap m;m.initialRate=j.value("initial_rate",1.);m.steadyRate=j.value("steady_rate",1.);m.rampIn=j.value("ramp_in_seconds",0.);m.holdEnd=j.value("hold_end_seconds",0.);m.rampOut=j.value("ramp_out_seconds",0.);m.finalRate=j.value("final_rate",1.);if(j.contains("scene_clock")){const auto&c=j.at("scene_clock");m.clockStart=c.at("start");m.clockEnd=c.at("end");m.clockOffset=c.at("offset");if(!std::isfinite(m.clockStart)||!std::isfinite(m.clockEnd)||!std::isfinite(m.clockOffset)||!m.clocked()||m.clockEnd>240||m.clockOffset<0||m.clockOffset>240||c.value("curve","")!="smoothstep")throw std::runtime_error("Invalid shared source clock");}
        if(j.contains("bar_phase_offsets")){
            for(const auto&k:j.at("bar_phase_offsets")){double t=k.at(0),v=k.at(1);if(!std::isfinite(t)||!std::isfinite(v)||t<0||t>240||abs(v)>.5||(!m.phaseCorrections.empty()&&t<=m.phaseCorrections.back().first))throw std::runtime_error("Invalid bar phase correction");m.phaseCorrections.emplace_back(t,v);}
            if(m.phaseCorrections.size()<2||m.phaseCorrections.front()!=std::pair<double,double>{0,0}||abs(m.phaseCorrections.back().second)>1e-9)throw std::runtime_error("Bar corrections must rejoin original phase");
        }return m;}
};
struct Plan {
    std::string id,recipe,family,explanation; int a=0,b=0,sr=44100,bars=4,pattern=0; uint64_t seed=0,generation=1;
    double outCue=0,inCue=0,seconds=0,lead=0,post=12,bStart=0,vocalEnd=0,vocalOpen=0,swap=0,delayBeats=.75,feedback=.38,score=0,harmony=0;
    TimeMap mapA,mapB; bool useStems=false,echo=false,roll=false,stop=false,reset=false; std::vector<std::string> reasons; Json candidates;
    double bassRamp=1, tailSeconds=4, wet=.65, reverbWet=.18, captureStart=0,captureEnd=0, impact=0, similarity=.5;
    int outBar=-1,inBar=-1; bool exitBoundary=false; double exitStructure=0,entryStructure=0;
    double entryAnchor=0,attackSource=0;
    double grooveFit=.5,timbreFit=.5,energyRatio=1;
    int filterStyle=0, relayStem=3,captureStem=0; bool blend=false, pulseRamp=false;
    struct VoiceClip {int deck=0;double source=0,start=0,length=0,gain=1;};
    std::vector<VoiceClip> voiceClips;
    Json scene=Json::object();
    Json sceneAttempt=Json::object();
    Frame frames()const{return std::llround((lead+seconds+post)*sr);}
    double aSourceStart()const{return scene.empty()?outCue-mapA.sourceAt(lead+seconds):scene.at("source_a_start_seconds").get<double>();}
    double bSourceEnd()const{return scene.empty()?inCue+mapB.sourceAt(lead+seconds+post-bStart):scene.at("recovery").at("b_source_seconds").get<double>();}
    Json json()const;
    static Plan from(const Json &j);
};
inline bool isOverlay(const std::string&f){return f=="bass_swap"||f=="phrase_blend"||f=="vocal_handoff"||f=="percussion_bridge"||f=="full_stem_swap"||f=="acapella_carryover"||f=="call_response"||f=="mashup"||f=="melody_relay"||f=="tempo_ramp"||f=="harmonic_pivot"||f=="vocal_tease"||f=="breakdown_bridge"||f=="loop_extension";}
inline const std::array<const char*,28> families={"natural","gapless","crossfade","phrase_blend","bass_swap","filter_sweep","echo_out","reverb_tail","cut_on_one","drop_switch","breakdown_bridge","loop_extension","loop_roll","half_time","percussion_bridge","full_stem_swap","vocal_handoff","acapella_carryover","vocal_tease","call_response","vocal_echo","mashup","melody_relay","tape_stop","gate_stutter","tempo_ramp","harmonic_pivot","energy_reset"};
inline std::string recipeFor(const std::string&family){for(size_t i=0;i<families.size();i++)if(family==families[i]){char id[8];std::snprintf(id,sizeof(id),"T%02zu",i);return id;}if(family=="echo_hard_drop")return "T20+T08";throw std::runtime_error("Unsupported recipe family");}
std::vector<Plan> planPlaylist(const std::vector<Track>&tracks,const Policy&,uint64_t seed);
Json recipeCatalog();
}
