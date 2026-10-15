#pragma once
#include "types.hpp"
#include "../structure.hpp"
#include <numeric>

namespace ladio::mobile {
inline std::vector<double> peaks(const std::vector<float>&v){std::vector<double> p;for(int i=0;i<int(v.size());i++)if(std::isfinite(v[i])&&v[i]>0){bool top=true;for(int k=std::max(0,i-3);k<std::min(int(v.size()),i+4);k++)if(v[k]>v[i])top=false;if(top)p.push_back(i);}std::vector<double> result;double centre=0,n=0,last=-2;for(double q:p){if(n&&q-last<=1){n++;centre+=(q-centre)/n;}else{if(n)result.push_back(centre/50);centre=q;n=1;}last=q;}if(n)result.push_back(centre/50);return result;}
inline double confidence(const std::vector<double>&events,const std::vector<float>&logits){double sum=0;for(double t:events){int i=int(std::nearbyint(t*50));float best=-1000;for(int k=std::max(0,i-2);k<std::min(int(logits.size()),i+3);k++)best=std::max(best,logits[k]);sum+=1/(1+std::exp(-best));}return events.empty()?0:sum/events.size();}
inline double gridMedian(std::vector<double> v){if(v.empty())return 0;std::sort(v.begin(),v.end());return v.size()%2?v[v.size()/2]:(v[v.size()/2-1]+v[v.size()/2])*.5;}

// Analyze enough context to observe bars, including on short/slow tracks.
// The planner still independently restricts entrances to the early part and
// exits to the late part. Broader observation does not move those boundaries.
inline std::vector<std::pair<double,double>> gridRanges(double duration,const std::vector<std::string>&scopes,bool expanded=false){
    if(std::find(scopes.begin(),scopes.end(),"full")!=scopes.end())return {};
    double length=std::min(duration,std::max(48.,std::min(96.,duration*.48))+(expanded?48.:0.));
    std::vector<std::pair<double,double>> ranges;
    for(const auto&s:scopes){if(s=="entry")ranges.push_back({0,length});else if(s=="exit")ranges.push_back({duration-length,duration});else throw std::runtime_error("Unknown analysis scope");}
    return ranges;
}

struct MeasuredGrid {
    std::vector<double> beats,downs,rawDowns,intervals;
    std::vector<int> missing;
    double bpm=0,rhythm=0,downConfidence=0,downProbability=0,meterFit=0,consensus=0;
    int measuredBars=0,regularBars=0,longestRegularRun=0,supportedBarRun=0,intervalCount=0,directIntervals=0;
    bool covered(double first,double last)const{
        if(first<0||last<first||last*50>missing.size()-1)return false;
        int a=std::max(0,int(first*50)),b=std::min(int(missing.size()-1),int(std::ceil(last*50)));
        return b>a&&missing[b]==missing[a];
    }
    bool localEvidence()const{return supportedBarRun>=8&&meterFit>=.50&&rhythm>=.70&&downProbability>=.85;}
    bool reliable()const{return directIntervals>=12&&downs.size()>=4&&regularBars>=3&&((consensus>=.6&&rhythm>=.55&&(downConfidence>=.55||(downProbability>=.70&&rhythm>=.70&&consensus>=.75&&longestRegularRun>=4)))||localEvidence());}
    Json diagnostic()const{return {{"beat_count",beats.size()},{"downbeat_count",downs.size()},{"duplicate_downbeats_removed",rawDowns.empty()?0:rawDowns.size()-downs.size()},{"interval_count",intervalCount},{"regular_intervals",intervals.size()},{"direct_intervals",directIntervals},{"regular_bars",regularBars},{"longest_regular_run",longestRegularRun},{"beat_supported_bar_run",supportedBarRun},{"local_phrase_evidence",localEvidence()},{"observed_downbeat_probability",downProbability},{"measured_bars",measuredBars},{"interval_consensus",consensus},{"bpm",bpm},{"beat_confidence",rhythm},{"downbeat_confidence",downConfidence},{"bar_meter_fit",meterFit}};}
};
inline MeasuredGrid measureGrid(const std::vector<float>&beatlog,const std::vector<float>&downlog){
    if(beatlog.size()!=downlog.size())throw std::runtime_error("Mismatched measured grid logits");
    MeasuredGrid g;g.missing.push_back(0);for(float v:beatlog)g.missing.push_back(g.missing.back()+(v==-1000||!std::isfinite(v)));
    g.beats=peaks(beatlog);
    // A downbeat may snap only to a nearby observed beat, not across a quiet
    // outro or an unmeasured gap to an unrelated event several seconds away.
    for(double d:peaks(downlog))if(!g.beats.empty()){
        auto it=std::lower_bound(g.beats.begin(),g.beats.end(),d);double near=it==g.beats.end()?g.beats.back():*it;
        if(it!=g.beats.begin()&&std::abs(*std::prev(it)-d)<std::abs(near-d))near=*std::prev(it);
        if(std::abs(near-d)<=.10)g.downs.push_back(near);
    }
    std::sort(g.downs.begin(),g.downs.end());g.downs.erase(std::unique(g.downs.begin(),g.downs.end()),g.downs.end());
    for(size_t k=1;k<g.beats.size();k++){double d=g.beats[k]-g.beats[k-1];if(d>=.23-1e-8&&d<=1.52+1e-8&&g.covered(g.beats[k-1],g.beats[k]))g.intervals.push_back(d);}
    g.intervalCount=int(g.intervals.size());if(g.intervals.empty())return g;
    // Missing model peaks often interleave quarter and half-time intervals.
    // Their median can pick 70 BPM while measured bar accents support 140.
    // Compare observed period clusters against both beat and bar evidence.
    // Multiples explain missing peaks; they never insert new audio events.
    const auto observed=g.intervals;auto candidates=observed;std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end(),[](double a,double b){return std::abs(a-b)<.005;}),candidates.end());
    double period=0,best=-1;g.intervals.clear();
    for(double candidate:candidates){
        std::vector<double> support;int direct=0;
        for(int iteration=0;iteration<3;iteration++){
            support.clear();direct=0;
            for(double d:observed){int multiple=int(std::nearbyint(d/candidate));if(multiple<1||multiple>4||std::abs(d/multiple/candidate-1)>=.08)continue;support.push_back(d/multiple);direct+=multiple==1;}
            if(support.empty())break;candidate=std::accumulate(support.begin(),support.end(),0.)/support.size();
        }
        if(direct<12)continue;
        int bars=0,regular=0;for(size_t k=1;k<g.downs.size();k++)if(g.covered(g.downs[k-1],g.downs[k])){bars++;regular+=std::abs((g.downs[k]-g.downs[k-1])/candidate-4)<.3;}
        double error=0;for(double d:support)error+=std::abs(d/candidate-1)/support.size();
        double score=.65*regular/std::max(1,bars)+.25*support.size()/observed.size()+.10*direct/observed.size()-.05*error;
        if(score>best){best=score;period=candidate;g.intervals=support;g.directIntervals=direct;}
    }
    if(period<=0)return g;
    g.consensus=double(g.intervals.size())/g.intervalCount;
    // Fit each continuous measured region with its own intercept. An unknown
    // gap must not manufacture tempo/phase evidence between two scope windows.
    auto fit=[&](double slope,bool filter){double xx=0,xy=0;int support=0;size_t first=0;
        while(first<g.beats.size()){
            size_t end=first+1;while(end<g.beats.size()&&g.covered(g.beats[end-1],g.beats[end]))end++;
            double x=0,y=0,x2=0,cross=0,n=0;
            for(size_t k=first;k<end;k++){double q=std::nearbyint((g.beats[k]-g.beats[first])/period),t=g.beats[k]-g.beats[first];if(filter&&std::abs(t-slope*q)>.055)continue;x+=q;y+=t;x2+=q*q;cross+=q*t;n++;}
            if(n>=4){xx+=x2-x*x/n;xy+=cross-x*y/n;support+=int(n);}first=end;
        }
        return support>=13&&xx>1e-9?xy/xx:period;
    };
    double fitted=fit(fit(period,false),true);if(std::isfinite(fitted)&&std::abs(fitted/period-1)<.025)period=fitted;
    g.bpm=60/period;
    // Apply the existing conservative duplicate repair before deciding that
    // the grid needs more neural inference. It requires measured neighbours
    // on both sides and never synthesizes a missing bar.
    Json repaired={{"tempo",g.bpm},{"downbeats",g.downs}};repairDuplicateDownbeats(repaired);
    if(repaired.contains("raw_downbeats")){g.rawDowns=g.downs;g.downs=repaired.at("downbeats").get<std::vector<double>>();}
    int run=0,supportedRun=0;
    for(size_t k=1;k<g.downs.size();k++){
        bool covered=g.covered(g.downs[k-1],g.downs[k]),regular=covered&&std::abs((g.downs[k]-g.downs[k-1])/period-4)<.3;
        if(covered){g.regularBars+=regular;g.measuredBars++;}
        run=regular?run+1:0;g.longestRegularRun=std::max(g.longestRegularRun,run);
        // Adjacent-interval consensus drops when a neural model also marks
        // subdivisions. Verify real interior quarter beats in each real bar,
        // rather than re-running the whole song or inventing a uniform grid.
        int interior=0;
        if(regular)for(int q=1;q<4;q++){
            double at=g.downs[k-1]+(g.downs[k]-g.downs[k-1])*q/4;
            auto it=std::lower_bound(g.beats.begin(),g.beats.end(),at-.065);
            interior+=it!=g.beats.end()&&std::abs(*it-at)<.065;
        }
        supportedRun=regular&&interior>=2?supportedRun+1:0;
        g.supportedBarRun=std::max(g.supportedBarRun,supportedRun);
    }
    g.meterFit=double(g.regularBars)/std::max(1,g.measuredBars);
    std::vector<double> errors;for(double d:g.intervals)errors.push_back(std::abs(d-period));
    g.rhythm=std::min(.99,confidence(g.beats,beatlog)*(1-std::min(.15,gridMedian(errors)/period)));
    g.downProbability=confidence(g.downs,downlog);g.downConfidence=g.downProbability*g.meterFit;return g;
}
}
