#pragma once
#include "types.hpp"
namespace ladio {
// Backend-independent model observations: identical musical interpretation on
// ONNX and Core ML. Full-track coverage is required; no edge clamping across gaps.
inline void applyModelEvents(Json&analysis,Json observations,bool hit=false,
                             const std::string&provenance="host-supplied Core ML models"){
    const auto&identity=observations.at("identity");
    const bool sourceBound=identity.contains("source_hash") && analysis.contains("source_hash") &&
        identity.at("source_hash").is_string() && !identity.at("source_hash").get<std::string>().empty() &&
        identity.at("source_hash")==analysis.at("source_hash");
    const bool pcmBound=identity.contains("pcm") && analysis.contains("pcm_fnv1a64") &&
        identity.at("pcm")==analysis.at("pcm_fnv1a64");
    if(!sourceBound&&!pcmBound)throw std::runtime_error("Model observations belong to different audio");
    if(!identity.contains("models")||!identity.at("models").is_object()||identity.at("models").empty())
        throw std::runtime_error("Missing model identity");
    if(identity.value("frontend",std::string{})!="magnitude-slaney-A-dB-24k-v1")
        throw std::runtime_error("Unsupported musical event frontend");
    if(observations.at("step_seconds")!=.05||observations.at("frame_center_seconds")!=1024./24000)
        throw std::runtime_error("Unsupported musical event clock");
    const double duration=analysis.at("duration").get<double>();
    if(!std::isfinite(duration)||duration<=0||duration>600)throw std::runtime_error("Invalid event duration");
    size_t count=0;
    for(const auto&group:{std::make_pair("activity",std::vector<const char*>{"vocals","drums","bass","others"}),
                          std::make_pair("structure",std::vector<const char*>{"phrases","segments","sections"})}){
        for(auto name:group.second){
            const auto&row=observations.at(group.first).at(name);
            if(!row.is_array()||row.size()<400||row.size()>12000||(count&&row.size()!=count))
                throw std::runtime_error("Musical event output clock mismatch");
            count=row.size();
            for(const auto&v:row)if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<0||v.get<double>()>1)
                throw std::runtime_error("Invalid musical event probability");
        }
    }
    if(std::abs(double(count)-std::max(400.,std::ceil(duration/.05)))>2)
        throw std::runtime_error("Musical events must cover the complete track");
    const auto&curves=analysis.at("curves");
    if(!curves.is_array())throw std::runtime_error("Missing analysis curves");
    double previousTime=-1;
    for(const auto&c:curves){double t=c.at("time").get<double>();
        if(!std::isfinite(t)||t<0||t>duration||t<previousTime)throw std::runtime_error("Invalid analysis curve clock");
        previousTime=t;
    }
    const double offset=observations.at("frame_center_seconds"),step=observations.at("step_seconds");
    auto sample=[&](const char*group,const char*field,double time){const auto&v=observations.at(group).at(field);int i=std::clamp(int(std::llround((time-offset)/step)),0,int(v.size())-1);return v.at(i).get<double>();};
    for(auto&c:analysis["curves"]){double t=c.at("time");for(auto name:{"vocals","drums","bass","others"})c[std::string(name)+"_activity"]=sample("activity",name,t);c["vocal"]=sample("activity","vocals",t);for(auto name:{"phrases","segments","sections"}){double peak=0;for(double dt:{-.10,-.05,0.,.05,.10})peak=std::max(peak,sample("structure",name,t+dt));c[std::string(name)+"_boundary"]=peak;}}
    Json intervals=Json::array();const auto&v=observations["activity"]["vocals"];int start=-1,last=-1;double confidence=0;
    for(int i=0;i<=int(v.size());i++){
        double x=i<int(v.size())?v[i].get<double>():0.;
        if(x>=.5&&start<0){start=i;confidence=x;}
        if(start>=0){confidence=std::max(confidence,x);if(x>=.28)last=i;
            if(i==int(v.size())||i-last>=4){double first=std::max(0.,start*step+offset-.12),end=std::min(analysis.at("duration").get<double>(),(last+1)*step+offset+.15);if(end-first>.16)intervals.push_back({{"start",first},{"end",end},{"confidence",std::min(.95,confidence)},{"method","supplied activity model; no text recognition"}});start=last=-1;}
        }
    }
    // Presence is not phrase segmentation: sustained singing may remain active
    // across a whole verse. Split protection only at a strong musical boundary
    // on the measured grid, with extra support from an activity valley unless
    // boundary confidence is very high. These remain estimates, never words.
    Json phrases=Json::array();double bpm=analysis.value("tempo",120.),minPhrase=std::max(1.5,120/bpm);
    for(const auto&range:intervals){
        double begin=range.at("start"),finish=range.at("end");
        for(double down:analysis.value("downbeats",std::vector<double>{})){
            if(down-begin<minPhrase||finish-down<minPhrase)continue;
            double boundary=0;for(double dt:{-.15,-.10,-.05,0.,.05,.10,.15})boundary=std::max(boundary,sample("structure","phrases",down+dt));
            double centre=sample("activity","vocals",down),surround=.5*(sample("activity","vocals",down-.4)+sample("activity","vocals",down+.4));
            if(boundary<.65||(boundary<.86&&centre>surround*.96))continue;
            phrases.push_back({{"start",begin},{"end",down-.08},{"confidence",range.at("confidence")},{"boundary_confidence",boundary},{"method","activity plus measured musical phrase; words unverified"}});begin=down+.08;
        }
        if(finish>begin)phrases.push_back({{"start",begin},{"end",finish},{"confidence",range.at("confidence")},{"method","activity range end"}});
    }
    analysis["vocal_intervals"]=intervals;analysis["protected_vocal_phrases"]=phrases;analysis["vocal_phrases"]=phrases;
    analysis["vocal_boundary_version"]="activity-and-musical-phrase-v1";
    analysis["musical_event_models"]={{"enabled",true},{"cache_hit",hit},{"frontend",identity.at("frontend")},{"identity",identity},{"separation",false},{"transcription",false},{"provenance",provenance},{"boundaries","activity ranges, not recognized words"}};
    analysis["model_observations"]=std::move(observations);
}
}
