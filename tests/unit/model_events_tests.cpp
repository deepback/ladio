#include "model_events.hpp"
#include "ladio/ladio.h"
#include <iostream>
using namespace ladio;
static int checks=0;
#define CHECK(x) do{checks++;if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(0)
int main(){try{
    Json track={{"duration",24.},{"source_hash","fixture-audio"},{"sample_rate",44100},
        {"tempo",120.},{"downbeat_confidence",.9},{"downbeats",Json::array()},
        {"beats",Json::array()},{"key",{{"name","Cm"}}},{"curves",Json::array()}};
    for(double t=0;t<24;t+=.5)track["beats"].push_back(t);
    for(double t=0;t<24;t+=2)track["downbeats"].push_back(t);
    for(double t=0;t<24;t+=.25)track["curves"].push_back({{"time",t},{"rms",.2},{"low_energy",.1}});
    track["cues"]=Json::array({{{"time",16.},{"kind","phrase"},{"vocal",.99}}});
    Json events={{"identity",{{"source_hash","fixture-audio"},{"models",{{"activity","sha-a"},{"structure","sha-b"}}},
        {"frontend","magnitude-slaney-A-dB-24k-v1"}}},{"step_seconds",.05},
        {"frame_center_seconds",1024./24000},{"activity",Json::object()},{"structure",Json::object()}};
    for(auto name:{"vocals","drums","bass","others"})events["activity"][name]=std::vector<double>(480,.1);
    for(auto name:{"phrases","segments","sections"})events["structure"][name]=std::vector<double>(480,.1);
    for(int i=100;i<240;i++)events["activity"]["vocals"][i]=.9;
    auto original=track;applyModelEvents(track,events);
    CHECK(track["musical_event_models"]["enabled"]==true);
    CHECK(track["beats"]==original["beats"]&&track["downbeats"]==original["downbeats"]);
    CHECK(track["key"]==original["key"]);
    CHECK(track["vocal_intervals"].size()==1);
    CHECK(track["vocal_intervals"][0]["start"].get<double>()>4.8);
    CHECK(track["vocal_intervals"][0]["end"].get<double>()>12.);
    CHECK(track["curves"][24]["vocals_activity"]==.9);
    auto reject=[&](Json bad){auto t=original;bool threw=false;try{applyModelEvents(t,bad);}catch(...){threw=true;}CHECK(threw);CHECK(t==original);};
    auto bad=events;bad["identity"]["source_hash"]="other-audio";reject(bad);
    bad=events;bad["activity"]["vocals"].erase(10);reject(bad);
    bad=events;bad["structure"]["phrases"][20]=1.1;reject(bad);
    bad=events;bad["activity"]["vocals"][10]=nullptr;reject(bad);
    bad=events;bad["step_seconds"]=.02;reject(bad);
    bad=events;bad["identity"]["frontend"]="unverified";reject(bad);
    bad=events;bad["identity"]["models"]=Json::object();reject(bad);
    // The new C API reuses the same postprocessing then refreshes cue evidence.
    auto dir=std::filesystem::temp_directory_path()/("ladio-events-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    auto input=dir/"track.json",observations=dir/"events.json",output=dir/"output.json";
    writeJson(input,original);writeJson(observations,events);
    CHECK(ladio_apply_model_events(input.string().c_str(),observations.string().c_str(),output.string().c_str())==1);
    auto merged=readJson(output);CHECK(merged["structure_qc"]["model_assisted"]==true);
    CHECK(merged["key"]==original["key"]);
    CHECK(merged["cues"].size()>0);
    for(const auto&cue:merged["cues"])if(cue.at("time")==16.)CHECK(cue.at("vocal").get<double>()<.2);
    bad=events;bad["identity"]["source_hash"]="wrong";writeJson(observations,bad);
    CHECK(ladio_apply_model_events(input.string().c_str(),observations.string().c_str(),output.string().c_str())==0);
    CHECK(readJson(output)==merged);std::filesystem::remove_all(dir);
    std::cout<<checks<<" model event checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
