#pragma once
#include "types.hpp"
#include <numeric>
namespace ladio {
// Neural peak detection can report two nearby candidates for one bar. Drop
// only a duplicate bracketed by independently measured neighbouring bars.
// Never synthesize a missing downbeat or smooth away a real tempo change.
inline void repairDuplicateDownbeats(Json&t){
    auto down=t.value("downbeats",std::vector<double>{});
    const double bpm=t.value("tempo",0.);
    if(down.size()<4||bpm<40||t.value("meter",4)!=4||t.value("meter_denominator",4)!=4)return;
    const double bar=240/bpm;auto original=down;
    for(size_t first=1;first+2<down.size();){
        size_t end=first+1;
        while(end<down.size()&&down[end]-down[end-1]<bar*.45)end++;
        if(end==first+1||end==down.size()){first=end;continue;}
        size_t selected=first;double best=1e9,second=1e9;
        for(size_t i=first;i<end;i++){
            double error=std::max(std::abs((down[i]-down[first-1])/bar-1),std::abs((down[end]-down[i])/bar-1));
            if(error<best){second=best;best=error;selected=i;}else second=std::min(second,error);
        }
        if(best<=.055&&second-best>=.05){
            const double keep=down[selected];down.erase(down.begin()+first,down.begin()+end);down.insert(down.begin()+first,keep);first++;
        }else first=end;
    }
    if(down!=original){
        if(!t.contains("raw_downbeats"))t["raw_downbeats"]=original;
        t["downbeats"]=down;
        t["beat_qc"]["duplicate_downbeats_removed"]=t["raw_downbeats"].size()-down.size();
        t["beat_qc"]["duplicate_rule"]="measured neighbours within 5.5 percent; no inserted events";
    }
}
// Bar-synchronous change and recurrence analysis. A periodic tick alone is not
// evidence of a section ending. This is shared by native A1 and the Mac ML adapter.
inline void analyzeStructure(Json &t) {
    repairDuplicateDownbeats(t);
    constexpr size_t dims=19;
    using Feature=std::array<double,dims>;
    const auto down=t.value("downbeats",std::vector<double>{});
    const auto &curves=t.at("curves");
    t["structure_version"]="musical-events-recurrence-v5";
    t["sections"]=Json::array();
    if(down.size()<10||curves.size()<10){t["cues"]=Json::array();return;}
    const double duration=t.at("duration"),dc=t.value("downbeat_confidence",0.);
    const int sr=t.at("sample_rate");
    std::vector<Feature> features(down.size());
    std::vector<double> energy(down.size()),low(down.size()),vocal(down.size(),.5);
    std::vector<double> phraseEvidence(down.size()),segmentEvidence(down.size()),sectionEvidence(down.size());
    size_t cursor=0,nearCursor=0;bool modelAssisted=t.value("musical_event_models",Json::object()).value("enabled",false);
    for(size_t i=0;i<down.size();i++) {
        double end=i+1<down.size()?down[i+1]:duration;size_t n=0;
        while(cursor<curves.size()&&curves[cursor].at("time").get<double>()<down[i])cursor++;
        for(;cursor<curves.size()&&curves[cursor].at("time").get<double>()<end;cursor++){
            const auto &c=curves[cursor];
            auto &f=features[i];f[0]+=std::log(std::max(1e-7,c.value("rms",0.)));
            f[1]+=std::log(std::max(1e-7,c.value("low_energy",c.value("bass_rms",0.))));
            f[2]+=std::log1p(c.value("onset",0.));
            if(c.contains("chroma"))for(size_t k=0;k<12;k++)f[k+3]+=c["chroma"][k].get<double>();
            if(modelAssisted){int k=15;for(auto key:{"vocals_activity","drums_activity","bass_activity","others_activity"})f[k++]+=c.value(key,0.);}
            energy[i]+=c.value("rms",0.);low[i]+=c.value("low_energy",0.);vocal[i]+=c.value("vocal",.5);n++;
        }
        if(n){for(auto &v:features[i])v/=n;energy[i]/=n;low[i]/=n;vocal[i]=(vocal[i]-.5)/n;}
        // A boundary observation proposes a region. Snap it only to an actual
        // measured downbeat nearby; never manufacture a beat or an exact word.
        double tolerance=std::min(.40,(end-down[i])*.20);
        while(nearCursor<curves.size()&&curves[nearCursor].at("time").get<double>()<down[i]-tolerance)nearCursor++;
        for(size_t k=nearCursor;k<curves.size()&&curves[k].at("time").get<double>()<=down[i]+tolerance;k++){
            const auto&c=curves[k];phraseEvidence[i]=std::max(phraseEvidence[i],c.value("phrases_boundary",0.));
            segmentEvidence[i]=std::max(segmentEvidence[i],c.value("segments_boundary",0.));sectionEvidence[i]=std::max(sectionEvidence[i],c.value("sections_boundary",0.));
        }
    }
    // Normalize feature families, without allowing noisy low-variance dimensions
    // to dominate a stable groove. Chroma carries less weight than instrumentation.
    for(size_t k=0;k<dims;k++){
        double mean=0,var=0;for(auto &f:features)mean+=f[k]/features.size();
        for(auto &f:features)var+=(f[k]-mean)*(f[k]-mean)/features.size();
        double scale=std::max(k<3?.25:.12,std::sqrt(var));
        for(auto &f:features)f[k]=(f[k]-mean)/scale*(k<3?1.:k>=15?.65:.35);
    }
    auto mean=[&](size_t begin,size_t end){Feature f{};end=std::min(end,features.size());
        for(size_t i=begin;i<end;i++)for(size_t k=0;k<dims;k++)f[k]+=features[i][k]/std::max<size_t>(1,end-begin);return f;};
    auto distance=[](const Feature&a,const Feature&b){double d=0;for(size_t k=0;k<dims;k++)d+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(d/3.);};
    std::vector<double> novelty(down.size());
    for(size_t i=2;i+2<down.size();i++)novelty[i]=distance(mean(i-2,i),mean(i,i+2));
    auto sorted=novelty;std::sort(sorted.begin(),sorted.end());
    double threshold=std::max(.32,sorted[size_t(sorted.size()*.70)]*1.20);
    std::vector<size_t> ranked;
    for(size_t i=4;i+4<down.size();i++){
        bool peak=novelty[i]>=threshold||(modelAssisted&&sectionEvidence[i]>=.55&&novelty[i]>=threshold*.35);
        for(size_t j=i-2;j<=i+2;j++)if(j!=i&&novelty[j]+sectionEvidence[j]*.65>novelty[i]+sectionEvidence[i]*.65)peak=false;
        if(peak)ranked.push_back(i);
    }
    // The broad contrast locates a change region; an adjacent-bar contrast
    // pins it to the actual change instead of one bar after a gradual envelope.
    for(auto &i:ranked){size_t selected=i;double best=-1;
        for(size_t j=i-1;j<=i+1;j++){double d=distance(mean(j-1,j),mean(j,j+1));if(d>best){best=d;selected=j;}}
        if(best>.20)i=selected;
    }
    std::sort(ranked.begin(),ranked.end());ranked.erase(std::unique(ranked.begin(),ranked.end()),ranked.end());
    std::stable_sort(ranked.begin(),ranked.end(),[&](auto a,auto b){return novelty[a]>novelty[b];});
    std::vector<size_t> boundaries{0};
    for(size_t i:ranked){bool separated=true;for(size_t j:boundaries)if(std::abs(int(i)-int(j))<4)separated=false;if(separated)boundaries.push_back(i);}
    std::sort(boundaries.begin(),boundaries.end());boundaries.push_back(down.size());
    std::vector<Feature> sectionFeatures;std::vector<int> labels;
    for(size_t n=0;n+1<boundaries.size();n++){
        auto f=mean(boundaries[n],boundaries[n+1]);int label=int(sectionFeatures.size());
        for(size_t j=0;j<sectionFeatures.size();j++)if(distance(f,sectionFeatures[j])<.48){label=labels[j];break;}
        sectionFeatures.push_back(f);labels.push_back(label);
        t["sections"].push_back({{"start",down[boundaries[n]]},{"end",boundaries[n+1]<down.size()?down[boundaries[n+1]]:duration},
            {"start_bar",boundaries[n]},{"end_bar",boundaries[n+1]},{"recurrence_group",label},
            {"boundary_strength",clamp(std::max(novelty[boundaries[n]]/1.2,sectionEvidence[boundaries[n]]))},{"grid_confidence",dc}});
    }
    const auto old=t.value("cues",Json::array());Json cues=Json::array();
    for(size_t i=0;i+2<down.size();i++){
        if(down[i]<.25||down[i]>duration-4)continue;
        auto anchor=*std::prev(std::upper_bound(boundaries.begin(),boundaries.end(),i));
        bool boundary=i!=0&&std::binary_search(boundaries.begin(),boundaries.end(),i);
        bool phrase=(i-anchor)%8==0;
        bool observedPhrase=phraseEvidence[i]>=.45||segmentEvidence[i]>=.50;
        if(observedPhrase){for(size_t j=i>1?i-2:0;j<std::min(i+3,down.size());j++)if(phraseEvidence[j]+segmentEvidence[j]>phraseEvidence[i]+segmentEvidence[i]+1e-6)observedPhrase=false;}
        phrase|=observedPhrase;
        if(!boundary&&!phrase)continue;
        size_t before=i>=2?i-2:0,after=std::min(i+2,down.size());
        double eb=0,ea=0,lb=0,la=0;for(size_t j=before;j<i;j++){eb+=energy[j]/std::max<size_t>(1,i-before);lb+=low[j]/std::max<size_t>(1,i-before);}
        for(size_t j=i;j<after;j++){ea+=energy[j]/(after-i);la+=low[j]/(after-i);}
        double fall=clamp((eb-ea)/std::max(.01,eb)),rise=clamp((ea-eb)/std::max(.01,eb));
        bool impact=boundary&&rise>.16&&la>lb*1.20;
        Json c={{"time",down[i]},{"frame",std::llround(down[i]*sr)},{"bar",i},{"confidence",dc},{"kind",impact?"impact":"phrase"},
            {"energy",ea},{"energy_jump",ea-eb},{"novelty",clamp(novelty[i]/1.2)},{"vocal",vocal[i]},
            {"vocal_before",i?vocal[i-1]:.5},{"vocal_after",vocal[i]},
            {"section_boundary",boundary},{"phrase_bars",8},{"phrase_anchor_bar",anchor},
            {"boundary_strength",boundary?clamp(std::max(.45+novelty[i]/2,sectionEvidence[i])):std::max(phraseEvidence[i],segmentEvidence[i])*.65},
            {"exit_suitability",boundary?clamp(.50+novelty[i]/3+fall*.3):clamp(.15+fall*.6+.25*phraseEvidence[i])},
            {"entry_suitability",clamp((boundary?.45:.15)+rise*.4+.20*segmentEvidence[i])},
            {"boundary_observations",{{"phrase",phraseEvidence[i]},{"segment",segmentEvidence[i]},{"section",sectionEvidence[i]}}},
            {"energy_fall",fall},{"structure_confidence",dc}};
        // Keep measured transient, harmony and stem descriptors at this downbeat.
        for(const auto&o:old)if(abs(o.value("time",-99.)-down[i])<.08){
            for(auto key:{"key","rhythm","timbre","attack_seconds","attack_confidence","drum_jump","bass_jump","vocal","vocal_before","vocal_after"})if(o.contains(key))c[key]=o.at(key);
            if(o.value("kind","")=="impact"&&boundary){c["kind"]="impact";c["energy_jump"]=std::max(c["energy_jump"].get<double>(),o.value("energy_jump",0.));}
            break;
        }
        cues.push_back(c);
    }
    // A structural change may land just before the last word of a line.
    // Carry that line through and exit on the next measured downbeat (at most
    // two bars later), rather than cutting it or inventing a fractional beat.
    Json deferred=Json::array();
    const auto vocalPhrases=t.value("vocal_phrases",Json::array());
    const auto words=t.value("words",Json::array());
    auto clean=[&](double time){
        for(const auto &v:vocalPhrases)if(v.value("confidence",0.)>.35&&v.at("start").get<double>()<time-.08&&v.at("end").get<double>()>time+.04)return false;
        for(const auto &w:words)if(w.value("probability",0.)>.45&&w.at("start").get<double>()<time+.04&&w.at("end").get<double>()>time-.06)return false;
        return true;
    };
    for(const auto &c:cues)if(c.at("section_boundary").get<bool>()&&!clean(c.at("time"))){
        size_t bar=c.at("bar");
        for(size_t delay=1;delay<=2&&bar+delay<down.size();delay++)if(down[bar+delay]<duration-4&&clean(down[bar+delay])){
            Json d=c;d["time"]=down[bar+delay];d["frame"]=std::llround(down[bar+delay]*sr);d["bar"]=bar+delay;
            d["boundary_origin_bar"]=bar;d["boundary_delay_bars"]=delay;d["exit_only"]=true;d["exit_suitability"]=std::max(0.,c.at("exit_suitability").get<double>()-.06*delay);d["kind"]="phrase";
            d.erase("attack_seconds");d.erase("attack_confidence");deferred.push_back(d);break;
        }
    }
    for(auto &c:deferred)cues.push_back(c);
    std::sort(cues.begin(),cues.end(),[](const auto&a,const auto&b){return a.at("time").template get<double>()<b.at("time").template get<double>();});
    t["cues"]=cues;
    t["structure_qc"]={{"measured_section_changes",boundaries.size()-2},{"bar_count",down.size()},
        {"grid_confidence",dc},{"method","bar-synchronous energy, instrumentation, chroma change and recurrence"},
        {"semantic_verse_chorus_labels",false},{"model_assisted",modelAssisted},{"periodic_ticks_are_not_section_boundaries",true}};
}
}
