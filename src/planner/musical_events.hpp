#pragma once
#include "scene.hpp"
namespace ladio {
// Small immutable feature index: no audio decoding or model invocation while
// enumerating cue pairs, roles, motifs and musical-event positions.
class MusicalIndex {
    using Sum=std::array<double,13>;
    std::vector<double> time;std::vector<Sum> cumulative;
    Sum at(double t)const{
        if(t<=time.front())return {};if(t>=time.back())return cumulative.back();
        size_t i=std::upper_bound(time.begin(),time.end(),t)-time.begin()-1;
        double u=(t-time[i])/std::max(1e-9,time[i+1]-time[i]);auto v=cumulative[i];
        for(size_t k=0;k<v.size();k++)v[k]+=u*(cumulative[i+1][k]-v[k]);return v;
    }
public:
    bool observed=false;
    explicit MusicalIndex(const Track&t){
        Sum sum{};const auto rows=t.source.value("curves",Json::array());
        for(size_t i=0;i<rows.size();i++){
            const auto&r=rows[i];double start=r.value("time",0.),end=i+1<rows.size()?rows[i+1].value("time",start):t.duration;
            if(!std::isfinite(start)||end<=start)continue;
            if(!time.empty()&&start<=time.back())throw std::runtime_error("Unordered musical observations");
            time.push_back(start);cumulative.push_back(sum);double d=end-start,rms=r.value("rms",0.),low=r.value("low_energy",r.value("bass_rms",0.));
            observed|=r.contains("vocals_activity");
            bool activity=r.contains("vocals_activity")&&r.contains("drums_activity")&&r.contains("others_activity");
            bool structure=r.contains("phrases_boundary")&&r.contains("segments_boundary")&&r.contains("sections_boundary");
            Sum value{rms*rms,low*low,clamp(r.value("vocals_activity",r.value("vocal",.5))),clamp(r.value("drums_activity",.5)),clamp(r.value("bass_activity",.5)),clamp(r.value("others_activity",.5)),clamp(r.value("onset",0.)),clamp(r.value("novelty",0.)),clamp(r.value("phrases_boundary",0.)),clamp(r.value("segments_boundary",0.)),clamp(r.value("sections_boundary",0.)),activity?1.:0.,structure?1.:0.};
            for(size_t k=0;k<sum.size();k++)sum[k]+=value[k]*d;
        }
        if(time.empty()){time.push_back(0);cumulative.push_back({});}
        time.push_back(std::max(t.duration,time.back()+1e-6));cumulative.push_back(sum);
    }
    double mean(double first,double last,size_t field)const{
        if(last<=first)return 0;auto a=at(first),b=at(last);return std::max(0.,(b[field]-a[field])/(last-first));
    }
    double energy(double first,double last)const{return std::sqrt(mean(first,last,0));}
    double vocal(double first,double last)const{return mean(first,last,2);}
    double foundation(double first,double last)const{return std::sqrt(mean(first,last,1));}
};
inline double outputForSource(const TimeMap&map,double sourceStart,double source,double duration){
    double lo=0,hi=duration;for(int i=0;i<36;i++){double m=(lo+hi)*.5;if(sourceStart+map.sourceAt(m)<source)lo=m;else hi=m;}return (lo+hi)*.5;
}
struct MusicalEvents {
    Frame first=0,hybrid=0,foundation=0,aPhrase=0,aRelease=0,bLead=0,arrival=0,last=0;
    double voiceOverlap=0;bool measuredVoice=false;
    Json json(int sr)const{return {{"first_b_seconds",double(first)/sr},{"hybrid_seconds",double(hybrid)/sr},{"foundation_seconds",double(foundation)/sr},{"a_phrase_end_seconds",double(aPhrase)/sr},{"a_release_seconds",double(aRelease)/sr},{"b_lead_seconds",double(bLead)/sr},{"arrival_seconds",double(arrival)/sr},{"last_a_seconds",double(last)/sr},{"competing_vocal_seconds",voiceOverlap},{"activity_observed",measuredVoice}};}
};
inline MusicalEvents musicalEvents(const Track&a,const Track&b,const MusicalIndex&ia,const MusicalIndex&ib,double startA,double startB,const TimeMap&ma,const TimeMap&mb,double pre,double post,bool synchronized){
    const int sr=a.sampleRate;const double beat=60/b.bpm;
    auto frame=[&](double v){return Frame(std::llround(std::clamp(v,0.,pre+post)*sr));};
    auto fromB=[&](double s){return outputForSource(mb,startB,s,pre+post);};
    auto fromA=[&](double s){return outputForSource(ma,startA,s,pre);};
    MusicalEvents e;e.arrival=frame(pre);e.measuredVoice=ia.observed&&ib.observed;
    double lastVoice=0;
    for(const auto&v:a.source.value("protected_vocal_phrases",a.source.value("vocal_phrases",Json::array())))
        if(v.value("end",0.)>startA&&v.value("end",0.)<=startA+ma.sourceAt(pre)+.02)lastVoice=std::max(lastVoice,fromA(v.value("end",0.)));
    e.aPhrase=frame(lastVoice);e.aRelease=frame(std::max(lastVoice,std::max(0.,pre-std::min(2*beat,1.2))));
    // Earliest nonempty B material, rather than a fictitious first appearance
    // even when the file's intro or its selected role is silent.
    double first=std::min(beat,pre*.10),bEnd=startB+mb.sourceAt(pre);
    const double reference=ib.energy(bEnd,bEnd+std::min(post,4.));
    for(double t=first;t<pre-.15;t+=.2){double s=startB+mb.sourceAt(t);if(ib.energy(s,s+.3)>std::max(.0002,reference*.08)){first=t;break;}}
    e.first=frame(first);e.hybrid=frame(std::max(first+std::min(2*beat,pre*.1),pre*.28));
    e.hybrid=std::min(e.hybrid,e.arrival);
    // A pickup belongs to the incoming lead. It must open before the downbeat,
    // not wait for an arbitrary end-of-transition timer.
    double lead=pre+post;bool leadFound=false;
    auto incomingLead=[&](double source){lead=std::min(lead,fromB(std::max(startB,source)));leadFound=true;};
    const auto incomingPhrases=b.source.value("protected_vocal_phrases",Json::array());
    if(!incomingPhrases.empty()){
        for(const auto&v:incomingPhrases)if(v.value("confidence",.8)>=.45&&v.value("end",0.)>bEnd+.02&&v.value("start",0.)<bEnd+4*beat)incomingLead(v.value("start",0.));
    }else for(const auto&v:b.voices)if(v.confidence>=.45&&v.end>bEnd+.02&&v.start<bEnd+4*beat)incomingLead(v.start);
    if(!leadFound)lead=pre;
    e.bLead=frame(lead);if(lead<first+.2)e.first=frame(std::max(0.,lead-.2));
    double foundation=pre;
    if(synchronized){
        double targetLow=ib.foundation(bEnd,bEnd+2*beat),targetEnergy=ib.energy(bEnd,bEnd+2*beat);
        const double minimumJoint=std::min(16*60/b.bpm,pre*.55);
        for(double s:b.downbeats){double t=fromB(s);if(t<std::max(pre*.50,double(e.hybrid)/sr+minimumJoint)||t>pre-.05)continue;
            if(ib.energy(s,s+2*beat)<std::max(.0002,targetEnergy*.60))continue;
            if(targetLow>.0001&&ib.foundation(s,s+2*beat)<targetLow*.65)continue;
            if(ib.observed&&ib.mean(s,s+2*beat,3)<.40)continue;
            foundation=t;break;
        }
    }
    e.foundation=frame(foundation);e.last=frame(pre+std::min(post-.2,7.));
    // Full mixes cannot independently mute one singer. Estimate conflict only
    // during the interval in which B will actually be audible, then prefer a
    // cleaner source pair rather than pretending those are isolated stems.
    if(e.measuredVoice)for(double t=double(e.hybrid)/sr;t<pre;t+=.2){double sa=startA+ma.sourceAt(t),sb=startB+mb.sourceAt(t);if(ia.vocal(sa,sa+.2)>.45&&ib.vocal(sb,sb+.2)>.45)e.voiceOverlap+=.2;}
    return e;
}
}
