// Independent functional planner. No Apple styles, thresholds, curves or models
// are imported by this planner. Optional supplied observation models are run
// separately by the analyzer; each scene binds their events to a shared clock.
#include "scene.hpp"
#include <numeric>
#include <map>
#include "dj_score.hpp"
#include "harmony.hpp"
#include "scene_preference.hpp"
namespace ladio {
namespace {
constexpr const char* roles[]={"vocals","drums","bass","other","residual"};
double quarters(const Track&t){return MusicalTransport::quartersPerBar(t.meter,t.meterDenominator);}
double localTempo(const Track&t,double at){
    std::vector<double> periods;
    for(size_t i=1;i<t.downbeats.size();++i)if(std::abs(t.downbeats[i]-at)<12){
        double d=t.downbeats[i]-t.downbeats[i-1];if(d>0&&std::abs(d-quarters(t)*60/t.bpm)<.2)periods.push_back(d);
    }
    if(periods.size()<3)return t.bpm;
    std::sort(periods.begin(),periods.end());return quarters(t)*60/periods[periods.size()/2];
}
bool covers(const Track&t,double first,double last){
    if(first<0||last>t.duration-.01)return false;
    if(!t.source.contains("grid_coverage"))return true;
    for(const auto&r:t.source.at("grid_coverage"))if(r[0].get<double>()<=first+.02&&r[1].get<double>()>=last-.02)return true;
    return false;
}
bool cutsPhrase(const Track&t,double at){
    for(const auto&p:t.source.value("protected_vocal_phrases",t.source.value("vocal_phrases",Json::array()))){
        double start=p.value("start",0.),end=p.value("end",0.);if(at>start+.02&&at<end-.02)return true;
    }
    for(const auto&p:t.wordRanges)if(p.confidence>.6&&at>p.start+.015&&at<p.end-.015)return true;
    return false;
}
bool measuredCue(const Track&t,const Cue&c){
    return c.bar>=0&&size_t(c.bar)<t.downbeats.size()&&std::abs(t.downbeats[c.bar]-c.time)<.035;
}
// Structural models describe many more boundaries than the sparse catalogue
// of eight-bar cues. Enumerate those observed boundaries, keeping the actual
// downbeat and the original word/phrase protection instead of inventing ticks.
std::vector<Cue> eventCues(const Track&t,const MusicalIndex&index,bool incoming){
    auto result=t.cues;if(!index.observed)return result;
    const auto phrases=t.source.value("protected_vocal_phrases",Json::array());
    for(size_t k=1;k+1<t.downbeats.size();k++){
        double at=t.downbeats[k];if(incoming?(at<1||at>t.duration*.46):(at<t.duration*.55))continue;
        bool duplicate=false;for(const auto&c:result)duplicate|=std::abs(c.time-at)<.035;if(duplicate)continue;
        double boundary=std::max({index.mean(at-.15,at+.15,8),index.mean(at-.15,at+.15,9),index.mean(at-.15,at+.15,10)});
        bool phrase=false;for(const auto&p:phrases){
            phrase|=std::min(std::abs(p.value("start",0.)-at),std::abs(p.value("end",0.)-at))<.20;
            phrase|=!incoming&&p.value("end",0.)<=at&&at-p.value("end",0.)<quarters(t)*60/t.bpm;
        }
        double before=index.vocal(at-.35,at),after=index.vocal(at,at+.35);
        if(boundary<.55&&!phrase&&std::abs(after-before)<.25)continue;
        Cue c;c.bar=int(k);c.time=c.attack=at;c.kind="observed_phrase";c.phraseAnchor=int(k);
        c.confidence=c.structureConfidence=std::min(t.beatConfidence,t.downbeatConfidence);
        c.boundaryStrength=boundary;c.energy=index.energy(at,at+1);c.vocalBefore=before;c.vocalAfter=after;
        double previous=index.energy(at-1,at);c.jump=std::log((c.energy+.001)/(previous+.001));
        c.entrySuitability=.35*boundary+.25*clamp(c.jump)+.20*(1-before);
        c.exitSuitability=.35*boundary+.25*(1-before)+.20*clamp(-c.jump);
        c.exitOnly=!incoming;result.push_back(c);
    }
    return result;
}
struct BarSpan {double first=0,last=0;int bars=0;std::vector<std::pair<int,double>> anchors;};
std::optional<BarSpan> measuredSpan(const Track&t,double last,int bars,double tempo){
    double accentConfidence=t.source.value("beat_qc",Json::object()).value("observed_downbeat_probability",t.downbeatConfidence);
    if(bars<1||t.beatConfidence<.55||accentConfidence<.40)return std::nullopt;
    double period=quarters(t)*60/tempo,target=last-bars*period;
    auto it=std::lower_bound(t.downbeats.begin(),t.downbeats.end(),target);
    if(it==t.downbeats.end())return std::nullopt;
    if(it!=t.downbeats.begin()&&std::abs(*std::prev(it)-target)<std::abs(*it-target))--it;
    // The tempo estimate is local, not an exact integral of a long phrase.
    // Permit bounded accumulated drift, then verify the measured interval.
    if(std::abs(*it-target)>std::min(.35,.05+bars*period*.015))return std::nullopt;
    BarSpan span{*it,last,bars,{}};double measured=(last-*it)/bars;
    if(std::abs(measured/period-1)>.025)return std::nullopt;
    int invalid=0,total=0,subdivisions=0;
    for(;it!=t.downbeats.end()&&*it<=last+.01;++it){
        int bar=int(std::llround((*it-span.first)/measured));total++;
        if(std::abs(*it-span.first-bar*measured)>.12){
            // Some observations contain both bar and half-bar accents. They
            // are not contradictory tempo evidence when a real beat supports
            // the subdivision. Never feed them to the bar-phase correction.
            double half=std::round((*it-span.first)/measured*2)/2;
            auto beat=std::lower_bound(t.beats.begin(),t.beats.end(),*it-.065);
            bool supported=beat!=t.beats.end()&&std::abs(*beat-*it)<.065;
            if(supported&&quarters(t)>=2&&std::abs(half-std::round(half))>.25&&std::abs(*it-span.first-half*measured)<.09){subdivisions++;continue;}
            invalid++;continue;
        }
        if(!span.anchors.empty()&&bar<=span.anchors.back().first){invalid++;continue;}
        span.anchors.emplace_back(bar,*it);
    }
    // Missing alternate bar detections are usable with a supported beat grid.
    // Still require actual phrase endpoints and at least half the bar anchors.
    // Arbitrary off-grid detections remain a veto. No beat is synthesized.
    if(invalid>std::max(0,(total-subdivisions)/4)||span.anchors.size()<size_t(std::max(2,bars/2+1))||span.anchors.front().first!=0||span.anchors.back().first!=bars)return std::nullopt;
    if(!t.beats.empty()){
        int supported=0;double beat=measured/quarters(t);
        for(double b:t.beats)if(b>=span.first-.02&&b<=last+.02&&std::abs((b-span.first)/beat-std::round((b-span.first)/beat))*beat<.065)supported++;
        if(supported<std::max(4,int(bars*quarters(t)*.45)))return std::nullopt;
    }
    return span;
}
bool sustained(const MusicalIndex&t,double at,double length){
    double average=t.energy(at,at+length);
    if(average<0)return true;
    if(average<.0001)return false;
    // A crash followed by a gap is not a complete incoming foundation.
    for(double s=at+.25;s+.25<at+length;s+=.25){double e=t.energy(s,s+.3);if(e>=0&&e<std::max(.0001,average*.035))return false;}
    return true;
}
std::vector<Cue> instrumentalExits(const Track&t,const MusicalIndex&index,double earliest){
    std::vector<Cue> exits;
    if(!index.observed)return exits;
    const auto phrases=t.source.value("protected_vocal_phrases",t.source.value("vocal_phrases",Json::array()));
    int run=-1;double minimumEnergy=0,maximumEnergy=0;
    for(size_t k=0;k+1<t.downbeats.size();k++){
        double first=t.downbeats[k],last=t.downbeats[k+1];
        bool occupied=false;
        for(const auto&p:phrases)occupied|=p.value("start",0.)<last&&p.value("end",0.)>first;
        for(const auto&p:t.wordRanges)occupied|=p.confidence>.6&&p.start<last&&p.end>first;
        double energy=index.energy(first,last);
        bool usable=first>=t.duration*.48&&covers(t,first,last)&&!occupied&&
            energy>=.002&&index.vocal(first,last)<.18&&index.mean(first,last,3)>=.35&&
            sustained(index,first,last-first);
        if(!usable){run=-1;continue;}
        if(run<0){run=int(k);minimumEnergy=maximumEnergy=energy;}
        else{minimumEnergy=std::min(minimumEnergy,energy);maximumEnergy=std::max(maximumEnergy,energy);}
        if(maximumEnergy/minimumEnergy>1.8){run=int(k);minimumEnergy=maximumEnergy=energy;}
        int bars=int(k)+1-run;
        // Extend a complete measured instrumental phrase after the lead has
        // finished. Sparse structural cues can miss this useful late material.
        // Compare these real late phrases with structural cues as well: a
        // technically usable cue need not offer the best release runway.
        if((bars!=2&&bars!=4&&bars!=8)||last<earliest||last>t.duration-.15)continue;
        bool duplicate=false;for(const auto&c:t.cues)duplicate|=std::abs(c.time-last)<.035;
        if(duplicate)continue;
        Cue c;c.bar=int(k)+1;c.time=last;c.kind="instrumental_outro";c.exitOnly=true;c.phraseAnchor=run;
        c.confidence=std::min(t.beatConfidence,t.downbeatConfidence);c.structureConfidence=.65;
        c.exitSuitability=.65;c.energy=index.energy(t.downbeats[run],last);c.vocalBefore=index.vocal(first,last);
        exits.push_back(c);
    }
    return exits;
}
// Index the existing DSP curves once. Candidate windows then cost two binary
// searches instead of rescanning audio or the full track for each cue pair.
struct HarmonyIndex {
    using Sum=std::array<double,15>;
    std::vector<double> times;
    std::vector<Sum> sums;
    explicit HarmonyIndex(const Track&t){
        static const Json empty=Json::array();
        Sum sum{};const auto&rows=t.source.contains("curves")?t.source.at("curves"):empty;
        for(size_t i=0;i<rows.size();i++){
            const auto&r=rows[i];double start=r.value("time",0.);
            double end=i+1<rows.size()?rows[i+1].value("time",start):std::min(t.duration,start+t.source.value("curve_step",.2));
            if(!times.empty()&&start<times.back())throw std::runtime_error("Unordered harmonic curves");
            times.push_back(start);sums.push_back(sum);
            if(end<=start||!r.contains("chroma")||r.value("percussive_only",false))continue;
            Chroma c=r.at("chroma").get<Chroma>();auto stats=chromaStats(c);
            double rms=r.value("rms",0.),tonal=clamp(r.value("tonality",stats.contrast));
            if(rms<=.0003||stats.sum<1e-10)continue;
            double signal=rms*(end-start),weight=signal*std::sqrt(tonal);
            for(int k=0;k<12;k++)sum[k]+=c[k]*weight;
            sum[12]+=signal*tonal;sum[13]+=signal;sum[14]+=end-start;
        }
        times.push_back(t.duration);sums.push_back(sum);
    }
    Sum at(double time)const{
        if(time<=times.front())return {};
        if(time>=times.back())return sums.back();
        size_t i=std::upper_bound(times.begin(),times.end(),time)-times.begin()-1;
        double u=(time-times[i])/(times[i+1]-times[i]);Sum value{};
        for(size_t k=0;k<value.size();k++)value[k]=sums[i][k]+u*(sums[i+1][k]-sums[i][k]);
        return value;
    }
    Sum range(double first,double last)const{auto a=at(first),b=at(last);for(size_t k=0;k<a.size();k++)b[k]-=a[k];return b;}
};
HarmonicFit localHarmony(const HarmonyIndex&a,const HarmonyIndex&b,double af,double al,double bf,double bl,double loopFirst=0,double loopEnd=0){
    auto x=a.range(af,loopEnd>loopFirst?loopEnd:al),y=b.range(bf,bl);
    if(loopEnd>loopFirst&&al>loopEnd){auto loop=a.range(loopFirst,loopEnd);double repeats=(al-loopEnd)/(loopEnd-loopFirst);for(size_t k=0;k<x.size();k++)x[k]+=loop[k]*repeats;}
    Chroma ca{},cb{};std::copy_n(x.begin(),12,ca.begin());std::copy_n(y.begin(),12,cb.begin());
    auto fit=pitchClassFit(ca,cb);
    fit.confidence*=std::min(x[13]>1e-10?x[12]/x[13]:0.,y[13]>1e-10?y[12]/y[13]:0.);
    fit.confidence*=std::min(clamp(x[14]/std::max(1e-10,al-af)),clamp(y[14]/std::max(1e-10,bl-bf)));
    return fit;
}
Json gainCurve(Frame start,Frame end,bool incoming){
    Json curve=Json::array();for(int k=0;k<=16;k++){double u=smooth(double(k)/16);curve.push_back({start+std::llround(double(end-start)*k/16),incoming?std::sin(u*pi/2):std::cos(u*pi/2)});}return curve;
}
Json voice(const char*id,int deck,double first,Frame end,const TimeMap&map,const Track&t,const Json&gain){
    Json lanes=Json::object();for(auto role:roles)lanes[role]=gain;
    return {{"id",id},{"deck",deck},{"playhead_mode","main"},{"start_frame",0},{"end_frame",end},{"prepare_frames",end},{"source_start_seconds",first},{"source_end_seconds",first+map.sourceAt(double(end)/t.sampleRate)},{"time_map",map.json()},{"original_only",true},{"channels",2},{"gain_db",clamp(-14-t.lufs,-24.,12.)},{"roles",lanes}};
}

// Align measured interior downbeats, not merely two averaged BPM numbers.
// Source positions stay monotonic and the correction vanishes at both joins.
bool followSpan(TimeMap&map,const BarSpan&span,double sourceStart,double duration,double budget){
    map.phaseCorrections={{0,0}};
    for(auto [bar,source]:span.anchors)if(bar>0&&bar<span.bars){
        double ideal=map.baseSourceAt(duration)*bar/span.bars,lo=0,hi=duration;
        for(int n=0;n<42;n++){double mid=(lo+hi)*.5;if(map.baseSourceAt(mid)<ideal)lo=mid;else hi=mid;}
        map.phaseCorrections.emplace_back((lo+hi)*.5,source-sourceStart-ideal);
    }
    map.phaseCorrections.emplace_back(duration,0);
    for(size_t k=1;k<map.phaseCorrections.size();k++){
        auto [start,x]=map.phaseCorrections[k-1];auto [end,y]=map.phaseCorrections[k];double change=1.5*(y-x)/(end-start);
        if(std::min(map.baseRateAt(start),map.baseRateAt(end))+std::min(0.,change)<1/(1+budget)||std::max(map.baseRateAt(start),map.baseRateAt(end))+std::max(0.,change)>1+budget)return false;
    }
    return true;
}
bool vocalOverlap(const Track&a,const Track&b,double startA,double startB,const TimeMap&ma,const TimeMap&mb,double duration){
    auto output=[&](double source,double start,const TimeMap&map){double lo=0,hi=duration;for(int i=0;i<32;i++){double mid=(lo+hi)*.5;if(start+map.sourceAt(mid)<source)lo=mid;else hi=mid;}return (lo+hi)*.5;};
    for(const auto&x:a.voices)if(x.confidence>=.5&&x.end>startA&&x.start<startA+ma.sourceAt(duration))
        for(const auto&y:b.voices)if(y.confidence>=.5&&y.end>startB&&y.start<startB+mb.sourceAt(duration)){
            double begin=std::max(output(x.start,startA,ma),output(y.start,startB,mb));
            double end=std::min(output(x.end,startA,ma),output(y.end,startB,mb));
            if(end-begin>.03)return true;
        }
    return false;
}
struct LoopMaterial {double first=0,end=0,quality=-1;int bars=0;bool subdivide=false;BarSpan span;Json diagnostics;};
std::optional<LoopMaterial> findLoop(const Track&a,const MusicalIndex&index,const BarSpan&span,double start,double tempo,bool build){
    std::optional<LoopMaterial> best;const int bars=span.bars;
    const auto phrases=a.source.value("protected_vocal_phrases",a.source.value("vocal_phrases",Json::array()));
    std::map<int,double> observed(span.anchors.begin(),span.anchors.end());
    for(int length:{4,2,1})for(auto [begin,first]:span.anchors){
        int finish=begin+length,capture=finish;
        if(begin<1||finish+length>bars||!observed.contains(finish))continue;
        if(capture>std::max(3,bars*2/3)||(bars-capture)%length)continue;
        double end=observed.at(finish),period=end-first;
        if(first<=start||cutsPhrase(a,first)||cutsPhrase(a,end))continue;
        double mean=index.energy(first,end),vocal=index.vocal(first,end);if(mean<.002)continue;
        bool containsVoice=false;for(const auto&v:phrases)containsVoice|=v.value("start",0.)<end&&v.value("end",0.)>first;
        // A coherent sung motif may be held whole. Subdivision is reserved for
        // a measured instrumental fragment, never an arbitrary syllable.
        bool canDivide=!containsVoice&&(!index.observed||vocal<.22);
        if(build&&!canDivide)continue;
        double minBar=1e9,maxBar=0;
        // Energy windows need not create transport anchors. Missing a model
        // detection does not turn a complete four-bar motif into eight bars.
        for(int i=0;i<length;i++){double e=index.energy(first+period*i/length,first+period*(i+1)/length);minBar=std::min(minBar,e);maxBar=std::max(maxBar,e);}
        if(minBar<.002||maxBar/minBar>1.8)continue;
        double head=index.energy(first,first+.12),tail=index.energy(end-.12,end);
        double seam=std::abs(std::log((head+.001)/(tail+.001)));
        double headLow=index.foundation(first,first+.2),tailLow=index.foundation(end-.2,end);
        double colour=std::abs(std::log((headLow+1e-5)/(tailLow+1e-5)));
        double ideal=length*MusicalTransport::quartersPerBar(a.meter,a.meterDenominator)*60/tempo;
        double tempoError=std::abs(period/ideal-1);if(tempoError>.025)continue;
        LoopMaterial loop;loop.first=first;loop.end=end;loop.bars=length;loop.subdivide=canDivide&&build;
        loop.span={span.first,span.last,bars,{}};
        for(auto anchor:span.anchors)if(anchor.first<=capture)loop.span.anchors.push_back(anchor);
        for(int at=capture;at<bars;at+=length)for(auto [bar,source]:span.anchors)if(bar>begin&&bar<=finish)
            loop.span.anchors.emplace_back(at+bar-begin,end+(at-capture)/length*period+source-first);
        if(loop.span.anchors.back().first!=bars||std::abs(loop.span.anchors.back().second-span.last)>.12)continue;
        loop.span.anchors.back().second=span.last;
        double stability=clamp(1-std::log(maxBar/minBar)/std::log(1.8));
        loop.quality=.30*stability+.20*clamp(1-seam/2)+.15*clamp(1-colour/3)+.20*(index.observed?1-vocal:.5)+.15*clamp(1-tempoError/.025);
        // Prefer a complete multi-bar motif to a small mechanically repeated hit.
        loop.quality+=.035*length-.008*std::max(0,(bars-capture)/length-4);
        loop.diagnostics={{"candidate_bars",length},{"quality",loop.quality},{"energy_stability",stability},{"seam_level_log_difference",seam},{"low_band_log_difference",colour},{"vocal_activity",vocal},{"instrumental_subdivision",loop.subdivide},{"repeat_count",(bars-capture)/length},{"observed_bar_anchors",loop.span.anchors.size()},{"selection","measured bar ordinals, motif boundaries, level/timbre seam, pulse stability"}};
        if(!best||loop.quality>best->quality)best=std::move(loop);
    }
    return best;
}
struct Introduction {double first=0,end=0,quality=-1;};
std::optional<Introduction> findIntroduction(const Track&b,const MusicalIndex&index,double before,bool synchronized){
    if(!index.observed)return std::nullopt;
    std::optional<Introduction> best;
    for(double end:b.downbeats)for(int bars:{1,2}){
        if(end>before-.25)continue;
        auto span=measuredSpan(b,end,bars,localTempo(b,end));if(!span)continue;
        double first=span->first,length=end-first;
        // A genuine first bar often starts at 20–80 ms. This is an independent
        // preview, not B.main's entrance: discarding it removed the only clean
        // instrumental phrase before a vocal-heavy first verse.
        if(first<0||length<.6||length>6||!covers(b,first,end)||cutsPhrase(b,first)||cutsPhrase(b,end))continue;
        bool voice=false;for(const auto&v:b.source.value("protected_vocal_phrases",Json::array()))voice|=v.value("start",0.)<end&&v.value("end",0.)>first;
        double drums=index.mean(first,end,3),vocals=index.vocal(first,end),other=index.mean(first,end,5);
        // A measured, phase-compatible instrumental beat is useful material
        // for a synchronized pair. Only a pulse change needs a neutral bridge.
        if(voice||vocals>.22||index.energy(first,end)<.002)continue;
        if(!synchronized&&(drums>.30||other<.40))continue;
        if(synchronized&&(std::max(drums,other)<.40||std::abs(length/(bars*quarters(b)*60/b.bpm)-1)>.025))continue;
        double seam=std::abs(std::log((index.energy(first,first+.12)+.001)/(index.energy(end-.12,end)+.001)));
        double quality=.4*(1-vocals)+.3*(1-drums)+.2*clamp(1-seam/2)+.1*clamp(length/3.);
        if(!best||quality>best->quality)best=Introduction{first,end,quality};
    }
    return best;
}
struct Candidate {const DJStyle*style=nullptr;double factor=1,loopFirst=0,loopEnd=0,loopQuality=0,mainJoinSource=0,bDelay=0;Cue a,b;HarmonicFit harmonic;SceneMaterial material;double tempoA=120,tempoB=120,pre=0,post=0,aStart=0,bStart=0,score=0;int bars=0;bool sync=false,hold=false,subdivide=false,lateLead=false;std::optional<Introduction> introduction;std::string family;TimeMap ma,mb;MusicalEvents events;Json loopDecision,scorecard;};
double unsupportedSeconds(const Candidate&c,const MusicalIndex&a,const MusicalIndex&b,int sr){
    const double reference=std::max(a.energy(c.aStart,c.a.time),b.energy(c.b.time,c.b.time+std::min(4.,c.post)));
    const double floor=std::max(.0001,reference*.035),first=double(c.events.first)/sr;
    double gap=0,longest=0;
    // Test the actual readers and their audible start, not just the average
    // energy of two selected intervals. A muted lead cannot fill an empty outro.
    // Held material is checked at its repeated source position.
    for(double t=0;t<c.pre;t+=.125){
        double sa=c.aStart+c.ma.sourceAt(t),sb=c.bStart+c.mb.sourceAt(t);
        if(c.hold&&sa>=c.loopEnd)sa=c.loopFirst+std::fmod(sa-c.loopEnd,c.loopEnd-c.loopFirst);
        double ea=a.energy(sa,sa+.20),eb=0;
        if(t>=first&&t>=c.bDelay){
            if(c.introduction&&sb<c.mainJoinSource){
                double source=c.introduction->first+std::fmod(c.mb.sourceAt(t),c.introduction->end-c.introduction->first);
                eb=b.energy(source,std::min(source+.20,c.introduction->end));
            }else if(!c.lateLead||sb>=c.mainJoinSource)eb=b.energy(sb,sb+.20);
        }
        if(std::hypot(ea,eb)<floor){gap+=.125;longest=std::max(longest,gap);}else gap=0;
    }
    return longest;
}
}
std::optional<Plan> directScene(const Track&a,const Track&b,int ai,int bi,const Policy&policy,uint64_t seed,double minimum,const std::vector<std::string>&history,Json&vetoes,double bLimit){
    vetoes=Json::array();std::vector<Candidate> candidates;
    std::map<std::string,size_t> rejected;
    auto reject=[&](const char*reason){++rejected[reason];};
    const HarmonyIndex harmonyA(a),harmonyB(b);
    const MusicalIndex indexA(a),indexB(b);
    // The same A motif is considered against many B cues and score families.
    // Cache material searches within this immutable pair, including failures.
    // Playback-generation or source revisions cannot leak across this scope.
    std::map<std::tuple<int,int,Frame,bool>,std::optional<LoopMaterial>> loopCache;
    std::map<std::pair<Frame,bool>,std::optional<Introduction>> introCache;
    std::map<std::pair<Frame,int>,std::optional<BarSpan>> spanCacheA,spanCacheB;
    auto localSpan=[&](const Track&t,double end,int bars,double tempo){
        auto&cache=&t==&a?spanCacheA:spanCacheB;
        auto key=std::make_pair(Frame(std::llround(end*t.sampleRate)),bars);
        auto it=cache.find(key);if(it==cache.end())it=cache.emplace(key,measuredSpan(t,end,bars,tempo)).first;
        return it->second;
    };
    auto indexedEnergy=[&](const Track&t,double first,double last){return (&t==&a?indexA:indexB).energy(first,last);};
    const bool reliable=std::min({a.beatConfidence,a.downbeatConfidence,b.beatConfidence,b.downbeatConfidence})>=.55;
    const double solo=std::min(30.,a.duration*.18),incomingSolo=std::min(30.,b.duration*.18),earliest=std::max(minimum+solo,a.duration*.55);
    int maxBars=a.source.value("scene_request",Json::object()).value("pre_bars",18);maxBars=std::clamp(maxBars,2,32);
    const double budget=policy.mode=="dj"?std::max(policy.maxWarp,.18):policy.maxWarp;
    std::string preferred=a.source.value("preferred_transition",std::string("auto"));
    const std::map<std::string,std::string> aliases={{"spectral_bridge_v1","dj_spectral_sweep"},{"phrase_blend_v1","dj_phrase_exchange"},{"echo_bridge_v1","dj_echo_filter"},{"repeat_bridge_v1","dj_dance_build"},{"pulse_bridge_v1","dj_pulse_transition"},{"gentle_overlap_v1","dj_equal_power"}};
    if(aliases.contains(preferred))preferred=aliases.at(preferred);
    auto exits=eventCues(a,indexA,false);const auto entries=eventCues(b,indexB,true);
    const size_t structuralExits=exits.size();const auto adaptiveExits=instrumentalExits(a,indexA,earliest);
    exits.insert(exits.end(),adaptiveExits.begin(),adaptiveExits.end());
    for(size_t outIndex=0;outIndex<exits.size();outIndex++){
        const auto&out=exits[outIndex];
        if(out.time<earliest||out.time>a.duration-.15){reject("exit_position_or_runway");continue;}
        if(!measuredCue(a,out)){reject("exit_not_on_measured_bar");continue;}
        const bool exitCutsVoice=cutsPhrase(a,out.time);
        for(const auto&in:entries){
            if(in.exitOnly||in.time<1||in.time>b.duration*.46||!measuredCue(b,in)){reject("entry_position_or_bar");continue;}
            if(!reliable&&std::min({a.beatConfidence,b.beatConfidence})<.55&&std::min(out.structureConfidence,in.structureConfidence)<.6){reject("insufficient_rhythm_and_structure_evidence");continue;}
            double ta=localTempo(a,out.time),tb=localTempo(b,in.time),factor=1;
            for(double option:{.5,2.})if(std::abs(std::log(tb*option/ta))<std::abs(std::log(tb*factor/ta)))factor=option;
            double ratio=tb*factor/ta;
            const bool compatible=a.meter==b.meter&&a.meterDenominator==b.meterDenominator&&ratio>=1/(1+budget)&&ratio<=1+budget&&policy.mode!="gentle";
            double voiceRisk=std::min(out.vocalBefore,in.vocalBefore);
            // An average half/double-time ratio is only a hypothesis. Missing
            // interior bars or occupied leads can invalidate its realization;
            // still consider a deliberate pulse scene for the same safe cues.
            for(bool sync:{true,false}){
                if(sync&&!compatible)continue;
            for(int bars:{18,16,12,10,8,6,4,2}){
                if(bars>maxBars||(!sync&&bars>4))continue;
                Candidate base;base.a=out;base.b=in;base.tempoA=ta;base.tempoB=tb;base.bars=bars;base.sync=sync;base.factor=factor;
                std::optional<BarSpan> spanA,spanB;
                double rateA=ta,rateB=tb*factor,warp=ratio;
                if(sync){
                    int barsB=int(std::llround(bars/factor));
                    spanA=localSpan(a,out.time,bars,ta);spanB=localSpan(b,in.time,barsB,tb);
                    if(!spanA||!spanB){reject("insufficient_local_measured_phrase");continue;}
                    rateA=bars*quarters(a)*60/(out.time-spanA->first);rateB=bars*quarters(a)*60/(in.time-spanB->first);warp=rateB/rateA;
                    if(warp<1/(1+budget)||warp>1+budget){reject("local_tempo_budget");continue;}
                    base.tempoA=rateA;base.tempoB=rateB/factor;
                    base.pre=bars*quarters(a)*120/(rateA+rateB);
                    base.ma.initialRate=1;base.ma.steadyRate=warp;base.ma.finalRate=warp;
                    base.mb.initialRate=1/warp;base.mb.steadyRate=1;base.mb.finalRate=1;
                }else{
                    // A far tempo change still has a deliberate acceleration
                    // gesture. Only a restricted band of B appears until its
                    // own rhythm takes over; this is not called beatmatching.
                    base.factor=1;base.pre=std::min(12.,bars*quarters(a)*60/ta);
                    if(reliable&&policy.mode!="gentle"){
                        base.ma.initialRate=1;base.ma.steadyRate=std::clamp(tb/ta,1/(1+budget),1+budget);base.ma.finalRate=base.ma.steadyRate;
                        base.mb.initialRate=std::clamp(ta/tb,1/(1+budget),1+budget);base.mb.steadyRate=1;base.mb.finalRate=1;
                    }
                }
                base.pre=std::round(base.pre*a.sampleRate)/a.sampleRate;
                base.post=std::round(std::min(10.,4*quarters(b)*60/base.tempoB)*a.sampleRate)/a.sampleRate;
                if(policy.mode!="gentle"){base.ma.clockStart=base.mb.clockStart=0;base.ma.clockEnd=base.mb.clockEnd=base.pre;}
                base.aStart=out.time-base.ma.sourceAt(base.pre);base.bStart=in.time-base.mb.sourceAt(base.pre);
                if(sync&&(!followSpan(base.ma,*spanA,base.aStart,base.pre,budget)||!followSpan(base.mb,*spanB,base.bStart,base.pre,budget))){reject("interior_bar_phase_or_rate");continue;}
                double endB=base.bStart+base.mb.sourceAt(base.pre+base.post);
                // A short real intro can begin later on the scene clock. The
                // outgoing phrase continues before that; do not reject an early
                // B anchor just because it lacks a whole A-length pre-roll.
                if(!sync&&base.bStart<.1)base.bDelay=outputForSource(base.mb,base.bStart,.1,base.pre);
                if(base.aStart<minimum||base.aStart<a.duration*.48||(base.bStart<.1&&(sync||base.pre-base.bDelay<.7))||endB>b.duration-incomingSolo||(bLimit>=0&&endB>bLimit)){reject("source_position_or_continuation");continue;}
                if(!covers(a,base.aStart,out.time)||!covers(b,std::max(.1,base.bStart),endB)){reject("measured_window_too_short");continue;}
                double ea=indexedEnergy(a,base.aStart,out.time),eb=indexedEnergy(b,in.time,in.time+std::min(base.post,4.));
                if(base.pre>65||(ea>=0&&ea<.0001)||(eb>=0&&eb<.0001)||!sustained(indexB,in.time,std::min(base.post,4.))){reject("silence_or_empty_arrival");continue;}
                base.harmonic=localHarmony(harmonyA,harmonyB,base.aStart,out.time,base.bStart,in.time);
                base.events=musicalEvents(a,b,indexA,indexB,base.aStart,base.bStart,base.ma,base.mb,base.pre,base.post,sync);
                if(base.bDelay>0){base.events.first=std::max(base.events.first,Frame(std::llround((base.bDelay+.04)*a.sampleRate)));base.events.hybrid=std::max(base.events.hybrid,base.events.first);}
                const Candidate unheld=base;
                const bool entryCutsVoice=cutsPhrase(b,base.bStart);
                // A held instrumental phrase removes future A vocals. Judge
                // that actual reader separately, before rejecting a long loop
                // because of voices that it will never play.
                auto prepareHandoff=[&]()->bool{
                if(entryCutsVoice||(base.events.measuredVoice&&base.events.voiceOverlap>std::max(.35,90/std::min(ta,tb)))){
                    // When two complete stereo files cannot share their leads,
                    // an independent, quiet-rhythm instrumental introduction
                    // can prepare a pulse change. B.main keeps its playhead and
                    // remains inaudible until a clean gap before the new attack.
                    if(!base.events.measuredVoice){reject("unverified_vocal_entry");return false;}
                    double join=in.time-.06;
                    for(const auto&v:b.source.value("protected_vocal_phrases",Json::array()))if(v.value("start",0.)<in.time&&v.value("end",0.)>in.time-.06)join=std::min(join,v.value("start",0.)-.04);
                    const double arrivalJoin=join;
                    // A hidden clip need only wait for the next complete B
                    // phrase after A's lead, not necessarily the final drop.
                    for(const auto&v:b.source.value("protected_vocal_phrases",Json::array())){
                        double start=v.value("start",0.)-.04;
                        if(start<=base.bStart+.1||start>=join)continue;
                        double output=outputForSource(base.mb,base.bStart,start,base.pre+base.post);
                        if(output+.20>=double(base.events.aPhrase)/a.sampleRate)join=start;
                    }
                    double outputJoin=outputForSource(base.mb,base.bStart,join,base.pre+base.post);
                    bool unsafeJoin=false;
                    for(const auto&v:b.source.value("protected_vocal_phrases",Json::array()))unsafeJoin|=join>v.value("start",0.)+.02&&join<v.value("end",0.)-.12;
                    for(const auto&w:b.wordRanges)unsafeJoin|=w.confidence>.6&&join>w.start+.015&&join<w.end-.015;
                    if(unsafeJoin||join<=base.bStart+.1||double(base.events.aPhrase)/a.sampleRate>outputJoin+.20){reject("foreground_leads_cannot_handoff");return false;}
                    auto introKey=std::make_pair(Frame(std::llround(join*b.sampleRate)),sync);
                    auto cached=introCache.find(introKey);
                    if(cached==introCache.end())cached=introCache.emplace(introKey,findIntroduction(b,indexB,join,sync)).first;
                    base.introduction=cached->second;
                    // A sparse instrumental intro is an opportunity, not a
                    // universal prerequisite. A complete incoming phrase can
                    // also follow the old lead over a filtered spatial bridge.
                    // It stays hidden before its own clean boundary; no chopped
                    // vocal or fictitious isolated stem is introduced.
                    base.lateLead=!base.introduction;
                    base.mainJoinSource=join;
                    if(join<arrivalJoin-.001)base.events.bLead=std::llround(outputForSource(base.mb,base.bStart,join+.04,base.pre+base.post)*a.sampleRate);
                    base.events.foundation=base.events.arrival;base.events.voiceOverlap=std::max(0.,double(base.events.aPhrase)/a.sampleRate-outputJoin);
                    base.events.first=std::llround(std::min(60/tb,base.pre*.10)*a.sampleRate);
                    base.events.hybrid=std::llround(std::max(double(base.events.first)/a.sampleRate+std::min(120/tb,base.pre*.10),base.pre*.28)*a.sampleRate);
                    // The old lead at B.main's hidden position is never heard.
                    // The incoming phrase detector already includes pickups.
                    // Hiding earlier B must not move its real lead to the drop.
                    if(base.introduction)base.harmonic=localHarmony(harmonyA,harmonyB,base.aStart,out.time,base.introduction->first,base.introduction->end);
                    else{base.events.first=std::llround(outputJoin*a.sampleRate);base.events.hybrid=base.events.first;}
                }
                return true;};
                const bool handoffUsable=prepareHandoff();
                base.material=SceneMaterial::measure(indexA,indexB,base.aStart,out.time,base.introduction?base.introduction->first:base.bStart,base.introduction?base.introduction->end:in.time,base.post,in.time);
                double unheldUnsupported=-1;
                for(const auto&style:djStyles){
                    bool requested=preferred==style.id;
                    if(policy.mode=="gentle"&&style.autoDJ)continue;
                    if(policy.mode!="gentle"&&!style.autoDJ&&!requested)continue;
                    const int longest=style.autoDJ&&style.bars>=8?std::min(32,style.bars*2):style.bars;
                    if(bars>longest||bars<std::max(2,style.bars/2))continue;
                    if(!sync&&style.autoDJ&&std::string(style.shape)!="pulse"&&std::string(style.shape)!="release"&&std::string(style.shape)!="hook")continue;
                    if(sync&&std::string(style.shape)=="pulse")continue;
                    if(style.repeat?entryCutsVoice:(!handoffUsable||exitCutsVoice))continue;
                    Candidate c=style.repeat?unheld:base;c.family=style.id;c.style=&style;
                    if(c.lateLead&&(!style.room||bars>4))continue;
                    if(std::string(style.shape)=="voice"&&(a.voices.empty()||b.voices.empty()||vocalOverlap(a,b,base.aStart,base.bStart,base.ma,base.mb,base.pre)))continue;
                    c.score=.27*out.exitSuitability+.28*in.entrySuitability+.14*out.boundaryStrength+.12*in.boundaryStrength+.10*out.confidence+.10*in.confidence;
                    c.score+=.15*clamp((out.time/a.duration-.55)/.4)+.12*(1-in.time/(b.duration*.46));
                    double audibleDevelopment=c.lateLead?std::max(0.,c.pre-double(c.events.first)/a.sampleRate):c.pre-c.bDelay;
                    c.score+=.10*clamp(in.jump)+(.24*std::min(1.,audibleDevelopment/16)+.08*std::min(1.,audibleDevelopment/36));
                    c.score-=.025*voiceRisk*std::min(1.,c.pre/20);
                    if(!sync&&compatible)c.score-=.12;
                    if(c.introduction)c.score-=.08; // Prefer a continuous native introduction when it fits.
                    if(c.lateLead)c.score-=.20; // A joint arrangement wins whenever usable material exists.
                    if(c.bDelay>0)c.score-=.12*c.bDelay/c.pre;
                    if(bars<style.bars)c.score-=.05;
                    if(bars>style.bars)c.score-=.025; // Extend only when the extra joint phrase earns its place.
                    std::string shape=style.shape;
                    if(requested)c.score+=1.;
                    // DJ mode values a sustained arrangement once all cue, grid,
                    // continuation and stretch checks have passed. A short
                    // accent should not win merely because its first cue is early.
                    if(policy.mode=="dj"&&preferred=="auto"&&!c.lateLead)c.score+=(bars>=8?.16:0)+(bars>=16?.05*c.material.arrivalLift:0);
                    c.score-=.0004*(c.pre+c.post);
                    if(style.repeat){
                        if(!sync||!policy.loops||!spanA||bars<4)continue;
                        auto loopKey=std::make_tuple(out.bar,bars,Frame(std::llround(c.aStart*a.sampleRate)),shape=="build");
                        auto cached=loopCache.find(loopKey);
                        if(cached==loopCache.end())cached=loopCache.emplace(loopKey,findLoop(a,indexA,*spanA,c.aStart,ta,shape=="build")).first;
                        const auto&loop=cached->second;if(!loop)continue;
                        c.loopFirst=loop->first;c.loopEnd=loop->end;c.subdivide=loop->subdivide;c.loopDecision=loop->diagnostics;
                        if(!followSpan(c.ma,loop->span,c.aStart,c.pre,budget))continue;
                        c.material=SceneMaterial::measure(indexA,indexB,c.loopFirst,c.loopEnd,c.bStart,in.time,c.post,in.time);
                        c.hold=true;c.loopQuality=loop->quality;c.score+=.04*loop->quality;
                        c.harmonic=localHarmony(harmonyA,harmonyB,c.aStart,out.time,c.bStart,in.time,c.loopFirst,c.loopEnd);
                        // Future A phrases are not played after the held motif.
                        double endVoice=0;for(const auto&v:a.source.value("protected_vocal_phrases",Json::array()))if(v.value("end",0.)>c.aStart&&v.value("end",0.)<=c.loopEnd)endVoice=std::max(endVoice,outputForSource(c.ma,c.aStart,v.value("end",0.),c.pre));
                        c.events.aPhrase=std::llround(endVoice*a.sampleRate);
                        c.events.voiceOverlap=0;
                        if(c.events.measuredVoice)for(double t=double(c.events.hybrid)/a.sampleRate;t<c.pre;t+=.2){
                            double sa=c.aStart+c.ma.sourceAt(t),sb=c.bStart+c.mb.sourceAt(t);
                            if(sa>=c.loopEnd)sa=c.loopFirst+std::fmod(sa-c.loopEnd,c.loopEnd-c.loopFirst);
                            if(indexA.vocal(sa,sa+.2)>.45&&indexB.vocal(sb,sb+.2)>.45)c.events.voiceOverlap+=.2;
                        }
                    }
                    // Unknown or ambiguous pitch evidence has no influence.
                    c.score+=.08*c.harmonic.confidence*(c.harmonic.fit-.5);
                    if(shape=="expand"&&c.harmonic.fit>.65)c.score+=.10*c.harmonic.confidence;
                    if(shape=="phrase"&&voiceRisk<.3&&c.harmonic.fit>.7)c.score+=.12*c.harmonic.confidence;
                    if(shape=="blur"&&c.harmonic.fit<.55)c.score+=.15*c.harmonic.confidence;
                    double voiceConflict=c.events.measuredVoice?clamp(c.events.voiceOverlap/std::max(1.,c.pre*.5)):0;
                    // Two original stereo files cannot safely play independent
                    // foreground singers. Choose another cue/length instead of
                    // hiding a long duet behind a favourable duration score.
                    if(c.events.measuredVoice&&c.events.voiceOverlap>std::max(.35,90/std::min(ta,tb))){reject("competing_foreground_leads");continue;}
                    if(!c.hold&&unheldUnsupported<0)unheldUnsupported=unsupportedSeconds(c,indexA,indexB,a.sampleRate);
                    double empty=c.hold?unsupportedSeconds(c,indexA,indexB,a.sampleRate):unheldUnsupported;
                    if(empty>std::max(.45,45/ta)){reject("inaudible_gap_before_handoff");continue;}
                    if(c.events.aPhrase>Frame((c.pre-.20)*a.sampleRate)&&!style.delay&&!style.room)continue;
                    double tempoCost=std::abs(std::log(c.ma.steadyRate))/.17;
                    double development=clamp(double(c.events.arrival-c.events.hybrid)/a.sampleRate/8.);
                    double energyFit=clamp(1-std::abs(std::log((eb+.001)/(ea+.001)))/3);
                    c.score+=.12*energyFit+.10*development-.32*voiceConflict-.04*tempoCost;
                    if((shape=="phrase"||shape=="expand"||shape=="voice")&&voiceConflict>.65)continue;
                    c.scorecard={{"cue_and_style_score",c.score-.12*energyFit-.10*development+.32*voiceConflict+.04*tempoCost},{"energy_compatibility",energyFit},{"shared_development",development},{"vocal_conflict",voiceConflict},{"tempo_cost",tempoCost},{"preparation_work",c.introduction?"three bounded stereo voices; zero separation":"two stereo voices; observation cache; zero separation"},{"total",c.score}};
                    c.scorecard["longest_unsupported_seconds"]=empty;
                    double affinity=c.material.affinity(style,c.harmonic.fit,c.harmonic.confidence,c.loopQuality,c.sync);
                    double expression=c.material.expression(style,c.loopQuality,c.sync);
                    // Score what can actually develop before the entrance.
                    // A nominal eight-bar sweep with only 80 ms after A's lead
                    // is not equivalent to an audible two-beat filter release.
                    double releaseRoom=clamp((c.pre-double(c.events.aPhrase)/a.sampleRate)/(120/c.tempoA));
                    double appearance=clamp(audibleDevelopment/std::max(2.,240/c.tempoA));
                    double executableExpression=style.autoDJ?.13*releaseRoom+.09*appearance:0;
                    c.score+=affinity+expression+executableExpression;c.scorecard["material"]=c.material.json();c.scorecard["neural_scene_affinity"]=affinity;c.scorecard["expressive_opportunity"]=expression;
                    c.scorecard["audible_release_runway"]=releaseRoom;c.scorecard["audible_introduction"]=appearance;c.scorecard["executable_expression"]=executableExpression;
                    c.scorecard["musical_quality"]=c.score;c.scorecard["total"]=c.score;
                    candidates.push_back(std::move(c));
                }
            }
            }
        }
    }
    Json search={{"rejections",rejected},{"source_a_cues",a.cues.size()},{"source_b_cues",b.cues.size()},{"observed_exit_candidates",structuralExits},{"observed_entry_candidates",entries.size()},{"minimum_start_seconds",minimum},{"earliest_exit_seconds",earliest},{"measured_grid_reliable",reliable},{"candidate_count",candidates.size()}};
    if(candidates.empty()){vetoes.push_back("NO_MEASURED_EARLY_ENTRY_LATE_EXIT_WITH_RUNWAY");vetoes.push_back(search);return std::nullopt;}
    // Diversity only breaks a near-quality tie. It cannot bypass musical
    // safety or promote a substantially worse source interval. No new inference.
    double bestQuality=std::max_element(candidates.begin(),candidates.end(),[](const auto&x,const auto&y){return x.score<y.score;})->score;
    for(auto&c:candidates){double penalty=sceneRepetition(*c.style,history);c.score-=penalty;
        if(c.scorecard["musical_quality"].get<double>()<bestQuality-.12)c.score-=.12;
        c.scorecard["recent_scene_penalty"]=penalty;c.scorecard["quality_gap"]=bestQuality-c.scorecard["musical_quality"].get<double>();c.scorecard["total"]=c.score;
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto&x,const auto&y){return x.score>y.score;});
    auto compile=[&](const Candidate&c){const int sr=a.sampleRate;const Frame arrival=std::llround(c.pre*sr),end=std::llround((c.pre+c.post)*sr);
    auto events=c.events;events.last=(c.sync&&(c.style->delay||c.style->room))||(!c.sync&&c.style->room)?end-sr/5:arrival;
    Plan p;p.id=a.id+"-"+b.id;p.a=ai;p.b=bi;p.sr=sr;p.seed=seed;p.recipe="I";p.family=c.family;p.seconds=c.pre;p.post=c.post;p.mapA=c.ma;p.mapB=c.mb;
    p.outCue=c.a.time;p.inCue=c.bStart;p.entryAnchor=c.b.time;
    p.attackSource=c.b.attackConfidence>.5&&std::abs(c.b.attack-c.b.time)<.12?c.b.attack:c.b.time;
    p.impact=c.pre;p.bars=c.bars;p.score=c.score;p.harmony=c.harmonic.fit;
    p.blend=true;p.reset=!c.sync;p.exitBoundary=c.a.sectionBoundary;
    p.vocalEnd=double(events.aPhrase)/sr;p.vocalOpen=double(events.bLead)/sr;p.swap=double(events.foundation)/sr;p.outBar=c.a.bar;p.inBar=c.b.bar;p.exitStructure=c.a.exitSuitability;p.entryStructure=c.b.entrySuitability;
    p.tailSeconds=std::min(7.,c.post-.2);p.captureStart=std::max(0.,c.pre-12.);p.captureEnd=c.pre;p.explanation=c.sync?"Multi-stage DJ score; both sources follow the measured pulse, reaching native B at its own entrance":"Filtered tempo approach, deliberate pulse handover and independent spatial tail at B's entrance";
    Json aGain,bGain;Frame fadeStart=events.aRelease;
    aGain=gainCurve(fadeStart,arrival,false);aGain.insert(aGain.begin(),Json::array({0,1.}));
    bGain=gainCurve(0,arrival,true);bGain.push_back({end,1.});
    auto va=voice("A.main",0,c.aStart,arrival,c.ma,a,aGain),vb=voice("B.main",1,c.bStart,end,c.mb,b,bGain);
    if(c.bDelay>0){
        Frame delay=std::llround(c.bDelay*sr);double offset=double(delay)/sr;auto map=c.mb;map.clockOffset=offset;
        double source=c.bStart+c.mb.sourceAt(offset);
        vb["start_frame"]=delay;vb["source_start_seconds"]=source;vb["time_map"]=map.json();vb["prepare_frames"]=end-delay;
        p.inCue=source;p.bStart=offset;p.mapB=map;
    }
    if(c.hold){
        va["playhead_mode"]="rendered_hold";va["source_end_seconds"]=c.loopEnd;
        double beat=60/c.tempoA;
        Json stages=Json::array();
        if(c.subdivide){
            // Hold the complete motif through the shared phrase. A repeater
            // accelerates only in the final two bars, on measured beat units;
            // percentages shortened long builds into a stutter halfway through.
            const double remaining[]={8,4,2,1,.5},periods[]={4,2,1,.5,.25};
            for(int k=0;k<5;k++){
                double source=std::max(c.loopEnd,c.a.time-remaining[k]*beat);
                if(!stages.empty()&&source<=stages.back()[0].get<double>())continue;
                stages.push_back({source,std::min(c.loopEnd-c.loopFirst,periods[k]*beat)});
            }
        }else stages.push_back({c.loopEnd,c.loopEnd-c.loopFirst});
        va["source_loop"]={{"start",c.loopFirst},{"end",c.loopEnd},{"activate",c.loopEnd},{"roll",false},{"full_mix_repeat",true},{"repeat_stages",stages}};
    }
    Json djProgram=djScore(*c.style,va,vb,arrival,end,sr,c.tempoB,c.sync,events.aPhrase,&events);
    Json voices=Json::array({va,vb});
    if(c.introduction||c.lateLead){
        Frame join=std::llround(outputForSource(c.mb,c.bStart,c.mainJoinSource,c.pre+c.post)*sr);
        Frame attack=std::llround(outputForSource(c.mb,c.bStart,p.attackSource,c.pre+c.post)*sr);
        Frame full=std::max(join+sr/200,std::min(join+sr/25,attack-sr/50));
        auto mainGain=gainCurve(join,full,true);mainGain.insert(mainGain.begin(),Json::array({0,0.}));mainGain.push_back({end,1.});
        for(auto role:roles)voices[1]["roles"][role]=mainGain;
        if(c.introduction){
        Frame peak=std::clamp<Frame>(events.hybrid,events.first+1,join-sr/5);
        Frame release=std::max(peak+1,join-sr/25);
        Json gain=Json::array({{0,0.},{events.first,0.},{peak,.60},{release,.60},{full,0.}});
        auto teaser=voice("B.teaser",1,c.introduction->first,full,c.mb,b,gain);
        teaser["playhead_mode"]="rendered_hold";teaser["source_end_seconds"]=c.introduction->end;
        teaser["source_loop"]={{"start",c.introduction->first},{"end",c.introduction->end},{"activate",c.introduction->end},{"roll",false},{"full_mix_repeat",true}};
        voices.push_back(teaser);
        }
    }
    Json segments=Json::array();
    if(c.sync)segments.push_back({{"seconds",0.},{"quarter",0.},{"bpm",c.tempoA},{"end_bpm",c.tempoB*c.factor},{"ramp_seconds",c.pre},{"curve","smoothstep"},{"meter_numerator",a.meter},{"meter_denominator",a.meterDenominator}});
    else {
        double target=c.tempoA*c.ma.steadyRate;
        segments.push_back({{"seconds",0.},{"quarter",0.},{"bpm",c.tempoA},{"end_bpm",target},{"ramp_seconds",c.pre},{"curve","smoothstep"},{"meter_numerator",a.meter},{"meter_denominator",a.meterDenominator}});
        segments.push_back({{"seconds",c.pre},{"quarter",c.pre*(c.tempoA+target)/120},{"bpm",c.tempoB},{"meter_numerator",b.meter},{"meter_denominator",b.meterDenominator}});
    }
    if(c.sync&&c.factor!=1)segments.push_back({{"seconds",c.pre},{"quarter",c.pre*(c.tempoA+c.tempoB*c.factor)/120},{"bpm",c.tempoB},{"meter_numerator",b.meter},{"meter_denominator",b.meterDenominator}});
    MusicalTransport transport;transport.sr=sr;for(const auto&v:segments){MusicalTransport::Segment s{v.at("seconds"),v.at("quarter"),v.at("bpm"),v.at("meter_numerator"),v.at("meter_denominator")};s.endBpm=v.value("end_bpm",s.bpm);s.rampSeconds=v.value("ramp_seconds",0.);s.smoothRamp=v.value("curve","")=="smoothstep";transport.segments.push_back(s);}
    Json anchors=Json::object();auto anchor=[&](const char*name,Frame frame){anchors[name]={{"output_frame",frame},{"seconds",double(frame)/sr},{"scene_quarter",transport.quarterAt(frame)}};};
    anchor("A_prepare_start",0);anchor("B_first_audible",events.first);anchor("hybrid_start",events.hybrid);anchor("foundation_takeover",events.foundation);anchor("A_vocal_phrase_end",events.aPhrase);anchor("A_vocal_release",events.aRelease);anchor("B_lead_entry",events.bLead);anchor("B_full_arrival",arrival);anchor("A_last_audible",events.last);anchor("recovery_complete",end);
    anchor("B_featured_attack",std::llround(outputForSource(c.mb,c.bStart,p.attackSource,c.pre+c.post)*sr));
    Json recovery={{"output_frame",end},{"b_source_seconds",c.bStart+c.mb.sourceAt(double(end)/sr)},{"b_rate",1.},{"full_original",true}};
    if(c.sync)recovery["tempo"]={{"start_seconds",0.},{"complete_seconds",c.pre}};
    p.scene={{"contract_version",1},{"planner","independent-dj-5"},{"family",p.family},{"variant",c.introduction?"independent_instrumental_introduction":c.hold?"phrase_capture_and_repeat":c.sync?"shared_clock":"pulse_change"},{"material_mode","stereo_originals"},{"sample_rate",sr},{"duration_frames",end},{"source_a_start_seconds",c.aStart},{"source_revisions",{a.hash,b.hash}},{"resources",Json::array()},{"voices",voices},{"anchors",anchors},{"transport",{{"bpm",c.tempoA},{"pulse_reset",!c.sync},{"segments",segments}}},{"recovery",recovery},{"effects_enabled",true},{"accent","none"},{"dj_program",djProgram},{"execution",{{"resource_state","ready"},{"vocal_plan_state",a.voices.empty()?"unknown_conservative_stereo":"activity_only"},{"attempted_family",preferred},{"executed_family",p.family},{"candidate_count",candidates.size()},{"separation_model","none"}}},{"decision",{{"local_bpm_a",c.tempoA},{"local_bpm_b",c.tempoB},{"max_recording_rate_change",budget},{"pulse_factor_b",c.factor},{"tempo_mode",c.sync?"shared_phrase_ramp":"filtered_pulse_ramp"},{"reason",p.explanation},{"harmony_scope","sounding full stereo material"},{"voice_boundaries_verified",false},{"outgoing_phrase_observations_used",events.aPhrase>0},{"selection_cost",c.introduction?"two main stereo voices plus short independently held introduction; no separator":"two bounded stereo voices; no ASR; no separator"}}}};
    if(c.introduction)p.scene["introduction_selection"]={{"source_start",c.introduction->first},{"source_end",c.introduction->end},{"quality",c.introduction->quality},{"independent_main_playhead",true}};
    p.scene["musical_events"]=events.json(sr);
    p.scene["decision"]["search"]=search;
    p.scene["decision"]["scorecard"]=c.scorecard;
    p.scene["decision"]["outgoing_cue_kind"]=c.a.kind;
    p.scene["decision"]["outgoing_phrase_anchor_bar"]=c.a.phraseAnchor;
    p.scene["decision"]["adaptive_instrumental_exit"]=c.a.kind=="instrumental_outro";
    if(c.hold)p.scene["loop_selection"]=c.loopDecision;
    p.scene["decision"]["observation_models_a"]=a.source.value("musical_event_models",Json::object());
    p.scene["decision"]["observation_models_b"]=b.source.value("musical_event_models",Json::object());
    p.scene["required_hybrid_bars"]=c.sync&&c.bars>=8&&!c.lateLead?4:0;
    if(c.lateLead){p.scene["variant"]="whole_phrase_spatial_handoff";p.scene["decision"]["main_join_source_seconds"]=c.mainJoinSource;}
    if(c.bDelay>0)p.scene["decision"]["short_intro_start_seconds"]=c.bDelay;
    p.scene["decision"]["harmony_scope"]="local overlapping stereo source intervals, including held material";
    p.scene["decision"]["harmony_method"]="DSP chroma and pitch-class consonance";
    p.scene["decision"]["harmony_model"]="none";
    p.scene["decision"]["harmony_confidence"]=c.harmonic.confidence;
    p.scene["decision"]["harmony_source_a_range"]={c.aStart,c.a.time};
    p.scene["decision"]["harmony_source_b_range"]={std::max(.1,c.bStart),c.b.time};
    if(c.introduction)p.scene["decision"]["harmony_source_b_range"]={c.introduction->first,c.introduction->end};
    if(c.hold)p.scene["decision"]["harmony_repeated_source_range"]={c.loopFirst,c.loopEnd};
    p.scene["phase_alignment"]={{"method","measured interior bars on shared smooth tempo"},{"offset_limit_seconds",.2},{"output_position_tolerance_frames",1}};
    Scene::from(p.scene,false);return p;};
    Plan result=compile(candidates.front());Json alternatives=Json::array();
    // Preserve materially different alternatives, including another arrangement
    // of the same cue pair. Mere seeds of the same score add no useful choice.
    for(size_t i=1;i<candidates.size()&&alternatives.size()<3;i++){
        const auto&c=candidates[i];bool distinct=c.a.time!=result.outCue||c.b.time!=result.entryAnchor||c.bars!=result.bars||c.family!=result.family;
        for(const auto&p:alternatives)if(p["source_out_seconds"]==c.a.time&&p["structural_alignment"]["entry_anchor_seconds"]==c.b.time&&p["family"]==c.family&&p["bars"]==c.bars)distinct=false;
        if(distinct)alternatives.push_back(compile(c).json());
    }
    result.scene["candidate_plans"]=alternatives;return result;
}
}
