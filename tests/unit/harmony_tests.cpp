#include "harmony.hpp"
#include <iostream>
#include <limits>
using namespace ladio;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(0)
static Chroma rotate(Chroma c,int n){Chroma r{};for(int k=0;k<12;k++)r[(k+n)%12]=c[k];return r;}
int main(){try{
    Chroma silence{},flat{};flat.fill(1);
    for(auto c:{silence,flat}){auto key=estimateKey(c);CHECK(!key.known);CHECK(key.confidence==0);CHECK(pitchClassFit(c,c).confidence==0);}
    Chroma root{},fifth{},tritone{};root[0]=fifth[7]=tritone[6]=1;
    CHECK(!estimateKey(root).known);
    CHECK(pitchClassFit(root,fifth).fit>pitchClassFit(root,tritone).fit+.5);
    Chroma major{};major[0]=1;major[4]=.8;major[7]=.7;
    Chroma minor{};minor[0]=1;minor[3]=.8;minor[7]=.7;
    for(int k=0;k<12;k++){
        auto ma=estimateKey(rotate(major,k)),mi=estimateKey(rotate(minor,k));
        CHECK(ma.known&&mi.known);CHECK(ma.candidates[0].tonic==k&&ma.candidates[0].mode==0);
        CHECK(mi.candidates[0].tonic==k&&mi.candidates[0].mode==1);
        CHECK(ma.confidence>0&&ma.confidence<=1);CHECK(mi.confidence>0&&mi.confidence<=1);
        CHECK(std::abs(pitchClassFit(rotate(major,k),rotate(minor,k)).fit-pitchClassFit(major,minor).fit)<1e-12);
    }
    CHECK(std::abs(pitchClassFit(major,major).fit-1)<1e-12);
    auto scaled=major;for(auto&v:scaled)v*=.001;
    CHECK(estimateKey(scaled).candidates[0].tonic==estimateKey(major).candidates[0].tonic);
    CHECK(std::abs(estimateKey(scaled).confidence-estimateKey(major).confidence)<1e-12);
    for(double invalid:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
        auto c=major;c[1]=invalid;bool rejected=false;try{estimateKey(c);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
    }
    std::cout<<checks<<" harmonic DSP checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
