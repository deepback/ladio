#include "../../src/analysis/onnx/measured_grid.hpp"
#include <iostream>
using namespace ladio;using namespace ladio::mobile;
static int checks=0;
#define CHECK(x) do{checks++;if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(0)
static std::pair<std::vector<float>,std::vector<float>> logits(double bpm,double duration,double start=0,double finish=-1){
    std::vector<float>b(int(duration*50)+1,-8),d=b;if(finish<0)finish=duration;
    int n=0;for(double t=start+.2;t<finish;t=start+.2+(++n)*60/bpm){int i=int(std::lround(t*50));b[i]=5;if(n%4==0)d[i]=5;}return {b,d};
}
int main(){try{
    // This used to fail solely because a short scope had <32 intervals or
    // <8 downbeats, even with three complete measured bars.
    for(double bpm:{40.,60.,80.,125.,175.,210.,240.}){auto [b,d]=logits(bpm,bpm<80?23:18);auto g=measureGrid(b,d);CHECK(g.reliable());CHECK(std::abs(g.bpm/bpm-1)<.003);CHECK(g.meterFit>.99);}
    auto [b,d]=logits(80,15);auto shortGrid=measureGrid(b,d);CHECK(shortGrid.reliable());CHECK(shortGrid.intervals.size()<32);CHECK(shortGrid.downs.size()<8);
    // Sparse rap detections mix 70 and 140 BPM intervals. Median alone
    // chooses 70 and rejects the bar grid; measured accents resolve 140.
    auto mixed=logits(140,135);int quarter=0;
    for(float&v:mixed.first)if(v>0){if(quarter%4&&quarter%7!=1)v=-8;quarter++;}
    auto resolved=measureGrid(mixed.first,mixed.second);CHECK(resolved.reliable());CHECK(std::abs(resolved.bpm-140)<.03);CHECK(resolved.consensus>.95);CHECK(resolved.beats.size()==peaks(mixed.first).size());
    auto duplicate=logits(125,40);int extra=int(std::lround((.2+4*60./125+.24)*50));duplicate.first[extra]=duplicate.second[extra]=5;auto repaired=measureGrid(duplicate.first,duplicate.second);CHECK(repaired.reliable());CHECK(repaired.rawDowns.size()==repaired.downs.size()+1);CHECK(repaired.meterFit>.99);
    b.assign(5000,-8);d=b;CHECK(!measureGrid(b,d).reliable());
    auto weak=logits(120,25);for(float&v:weak.second)if(v>0)v=.01;CHECK(!measureGrid(weak.first,weak.second).reliable());
    // Local measured bars remain usable when another section misses accents.
    // This does not rescue uniformly weak neural observations above.
    auto local=logits(120,60);int bar=0;for(int i=0;i<int(local.second.size());i++)if(local.second[i]>0){if(i>12*50&&bar%2)local.second[i]=-8;bar++;}
    auto localGrid=measureGrid(local.first,local.second);CHECK(localGrid.downConfidence<.40);CHECK(localGrid.longestRegularRun>=4);CHECK(localGrid.reliable());
    // Extra observed subdivisions can lower adjacent-interval consensus even
    // though eight complete bars contain independently observed quarter beats.
    auto subdivided=logits(120,100);
    for(double at=44.2;at<99.9;at+=.5)subdivided.first[int(std::lround((at+.26)*50))]=5;
    auto localPulse=measureGrid(subdivided.first,subdivided.second);
    CHECK(localPulse.consensus<.6);CHECK(localPulse.supportedBarRun>=8);CHECK(localPulse.reliable());CHECK(std::abs(localPulse.bpm-120)<.05);
    // Strong bar accents alone are insufficient. After a short valid opening,
    // remove every interior quarter; they cannot support a local eight-bar grid.
    auto unsupported=subdivided;
    for(int k=0;k<int(unsupported.first.size());k++)if(unsupported.first[k]>0){
        double time=double(k)/50;
        if(time>10&&std::abs((time-.2)/2-std::round((time-.2)/2))>.02)unsupported.first[k]=-8;
    }
    CHECK(measureGrid(unsupported.first,unsupported.second).supportedBarRun<8);
    auto sparse=logits(120,6);CHECK(!measureGrid(sparse.first,sparse.second).reliable());
    // Strong but irregular/mismatched bar events remain rejected.
    auto wrong=logits(120,25);wrong.second.assign(wrong.second.size(),-8);for(int i=10;i<int(wrong.second.size());i+=73)wrong.second[i]=5;CHECK(!measureGrid(wrong.first,wrong.second).reliable());
    // No nearest-beat snap across silence. It previously created a fake
    // downbeat at the last surviving beat of an unrelated region.
    auto isolated=logits(120,35,0,18);isolated.second[30*50]=5;auto clean=measureGrid(isolated.first,isolated.second);CHECK(clean.downs.back()<18);
    auto split=logits(125,80);for(int k=20*50;k<60*50;k++)split.first[k]=split.second[k]=-1000;auto g=measureGrid(split.first,split.second);CHECK(g.reliable());CHECK(!g.covered(19,61));CHECK(std::abs(g.bpm-125)<.01);
    for(double duration:{20.,36.,80.,180.,360.})for(auto scope:{"entry","exit"}){auto a=gridRanges(duration,{scope}),w=gridRanges(duration,{scope},true);CHECK(a.size()==1&&w.size()==1);CHECK(a[0].first>=0&&a[0].second<=duration);CHECK(a[0].second-a[0].first>=std::min(48.,duration)-1e-8);CHECK(w[0].first<=a[0].first&&w[0].second>=a[0].second);CHECK((w[0].second-w[0].first)-(a[0].second-a[0].first)<=48.0001);}
    CHECK(gridRanges(180,{"full"}).empty());
    std::cout<<checks<<" measured grid checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
