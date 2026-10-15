#include "audio.hpp"
#include "checksum.hpp"
#include "waveform_join.hpp"
#include "ladio/ladio.h"
#include "../../src/analysis/structure.hpp"
#include "../../src/analysis/onnx/level_match.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <new>
using namespace ladio;
static bool watch=false;static int allocations=0,checks=0;
void*operator new(std::size_t n){if(watch)allocations++;if(void*p=std::malloc(std::max<size_t>(1,n)))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#define CHECK(x) do{checks++;if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(0)
static void duplicateDownbeats(){
    Json t={{"tempo",120.},{"downbeats",std::vector<double>{0,2,4,4.24,6,8}}};
    repairDuplicateDownbeats(t);CHECK(t["downbeats"]==Json(std::vector<double>{0,2,4,6,8}));
    CHECK(t["beat_qc"]["duplicate_downbeats_removed"]==1);
    auto repaired=t;repairDuplicateDownbeats(t);CHECK(t==repaired);
    t["downbeats"]=std::vector<double>{0,2,6,8,10};auto missing=t;repairDuplicateDownbeats(t);CHECK(t==missing);
    t["downbeats"]=std::vector<double>{0,2,3.7,4.3,6,8};auto ambiguous=t;repairDuplicateDownbeats(t);CHECK(t==ambiguous);
}
static void transport(){
    // The roll uses the same quarter pattern for a one-bar and four-bar hold;
    // stage boundaries re-enter the first attack, not an arbitrary clip phase.
    SceneVoice shortLoop;shortLoop.start=0;shortLoop.end=44100*8;shortLoop.rollStart=44100*4;shortLoop.loop=88200;shortLoop.loopPeriod=88200;shortLoop.roll=true;shortLoop.rollPeriods={88200,44100,22050,11025};
    SceneVoice longLoop=shortLoop;longLoop.loop=352800;longLoop.loopPeriod=352800;
    for(Frame f:{Frame(44100*4),Frame(44100*6),Frame(44100*7),Frame(44100*7+22050)}){CHECK(shortLoop.cycleAt(f,1)==longLoop.cycleAt(f,1));CHECK(shortLoop.position(f,1)==shortLoop.offset);CHECK(longLoop.position(f,1)==longLoop.offset);}
    CHECK(MusicalTransport::quartersPerBar(6,8)==3);CHECK(MusicalTransport::quartersPerBar(3,4)==3);
    MusicalTransport t;t.sr=48000;t.segments={{0,0,127.3,6,8,127.3,0},{120,254.6,127.3,6,8,132,12},{132,280.53,132,4,4,132,0}};
    // Long fractional BPM and a linear recovery ramp retain absolute phase;
    // neither per-block truncation nor denominator=4 assumptions are allowed.
    for(Frame f=0;f<48000LL*600;f+=997){double q=t.quarterAt(f);CHECK(abs(t.frameAt(q)-f)<=1);}
    CHECK(abs(t.quarterAt(132*48000)-280.53)<1e-9);
    TimeMap m{.9705,.9705,0,30,10};Frame input=0;
    for(Frame f=0;f<48000*50;){Frame n=std::min<Frame>(f%3?257:509,48000*50-f);Frame target=std::llround(m.sourceAt(double(f+n)/48000)*48000);input+=target-input;f+=n;CHECK(abs(input-m.sourceAt(double(f)/48000)*48000)<=.5+1e-8);}
    TimeMap carry{1.,1.02,2.,10.,4.,1.04};
    CHECK(abs(carry.sourceAt(16.)-(2.02+8*1.02+4*1.03+2*1.04))<1e-10);
    CHECK(abs(carry.rateAt(12.)-1.03)<1e-10);CHECK(TimeMap::from(carry.json()).sourceAt(16.)==carry.sourceAt(16.));
}
static Json runtimeContract(){
    Json roles=Json::object();for(auto key:{"vocals","drums","bass","other","residual"})roles[key]=Json::array({Json::array({0,0.})});
    Json voices=Json::array();auto add=[&](const char*id,int deck,const char*mode,Frame from,Frame to,Frame loop,int stem){auto r=roles;r[stem==0?"vocals":stem==1?"drums":stem==2?"bass":"other"]=Json::array({Json::array({0,1.})});voices.push_back({{"id",id},{"deck",deck},{"playhead_mode",mode},{"start_frame",from},{"end_frame",to},{"source_start_seconds",0.},{"source_end_seconds",6.},{"prepare_frames",44100*6},{"time_map",TimeMap{}.json()},{"loop_frames",loop},{"loop_period_frames",double(loop)+.37*(loop>0)},{"pack_offset_frames",loop?441:0},{"roles",r}});};
    add("A.main",0,"main",0,44100*3,0,0);add("B.main",1,"main",0,44100*6,0,3);add("A.loop",0,"hold",44100,44100*4,22050,1);add("B.teaser",1,"clip",44100*2,44100*3,0,0);
    for(auto key:{"vocals","drums","bass","residual"})voices[1]["roles"][key]=Json::array({Json::array({0,0.}),Json::array({44100*5,0.}),Json::array({44100*5+22050,1.})});
    Json anchors=Json::object();for(auto&[name,seconds]:std::vector<std::pair<std::string,double>>{{"A_prepare_start",0},{"B_first_audible",0},{"hybrid_start",1},{"A_vocal_release",3},{"foundation_takeover",4},{"B_lead_entry",2},{"B_full_arrival",4},{"A_last_audible",4},{"recovery_complete",6}})anchors[name]={{"output_frame",std::llround(seconds*44100)},{"seconds",seconds},{"scene_quarter",seconds*2}};
    return {{"contract_version",1},{"family","tease_reveal"},{"sample_rate",44100},{"duration_frames",44100*6},{"effects_enabled",false},{"accent","none"},{"source_revisions",{"runtime","runtime"}},{"source_a_start_seconds",0.},{"anchors",anchors},{"voices",voices},{"transport",{{"bpm",120.},{"segments",Json::array({Json{{"seconds",0.},{"quarter",0.},{"bpm",120.},{"meter_numerator",4},{"meter_denominator",4}}})}}},{"recovery",{{"output_frame",44100*6},{"b_source_seconds",6.},{"b_rate",1.},{"full_original",true}}}};
}
static void spectralRuntime(const std::filesystem::path&dir){
    constexpr int sr=44100;constexpr Frame frames=sr*6;std::filesystem::create_directories(dir);
    std::ofstream packFile(dir/"spectral.f32",std::ios::binary),originalFile(dir/"spectral-original.f32",std::ios::binary);
    for(Frame f=0;f<frames;f++){float value=float(.01*(std::sin(2*pi*60*f/sr)+std::sin(2*pi*500*f/sr)+std::sin(2*pi*6000*f/sr)));std::array<float,12>v{};v[0]=v[1]=v[8]=v[9]=value;packFile.write(reinterpret_cast<char*>(v.data()),48);originalFile.write(reinterpret_cast<char*>(v.data()),8);}packFile.close();originalFile.close();
    auto scene=runtimeContract();scene["family"]="groove_weave";scene["variant"]="spectral_exchange";scene["source_revisions"]={"spectral","spectral"};scene["resources"]=Json::array();scene["voices"].erase(scene["voices"].begin()+2,scene["voices"].end());
    scene["voices"][0]["end_frame"]=sr*4;scene["voices"][0]["roles"]=scene["voices"][1]["roles"];
    scene["voices"][0]["soft_filters"]=Json::array({Json{{"role","other"},{"stages",2},{"lowpass_curve_hz",{{0,12000.},{sr*4,220.}}},{"wet",{{0,1.},{frames,1.}}}}});
    scene["voices"][1]["soft_filters"]=Json::array({Json{{"role","other"},{"stages",2},{"highpass_curve_hz",{{0,3000.},{sr*4-sr/10,30.}}},{"wet",{{0,1.},{sr*4-sr/10,1.},{sr*4-sr/20,0.}}}}});
    Plan p;p.id="spectral-runtime";p.a=0;p.b=1;p.family="groove_weave";p.recipe="D1";p.seconds=4;p.post=2;p.outCue=4;p.scene=scene;
    Json assets=Json::array();for(auto&v:scene["voices"])assets.push_back({{"id",v["id"]},{"pcm","spectral.f32"},{"channels",12},{"frames",frames},{"fnv1a64",checksumFile(dir/"spectral.f32")}});
    Json t={{"id","spectral"},{"source_hash","spectral"},{"pcm_path","spectral-original.f32"},{"frames",frames},{"sample_rate",sr},{"loudness_lufs",-14},{"tempo",120.}};
    Json session={{"schema_version",1},{"sample_rate",sr},{"loop",false},{"total_frames",frames},{"tracks",{t,t}},{"transitions",Json::array({Json{{"plan",p.json()},{"voice_assets",assets}}})},{"spans",Json::array({Json{{"start_frame",0},{"frames",frames},{"track",0},{"source_frame",0},{"transition",0}}})}};
    writeJson(dir/"spectral-session.json",session);auto s=Session::load(dir/"spectral-session.json");Engine e(s);
    std::vector<float>continuous(size_t(frames),0),old(size_t(frames),0),incoming(size_t(frames),0);std::array<float,512>l{},r{};std::array<float,3072>buses{};
    for(Frame pos=0;pos<frames;){uint32_t n=uint32_t(std::min<Frame>(pos%2?509:127,frames-pos));watch=true;e.render(l.data(),r.data(),n,buses.data());watch=false;for(uint32_t k=0;k<n;k++){continuous[pos+k]=l[k];old[pos+k]=buses[k*6];incoming[pos+k]=buses[k*6+2];CHECK(abs(l[k]-old[pos+k]-incoming[pos+k])<1e-7);}pos+=n;}CHECK(allocations==0);
    auto amplitude=[&](const std::vector<float>&bus,double from,double to,double hz){double re=0,im=0;Frame begin=std::llround(from*sr),end=std::llround(to*sr);for(Frame f=begin;f<end;f++){re+=bus[f]*std::cos(2*pi*hz*f/sr);im+=bus[f]*std::sin(2*pi*hz*f/sr);}return 2*std::hypot(re,im)/(end-begin);};
    CHECK(amplitude(old,3.1,3.7,6000)<amplitude(old,.2,.8,6000)*.05);
    CHECK(amplitude(old,3.1,3.7,60)>amplitude(old,.2,.8,60)*.8);
    CHECK(amplitude(incoming,.2,.8,60)<amplitude(incoming,.2,.8,6000)*.02);
    CHECK(amplitude(incoming,3.1,3.7,60)>amplitude(incoming,.2,.8,60)*100);
    for(Frame f=sr*4+200;f<sr*5;f+=101)CHECK(abs(continuous[f]-s->transitions[0].voices[1]->sample(f,0))<1e-7);
    for(Frame target:{Frame(123),Frame(sr+17),Frame(sr*3+71),Frame(sr*4+9),frames-512}){e.seek(target);watch=true;e.render(l.data(),r.data(),512);watch=false;for(int k=0;k<512;k++)CHECK(abs(l[k]-continuous[target+k])<1e-7);}CHECK(allocations==0);
    auto invalid=scene;invalid["voices"][1]["soft_filters"][0]["wet"]={{0,1.},{frames,1.}};bool refused=false;try{Scene::from(invalid);}catch(...){refused=true;}CHECK(refused);
    invalid=scene;invalid["voices"][1]["soft_filters"][0]["highpass_curve_hz"][0][1]=30000.;refused=false;try{Scene::from(invalid);}catch(...){refused=true;}CHECK(refused);
}
static void runtime(const std::filesystem::path&dir){
    int sr=44100;Frame frames=sr*6;std::filesystem::create_directories(dir);std::ofstream packFile(dir/"pack.f32",std::ios::binary),originalFile(dir/"source.f32",std::ios::binary);
    for(Frame f=0;f<frames;f++){std::array<float,12>p{};for(int ch=0;ch<2;ch++){p[2+ch]=float(.02*std::sin(2*pi*190*f/sr));p[4+ch]=float(f%22050<32?.12:0);p[6+ch]=float(.02*std::sin(2*pi*55*f/sr));p[8+ch]=float(.03*std::sin(2*pi*410*f/sr));p[10+ch]=.001f;p[ch]=p[2+ch]+p[4+ch]+p[6+ch]+p[8+ch]+p[10+ch];}packFile.write(reinterpret_cast<char*>(p.data()),48);originalFile.write(reinterpret_cast<char*>(p.data()),8);}packFile.close();originalFile.close();
    Plan p;p.id="runtime";p.a=0;p.b=1;p.family="tease_reveal";p.recipe="D4";p.seconds=4;p.post=2;p.outCue=4;p.scene=runtimeContract();p.scene["resources"]=Json::array();
    // Exercise the held foundation alongside a subdivided roll in the same
    // block, seek and allocation checks as ordinary playback.
    p.scene["voices"][2]["percussion_roll"]=true;
    p.scene["voices"][2]["roll_start_frame"]=sr*2;
    p.scene["voices"][2]["roll_quarters"]={1.,.5,.25,.125};
    p.scene["voices"][2]["roll_foundation_mix"]=.75;
    Json assets=Json::array();for(auto&v:p.scene["voices"])assets.push_back({{"id",v["id"]},{"pcm","pack.f32"},{"channels",12},{"frames",frames},{"fnv1a64",checksumFile(dir/"pack.f32")}});
    Json t={{"id","runtime"},{"source_hash","runtime"},{"pcm_path","source.f32"},{"frames",frames},{"sample_rate",sr},{"loudness_lufs",-14},{"tempo",120.}};
    Json j={{"schema_version",1},{"sample_rate",sr},{"loop",false},{"total_frames",frames},{"tracks",{t,t}},{"transitions",Json::array({Json{{"plan",p.json()},{"voice_assets",assets}}})},{"spans",Json::array({Json{{"start_frame",0},{"frames",frames},{"track",0},{"source_frame",0},{"transition",0}}})}};writeJson(dir/"session.json",j);
    // A phrase can end before its dry fade. Its last sounding syllable must
    // seed a tail even when the old release-based capture would see silence.
    for(double phraseEnd:{2.65,4.65}){
    double fadeEnd=std::ceil(phraseEnd);Frame fadeFrame=std::llround(fadeEnd*sr);
    std::ofstream tailFile(dir/"tail-pack.f32",std::ios::binary);
    for(Frame f=0;f<frames;f++){std::array<float,12>v{};if(f>sr*(phraseEnd-.35)&&f<sr*(phraseEnd-.025))for(int ch=0;ch<2;ch++)v[ch]=v[2+ch]=float(.05*std::sin(2*pi*700*f/sr));tailFile.write(reinterpret_cast<char*>(v.data()),48);}tailFile.close();
    auto tailSession=j;auto&tailScene=tailSession["transitions"][0]["plan"]["compiled_scene"];
    tailScene["effects_enabled"]=true;tailScene["accent"]="reverb";
    tailScene["anchors"]["A_vocal_phrase_end"]={{"output_frame",std::llround(phraseEnd*sr)},{"seconds",phraseEnd},{"scene_quarter",phraseEnd*2}};
    for(const char*name:{"A_vocal_release","A_last_audible"}){double seconds=std::max(fadeEnd,name==std::string("A_last_audible")?4.:0.);tailScene["anchors"][name]={{"output_frame",std::llround(seconds*sr)},{"seconds",seconds},{"scene_quarter",seconds*2}};}
    tailScene["voices"][0]["end_frame"]=fadeFrame;
    tailScene["voices"][0]["roles"]["vocals"]=Json::array({Json::array({0,1.}),Json::array({std::llround(phraseEnd*sr),1.}),Json::array({fadeFrame,0.})});
    tailSession["transitions"][0]["voice_assets"][0]["pcm"]="tail-pack.f32";tailSession["transitions"][0]["voice_assets"][0]["fnv1a64"]=checksumFile(dir/"tail-pack.f32");writeJson(dir/"tail.json",tailSession);
    auto ts=Session::load(dir/"tail.json");Engine te(ts);std::array<float,1024>tl{},tr{};std::array<float,6144>tb{};std::array<float,512>afterFade{};double tailEnergy=0;Frame lateTail=0;
    for(Frame pos=0;pos<frames;pos+=512){int n=int(std::min<Frame>(512,frames-pos));watch=true;te.render(tl.data(),tr.data(),n,tb.data());watch=false;for(int k=0;k<n;k++){Frame f=pos+k;float fx=tb[k*6+4];if(f>=fadeFrame&&f<fadeFrame+sr*.25)tailEnergy+=fx*fx;if(f>=std::llround(sr*(fadeEnd+.8))&&fx!=0)lateTail++;if(f>=fadeFrame+17&&f<fadeFrame+17+512)afterFade[f-fadeFrame-17]=tl[k];}}
    CHECK(tailEnergy>1e-7);CHECK(lateTail==0);te.seek(fadeFrame+17);te.render(tl.data(),tr.data(),512);for(int k=0;k<512;k++)CHECK(abs(tl[k]-afterFade[k])<1e-7);
    auto invalidTail=tailScene;invalidTail["anchors"]["A_vocal_phrase_end"]["seconds"]=fadeEnd+.1;bool invalidCapture=false;try{Scene::from(invalidTail);}catch(...){invalidCapture=true;}CHECK(invalidCapture);
    }
    auto s=Session::load(dir/"session.json");Engine e(s);std::vector<float>continuous(size_t(frames)*2);std::array<float,1024>l{},r{};std::array<float,6144>buses{};std::array<float,20480>roles{};
    for(Frame pos=0;pos<frames;){uint32_t n=uint32_t(std::min<Frame>(pos%2?509:127,frames-pos));watch=true;e.render(l.data(),r.data(),n,buses.data(),roles.data());watch=false;for(uint32_t k=0;k<n;k++){continuous[(pos+k)*2]=l[k];continuous[(pos+k)*2+1]=r[k];for(int ch=0;ch<2;ch++){double sum=buses[k*6+ch]+buses[k*6+2+ch]+buses[k*6+4+ch];CHECK(abs(sum-(ch?r[k]:l[k]))<1e-7);}CHECK(std::isfinite(l[k]));}pos+=n;}CHECK(allocations==0);
    for(Frame target:{Frame(123),Frame(sr+17),Frame(sr*2+71),Frame(sr*3+1),Frame(sr*4+9),frames-512}){e.seek(target);e.render(l.data(),r.data(),512);for(int k=0;k<512;k++)CHECK(abs(l[k]-continuous[(target+k)*2])<1e-7);}
    auto main=s->transitions.front().scene->voices[1];CHECK(main.position(sr*4,0)==sr*4);auto loop=s->transitions.front().scene->voices[2];CHECK(loop.position(sr*3,1)<loop.offset+loop.loop+1);
    auto slip=loop;slip.mode="slip";CHECK(slip.position(sr*3,1)==loop.position(sr*3,1));CHECK(slip.virtualSourceSeconds(sr*3,sr)>loop.virtualSourceSeconds(sr*3,sr)+1);
    LadioPlayer*player=ladio_player_create((dir/"session.json").string().c_str(),8192);CHECK(player);
    for(Frame target:{Frame(123),Frame(sr+17),Frame(sr*2+71),Frame(sr*3+1),Frame(sr*4+9),frames-512}){
        CHECK(ladio_player_pause(player));CHECK(ladio_player_seek(player,target));CHECK(ladio_player_play(player));uint32_t got=0;
        auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!got&&std::chrono::steady_clock::now()<readyDeadline){got=ladio_player_pull(player,l.data(),r.data(),127);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        CHECK(got==127);for(int k=0;k<127;k++)CHECK(abs(l[k]-continuous[(target+k)*2])<1e-7);
        CHECK(ladio_player_pause(player));CHECK(ladio_player_pull(player,l.data(),r.data(),127)==0);
    }
    CHECK(ladio_player_skip(player,1));CHECK(ladio_player_skip(player,-1));ladio_player_destroy(player);
    auto invalid=p.scene;invalid["contract_version"]=2;bool rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
    invalid=p.scene;invalid["voices"][0]["time_map"]["steady_rate"]=1.12;rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
    invalid=p.scene;invalid["voices"][2]["pack_offset_frames"]=frames;rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
    invalid=p.scene;invalid["anchors"]["hybrid_start"]["seconds"]=5.;rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
    auto tooLong=j;tooLong["transitions"][0]["plan"]["compiled_scene"]["voices"][0]["source_start_seconds"]=5.;tooLong["transitions"][0]["plan"]["compiled_scene"]["voices"][0]["source_end_seconds"]=11.;writeJson(dir/"extent.json",tooLong);rejected=false;try{Session::load(dir/"extent.json");}catch(...){rejected=true;}CHECK(rejected);
    auto changed=j;changed["transitions"][0]["voice_assets"][0]["fnv1a64"]="wrong";writeJson(dir/"corrupt.json",changed);rejected=false;try{Session::load(dir/"corrupt.json");}catch(...){rejected=true;}CHECK(rejected);
    auto pending=j;pending["transitions"][0]["plan"]["compiled_scene"]["execution"]={{"resource_state","draft"},{"vocal_plan_state","pending_window_analysis"}};writeJson(dir/"pending.json",pending);rejected=false;try{Session::load(dir/"pending.json");}catch(...){rejected=true;}CHECK(rejected);
    auto refused=j;refused["transitions"][0]["plan"]["compiled_scene"]["preview_critic"]={{"passed",false}};writeJson(dir/"refused.json",refused);rejected=false;try{Session::load(dir/"refused.json");}catch(...){rejected=true;}CHECK(rejected);
    std::ofstream nanFile(dir/"nan.f32",std::ios::binary);std::array<float,2>nan{{std::numeric_limits<float>::quiet_NaN(),0}};nanFile.write(reinterpret_cast<char*>(nan.data()),8);nanFile.close();rejected=false;try{PCMFile nonfinite(dir/"nan.f32");}catch(...){rejected=true;}CHECK(rejected);
}
static void nativeRejoin(const std::filesystem::path&dir){
    std::filesystem::create_directories(dir);
    constexpr int sr=44100;constexpr Frame frames=sr,first=sr/4,length=sr/2;
    auto native=[](Frame f,int c){return float(.18*std::sin(2*pi*173*f/sr)+.07*std::sin(2*pi*(c?739:521)*f/sr));};
    // Same music at a different waveform phase, unrelated phase and identical
    // input cover the three correlations that a fixed fade cannot distinguish.
    for(int mode=0;mode<3;mode++){
        auto file=dir/("rejoin-"+std::to_string(mode)+".f32");
        std::vector<float> source(frames*2);
        for(Frame f=0;f<frames;f++)for(int c=0;c<2;c++)source[f*2+c]=mode==0?native(f,c):mode==1?native(f-91,c):float(.18*std::sin(2*pi*229*f/sr)+.07*std::sin(2*pi*(c?997:631)*f/sr));
        {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(source.data()),source.size()*4);}
        rejoinNative(file,2,sr,frames,first,length,native);PCMFile joined(file);
        CHECK(joined.sample(first-1,0)==source[(first-1)*2]);CHECK(joined.sample(first,0)==source[first*2]);
        CHECK(joined.sample(first+length-1,0)==native(first+length-1,0));
        for(Frame block=first+sr/10;block<first+length-sr/10;block+=sr/25){
            double got=0,reference=0;for(Frame f=block;f<block+sr/25;f++)for(int c=0;c<2;c++){double x=joined.sample(f,c);CHECK(std::isfinite(x));got+=x*x;reference+=std::pow(native(f,c),2);if(mode==0)CHECK(std::abs(x-source[f*2+c])<1e-7);}
            CHECK(got/reference>.80);CHECK(got/reference<1.23);
        }
    }
    // Exercise Signalsmith's real rate automation and its first return to B,
    // not only the later host-stream splice. Native B must be sample-exact.
    auto original=dir/"warp-source.f32";{std::ofstream out(original,std::ios::binary);for(Frame f=0;f<sr*6;f++)for(int c=0;c<2;c++){float x=native(f,c);out.write(reinterpret_cast<const char*>(&x),4);}}
    Source source;source.original=std::make_shared<PCMFile>(original);TimeMap map{1.10,1.10,0,1.5,.5};auto file=dir/"warped.f32";
    prepareOriginalVoice(source,.5,map,4.,sr,file,{},-1,-1,2.1);PCMFile warped(file);
    for(Frame f=sr*3;f<sr*4;f+=97)CHECK(warped.sample(f,0)==native(std::llround((.5+map.sourceAt(double(f)/sr))*sr),0));
    for(Frame block=sr*2;block<sr*2.7;block+=sr/25){double energy=0;for(Frame f=block;f<block+sr/25;f++)energy+=std::pow(warped.sample(f,0),2);CHECK(energy/(sr/25)>.012);CHECK(energy/(sr/25)<.025);}
    TimeMap clock;clock.initialRate=.85;clock.steadyRate=clock.finalRate=1;clock.clockStart=0;clock.clockEnd=2.5;
    auto dropFile=dir/"native-drop.f32";prepareOriginalVoice(source,.5,clock,4.,sr,dropFile,{},2.5,.5+clock.sourceAt(2.5),2.40);PCMFile drop(dropFile);
    for(Frame f=std::llround(2.5*sr);f<sr*3;f+=31)CHECK(drop.sample(f,0)==native(std::llround((.5+clock.sourceAt(double(f)/sr))*sr),0));
}
static void boundedComponentsAndLevel(const std::filesystem::path&dir){
    std::filesystem::create_directories(dir);
    Json v={{"start_frame",0},{"end_frame",44100*8},{"source_start_seconds",30.},{"source_end_seconds",38.},{"time_map",TimeMap{}.json()},{"roles",Json::object()}};
    for(auto role:{"vocals","drums","bass","other","residual"})v["roles"][role]=Json::array({Json::array({0,1})});
    CHECK(roleSourceRanges(v,44100).empty());v["roles"]["vocals"]={{0,1},{44100*5,1},{44100*6,0},{44100*8,0}};
    auto ranges=roleSourceRanges(v,44100);CHECK(ranges.size()==1);CHECK(ranges[0]["role"]=="vocals");CHECK(ranges[0]["start"]==35.);CHECK(ranges[0]["end"]==38.);
    v["roles"]["vocals"]={{0,1}};v["soft_filters"]=Json::array({Json{{"role","drums"},{"lowpass_hz",900},{"wet",Json::array({Json::array({0,0}),Json::array({44100*4,0}),Json::array({44100*5,1}),Json::array({44100*6,0}),Json::array({44100*8,0})})}}});
    ranges=roleSourceRanges(v,44100);CHECK(ranges.size()==1);CHECK(ranges[0]["start"]==34.);CHECK(ranges[0]["end"]==36.);
    Source missing;std::vector<float>samples(44100*2,.1);auto file=dir/"level-guard.f32";{std::ofstream out(file,std::ios::binary);for(int k=0;k<44100;k++){samples[k*2]=float(.3*std::sin(2*pi*100*k/44100));samples[k*2+1]=samples[k*2];}samples[22050*2]=1.7;out.write(reinterpret_cast<char*>(samples.data()),samples.size()*4);}
    auto pcm=std::make_shared<PCMFile>(file);missing.original=pcm;missing.windows.push_back({0,44100,{pcm,{},{},{}}});CHECK(missing.coveredRole(0,44100,0));CHECK(!missing.coveredRole(0,44100,1));
    mobile::ScenePeakGuard guard(*pcm,44100);double peak=0;for(int k=0;k<44100;k++)for(int ch=0;ch<2;ch++)peak=std::max(peak,std::abs(pcm->sample(k,ch)*guard.at(k)));CHECK(peak<=.990001);CHECK(guard.at(0)>.999);CHECK(guard.at(44100-1)>.998);CHECK(guard.at(22050-100)<1.);
    CHECK(abs(mobile::originalLevelGain(-8,-10,0,2,8)-db(6))<1e-12);CHECK(abs(mobile::originalLevelGain(-8,-10,8,2,8)-db(4))<1e-12);
}
int main(){auto dir=std::filesystem::temp_directory_path()/("automix-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{nativeRejoin(dir);boundedComponentsAndLevel(dir);duplicateDownbeats();transport();runtime(dir);spectralRuntime(dir);std::filesystem::remove_all(dir);std::cout<<checks<<" shared scene/DSP checks passed\n";return 0;}catch(const std::exception&e){std::filesystem::remove_all(dir);std::cerr<<e.what()<<'\n';return 1;}}
