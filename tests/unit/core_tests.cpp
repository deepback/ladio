#include "types.hpp"
#include "audio.hpp"
#include "scene.hpp"
#include "../../src/analysis/structure.hpp"
#include "ladio/ladio.h"
#include <chrono>
#include <iostream>
#include <random>
#include <cstdlib>
#include <new>
using namespace ladio;
static thread_local bool watchingCallback=false;
static thread_local uint64_t callbackAllocations=0;
void*operator new(std::size_t n){if(watchingCallback)callbackAllocations++;if(void*p=std::malloc(std::max<std::size_t>(n,1)))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
static uint32_t pull(LadioPlayer*p,float*l,float*r,uint32_t n){watchingCallback=true;auto got=ladio_player_pull(p,l,r,n);watchingCallback=false;return got;}
static int checks=0;
#define REQUIRE(x) do{checks++;if(!(x))throw std::runtime_error(std::string("Failed: ")+ #x +" at "+std::to_string(__LINE__));}while(0)
static void maps(){TimeMap m{1,1.04,2,16,8};double previous=0;for(int k=1;k<30000;k++){double t=k*.001;double now=m.sourceAt(t);REQUIRE(now>previous);REQUIRE(abs((now-previous)/.001-m.rateAt(t-.0005))<1e-6);previous=now;}REQUIRE(abs(m.sourceAt(2)-2.04)<1e-10);REQUIRE(abs(m.sourceAt(24)-(2.04+14*1.04+8*1.02))<1e-10);}
static std::filesystem::path fixture(){
    auto dir=std::filesystem::temp_directory_path()/("automix-cpp-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);int sr=44100;Frame frames=sr*2;
    std::vector<float>pcm(size_t(frames)*2);for(Frame i=0;i<frames;i++){pcm[i*2]=float(.15*std::sin(2*pi*440*i/sr));pcm[i*2+1]=float(.12*std::sin(2*pi*660*i/sr));}
    std::ofstream out(dir/"original.f32",std::ios::binary);out.write(reinterpret_cast<char*>(pcm.data()),pcm.size()*4);out.close();
    Json t={{"id","fixture"},{"source_hash","fixture"},{"pcm_path","original.f32"},{"frames",frames},{"sample_rate",sr},{"loudness_lufs",-14},{"tempo",120}};
    Json j={{"schema_version",1},{"sample_rate",sr},{"loop",false},{"total_frames",frames*3},{"tracks",{t,t,t}},{"transitions",Json::array()},{"spans",{Json{{"start_frame",0},{"frames",frames},{"track",0},{"source_frame",0}},Json{{"start_frame",frames},{"frames",frames},{"track",1},{"source_frame",0}},Json{{"start_frame",frames*2},{"frames",frames},{"track",2},{"source_frame",0}}}}};writeJson(dir/"session.json",j);return dir;
}
static void rendering(const std::filesystem::path&dir){auto s=Session::load(dir/"session.json");Engine e(s);std::array<float,2050>l{},r{};Frame pos=0;for(uint32_t n:{1u,7u,509u,1024u,2048u,13u}){e.render(l.data(),r.data(),n);for(uint32_t k=0;k<n;k++){REQUIRE(abs(l[k]-.15*std::sin(2*pi*440*(pos+k)/44100))<1e-7);REQUIRE(abs(r[k]-.12*std::sin(2*pi*660*(pos+k)/44100))<1e-7);}pos+=n;}
    e.seek(2303);e.render(l.data(),r.data(),1);REQUIRE(abs(l[0]-.15*std::sin(2*pi*440*2303/44100))<1e-7);
    e.seek(s->frames);e.render(l.data(),r.data(),1024);for(int i=0;i<1024;i++)REQUIRE(l[i]==0&&r[i]==0);
    auto broken=s->manifest;broken["spans"][0]["frames"]=123;writeJson(dir/"broken.json",broken);bool failed=false;try{Session::load(dir/"broken.json");}catch(...){failed=true;}REQUIRE(failed);
}
static void commands(const std::filesystem::path&dir){LadioPlayer*p=ladio_player_create((dir/"session.json").string().c_str(),8192);REQUIRE(p);std::this_thread::sleep_for(std::chrono::milliseconds(30));std::array<float,8192>l{},r{};
    REQUIRE(pull(p,l.data(),r.data(),512)==0);REQUIRE(ladio_player_snapshot(p).output_frame==0);REQUIRE(ladio_player_play(p));REQUIRE(pull(p,l.data(),r.data(),512)==512);auto before=ladio_player_snapshot(p);REQUIRE(before.output_frame==512);
    REQUIRE(ladio_player_pause(p));REQUIRE(pull(p,l.data(),r.data(),1024)==0);REQUIRE(ladio_player_snapshot(p).output_frame==512);
    REQUIRE(ladio_player_seek(p,22050));REQUIRE(ladio_player_snapshot(p).output_frame==22050);std::this_thread::sleep_for(std::chrono::milliseconds(10));REQUIRE(ladio_player_play(p));
    uint32_t got=0;for(int n=0;n<20&&!got;n++){got=pull(p,l.data(),r.data(),127);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(3));}
    REQUIRE(got==127);auto snap=ladio_player_snapshot(p);REQUIRE(snap.generation>before.generation);REQUIRE(snap.output_frame==22050+127);REQUIRE(abs(l[0]-.15*std::sin(2*pi*440*22050/44100))<1e-7);
    REQUIRE(ladio_player_seek(p,100));REQUIRE(ladio_player_seek(p,300));REQUIRE(ladio_player_seek(p,500));std::this_thread::sleep_for(std::chrono::milliseconds(8));
    got=0;for(int n=0;n<20&&!got;n++){got=pull(p,l.data(),r.data(),73);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(3));}REQUIRE(got==73);REQUIRE(ladio_player_snapshot(p).output_frame==573);
    REQUIRE(ladio_player_skip(p,1));REQUIRE(ladio_player_skip(p,1));std::this_thread::sleep_for(std::chrono::milliseconds(8));got=0;for(int n=0;n<20&&!got;n++){got=pull(p,l.data(),r.data(),83);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(3));}REQUIRE(got==83);REQUIRE(ladio_player_snapshot(p).track_a==2);REQUIRE(callbackAllocations==0);
    ladio_player_destroy(p);
}
static void catalog(const std::filesystem::path&dir){
    std::vector<float>pack(size_t(44100)*10);for(int i=0;i<44100;i++)for(int c=0;c<2;c++){float original=float(.14*std::sin(2*pi*440*i/44100)),voice=float(.04*std::sin(2*pi*220*i/44100)),bass=float(.025*std::sin(2*pi*90*i/44100)),drum=i%22050<100?.02f:0;pack[i*10+c]=original;pack[i*10+2+c]=voice;pack[i*10+4+c]=drum;pack[i*10+6+c]=bass;pack[i*10+8+c]=original-voice-bass-drum;REQUIRE(abs(pack[i*10+c]-pack[i*10+2+c]-pack[i*10+4+c]-pack[i*10+6+c]-pack[i*10+8+c])<1e-7);}
    auto path=dir/"pack.f32";std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(pack.data()),pack.size()*4);out.close();auto pcm=std::make_shared<PCMFile>(path,10);
    std::array<float,512>l{},r{};for(auto family:families){auto session=Session::load(dir/"session.json");Plan p;p.family=family;p.recipe=recipeFor(family);p.id=family;p.a=0;p.b=1;p.sr=44100;p.outCue=1.5;p.seconds=.4;p.lead=.1;p.post=.4;p.blend=isOverlay(family);p.bStart=p.blend?.1:.47;p.impact=.5;p.captureStart=.2;p.captureEnd=.4;p.vocalEnd=.25;p.vocalOpen=.35;p.swap=.3;p.useStems=true;p.echo=std::string(family).find("echo")!=std::string::npos;p.roll=std::string(family)=="loop_roll";p.stop=std::string(family)=="tape_stop";p.reset=std::string(family)=="energy_reset";
        Compiled c{p,pcm,pcm};session->transitions={c};session->spans={Span{0,p.frames(),0,0,0}};session->frames=p.frames();Engine e(session);for(Frame pos=0;pos<session->frames;pos+=512){e.render(l.data(),r.data(),512);for(int k=0;k<512;k++){REQUIRE(std::isfinite(l[k])&&std::isfinite(r[k]));REQUIRE(abs(l[k])<=.813);REQUIRE(abs(r[k])<=.813);}}}
}
static void delays(){Delay d(44100);std::array<float,2>in{.2f,.1f};double total=0;for(int n=0;n<44100*6;n++){auto y=d.process(n==0?in:std::array<float,2>{},11025,.55f,1.f);REQUIRE(std::isfinite(y[0])&&std::isfinite(y[1]));REQUIRE(abs(y[0])<.3);total+=abs(y[0]);}REQUIRE(total>0);d.clear();auto y=d.process({},11025,.55f,1);REQUIRE(y[0]==0);}
static void musicalRegressions(const std::filesystem::path&dir){
    constexpr int sr=44100;std::vector<float>voice(size_t(sr*3)*10),silent(voice.size());
    for(int i=0;i<sr*3;i++)for(int ch=0;ch<2;ch++){float v=float(.20*std::sin(2*pi*80*i/sr));voice[i*10+ch]=voice[i*10+2+ch]=v;}
    auto save=[&](const char*name,const std::vector<float>&v){auto path=dir/name;std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size()*4);f.close();return std::make_shared<PCMFile>(path,10);};
    auto va=save("voice.f32",voice),zero=save("silent.f32",silent);
    auto s=Session::load(dir/"session.json");Plan p;p.family="vocal_handoff";p.recipe="T16";p.a=0;p.b=1;p.seconds=2;p.post=.2;p.blend=true;p.useStems=true;p.vocalEnd=1.8;p.vocalOpen=2;p.swap=1;p.reverbWet=0;p.echo=false;p.bassRamp=1;
    s->transitions={{p,va,zero}};s->spans={{0,p.frames(),0,0,0}};s->frames=p.frames();Engine e(s);std::array<float,512>l{},r{};
    for(Frame pos=0;pos<s->frames;pos+=512){e.render(l.data(),r.data(),512);for(int k=0;k<512;k++)if(pos+k>sr&&pos+k<sr*1.7){REQUIRE(abs(l[k]-voice[(pos+k)*10])<2e-7);REQUIRE(abs(l[k])<=.200001);}}
    // A 3 ms kick transient must survive the incoming microfade in its entirety.
    int attack=1323;for(int i=attack;i<attack+132;i++)for(int ch=0;ch<2;ch++){silent[i*10+ch]=silent[i*10+4+ch]=.50f;}
    auto kick=save("kick.f32",silent);p.family="drop_switch";p.recipe="T09";p.blend=false;p.useStems=false;p.seconds=.4;p.bStart=.37;p.impact=.4;p.vocalEnd=.3;p.post=.2;
    s->transitions={{p,zero,kick}};s->spans={{0,p.frames(),0,0,0}};s->frames=p.frames();Engine drop(s);
    Frame first=std::llround(p.impact*sr);for(Frame pos=0;pos<s->frames;pos+=512){drop.render(l.data(),r.data(),512);for(int k=0;k<512;k++)if(pos+k>=first&&pos+k<first+131)REQUIRE(abs(l[k]-.50)<1e-6);}
    // A stale reopen timestamp cannot remove the new vocal after handover.
    p.family="bass_swap";p.recipe="T04";p.blend=true;p.useStems=true;p.seconds=.4;p.bStart=0;p.impact=.2;p.vocalEnd=.1;p.vocalOpen=.9;p.swap=.1;p.post=.4;
    s->transitions={{p,zero,va}};s->spans={{0,p.frames(),0,0,0}};s->frames=p.frames();Engine returnVoice(s);
    for(Frame pos=0;pos<s->frames;pos+=512){returnVoice.render(l.data(),r.data(),512);for(int k=0;k<512;k++)if(pos+k>=sr*.45&&pos+k<sr*.75)REQUIRE(abs(l[k]-voice[(pos+k)*10])<2e-7);}
    // Bass swapping must preserve the incoming kick even halfway through a blend.
    std::fill(silent.begin(),silent.end(),0);int middle=sr*3/10;
    for(int i=middle;i<middle+132;i++)for(int ch=0;ch<2;ch++)silent[i*10+ch]=silent[i*10+4+ch]=.50f;
    auto middleKick=save("middle-kick.f32",silent);
    p.seconds=.6;p.impact=.3;p.swap=.2;p.vocalOpen=.1;p.post=.2;p.echo=false;p.roll=false;
    s->transitions={{p,zero,middleKick}};s->spans={{0,p.frames(),0,0,0}};s->frames=p.frames();Engine bassImpact(s);
    for(Frame pos=0;pos<s->frames;pos+=512){bassImpact.render(l.data(),r.data(),512);for(int k=0;k<512;k++)if(pos+k>=middle&&pos+k<middle+132)REQUIRE(abs(l[k]-.50)<1e-6);}
    Reverb reverb(sr);double late=0;for(int i=0;i<sr*3;i++){auto y=reverb.process(i<sr/10?std::array<float,2>{float(.2*std::sin(2*pi*800*i/sr)),.1f}:std::array<float,2>{});REQUIRE(std::isfinite(y[0]));if(i>sr/2&&i<sr)late+=y[0]*y[0];}REQUIRE(late>1e-5);
    // Seeking into a prepared FX tail reconstructs the same delay/reverb state.
    p.family="echo_hard_drop";p.recipe="T20+T08";p.seconds=.8;p.bStart=.77;p.impact=.8;p.post=2;p.useStems=true;p.echo=true;p.captureStart=.3;p.captureEnd=.72;p.captureStem=0;p.reverbWet=.4;p.tailSeconds=2.5;p.vocalEnd=.7;
    s->transitions={{p,va,zero}};s->spans={{0,p.frames(),0,0,0}};s->frames=p.frames();Engine continuous(s),sought(s);
    Frame target=sr*14/10;for(Frame pos=0;pos<target;){auto n=uint32_t(std::min<Frame>(512,target-pos));continuous.render(l.data(),r.data(),n);pos+=n;}
    std::array<float,512>sl{},srch{};continuous.render(l.data(),r.data(),512);sought.seek(target);sought.render(sl.data(),srch.data(),512);
    double tailEnergy=0;for(int i=0;i<512;i++){REQUIRE(abs(l[i]-sl[i])<1e-7);REQUIRE(abs(r[i]-srch[i])<1e-7);tailEnergy+=l[i]*l[i];}REQUIRE(tailEnergy>1e-8);
    // A window's source origin must not be mistaken for its local frame zero.
    Source windowed;windowed.original=std::make_shared<PCMFile>(dir/"original.f32");Source::Window window;window.start=12345;window.end=12345+windowed.original->frames();window.stems.fill(windowed.original);windowed.windows.push_back(window);
    REQUIRE(windowed.value(12345+17,2)==windowed.original->sample(17,0));REQUIRE(windowed.value(12344,2)==0);REQUIRE(!windowed.covered(0,22050));REQUIRE(windowed.covered(12345,22050));
}
static void preparationLifecycle(const std::filesystem::path&dir){
    auto j=readJson(dir/"session.json");writeJson(dir/"library.json",{{"schema_version",1},{"tracks",j["tracks"]}});
    auto destination=dir/"cancelled";bool stopped=false;
    try{prepareLibrary(dir/"library.json",destination,policyFor("off"),12,[]{return true;});}catch(const std::exception&e){stopped=std::string(e.what())=="Preparation cancelled";}
    REQUIRE(stopped);REQUIRE(!std::filesystem::exists(destination/"session.json"));
    auto job=ladio_preparation_start((dir/"library.json").string().c_str(),(dir/"async").string().c_str(),"off",12);REQUIRE(job);ladio_preparation_cancel(job);
    for(int i=0;i<100&&ladio_preparation_state(job)==LADIO_PREPARING;i++)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto state=ladio_preparation_state(job);REQUIRE(state==LADIO_CANCELLED||state==LADIO_READY);if(state==LADIO_CANCELLED)REQUIRE(!std::filesystem::exists(dir/"async/session.json"));ladio_preparation_destroy(job);
    prepareLibrary(dir/"library.json",dir/"plain",policyFor("off"),12);
    REQUIRE(Session::load(dir/"plain/session.json")->frames>0);REQUIRE(Session::load(dir/"plain/session.json")->transitions.empty());
    bool refused=false;try{prepareLibrary(dir/"library.json",dir/"invalid-dj",policyFor("dj"),12);}catch(...){refused=true;}
    REQUIRE(refused);REQUIRE(!std::filesystem::exists(dir/"invalid-dj/session.json"));
}
int main(){try{maps();auto dir=fixture();rendering(dir);commands(dir);delays();catalog(dir);musicalRegressions(dir);preparationLifecycle(dir);std::filesystem::remove_all(dir);std::cout<<checks<<" assertions passed: shared engine time maps, PCM/seek, variable blocks, allocation-free callback, complete incoming kick, unity solo vocal, audible bounded reverb, seek FX reconstruction, window origin, cancellation/atomic publish, legacy saved-session compatibility\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
