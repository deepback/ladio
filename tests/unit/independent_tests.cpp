#include "audio.hpp"
#include "checksum.hpp"
#include "harmony.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include "../../src/planner/dj_score.hpp"
#include "../../src/planner/scene_preference.hpp"
using namespace ladio;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(0)
static Track track(const std::string&id,double bpm){
    Track t;t.id=t.hash=id;t.bpm=bpm;t.sampleRate=44100;t.duration=140;t.frames=140*44100;t.lufs=-14;t.beatConfidence=t.downbeatConfidence=.95;
    for(double s=0;s<140;s+=240/bpm)t.downbeats.push_back(s);
    for(int k:{16,40,48,56})if(size_t(k)<t.downbeats.size()){
        Cue c;c.bar=k;c.time=t.downbeats[k];c.confidence=c.structureConfidence=.95;c.sectionBoundary=true;c.boundaryStrength=.9;c.entrySuitability=c.exitSuitability=.85;c.chroma[0]=1;c.energy=.1;c.vocalBefore=.5;t.cues.push_back(c);
    }
    t.source={{"curves",Json::array()},{"protected_vocal_phrases",Json::array()}};
    for(int i=0;i<700;i++)t.source["curves"].push_back({{"time",i*.2},{"rms",.1},{"vocal",.5}});
    return t;
}
static Plan choose(const Track&a,const Track&b){Json veto;auto p=directScene(a,b,0,1,policyFor("dj"),77,0,{},veto);CHECK(p.has_value());return *p;}
static void planner(){
    auto a=track("a",120),b=track("b",124);auto p=choose(a,b);auto s=Scene::from(p.scene);
    CHECK(p.json()==choose(a,b).json());CHECK(p.scene["resources"].empty());CHECK(!p.useStems);CHECK(p.bars>=4);
    CHECK(p.outCue>a.duration*.55);CHECK(p.entryAnchor<b.duration*.46);CHECK(p.aSourceStart()>=a.duration*.48);
    CHECK(p.mapA.rateAt(0)==1);CHECK(p.mapB.rateAt(p.impact)==1);CHECK(p.mapA.rateAt(p.impact)>1);
    for(double t=0;t<p.impact;t+=.017){CHECK(std::abs(120*p.mapA.rateAt(t)-124*p.mapB.rateAt(t))<1e-7);CHECK(p.mapA.rateAt(t)<=1.18);}
    for(const auto&v:s.voices){CHECK(v.originalOnly);CHECK(v.channels==2);CHECK(preparedChannels(p.scene["voices"][v.deck]).size()==2);}
    const auto&incoming=s.voices[1];
    for(Frame f=s.arrival;f<s.frames;f+=997){CHECK(incoming.roles[0].at(f)==1);CHECK(incoming.softFilters[4].wet.at(f)==0);CHECK(incoming.map.rateAt(double(f)/s.sr)==1);}
    for(auto bpm:{80.,174.}){b=track("contrast",bpm);auto bridge=choose(a,b);CHECK(bridge.reset);CHECK(bridge.mapA.rateAt(1)!=1);CHECK(bridge.mapB.rateAt(1)!=1);CHECK(bridge.mapB.rateAt(bridge.impact)==1);
        auto bs=Scene::from(bridge.scene);CHECK(bs.dj.decks[0].delayReturn.at(bs.arrival)==0);}
    b=track("b",124);Json veto;for(auto&c:a.cues)c.time+=.8;CHECK(!directScene(a,b,0,1,policyFor("dj"),77,0,{},veto));
    a=track("a",120);a.beatConfidence=.1;for(auto&c:a.cues)c.structureConfidence=.2;CHECK(!directScene(a,b,0,1,policyFor("dj"),77,0,{},veto));
    a=track("a",120);auto queue=planPlaylist({a,b,track("c",174)},policyFor("dj"),11);CHECK(queue.size()==3);
    for(size_t i=0;i<queue.size();i++){CHECK(queue[i].bSourceEnd()<queue[(i+1)%queue.size()].aSourceStart());CHECK(queue[i].scene["execution"]["separation_model"]=="none");}
    a.source["protected_vocal_phrases"]={{{"start",0.},{"end",139.}}};CHECK(!directScene(a,b,0,1,policyFor("dj"),77,0,{},veto));
}
static void localEvidenceAndShortIncoming(){
    auto a=track("local-evidence",120),b=track("local-in",124);
    a.downbeatConfidence=b.downbeatConfidence=.5;
    for(auto*t:{&a,&b})for(auto&c:t->cues)c.structureConfidence=.5;
    auto p=choose(a,b);CHECK(!p.reset);CHECK(p.scene["decision"]["search"]["measured_grid_reliable"]==false);
    for(auto*t:{&a,&b}){t->downbeatConfidence=.3;t->source["beat_qc"]["observed_downbeat_probability"]=.95;}
    CHECK(!choose(a,b).reset);
    // Keep only measured anchors after a missed bar, with valid cue indices.
    a.downbeats.erase(a.downbeats.begin()+45);
    for(auto&c:a.cues)c.bar=int(std::lower_bound(a.downbeats.begin(),a.downbeats.end(),c.time)-a.downbeats.begin());
    CHECK(!choose(a,b).reset);
    a=track("long-out",120);b=track("short-in",120);b.duration=30;b.frames=30*44100;
    b.downbeats.erase(std::remove_if(b.downbeats.begin(),b.downbeats.end(),[](double t){return t>=30;}),b.downbeats.end());
    b.cues.resize(1);b.cues[0].time=8;b.cues[0].bar=4;
    p=choose(a,b);CHECK(p.bSourceEnd()<b.duration-5.);CHECK(p.entryAnchor<14.);
}
static void barSubdivisionsAndMissingDetections(){
    auto a=track("half-bar-out",120),b=track("half-bar-in",124);
    auto beatEvidence=[](Track&t){for(double at=0;at<t.duration;at+=60/t.bpm)t.beats.push_back(at);};
    auto reindex=[](Track&t){for(auto&c:t.cues)c.bar=int(std::lower_bound(t.downbeats.begin(),t.downbeats.end(),c.time)-t.downbeats.begin());};
    for(auto*t:{&a,&b}){
        beatEvidence(*t);auto bars=t->downbeats;
        for(double at:bars)if(at+120/t->bpm<t->duration)t->downbeats.push_back(at+120/t->bpm);
        std::sort(t->downbeats.begin(),t->downbeats.end());reindex(*t);
    }
    auto p=choose(a,b);CHECK(!p.reset);CHECK(p.bars>=8);
    // Phase correction binds to full bars, not the extra half-bar accents.
    for(const auto&[at,offset]:p.mapA.phaseCorrections){
        double source=p.aSourceStart()+p.mapA.sourceAt(at);
        CHECK(std::abs(source-std::round(source/(240/a.bpm))*(240/a.bpm))<1./a.sampleRate);
    }
    a=track("missed-out",120);b=track("missed-in",124);
    for(auto*t:{&a,&b}){
        beatEvidence(*t);std::vector<double> everyOther;
        for(size_t k=0;k<t->downbeats.size();k+=2)everyOther.push_back(t->downbeats[k]);
        t->downbeats=everyOther;reindex(*t);
    }
    a.cues={a.cues.back()};b.cues={b.cues.front()};b.cues[0].time=b.downbeats[12];b.cues[0].bar=12;
    a.source["preferred_transition"]="dj_dance_build";
    p=choose(a,b);CHECK(!p.reset);CHECK(p.family=="dj_dance_build");CHECK(p.bars>=16);
    const auto&loop=p.scene["voices"][0]["source_loop"];
    CHECK(loop["repeat_stages"][0][0].get<double>()>=p.outCue-480/a.bpm-1e-6);
    // Unsupported arbitrary accents must still fail measured synchronization.
    a=track("contradictory-out",120);b=track("steady-in",124);beatEvidence(a);beatEvidence(b);
    auto bars=a.downbeats;for(double at:bars)if(at+.74<a.duration)a.downbeats.push_back(at+.74);
    std::sort(a.downbeats.begin(),a.downbeats.end());reindex(a);
    CHECK(choose(a,b).reset);
}
static void voiceAndRunway(){
    auto a=track("phrased",120),b=track("entry",120);a.cues={a.cues.back()};b.cues={b.cues.front()};
    a.source["protected_vocal_phrases"]={{{"start",88.},{"end",110.}}};
    auto p=choose(a,b);auto s=Scene::from(p.scene);CHECK(p.outCue==112.);
    // The phrase began before preparation: it still owns unity gain.
    Frame phrase=std::llround((110-p.aSourceStart())*44100);CHECK(s.voices[0].roles[0].at(phrase)==1);
    CHECK(s.dj.decks[0].filterWet.at(phrase)<.001);
    // A loud isolated crash does not authorize entering a silent continuation.
    for(auto&r:b.source["curves"])if(r["time"].get<double>()>32.3&&r["time"].get<double>()<36.)r["rms"]=0.;
    Json veto;CHECK(!directScene(a,b,0,1,policyFor("dj"),77,0,{},veto));
}
static void localHarmonicMaterial(){
    auto a=track("outgoing",120),b=track("incoming",120);
    a.cues={a.cues.back()};b.cues={b.cues.front()};
    a.source["preferred_transition"]=b.source["preferred_transition"]="dj_spectral_sweep";
    // Cue metadata describes the following phrase, not the overlapping one.
    // Deliberately identical cue values must not hide a tritone before arrival.
    for(auto&t:{&a,&b})for(auto&r:t->source["curves"]){Chroma c{};c[t==&a?0:6]=1;r["chroma"]=c;r["tonality"]=1.;}
    auto p=choose(a,b);CHECK(p.harmony<.1);CHECK(p.scene["decision"]["harmony_confidence"].get<double>()>.99);
    CHECK(p.scene["decision"]["harmony_model"]=="none");
    for(auto&r:b.source["curves"]){Chroma c{};c[0]=1;r["chroma"]=c;}
    auto match=choose(a,b);CHECK(match.harmony>.99);CHECK(match.score>p.score);
    // A few loud valid observations do not establish a whole phrase.
    for(auto&r:b.source["curves"])if(r["time"].get<double>()>.4)r.erase("chroma");
    CHECK(choose(a,b).scene["decision"]["harmony_confidence"].get<double>()<.1);
    for(auto&r:b.source["curves"]){r["rms"]=.0002;r["chroma"]=Chroma{};}
    auto unknown=choose(a,b);CHECK(unknown.harmony==.5);CHECK(unknown.scene["decision"]["harmony_confidence"]==0.);
}
static void measuredHold(){
    auto a=track("outro",120),b=track("building",124);
    a.cues={a.cues.back()};b.cues={b.cues.front()};b.cues[0].bar=24;b.cues[0].time=b.downbeats[24];
    a.source["preferred_transition"]="dj_dance_build";
    auto p=choose(a,b);CHECK(p.family=="dj_dance_build");auto s=Scene::from(p.scene);
    CHECK(p.seconds>25);CHECK(p.bars>=16);const auto&v=s.voices[0];
    CHECK(v.mode=="rendered_hold");CHECK(v.sourceLoopStart>v.sourceStart);CHECK(v.sourceLoopActivate==v.sourceLoopEnd);
    CHECK(v.virtualSourceSeconds(0,s.sr)==v.sourceStart);
    CHECK(v.virtualSourceSeconds(s.arrival-1,s.sr)>=v.sourceLoopStart);
    CHECK(v.virtualSourceSeconds(s.arrival-1,s.sr)<v.sourceLoopEnd);
    CHECK(p.scene["voices"][0]["source_loop"]["full_mix_repeat"]==true);
    // A phrase crossing the capture boundary cannot be chopped into a loop.
    a.source["protected_vocal_phrases"]={{{"start",v.sourceLoopStart-.1},{"end",p.outCue-.01}}};
    auto adapted=choose(a,b);
    if(adapted.family=="dj_dance_build")CHECK(adapted.scene["voices"][0]["source_loop"]["end"].get<double>()<=v.sourceLoopStart-.1);
}
static void heldLeadClearance(){
    for(double end:{110.,118.}){
        auto a=track("held-out",120),b=track("held-in",120);
        a.cues={a.cues.back()};b.cues={b.cues.front()};b.cues[0].time=48;b.cues[0].bar=24;
        a.source["preferred_transition"]="dj_dance_build";
        a.source["protected_vocal_phrases"]={{{"start",98.},{"end",end}}};
        b.source["protected_vocal_phrases"]={{{"start",20.},{"end",62.}}};
        for(auto*t:{&a,&b})for(auto&r:t->source["curves"]){
            double time=r["time"];r["vocals_activity"]=(t==&a?time>=98&&time<end:time>=20&&time<62)?.9:0.;
            r["drums_activity"]=.9;r["bass_activity"]=.8;r["others_activity"]=.7;
        }
        // A future verse would collide, or even cross the nominal exit. The
        // actual held reader never reaches it: its complete motif ends first.
        auto p=choose(a,b);auto s=Scene::from(p.scene);
        CHECK(p.family=="dj_dance_build");CHECK(!p.reset);CHECK(s.voices[0].mode=="rendered_hold");
        CHECK(s.voices[0].sourceLoopEnd<=98);CHECK(p.scene["musical_events"]["competing_vocal_seconds"]==0.);
        CHECK(s.dj.decks[0].flangerMix.at(s.arrival*3/4)>0);
        CHECK(s.dj.decks[0].roomSend.at(s.arrival*3/4)>0);
    }
}
static void musicalEventSelection(){
    auto a=track("phrased-out",120),b=track("pickup-in",120);a.cues={a.cues.back()};b.cues={b.cues.front()};
    b.cues[0].bar=24;b.cues[0].time=48;
    a.source["preferred_transition"]="dj_spectral_sweep";
    a.source["protected_vocal_phrases"]={{{"start",82.},{"end",94.}}};a.voices={{82,94,.9}};b.voices={{46.9,53.,.9}};
    for(auto&t:{&a,&b})for(auto&r:t->source["curves"]){double time=r["time"];r["vocals_activity"]=(t==&a?time>=82&&time<94:time>=46.9&&time<53.)?.8:0.;r["drums_activity"]=.9;r["bass_activity"]=.8;r["others_activity"]=.7;r["low_energy"]=.04;}
    auto p=choose(a,b);auto s=Scene::from(p.scene);auto&anchors=p.scene["anchors"];
    auto event=[&](const char*name){return anchors[name]["output_frame"].get<Frame>();};
    CHECK(event("B_first_audible")<event("hybrid_start"));CHECK(event("hybrid_start")<event("foundation_takeover"));
    CHECK(event("foundation_takeover")<event("B_full_arrival"));CHECK(event("B_lead_entry")<event("B_full_arrival"));
    CHECK(event("A_vocal_phrase_end")<event("A_vocal_release"));CHECK(s.voices[0].roles[4].at(event("A_vocal_phrase_end"))==1);
    CHECK(s.dj.decks[0].dryGain.at(event("A_vocal_phrase_end"))==1);
    // No near-empty passband during the advertised audible joint phrase.
    for(Frame f=event("hybrid_start");f<event("B_full_arrival");f+=997){CHECK(s.dj.decks[1].highpass.at(f)<s.dj.decks[1].lowpass.at(f));}
    CHECK(p.scene["decision"]["scorecard"]["vocal_conflict"]==0.);
    CHECK(p.scene["candidate_plans"].size()>0);
    // The pickup is legal even though the full-arrival downbeat is inside its
    // measured vocal range; the source clip itself still starts cleanly.
    b.source["protected_vocal_phrases"]={{{"start",46.9},{"end",53.}}};CHECK(choose(a,b).entryAnchor==48.);
    // A first lead that starts after the downbeat must not be reported as if
    // it started at arrival, and an early attack must keep its source offset.
    b.source["protected_vocal_phrases"]={{{"start",49.2},{"end",53.}}};b.voices={{49.2,53.,.9}};
    b.cues[0].attack=47.968;b.cues[0].attackConfidence=.9;
    auto later=choose(a,b);const auto&ev=later.scene["anchors"];
    CHECK(ev["B_lead_entry"]["output_frame"].get<Frame>()>ev["B_full_arrival"]["output_frame"].get<Frame>());
    double attackOut=ev["B_featured_attack"]["seconds"];
    CHECK(std::abs(later.inCue+later.mapB.sourceAt(attackOut)-47.968)<1./later.sr);
}
static void halfTime(){
    auto a=track("a",174),b=track("b",90);auto p=choose(a,b);
    CHECK(!p.reset);CHECK(p.scene["decision"]["pulse_factor_b"]==2.);
    auto scene=Scene::from(p.scene);
    for(double t=0;t<p.impact;t+=.031)CHECK(std::abs(174*p.mapA.baseRateAt(t)-180*p.mapB.baseRateAt(t))<1e-6);
    CHECK(p.mapB.rateAt(p.impact)==1);CHECK(scene.transport.segments.size()==2);
}
static void independentIntroduction(){
    auto a=track("late-lead",101),b=track("early-drop",174);
    a.cues={a.cues.back()};b.cues={b.cues.front()};double entry=b.cues[0].time,voiceEnd=a.cues[0].time-.08;
    a.source["protected_vocal_phrases"]={{{"start",60.},{"end",voiceEnd}}};a.voices={{60,voiceEnd,.9}};
    b.source["protected_vocal_phrases"]={{{"start",8.},{"end",entry-.08}},{{"start",entry+.10},{"end",40.}}};
    b.voices={{8,entry-.08,.9},{entry+.10,40,.9}};
    for(auto*t:{&a,&b})for(auto&r:t->source["curves"]){double time=r["time"];r["vocals_activity"]=(t==&a?time>=60&&time<voiceEnd:time>=8)?.8:.05;r["drums_activity"]=t==&a?.8:.1;r["others_activity"]=.8;r["low_energy"]=.03;}
    auto p=choose(a,b);auto s=Scene::from(p.scene);
    CHECK(p.scene["variant"]=="independent_instrumental_introduction");CHECK(s.voices.size()==3);
    CHECK(s.voices[2].id=="B.teaser");CHECK(s.voices[2].sourceEnd<8.);
    Frame middle=s.arrival/2;CHECK(s.voices[1].roles[4].at(middle)==0);CHECK(s.voices[2].roles[4].at(middle)>.1);
    CHECK(std::abs(s.voices[1].sourceStart+s.voices[1].map.sourceAt(p.impact)-entry)<1./p.sr);
    CHECK(s.voices[1].roles[4].at(s.arrival)==1);CHECK(p.vocalOpen>p.impact);CHECK(!p.useStems);
    CHECK(p.scene["decision"]["scorecard"]["preparation_work"]=="three bounded stereo voices; zero separation");
    // The sole clean first bar can begin just after the file starts. Do not
    // lose a real measured introduction to B.main's unrelated 100 ms margin.
    auto early=b;early.bpm=75;early.downbeats.clear();early.cues.clear();early.beats.clear();
    for(double at=.04;at<early.duration;at+=3.2)early.downbeats.push_back(at);
    for(double at=.04;at<early.duration;at+=.8)early.beats.push_back(at);
    Cue cue=b.cues.front();cue.bar=12;cue.time=early.downbeats[12];early.cues.push_back(cue);
    early.source["protected_vocal_phrases"]={{{"start",5.47},{"end",cue.time-.08}},{{"start",cue.time+.10},{"end",50.}}};
    early.voices={{5.47,cue.time-.08,.9},{cue.time+.10,50.,.9}};
    for(auto&r:early.source["curves"])r["vocals_activity"]=r["time"].get<double>()>=5.47?.8:0.;
    auto preview=choose(a,early);CHECK(preview.scene["variant"]=="independent_instrumental_introduction");
    CHECK(std::abs(preview.scene["introduction_selection"]["source_start"].get<double>()-.04)<1e-6);
    // Exercise the actual renderer: the teaser's gain must never open a
    // suppressed main B. Both readers share DSP, but retain their own gains.
    auto file=std::filesystem::temp_directory_path()/("automix-independent-gain-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".f32");
    {std::ofstream out(file,std::ios::binary);std::array<float,1024> pcm;pcm.fill(.2f);out.write(reinterpret_cast<const char*>(pcm.data()),sizeof(pcm));}
    auto pack=std::make_shared<PCMFile>(file);auto session=std::make_shared<Session>();session->sr=p.sr;session->frames=512;session->loop=false;session->spans={{0,512,0,0,0}};
    for(size_t k=0;k<s.voices.size();k++){s.voices[k].gain=1;for(auto&role:s.voices[k].roles)role.knots={{0,k==0?0.:k==1?.05:.4}};}
    for(auto&deck:s.dj.decks){deck=DJDeckProgram{};deck.serial=true;deck.inputGain.constant=deck.dryGain.constant=1;}
    Compiled c;c.plan=p;c.scene=s;c.voices={pack,pack,pack};session->transitions={c};
    Engine engine(session);std::array<float,512> left{},right{};engine.render(left.data(),right.data(),512);
    for(int k=0;k<512;k++)CHECK(std::abs(left[k]-.09)<1e-6);
    std::filesystem::remove(file);
}
static void instrumentalRunway(){
    auto a=track("continuous-lead",70.5),b=track("dense-incoming-lead",76.5);
    a.cues={a.cues.front()};a.cues[0].bar=33;a.cues[0].time=a.downbeats[33];
    b.cues={b.cues.front()};b.cues[0].bar=4;b.cues[0].time=b.downbeats[4];
    a.source["protected_vocal_phrases"]={{{"start",.52},{"end",109.14}},{{"start",121.97},{"end",123.24}}};
    a.voices={{.52,109.14,.9},{121.97,123.24,.9}};
    for(double start=.67;start<80;start+=240/76.5*2){
        double end=start+240/76.5*2-.8;
        b.source["protected_vocal_phrases"].push_back({{"start",start},{"end",end}});
        b.voices.push_back({start,end,.9});
    }
    for(auto*t:{&a,&b})for(auto&r:t->source["curves"]){
        double time=r["time"];bool voice=false;
        for(const auto&v:t->voices)voice|=time>=v.start&&time<v.end;
        r["vocals_activity"]=voice?.8:.01;r["drums_activity"]=.5;r["others_activity"]=.7;r["low_energy"]=.04;
    }
    auto p=choose(a,b);CHECK(p.scene["decision"]["adaptive_instrumental_exit"]==true||p.scene["decision"]["outgoing_cue_kind"]=="observed_phrase");
    CHECK(p.outCue>109.14);CHECK(p.outCue<121.97||p.outCue>=123.24);
    if(p.aSourceStart()<109.14){auto scene=Scene::from(p.scene);Frame phrase=p.scene["anchors"]["A_vocal_phrase_end"]["output_frame"];CHECK(scene.voices[0].roles[4].at(phrase)==1);CHECK(scene.dj.decks[0].dryGain.at(phrase)==1);}
    CHECK(p.scene["decision"]["scorecard"]["vocal_conflict"]==0.);
    CHECK(p.seconds>6);CHECK(!p.reset);CHECK(!p.useStems);
    // An empty outro cannot rescue occupied leads merely because its grid is
    // well measured. Preserve the original musical safety constraints.
    for(auto&r:a.source["curves"])if(r["time"].get<double>()>=109.14)r["rms"]=0.;
    Json veto;CHECK(!directScene(a,b,0,1,policyFor("dj"),77,0,{},veto));
}
static void measuredPhase(){
    auto a=track("drifting",120),b=track("steady",124);a.cues={a.cues.back()};b.cues={b.cues.front()};
    // Interior bars move 70 ms while the phrase endpoints stay anchored.
    for(size_t k=1;k<a.downbeats.size();k++)if(k%4==1)a.downbeats[k]+=.07;
    a.source["preferred_transition"]="dj_spectral_sweep";
    auto p=choose(a,b);const auto&map=p.mapA;CHECK(!map.phaseCorrections.empty());
    for(size_t k=1;k+1<map.phaseCorrections.size();k++){
        double t=map.phaseCorrections[k].first,actual=p.aSourceStart()+map.sourceAt(t);
        double nearest=1e9;for(double d:a.downbeats)nearest=std::min(nearest,std::abs(d-actual));CHECK(nearest<1e-5);
        CHECK(std::abs(map.rateAt(t-1e-7)-map.rateAt(t+1e-7))<1e-6);
    }
    CHECK(TimeMap::from(map.json()).sourceAt(p.impact)==map.sourceAt(p.impact));
}
static void neuralScenePreference(){
    auto a=track("groove",120),b=track("texture",120);a.cues={a.cues.back()};b.cues={b.cues.front()};b.cues[0].bar=24;b.cues[0].time=48;
    for(auto*t:{&a,&b})for(auto&r:t->source["curves"]){r["vocals_activity"]=.01;r["drums_activity"]=t==&a?.95:.05;r["bass_activity"]=t==&a?.90:.05;r["others_activity"]=t==&a?.3:.95;r["low_energy"]=.04;r["phrases_boundary"]=.1;r["segments_boundary"]=.1;r["sections_boundary"]=.1;}
    auto melodic=choose(a,b);
    CHECK(melodic.scene["decision"]["scorecard"]["material"]["activity_coverage"].get<double>()>.99);
    for(auto&r:b.source["curves"]){r["drums_activity"]=.98;r["bass_activity"]=.95;r["others_activity"]=.01;}
    auto percussive=choose(a,b);CHECK(melodic.family!=percussive.family);
    CHECK(percussive.scene["decision"]["scorecard"]["vocal_conflict"]==0.);
    Json veto;auto varied=directScene(a,b,0,1,policyFor("dj"),77,0,{percussive.family,percussive.family},veto);
    CHECK(varied.has_value());CHECK(varied->family!=percussive.family);
    CHECK(varied->scene["decision"]["scorecard"]["quality_gap"].get<double>()<=.12+1e-9);
    // Missing neural observations are neutral; the supplied model is not run
    // again and default activity values cannot masquerade as real evidence.
    auto unknown=track("unknown",120);MusicalIndex empty(unknown);
    auto material=SceneMaterial::measure(empty,empty,80,112,16,48,8);
    CHECK(material.confidence==0);CHECK(material.affinity(*djStyle("dj_phrase_exchange"),1,1,1,true)==0);
    CHECK(sceneRepetition(*djStyle("dj_echo_filter"),{"dj_spectral_sweep"})>0);
}
static void dsp(){
    auto p=choose(track("a",120),track("b",124));
    MusicalEvents events;events.arrival=std::llround(p.impact*p.sr);events.first=events.arrival*.12;events.hybrid=events.arrival*.4;events.foundation=events.arrival*.72;events.aPhrase=events.aRelease=events.arrival*.50;events.bLead=events.arrival*.90;
    std::map<std::string,double> lowpassAtHalf;
    // Every score must return to an unfiltered, unmuted B and have a bounded tail.
    for(const auto&style:djStyles){
        auto j=p.scene;auto&va=j["voices"][0];auto&vb=j["voices"][1];
        j["family"]=style.id;j["dj_program"]=djScore(style,va,vb,std::llround(p.impact*p.sr),p.frames(),p.sr,124,true,0);
        auto s=Scene::from(j);CHECK(s.dj.enabled);
        for(int d=0;d<2;d++){CHECK(s.dj.decks[d].filterWet.at(s.frames-64)==0);CHECK(s.dj.decks[d].delayReturn.at(s.frames-64)==0);CHECK(s.dj.decks[d].reverbReturn.at(s.frames-64)==0);}
        CHECK(s.voices[1].roles[4].at(s.arrival)==1);
        if(style.autoDJ){
            // Production passes independently measured events. These must
            // constrain the score without replacing every preset's filters.
            j["dj_program"]=djScore(style,va,vb,events.arrival,p.frames(),p.sr,124,true,events.aPhrase,&events);
            s=Scene::from(j);auto hz=[&](double g){return std::atan(g)*s.sr/pi;};lowpassAtHalf[style.id]=hz(s.dj.decks[1].lowpass.at(events.arrival/2));
            CHECK(s.dj.decks[0].dryGain.at(events.aPhrase-1)>.999);
            CHECK(s.dj.decks[0].filterWet.at(events.aPhrase-1)<.001);
            CHECK(s.voices[1].roles[4].at(events.bLead)==1);
            CHECK(hz(s.dj.decks[1].highpass.at(events.foundation))<23);
            CHECK(hz(s.dj.decks[1].lowpass.at(events.bLead))>18999);
            for(const auto&lane:{j["dj_program"]["decks"][1]["highpass_hz"],j["dj_program"]["decks"][1]["lowpass_hz"]})for(size_t k=1;k<lane.size();k++)CHECK(lane[k][0].get<Frame>()>lane[k-1][0].get<Frame>());
        }
    }
    CHECK(lowpassAtHalf["dj_soft_room"]<lowpassAtHalf["dj_band_expansion"]*.4);
    CHECK(lowpassAtHalf["dj_hook_echo"]>lowpassAtHalf["dj_soft_room"]*2);
    auto j=p.scene;auto&va=j["voices"][0];auto&vb=j["voices"][1];
    j["dj_program"]=djScore(*djStyle("dj_echo_filter"),va,vb,events.arrival,p.frames(),p.sr,124,true,events.aPhrase,&events);
    auto s=Scene::from(j);DJDeckDSP fx(s.sr);fx.configure(s.dj.decks[0]);double tail=0;
    for(Frame i=0;i<s.frames;i++){
        float x=i>s.arrival-s.sr*2&&i<s.arrival?.1f*std::sin(2*pi*440*i/s.sr):0;
        auto y=fx.process({x,x},s.dj.decks[0],i,s.transport.quarterAt(i));
        CHECK(std::isfinite(y.wet[0]));CHECK(std::abs(y.wet[0])<1);
        if(i>s.arrival&&i<s.arrival+s.sr)tail+=y.wet[0]*y.wet[0];
        if(i>s.frames-64)CHECK(y.wet[0]==0);
    }
    CHECK(tail>1e-3);fx.clear();CHECK(fx.process({},s.dj.decks[0],0,0).wet[0]==0);
    auto invalid=j;invalid["dj_program"]["decks"][0]["delay_feedback"]=1.2;bool rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
    invalid=j;invalid["voices"][0]["time_map"]["initial_rate"]=1.5;rejected=false;try{Scene::from(invalid);}catch(...){rejected=true;}CHECK(rejected);
}
static void serialFX(){
    // An impulse cannot leak around the serial delay -> room path. Source
    // mute and dry output mute must leave the already captured wet tail alive.
    constexpr int sr=16000;DJDeckProgram p;p.serial=true;p.auxActive=true;p.auxReverbActive=true;
    p.inputGain.constant=1;p.dryGain.constant=0;p.auxSend.constant=1;p.auxReturn.constant=1;
    p.delayMix.constant=1;p.reverbMix.constant=1;p.delayFrames.constant=sr/4;
    p.delayLowpassAlpha=1;p.tailHP=0;p.tailLP=1;p.delayFeedback=.45;
    p.reverbMinSeconds=.03;p.reverbMaxSeconds=.1;
    DJDeckDSP graph(sr);graph.configure(p);double early=0,late=0;
    for(int f=0;f<sr*2;f++){
        if(f==1){p.inputGain.constant=0;p.auxSend.constant=0;}
        auto y=graph.process({f==0?.5f:0,f==0?.5f:0},p,f,double(f)*2/sr);
        CHECK(y.dry[0]==0);CHECK(std::isfinite(y.wet[0]));
        if(f<sr*.27)early+=y.wet[0]*y.wet[0];if(f>sr*.3)late+=y.wet[0]*y.wet[0];
    }
    CHECK(early<1e-15);CHECK(late>1e-4);
    p.auxReturn.constant=0;CHECK(graph.process({},p,sr*2,4).wet[0]==0);
    // Capture while the room is inaudible, then reveal only its stored tail.
    graph.clear();p.inputGain.constant=1;p.auxSend.constant=1;p.auxReturn.constant=1;p.delayMix.constant=0;p.reverbMix.constant=0;
    double revealed=0;
    for(int f=0;f<sr;f++){
        if(f==1){p.inputGain.constant=0;p.auxSend.constant=0;}
        if(f==sr/5)p.reverbMix.constant=1;
        auto y=graph.process({f==0?.5f:0,f==0?.5f:0},p,f,double(f)*2/sr);
        if(f>=sr/5)revealed+=y.wet[0]*y.wet[0];
    }
    CHECK(revealed>1e-5);
    // Frequency easing acts in Hz before coefficient design; not a second
    // unintended logarithmic curve that hides B for nearly the entire scene.
    auto curve=compileDJLane(Json::array({{0,100.,"linear","linear-frequency"},{sr,10000.,"in:2","linear-frequency"}}),sr,100,20,11000,true,true,48000);
    CHECK(std::abs(std::atan(curve.at(sr/2))*48000/pi-2575)<2);
}
static void catalogAudio(){
    auto plan=choose(track("catalog-A",120),track("catalog-B",124));
    MusicalEvents ev;ev.arrival=std::llround(plan.impact*plan.sr);ev.first=ev.arrival*.12;ev.hybrid=ev.arrival*.4;ev.foundation=ev.arrival*.72;ev.aPhrase=ev.aRelease=ev.arrival*.3;ev.bLead=ev.arrival*.9;
    Json report=Json::array(),late,echo,phrase,expansion;
    for(const auto&style:djStyles){
        if(!style.autoDJ&&std::string(style.id)!="dj_equal_power")continue;
        auto j=plan.scene;j["family"]=style.id;
        j["dj_program"]=djScore(style,j["voices"][0],j["voices"][1],ev.arrival,plan.frames(),plan.sr,124,true,ev.aPhrase,&ev);
        if(std::string(style.id)=="dj_elastic_entry")late=j["dj_program"]["decks"];
        if(std::string(style.id)=="dj_echo_filter")echo=j["dj_program"]["decks"];
        if(std::string(style.id)=="dj_phrase_exchange")phrase=j["dj_program"]["decks"];
        if(std::string(style.id)=="dj_band_expansion")expansion=j["dj_program"]["decks"];
        auto scene=Scene::from(j);const auto&p=scene.dj.decks[0];
        if(style.autoDJ){CHECK(p.filterWet.at(scene.arrival/2)>.999);CHECK(p.filterWet.at(scene.arrival-1)>.999);}
        if(std::string(style.shape)=="pulse")CHECK(p.dryGain.at(scene.arrival*3/4)==1);
        if(std::string(style.shape)=="build"||std::string(style.shape)=="blur"||std::string(style.shape)=="release"){
            CHECK(p.roomSend.at(scene.arrival-std::llround(scene.sr*30./124))>.59);
            CHECK(p.roomSend.at(scene.arrival)==0);
        }
        auto noDelay=p,noRoom=p,noFlange=p,noGate=p;
        noDelay.delayMix={};noDelay.echoSend={};noDelay.echoReturn={};noDelay.echoActive=false;
        noRoom.reverbMix={};noRoom.auxReverbActive=false;noRoom.roomSend={};noRoom.roomReturn={};noRoom.roomActive=false;
        noFlange.flangerMix={};noGate.gateDepth={};
        DJDeckDSP full(scene.sr),withoutDelay(scene.sr),withoutRoom(scene.sr),withoutFlange(scene.sr),withoutGate(scene.sr);
        full.configure(p);withoutDelay.configure(noDelay);withoutRoom.configure(noRoom);withoutFlange.configure(noFlange);withoutGate.configure(noGate);
        double wet=0,tail=0,releasedTail=0,delayDifference=0,roomDifference=0,flangeDifference=0,gateDifference=0,peak=0;
        double incomingEnergy=0,incomingFilteredDifference=0;
        DJDeckDSP incoming(scene.sr);incoming.configure(scene.dj.decks[1]);
        for(Frame f=0;f<scene.frames;f++){
            double t=double(f)/scene.sr,q=scene.transport.quarterAt(f);
            // Multi-band musical probe, followed by silence. Compare actual
            // output with one processor bypassed; a nonzero send is not proof
            // that an effect survives its dry/role/output routing.
            float x=f<scene.arrival?float(.09*std::sin(2*pi*110*t)+.06*std::sin(2*pi*440*t)+.04*std::sin(2*pi*2300*t)):0;
            auto y=full.process({x,x},p,f,q);
            auto sum=[&](DJDeckDSP&fx,const DJDeckProgram&pr){auto v=fx.process({x,x},pr,f,q);return v.dry[0]*scene.voices[0].roles[4].at(f)+v.wet[0];};
            double output=y.dry[0]*scene.voices[0].roles[4].at(f)+y.wet[0];
            CHECK(std::isfinite(output));peak=std::max(peak,std::abs(output));wet+=y.wet[0]*y.wet[0];
            if(f>scene.arrival)tail+=y.wet[0]*y.wet[0];
            if(p.dryGain.at(f)*scene.voices[0].roles[4].at(f)==0)releasedTail+=y.wet[0]*y.wet[0];
            auto square=[](double v){return v*v;};
            delayDifference+=square(output-sum(withoutDelay,noDelay));roomDifference+=square(output-sum(withoutRoom,noRoom));
            flangeDifference+=square(output-sum(withoutFlange,noFlange));gateDifference+=square(output-sum(withoutGate,noGate));
            if(f>=scene.frames-64)CHECK(output==0);
            if(f<scene.arrival){
                auto v=incoming.process({x,x},scene.dj.decks[1],f,q);double gain=scene.voices[1].roles[4].at(f);
                incomingEnergy+=v.dry[0]*v.dry[0]*gain*gain;
                incomingFilteredDifference+=square((v.dry[0]-x)*gain);
            }
        }
        std::cout<<style.id<<" wet="<<wet<<" tail="<<tail<<" released_tail="<<releasedTail<<" delay="<<delayDifference<<" room="<<roomDifference<<" flanger="<<flangeDifference<<" gate="<<gateDifference<<'\n';
        CHECK(peak<2);
        const std::string shape=style.shape;
        if(style.delay)CHECK(delayDifference>1e-5);
        if(shape=="pulse"){
            CHECK(p.auxReturn.at(scene.arrival)==0);
            CHECK(p.auxSend.at(scene.arrival)==0);
        }
        if(style.room)CHECK(roomDifference>1e-5);
        if(shape=="build"||shape=="blur")CHECK(flangeDifference>1e-5);
        if(shape=="blur")CHECK(gateDifference>1e-5);
        // Vocal clearance uses a short room to clear the incoming lead. Its
        // captured tail belongs after dry release, not necessarily after drop.
        if(style.delay||style.room)CHECK(releasedTail>1e-7);
        // A nonzero numerical residue is not an audible spatial release. This
        // catches long-score capture ending before the room's decay reaches B.
        if(style.room&&shape!="voice")CHECK(tail>wet*.0001);
        CHECK(incomingEnergy>1.);
        if(style.autoDJ)CHECK(incomingFilteredDifference>1.);
        report.push_back({{"id",style.id},{"title",style.title},{"preferred_bars",style.bars},{"wet_energy",wet},{"tail_energy",tail},{"released_tail_energy",releasedTail},
            {"delay_output_difference",delayDifference},{"reverb_output_difference",roomDifference},{"flanger_output_difference",flangeDifference},{"gate_output_difference",gateDifference},{"incoming_pre_arrival_energy",incomingEnergy},{"incoming_filter_output_difference",incomingFilteredDifference},{"peak",peak}});
    }
    CHECK(report.size()==18);
    CHECK(late!=echo);CHECK(phrase!=expansion);
    for(const auto&style:djStyles)if(style.repeat){
        auto a=track("loop-A",120),b=track("loop-B",124);a.cues={a.cues.back()};b.cues={b.cues.front()};b.cues[0].bar=24;b.cues[0].time=b.downbeats[24];a.source["preferred_transition"]=style.id;
        auto p=choose(a,b);auto scene=Scene::from(p.scene);CHECK(p.family==style.id);CHECK(scene.voices[0].mode=="rendered_hold");
        const auto&v=scene.voices[0];CHECK(v.virtualSourceSeconds(scene.arrival-1,scene.sr)>=v.sourceLoopStart);CHECK(v.virtualSourceSeconds(scene.arrival-1,scene.sr)<v.sourceLoopEnd);
    }
    if(const char*path=std::getenv("LADIO_FX_REPORT")){std::ofstream out(path);out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot write catalog audio audit");}
}
int main(){try{planner();localEvidenceAndShortIncoming();barSubdivisionsAndMissingDetections();voiceAndRunway();localHarmonicMaterial();measuredHold();heldLeadClearance();musicalEventSelection();halfTime();independentIntroduction();instrumentalRunway();measuredPhase();neuralScenePreference();dsp();serialFX();catalogAudio();std::cout<<checks<<" independent planning/clock/phrase/hold/FX checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
