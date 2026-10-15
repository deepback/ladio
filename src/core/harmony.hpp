#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ladio {
using Chroma = std::array<double,12>;
struct ChromaStats {double sum=0,contrast=0;int pitches=0;};
inline ChromaStats chromaStats(const Chroma&c){
    ChromaStats s;double square=0,maximum=0;
    for(double v:c){if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid chroma");s.sum+=v;square+=v*v;maximum=std::max(maximum,v);}
    if(square>1e-20){s.contrast=std::sqrt(std::max(0.,1-s.sum*s.sum/(12*square)));for(double v:c)s.pitches+=v>maximum*.15;}
    return s;
}
struct KeyCandidate {double score=0;int tonic=0,mode=0;};
struct KeyEstimate {std::array<KeyCandidate,24> candidates{};double confidence=0;bool known=false;};
inline KeyEstimate estimateKey(const Chroma&c){
    // Krumhansl-Kessler profiles; only 24 * 12 scalar comparisons, no network.
    constexpr double profiles[2][12]={{6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88},{6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17}};
    KeyEstimate result;auto stats=chromaStats(c);double cm=stats.sum/12,ce=0;for(double v:c)ce+=(v-cm)*(v-cm);
    for(int mode=0;mode<2;mode++){
        double pm=std::accumulate(profiles[mode],profiles[mode]+12,0.)/12;
        for(int tonic=0;tonic<12;tonic++){
            double dot=0,pe=0;for(int k=0;k<12;k++){double v=profiles[mode][(k-tonic+12)%12]-pm;dot+=(c[k]-cm)*v;pe+=v*v;}
            result.candidates[mode*12+tonic]={ce>1e-20?dot/std::sqrt(ce*pe):0,tonic,mode};
        }
    }
    std::sort(result.candidates.begin(),result.candidates.end(),[](auto a,auto b){if(a.score!=b.score)return a.score>b.score;if(a.tonic!=b.tonic)return a.tonic<b.tonic;return a.mode<b.mode;});
    auto best=result.candidates[0];
    result.known=stats.sum>1e-10&&stats.contrast>.1&&stats.pitches>=3&&best.score>.3;
    if(result.known)result.confidence=std::clamp((best.score-result.candidates[1].score)*4,0.,1.)*std::max(0.,best.score)*std::min(1.,stats.contrast/.4);
    return result;
}
struct HarmonicFit {double fit=.5,confidence=0;};
inline HarmonicFit pitchClassFit(const Chroma&a,const Chroma&b){
    auto x=chromaStats(a),y=chromaStats(b);HarmonicFit result;
    if(x.sum<1e-10||y.sum<1e-10||std::min(x.contrast,y.contrast)<.1)return result;
    // A bass note and a melody need consonant intervals, not identical bins.
    // Normalize by each distribution's self-consonance so an identical chord
    // remains unity. This is a heuristic fit, not a probability of good sound.
    constexpr double consonance[]={1.,.10,.32,.75,.72,.82,.04,.82,.72,.75,.32,.10};
    double ab=0,aa=0,bb=0;
    for(int i=0;i<12;i++)for(int j=0;j<12;j++){double w=consonance[(i-j+12)%12];ab+=a[i]*b[j]*w;aa+=a[i]*a[j]*w;bb+=b[i]*b[j]*w;}
    result.fit=std::clamp(ab/std::sqrt(aa*bb),0.,1.);
    result.confidence=std::min(1.,std::min(x.contrast,y.contrast)/.4);
    return result;
}
}
