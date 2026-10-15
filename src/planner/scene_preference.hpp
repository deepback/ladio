#pragma once
#include "musical_events.hpp"
#include "dj_catalog.hpp"
namespace ladio {
// Descriptor construction is done once per candidate source interval. All
// queries use the prefix index: no inference, audio reads or per-style scans.
struct SceneMaterial {
    double confidence=0,structureConfidence=0,groove=0,texture=0,voiceSpace=0,arrivalLift=0,calm=0,boundary=0,capture=0;
    static SceneMaterial measure(const MusicalIndex&a,const MusicalIndex&b,double af,double al,double bf,double bl,double post,double arrival=-1){
        SceneMaterial m;double window=std::min(3.,post),middle=(bf+bl)*.5;
        if(arrival<0)arrival=bl;
        m.confidence=std::min(a.mean(af,al,11),b.mean(bf,bl+window,11));
        m.structureConfidence=std::min(a.mean(af,al,12),b.mean(bf,bl+window,12));
        double av=a.vocal(af,al),bv=b.vocal(bf,bl),drums=a.mean(af,al,3),bass=a.mean(af,al,4);
        double bd=b.mean(bf,bl,3),ba=b.mean(arrival,arrival+window,3),low=b.mean(arrival,arrival+window,4);
        m.groove=clamp((.75*drums+.25*bass)*(1-av));
        m.texture=clamp(b.mean(bf,bl,5)*(1-bv)*(1-.5*bd));
        m.voiceSpace=clamp(av*(1-bv));
        double early=b.energy(bf,middle),late=b.energy(arrival,arrival+window);
        double energyLift=clamp(std::log((late+.001)/(early+.001))/1.4);
        m.arrivalLift=clamp(.45*energyLift+.35*clamp(ba-bd)+.20*clamp(low-b.mean(bf,middle,4)));
        m.calm=clamp((1-ba)*.65+(1-low)*.35);
        m.boundary=clamp(std::max({b.mean(arrival-.25,arrival+.25,8),b.mean(arrival-.25,arrival+.25,9),b.mean(arrival-.25,arrival+.25,10)}));
        // Is there useful material to catch before the source releases? A
        // written echo with a silent capture window is not an expressive scene.
        double tail=std::max(af,al-std::min(4.,(al-af)*.3));
        m.capture=clamp((.55*a.vocal(tail,al)+.30*a.mean(tail,al,3)+.15*a.mean(tail,al,5))*clamp(a.energy(tail,al)/.015));
        return m;
    }
    double affinity(const DJStyle&s,double harmony,double pitchConfidence,double loopQuality,bool sync)const{
        const std::string shape=s.shape;
        double fit=.5;
        if(shape=="phrase")fit=.25+.35*texture+.25*groove+.15*pitchConfidence*harmony;
        else if(shape=="expand")fit=.25+.40*texture+.20*groove+.15*pitchConfidence*harmony;
        else if(shape=="sweep")fit=.35+.20*texture+.20*arrivalLift+.10*groove;
        else if(shape=="late")fit=.30+.45*arrivalLift+.15*voiceSpace;
        else if(shape=="voice")fit=.20+.65*voiceSpace+.15*texture;
        else if(shape=="soft")fit=.25+.55*calm+.20*texture;
        else if(shape=="rhythm")fit=.20+.65*groove+.15*arrivalLift;
        else if(shape=="hook")fit=.25+.45*voiceSpace+.20*arrivalLift;
        else if(shape=="build")fit=.10+.45*arrivalLift+.25*groove+.20*loopQuality;
        else if(shape=="loop")fit=.10+.65*groove+.25*loopQuality;
        else if(shape=="blur")fit=.15+.25*texture+.25*groove+.20*pitchConfidence*(1-harmony)+.15*loopQuality;
        else if(shape=="release")fit=.30+.40*arrivalLift+.20*calm;
        else if(shape=="pulse")fit=sync?0:.40+.35*arrivalLift+.20*texture;
        // Sparse/noisy evidence cannot manufacture a preference. Structural
        // evidence supports an expressive arrival only where it was measured.
        double emphasis=(shape=="late"||shape=="build"||shape=="hook"||shape=="pulse")?.05*structureConfidence*boundary:0;
        return confidence*.48*(clamp(fit)-.5)+emphasis;
    }
    double expression(const DJStyle&s,double loopQuality,bool sync)const{
        double delay=s.delay&&sync?.09*capture+.035*groove:0;
        double room=s.room?.065*capture+.045*arrivalLift+.035*texture:0;
        double repeat=s.repeat?.085*loopQuality*groove+.045*loopQuality*arrivalLift:0;
        double principal=std::max({delay,room,repeat});
        double value=principal+.15*(delay+room+repeat-principal);
        // Chosen opportunities, not a reward for stacking plugins. DSP cost
        // stays bounded and a plain spectral blend can still win on its cues.
        return confidence*std::min(.22,value);
    }
    Json json()const{return {{"activity_coverage",confidence},{"structure_coverage",structureConfidence},{"outgoing_groove",groove},{"incoming_texture",texture},{"lead_clearance",voiceSpace},{"arrival_lift",arrivalLift},{"incoming_calm",calm},{"arrival_boundary",boundary},{"tail_capture",capture}};}
};
inline double sceneRepetition(const DJStyle&style,const std::vector<std::string>&history){
    double penalty=0,recency=1;
    for(auto i=history.rbegin();i!=history.rend();++i){
        const auto*previous=djStyle(*i);
        if(*i==style.id)penalty+=.070*recency;
        else if(previous&&std::string(previous->shape)==style.shape)penalty+=.040*recency;
        else if(previous&&previous->repeat==style.repeat&&previous->delay==style.delay&&previous->room==style.room)penalty+=.012*recency;
        recency*=.55;
    }
    return std::min(.12,penalty);
}
}
