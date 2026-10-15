#include "audio.hpp"
#include "checksum.hpp"
namespace ladio {
std::shared_ptr<Session> Session::load(const std::filesystem::path&path){
    auto s=std::make_shared<Session>();s->manifest=readJson(path);auto &j=s->manifest;auto base=path.parent_path();
    if(j.at("schema_version")!=1)throw std::runtime_error("Unsupported session schema");s->sr=j.at("sample_rate");s->loop=j.value("loop",true);s->frames=j.at("total_frames");
    for(auto &t:j.at("tracks")){auto track=trackFromJson(t);Source source;auto pcm=resolve(base,track.pcm);if(t.contains("pcm_fnv1a64")&&checksumFile(pcm)!=t.at("pcm_fnv1a64").get<std::string>())throw std::runtime_error("Canonical original PCM revision mismatch");source.original=std::make_shared<PCMFile>(pcm);if(source.original->frames()!=track.frames)throw std::runtime_error("Original length/revision mismatch");s->tracks.push_back(track);s->sources.push_back(std::move(source));}
    for(auto &item:j.at("transitions")){Compiled c;c.plan=Plan::from(item.at("plan"));
        if(c.plan.sr!=s->sr||c.plan.a<0||c.plan.b<0||size_t(c.plan.a)>=s->tracks.size()||size_t(c.plan.b)>=s->tracks.size())throw std::runtime_error("Invalid plan resource reference");
        if(c.plan.scene.empty())recipeFor(c.plan.family);else c.scene=Scene::from(c.plan.scene);
        auto&p=c.plan;
        for(double v:{p.seconds,p.post,p.lead,p.outCue,p.inCue,p.vocalEnd,p.vocalOpen,p.swap,p.bStart,p.bassRamp,p.tailSeconds,p.captureStart,p.captureEnd,p.impact,p.wet,p.reverbWet,p.delayBeats,p.feedback})if(!std::isfinite(v)||v<0)throw std::runtime_error("Nonfinite/negative DSP parameter");
        for(auto&m:{p.mapA,p.mapB})if(!std::isfinite(m.initialRate)||!std::isfinite(m.steadyRate)||!std::isfinite(m.rampIn)||!std::isfinite(m.holdEnd)||!std::isfinite(m.rampOut)||m.initialRate<(m.clocked()?.5:.94)||m.initialRate>(m.clocked()?2.:1.06)||m.steadyRate<(m.clocked()?.5:.94)||m.steadyRate>(m.clocked()?2.:1.06)||m.rampIn<0||m.holdEnd<0||m.rampOut<0||m.rampIn>120||m.holdEnd>120||m.rampOut>120)throw std::runtime_error("Invalid time map");
        // Compiled scenes validate effect lifetime against their recovery
        // anchor. The legacy six-second tail ceiling must not reject an
        // already accepted longer spatial return when loading that scene.
        if(p.seconds>120||p.post>(c.scene?64:24)||p.lead>12||p.feedback>.55||p.pattern<0||p.pattern>7||p.bassRamp>12||p.tailSeconds>(c.scene&&c.scene->dj.enabled?std::min(64.,p.post):6.)||p.wet>1||p.reverbWet>1||p.filterStyle<0||p.filterStyle>2||p.relayStem<0||p.relayStem>3||p.captureStem<0||p.captureStem>3||p.delayBeats<.125||p.delayBeats>2||p.voiceClips.size()>4)throw std::runtime_error("Invalid DSP budget");
        if(c.scene){
            const auto&contract=c.scene->contract;
            if(contract.value("material_mode",std::string())=="original_preserve")throw std::runtime_error("End-to-start fallback is disabled in DJ mode");
            if(contract.contains("execution")&&(contract.at("execution").value("resource_state",std::string("draft"))!="ready"||contract.at("execution").value("vocal_plan_state",std::string("measured"))=="pending_window_analysis"))throw std::runtime_error("Pending scene analysis cannot be played");
            if(contract.contains("preview_critic")&&!contract.at("preview_critic").value("passed",false))throw std::runtime_error("Rejected scene cannot be played");
            if(c.scene->frames!=p.frames()||c.scene->sr!=s->sr||c.scene->contract.at("source_revisions")!=Json::array({s->tracks[p.a].hash,s->tracks[p.b].hash}))throw std::runtime_error("Scene identity/source mismatch");
            if(c.scene->contract.contains("source_pcm_checksums")&&c.scene->contract.at("source_pcm_checksums")!=Json::array({s->tracks[p.a].source.at("pcm_fnv1a64"),s->tracks[p.b].source.at("pcm_fnv1a64")}))throw std::runtime_error("Scene canonical source revision mismatch");
            for(const auto&voice:c.scene->voices){const auto&track=s->tracks[voice.deck?p.b:p.a];if(voice.sourceEnd>track.duration+1.1/s->sr)throw std::runtime_error("Scene voice exceeds original source extent");if(voice.id=="B.main"&&abs(voice.sourceStart+voice.map.sourceAt(double(voice.end-voice.start)/s->sr)-p.bSourceEnd())>1.1/s->sr)throw std::runtime_error("Native source handoff disagrees with B playhead");}
            if(item.contains("rendered_scene")){
                const auto&asset=item.at("rendered_scene");auto file=resolve(base,asset.at("pcm"));
                if(asset.at("channels")!=2||asset.at("frames")!=p.frames()||asset.at("fnv1a64")!=contract.at("rendered_scene_checksum")||checksumFile(file)!=asset.at("fnv1a64").get<std::string>())throw std::runtime_error("Rendered scene revision mismatch");
                c.rendered=std::make_shared<PCMFile>(file,2);
                if(c.rendered->frames()!=p.frames())throw std::runtime_error("Rendered scene extent mismatch");
            }else{
            const auto&assets=item.at("voice_assets");if(assets.size()!=c.scene->voices.size())throw std::runtime_error("Missing independent voice");
            for(size_t k=0;k<assets.size();k++){auto&asset=assets[k];auto file=resolve(base,asset.at("pcm"));auto&voice=c.scene->voices[k];if(asset.at("id")!=voice.id||(asset.contains("channel_map")?asset.at("channel_map")!=Json(preparedChannels(contract.at("voices")[k])):asset.at("channels")!=voice.channels)||checksumFile(file)!=asset.at("fnv1a64").get<std::string>())throw std::runtime_error("Scene asset identity/checksum mismatch");auto pack=std::make_shared<PCMFile>(file,asset.at("channels"),asset.value("channel_map",std::vector<int>{}));if(pack->frames()!=asset.at("frames").get<Frame>()||pack->frames()<c.scene->contract.at("voices")[k].at("prepare_frames").get<Frame>())throw std::runtime_error("Scene asset range incomplete");c.voices.push_back(pack);}
            if(c.scene->contract.contains("compiled_asset_checksums")){
                const auto&identities=c.scene->contract.at("compiled_asset_checksums");if(identities.size()!=assets.size())throw std::runtime_error("Scene compiled asset identity incomplete");
                for(size_t k=0;k<assets.size();k++)for(auto key:{"id","frames","channels","fnv1a64"})if(identities[k].at(key)!=assets[k].at(key))throw std::runtime_error("Scene compiled asset revision mismatch");
                for(size_t k=0;k<assets.size();k++)if(assets[k].value("channel_map",Json::array())!=identities[k].value("channel_map",Json::array()))throw std::runtime_error("Scene channel map mismatch");
            }
            }
        }else if(item.contains("a_pcm")){auto ap=resolve(base,item.at("a_pcm")),bp=resolve(base,item.at("b_pcm"));
            if(item.contains("a_fnv1a64")&&(checksumFile(ap)!=item.at("a_fnv1a64").get<std::string>()||checksumFile(bp)!=item.at("b_fnv1a64").get<std::string>()))throw std::runtime_error("Prepared PCM checksum mismatch");
            c.a=std::make_shared<PCMFile>(ap,10);c.b=std::make_shared<PCMFile>(bp,10);
            if(c.a->frames()<std::llround((c.plan.lead+c.plan.seconds)*s->sr)||c.b->frames()<c.plan.frames()-std::llround(c.plan.bStart*s->sr))throw std::runtime_error("Prepared range missing");}
        for(auto&clip:p.voiceClips){auto pack=clip.deck==0?c.a:c.b;if(clip.deck<0||clip.deck>1||!pack||!std::isfinite(clip.source)||!std::isfinite(clip.start)||!std::isfinite(clip.length)||!std::isfinite(clip.gain)||clip.source<0||clip.start<0||clip.length<=.035||clip.gain<0||clip.gain>1||(clip.source+clip.length)*s->sr>pack->frames()||clip.start+clip.length>p.lead+p.seconds)throw std::runtime_error("Unsafe phrase clip");}
        s->transitions.push_back(std::move(c));}
    Frame expected=0;for(auto &v:j.at("spans")){Span p{v.at("start_frame"),v.at("frames"),v.value("source_frame",Frame(0)),v.value("track",0),v.value("transition",-1)};
        if(p.start!=expected||p.length<=0||p.track<0||size_t(p.track)>=s->tracks.size()||p.transition>=int(s->transitions.size()))throw std::runtime_error("Noncontiguous/invalid session timeline");
        if(p.transition<0&&(p.source<0||p.source+p.length>s->tracks[p.track].frames))throw std::runtime_error("Source range unavailable");expected+=p.length;s->spans.push_back(p);}
    if(expected!=s->frames||s->spans.empty())throw std::runtime_error("Session length mismatch");s->loopStart=j.value("loop_start_frame",Frame(0));if(s->loopStart<0||s->loopStart>=s->frames)throw std::runtime_error("Invalid loop start");return s;
}
static float readPack(const PCMFile&p,double f,int c){Frame a=Frame(std::floor(f));float u=float(f-a);return p.sample(a,c)*(1-u)+p.sample(a+1,c)*u;}
void Engine::seek(Frame frame){
    Frame target=std::clamp<Frame>(frame,0,session_->frames);cursor_=target;transition_=-1;
    delay_.clear();reverb_.clear();bassA_.reset();bassB_.reset();sweep_.reset();sweepHigh_.reset();duck_=0;limiter_=1;
    resetSceneFilters();
    if(target>=session_->frames)return;
    const auto&span=session_->spans[session_->spanAt(target)];if(span.transition<0)return;
    const auto&p=session_->transitions[span.transition].plan;
    if(session_->transitions[span.transition].rendered)return;
    Frame history=std::llround(std::min(12.,p.tailSeconds+std::max(0.,p.captureEnd-p.captureStart)+.3)*session_->sr);
    cursor_=std::max(span.start,target-history);std::array<float,512>left{},right{};
    // State reconstruction is bounded and runs on the worker, never in pull().
    while(cursor_<target)render(left.data(),right.data(),uint32_t(std::min<Frame>(512,target-cursor_)));
}
std::array<float,2> Engine::mix(const Compiled&c,Frame local){
    roleBuses_={};
    const Plan&p=c.plan;const Track&ta=session_->tracks[p.a];const Track&tbTrack=session_->tracks[p.b];
    double t=double(local)/p.sr,begin=p.lead,end=begin+p.seconds,u=p.seconds>0?clamp((t-begin)/p.seconds):1;
    double beat=60/ta.bpm,pulse=60/((ta.bpm+tbTrack.bpm)*.5),tb=t-p.bStart;
    Frame fa=local,fb=local-std::llround(p.bStart*p.sr);bool overlay=p.blend;
    bool drumSwap=overlay&&p.useStems&&(p.family=="bass_swap"||((p.family=="vocal_handoff"||p.family=="phrase_blend")&&p.impact<end-.15));
    std::array<float,2>a{},b{},va{},vb{},ia{},ib{},drumsA{},drumsB{},capture{};
    for(int ch=0;ch<2;ch++){
        if(c.a&&t<end){a[ch]=c.a->sample(fa,ch);va[ch]=c.a->sample(fa,2+ch);}
        if(c.b&&tb>=0){b[ch]=c.b->sample(fb,ch);vb[ch]=c.b->sample(fb,2+ch);}
        ia[ch]=p.useStems?a[ch]-va[ch]:a[ch];ib[ch]=p.useStems?b[ch]-vb[ch]:b[ch];
        if(drumSwap){drumsA[ch]=c.a?c.a->sample(fa,4+ch):0;drumsB[ch]=c.b?c.b->sample(fb,4+ch):0;ia[ch]-=drumsA[ch];ib[ch]-=drumsB[ch];}
        capture[ch]=p.useStems&&c.a?c.a->sample(fa,2+p.captureStem*2+ch):a[ch];
    }
    double gainA=db(-14-ta.lufs),gainB=db(-14-tbTrack.lufs);
    double ga=1,gb=0,vga=1,vgb=0,lowA=1,lowB=0;
    if(overlay){
        double z=smooth(u);ga=std::cos(pi*.5*z);gb=std::sin(pi*.5*z);
        // Correlated, dense beds need less than equal-power overlap.
        double correlation=clamp((p.similarity-.60)/.40)*.65,normal=1/std::sqrt(1+2*correlation*ga*gb);
        ga*=normal;gb*=normal;vga=ga;vgb=gb;
        lowB=smooth((t-(p.swap-p.bassRamp*.5))/std::max(.08,p.bassRamp));lowA=1-lowB;
        if(p.useStems){
            vga=1-smooth((t-p.vocalEnd)/.100);
            vgb=smooth((t-(p.vocalOpen-.025))/.025);
            if(p.family=="vocal_handoff"||p.family=="acapella_carryover"||p.family=="mashup"||p.family=="call_response"){
                ga=1-smooth((t-begin)/std::min(p.seconds*.55,6*pulse));
                gb=smooth((t-begin)/std::max(.2,std::min(p.seconds*.45,4*pulse)));
            }
            if(p.family=="acapella_carryover"||p.family=="mashup")ga=1-smooth((t-begin)/(2*pulse));
            if(p.family=="percussion_bridge"||p.family=="harmonic_pivot"||p.family=="half_time"){
                double isolate=smooth((t-begin)/std::max(.2,2*pulse));
                for(int ch=0;ch<2;ch++)ia[ch]=float(ia[ch]*(1-isolate)+c.a->sample(fa,4+ch)*isolate);
                // One dominant drum layer through a groove or metric change.
                double drums=smooth((t-(p.swap-pulse*.5))/pulse);
                for(int ch=0;ch<2;ch++)ib[ch]-=float(c.b->sample(fb,4+ch)*(1-drums));
                ga*=1-drums;gb=std::max(gb,drums);
            }
            if(p.family=="melody_relay"){
                for(int ch=0;ch<2;ch++)ia[ch]=float(c.a->sample(fa,2+p.relayStem*2+ch)+c.a->sample(fa,4+ch)*(1-smooth(u*2)));
                vga=0;ga=1-smooth((u-.45)/.55);gb=smooth(u*2);
            }
            if(p.family=="breakdown_bridge"){
                double rhythm=smooth((u-.40)/.5);
                for(int ch=0;ch<2;ch++)ib[ch]-=float((c.b->sample(fb,4+ch)+c.b->sample(fb,6+ch))*(1-rhythm));
            }
            if(p.family=="full_stem_swap"){
                double drumB=smooth(u/.40),otherB=smooth((u-(p.pattern%2?.20:.40))/.50);
                for(int ch=0;ch<2;ch++){
                    ia[ch]=float(c.a->sample(fa,4+ch)*(1-drumB)+c.a->sample(fa,6+ch)*(1-lowB)+c.a->sample(fa,8+ch)*(1-otherB));
                    ib[ch]=float(c.b->sample(fb,4+ch)*drumB+c.b->sample(fb,6+ch)*lowB+c.b->sample(fb,8+ch)*otherB);
                }ga=gb=1;lowA=lowB=1;
            }
        }
    }else{
        // Fade A over a musical fraction; B's microfade ends BEFORE the kick.
        double fade=std::max(.075,std::min(.30,beat*.45));
        ga=1-smooth((t-(end-fade))/fade);gb=smooth((t-p.bStart)/std::max(.001,p.impact-p.bStart-.002));
        vga=1-smooth((t-std::min(p.vocalEnd,end-.11))/.10);
        vgb=p.useStems?gb*smooth((t-(p.vocalOpen-.025))/.025):gb;
        lowA=ga;lowB=p.useStems?gb*smooth((t-(p.impact-.045))/.035):gb;
        if(p.reset){double pause=std::min(.30,beat*.5);ga*=1-smooth((t-(end-pause-.065))/.065);vga*=ga;}
        if(p.useStems&&(p.family=="percussion_bridge"||p.family=="harmonic_pivot")){
            double isolate=smooth((t-begin)/std::max(.2,2*beat));for(int ch=0;ch<2;ch++)ia[ch]=float(ia[ch]*(1-isolate)+c.a->sample(fa,4+ch)*isolate);
        }
    }
    if(p.family=="crossfade"){
        double z=smooth(u);ga=1-z;gb=z;vga=ga;vgb=gb;lowA=ga;lowB=gb;
    }
    if(t<begin){ga=1;gb=0;vga=1;vgb=0;lowA=1;lowB=0;}
    if(t>=end){ga=0;gb=1;vga=0;vgb=1;lowA=0;lowB=1;}
    // A completed transition always restores the complete incoming original.
    // A stale phrase timestamp must not keep B instrumental through the post-roll.
    if(overlay&&p.useStems&&t>=begin&&t<end){vgb=smooth((t-(p.vocalOpen-.025))/.025);}
    if(p.family=="call_response"&&p.useStems&&t>=begin&&t<end){va={};vb={};vga=vgb=1;}
    if(p.useStems)for(auto&clip:p.voiceClips)if(t>=clip.start&&t<clip.start+clip.length){
        double dt=t-clip.start;float envelope=float(clip.gain*smooth(dt/.015)*(1-smooth((dt-(clip.length-.035))/.035)));
        auto pack=clip.deck==0?c.a:c.b;if(!pack)continue;
        for(int ch=0;ch<2;ch++){float v=readPack(*pack,(clip.source+dt)*p.sr,2+ch)*envelope;
            if(clip.deck==0)va[ch]=v;else if(p.family=="vocal_tease"){va[ch]+=v*float(gainB/gainA);}else vb[ch]=v;
            if(p.family=="call_response"&&clip.deck==0)capture[ch]=v;
        }
    }
    // Finite drum grammars replace percussion only. Voices retain their timeline.
    if(p.useStems&&c.a&&t>=begin&&t<end){
        if(p.family=="loop_extension"){
            double length=4*beat,cycle=std::fmod(t-begin,length),anchor=std::max(begin,end-8*beat),weight=smooth(u*3);
            for(int ch=0;ch<2;ch++){
                float v=readPack(*c.a,(anchor+cycle)*p.sr,ch)-readPack(*c.a,(anchor+cycle)*p.sr,2+ch);
                if(cycle<.008){float previous=readPack(*c.a,(anchor+length+cycle)*p.sr,ch)-readPack(*c.a,(anchor+length+cycle)*p.sr,2+ch);v=float(previous*(1-smooth(cycle/.008))+v*smooth(cycle/.008));}
                ia[ch]=float(ia[ch]*(1-weight)+v*weight);
            }
        }
        double buildEnd=drumSwap?p.impact:end;
        double buildStart=std::max(begin,buildEnd-(drumSwap?4:8)*beat),buildLength=buildEnd-buildStart;
        if((p.roll||p.pulseRamp||p.family=="gate_stutter")&&t>=buildStart&&t<buildEnd&&buildLength>.02){
            double x=t-buildStart,progress=x/buildLength;
            double phase=p.pulseRamp?(ta.bpm*x+(tbTrack.bpm-ta.bpm)*x*x/(2*buildLength))/60:x/beat;
            static constexpr double grammar[8][4]={{1,1,.5,.25},{1,.5,.5,.25},{.5,1,.5,.25},{1,.75,.5,.5},{1,1,.25,.25},{.75,.75,.5,.25},{1,.5,.25,.125},{.5,.5,.25,.25}};
            double division=grammar[p.pattern%8][std::clamp(int(progress*4),0,3)];
            if(p.pulseRamp&&progress<.70)division=1;
            double cycle=std::fmod(phase,division)*beat,length=division*beat,anchor=std::max(0.,buildEnd-(drumSwap?4:8)*beat);
            double weight=(p.pulseRamp?.95:.72)*smooth(progress/.12);
            for(int ch=0;ch<2;ch++){
                float normal=c.a->sample(fa,4+ch),rolled=readPack(*c.a,(anchor+cycle)*p.sr,4+ch);
                if(cycle<.006)rolled=float(rolled*smooth(cycle/.006)+readPack(*c.a,(anchor+length+cycle)*p.sr,4+ch)*(1-smooth(cycle/.006)));
                if(p.family=="gate_stutter"){
                    double gate=smooth(cycle/.006)*(1-smooth((cycle-length*.66)/.008));
                    if(p.pattern%3==2){double reverse=anchor+length-cycle;rolled=readPack(*c.a,reverse*p.sr,4+ch);}
                    rolled*=float(gate);
                }
                if(drumSwap)drumsA[ch]+=float((rolled-normal)*weight);
                else ia[ch]+=float((rolled-normal)*weight);
            }
        }
        if(p.stop&&t>=end-std::min(.65,beat)&&t<end){
            double length=std::min(.65,beat),x=t-(end-length),z=x/length;
            double source=p.pattern%2?(end-length*.10)-(x+1.5*x*x/length):(end-length)+x-x*x/(2*length);
            double weight=smooth(z/.12),fade=1-smooth((z-.85)/.15);
            for(int ch=0;ch<2;ch++){
                float spin=readPack(*c.a,source*p.sr,ch)-readPack(*c.a,source*p.sr,2+ch);
                ia[ch]=float(ia[ch]*(1-weight)+spin*weight*fade);
            }
        }
    }
    double incomingVoice=std::max(abs(vb[0]),abs(vb[1]))*gainB*vgb;
    double focus=std::max(abs(va[0]),abs(va[1]))*gainA*vga;
    duck_+=(focus-duck_)*(focus>duck_?duckAttack_:duckRelease_);
    double bedDuck=p.family=="acapella_carryover"||p.family=="mashup"?1-.15*clamp(duck_*5):1;
    std::array<float,2>y{},fx{};
    double build=smooth((t-(end-8*beat))/std::max(.1,8*beat));
    bool filter=!overlay&&(p.family=="drop_switch"||p.family=="filter_sweep"||p.pulseRamp);
    double cutoff=filter?(p.filterStyle==1?70*std::pow(22.,build):18000*std::pow(.045,build)):1000;
    float sweepAlpha=float(1-std::exp(-2*pi*cutoff/p.sr));
    float highAlpha=float(1-std::exp(-2*pi*(140+build*420)/p.sr));
    for(int ch=0;ch<2;ch++){
        // EQ works on the unscaled instrumental, then independent vocal gain.
        // No division by a fading gain: a solo voice can never acquire a bass boost.
        double al=bassA_.low(ia[ch],ch,bassAlpha_),bl=bassB_.low(ib[ch],ch,bassAlpha_);
        double av=(ia[ch]-al)*ga+al*lowA,bv=((ib[ch]-bl)*gb+bl*lowB)*bedDuck;
        if(filter){
            double filtered=sweep_.low(float(av),ch,sweepAlpha);
            if(p.filterStyle==1)filtered=av-filtered;
            if(p.filterStyle==2)filtered-=sweepHigh_.low(float(filtered),ch,highAlpha);
            av=av*(1-build*.92)+filtered*build*.92;
        }
        buses_[0][ch]=float((av+va[ch]*vga)*gainA);
        buses_[1][ch]=float((bv+vb[ch]*vgb)*gainB);
        if(drumSwap){
            double handover=smooth((t-(p.impact-pulse*.5))/std::max(.025,pulse*.5-.008));
            if(t<begin)handover=0;
            if(t>=end)handover=1;
            buses_[0][ch]+=float(drumsA[ch]*(1-handover)*gainA);
            buses_[1][ch]+=float(drumsB[ch]*handover*gainB);
        }
        y[ch]=buses_[0][ch]+buses_[1][ch];
        double send=p.echo||p.family=="reverb_tail"?smooth((t-p.captureStart)/.055)*(1-smooth((t-p.captureEnd)/.060)):0;
        fx[ch]=float(capture[ch]*gainA*send);
    }
    double tail=1-smooth((t-(p.captureEnd+p.tailSeconds*.45))/std::max(.1,p.tailSeconds*.55));
    double wetDuck=1-.65*clamp(incomingVoice*4.5);
    if(p.reset){double pause=std::min(.3,beat*.5);tail*=1-.55*smooth((t-(end-pause))/.03)*(1-smooth((t-end)/.15));}
    auto delayed=delay_.process(fx,p.sr*beat*p.delayBeats,float(p.feedback),float(p.wet*tail*wetDuck),p.filterStyle);
    auto space=reverb_.process(fx);
    for(int ch=0;ch<2;ch++){buses_[2][ch]=float(delayed[ch]+space[ch]*p.reverbWet*tail*wetDuck);y[ch]+=buses_[2][ch];}
    return y;
}
std::array<float,2> Engine::mixScene(const Compiled&c,Frame local){
    const Scene&s=*c.scene;buses_={};roleBuses_={};
    if(c.rendered){std::array<float,2> y{c.rendered->sample(local,0),c.rendered->sample(local,1)};buses_[1]=y;return y;}
    std::array<float,2> independentSend{};
    std::array<std::array<float,2>,2> djInput{};std::array<double,2> djGain{};
    for(size_t k=0;k<s.voices.size();k++){
        const auto&v=s.voices[k];if(local<v.start||local>=v.end)continue;const auto&pack=*c.voices[k];
        if(s.dj.enabled&&v.originalOnly){
            // Independent B clips have separate envelopes before their shared
            // processing bus. A retains its post-DSP dry fader so its captured
            // auxiliary tail is not cut when the source releases.
            const bool layered=s.voices.size()>2&&v.deck==1;
            double envelope=v.roles[4].at(local);
            for(int ch=0;ch<2;ch++)djInput[v.deck][ch]+=float(readPack(pack,v.position(local,4),ch)*v.gain*(layered?envelope:1.));
            djGain[v.deck]=layered?1.:envelope;continue;
        }
        if(s.effects.enabled&&v.deck==s.effects.sourceDeck&&v.id==(v.deck?"B.main":"A.main"))for(int ch=0;ch<2;ch++)independentSend[ch]+=float(readPack(pack,v.position(local,4),ch)*v.gain*s.effects.capture.at(local));
        double gain=v.gain;
        for(int stem=0;stem<5;stem++){
            if(v.originalOnly&&stem!=4)continue;
            double g=v.roles[stem].at(local)*gain;if(g<=0)continue;
            double position=double(v.position(local,stem));
            const auto&filter=v.softFilters[stem];
            // Immutable automation cannot reactivate a filter after its last
            // dry knot. Warm its state before that point; bypass it afterwards.
            bool filterActive=!filter.wet.knots.empty()&&!(local>=filter.wet.knots.back().frame&&filter.wet.knots.back().value==0);
            float filterWet=filterActive?float(filter.wet.at(local)):0;
            float hp=filterActive?(filter.hp.values.empty()?filter.hpAlpha:filter.hp.at(local)):0,lp=filterActive?(filter.lp.values.empty()?filter.lpAlpha:filter.lp.at(local)):0;
            for(int ch=0;ch<2;ch++){
                float value=readPack(pack,position,v.originalOnly?ch:2+stem*2+ch);
                if(v.loop>0){
                    double cycle=v.cycleAt(local,stem);
                    double remaining=double(cycle)-(position-v.offset),seam=s.sr*.005;
                    if(remaining<seam){double previous=readPack(pack,v.offset-remaining,2+stem*2+ch);value+=float(smooth(1-remaining/seam))*(float(previous)-value);}
                }
                if(v.roll&&stem==1&&local>=v.rollStart&&v.rollFoundationMix>0){
                    // Subdivision decorates the held groove, rather than
                    // replacing its kick with whatever starts a tiny slice.
                    double held=v.position(local,2),cycle=v.loopPeriod>0?v.loopPeriod:double(v.loop);
                    float foundation=readPack(pack,held,4+ch);
                    double remaining=cycle-(held-v.offset),seam=s.sr*.005;
                    if(remaining<seam)foundation+=float(smooth(1-remaining/seam))*(readPack(pack,v.offset-remaining,4+ch)-foundation);
                    double mix=v.rollFoundationMix*smooth(double(local-v.rollStart)/std::max(1.,s.sr*.02));
                    value+=float(mix)*(foundation-value);
                }
                if(stem==3&&v.lpAlpha>0){
                    float high=textureHigh_[k*5+stem].low(value,ch,v.hpAlpha);
                    float low=textureLow_[k*5+stem].low(value-high,ch,v.lpAlpha);
                    value+=float(1-smooth(double(local-v.filterStart)/std::max<Frame>(1,v.filterEnd-v.filterStart)))*(low-value);
                }
                if(filterActive){
                    size_t index=k*5+stem;float low=value;
                    if(hp>0){low-=roleHigh_[index].low(low,ch,hp);if(filter.stages==2)low-=roleHigh2_[index].low(low,ch,hp);}
                    if(lp>0){low=roleLow_[index].low(low,ch,lp);if(filter.stages==2)low=roleLow2_[index].low(low,ch,lp);}
                    value+=filterWet*(low-value);
                }
                roleBuses_[v.deck][stem][ch]+=float(value*g);
            }
        }
    }
    for(int deck=0;deck<2;deck++)for(int stem=0;stem<5;stem++)for(int ch=0;ch<2;ch++)buses_[deck][ch]+=roleBuses_[deck][stem][ch];
    if(s.dj.enabled){
        std::array<float,2> result{};double quarter=s.transport.quarterAt(local);
        for(int deck=0;deck<2;deck++){
            djDecks_[deck].configure(s.dj.decks[deck]);
            auto processed=djDecks_[deck].process(djInput[deck],s.dj.decks[deck],local,quarter);
            for(int ch=0;ch<2;ch++){
                roleBuses_[deck][4][ch]=buses_[deck][ch]=float(processed.dry[ch]*djGain[deck]);
                buses_[2][ch]+=processed.wet[ch];result[ch]+=buses_[deck][ch]+processed.wet[ch];
            }
        }
        return result;
    }
    if(s.effects.enabled){
        const auto&fx=s.effects;double wet=fx.wet.at(local);
        // Dry and send are independent. A disappearing deck gain cannot cut
        // off the captured phrase; the bounded return owns the tail lifetime.
        double bpm=fx.tailBpm;
        if(!s.reset&&local<s.arrival)for(const auto&v:s.voices)if(v.id=="B.main")bpm=fx.tailBpm*v.map.rateAt(double(local-v.start)/s.sr);
        auto delay=delay_.process(independentSend,s.sr*60/bpm*fx.delayBeats,float(fx.feedback),float(fx.delayWet),1);
        auto room=reverb_.process(independentSend);std::array<float,2> result{},send{};
        for(int ch=0;ch<2;ch++)send[ch]=float(delay[ch]+room[ch]*fx.reverbWet);
        auto coloured=tailColour_.process(send,fx,s.transport.quarterAt(local));
        for(int ch=0;ch<2;ch++){buses_[2][ch]=float(wet*coloured[ch]);result[ch]=buses_[0][ch]+buses_[1][ch]+buses_[2][ch];}
        return result;
    }
    std::array<float,2>send{};double t=double(local)/s.sr,phraseEnd=s.voicePhraseEnd;
    // One decoration per scene. A loop lift's accent is its percussion roll,
    // not a second deck fade plus delay. Pulse resets never carry old delay.
    bool space=s.fx&&s.accent=="reverb";
    // Capture the sounding last syllable, before the independent dry fade.
    // A fade endpoint can be in silence and cannot seed a useful vocal tail.
    if(space){double capture=smooth((t-(phraseEnd-.30))/.04)*(1-smooth((t-(phraseEnd-.015))/.04));for(int ch=0;ch<2;ch++)send[ch]=float(roleBuses_[0][0][ch]*capture);}
    double end=s.fxEnd;
    bool tailActive=space&&t>=phraseEnd-.30&&t<end;
    auto tail=tailActive?reverb_.process(send):std::array<float,2>{};
    if(tailActive){double voice=std::clamp(std::max(abs(roleBuses_[1][0][0]),abs(roleBuses_[1][0][1]))/.06,0.,1.);duck_+=(voice-duck_)*(voice>duck_?duckAttack_:duckRelease_);}
    double wet=tailActive?.32*(1-.5*duck_)*(1-smooth((t-(end-.25))/.25)):0;
    std::array<float,2> y{};for(int ch=0;ch<2;ch++){buses_[2][ch]=float(tail[ch]*wet);y[ch]=buses_[0][ch]+buses_[1][ch]+buses_[2][ch];}return y;
}
void Engine::render(float*left,float*right,uint32_t n,float*diagnostics,float*roles){
    for(uint32_t k=0;k<n;k++){
        if(cursor_>=session_->frames){left[k]=right[k]=0;if(diagnostics)std::fill_n(diagnostics+k*6,6,0.f);if(roles)std::fill_n(roles+k*20,20,0.f);continue;}
        const Span&s=session_->spans[session_->spanAt(cursor_)];std::array<float,2> y{};
        if(s.transition>=0){if(transition_!=s.transition){transition_=s.transition;delay_.clear();reverb_.clear();bassA_.reset();bassB_.reset();sweep_.reset();sweepHigh_.reset();duck_=0;resetSceneFilters();}auto&c=session_->transitions[s.transition];y=c.scene?mixScene(c,cursor_-s.start):mix(c,cursor_-s.start);}
        else{transition_=-1;buses_={};roleBuses_={};float gain=float(db(-14-session_->tracks[s.track].lufs));Frame frame=s.source+cursor_-s.start;for(int c=0;c<2;c++)y[c]=buses_[0][c]=session_->sources[s.track].original->sample(frame,c)*gain;}
        // Immediate linked-stereo safety gain with release. No hard clipping/distortion.
        double peak=std::max(abs(y[0]),abs(y[1])),target=peak>db(-1.8)?db(-1.8)/peak:1;
        nonfiniteFrames_+=!std::isfinite(y[0])||!std::isfinite(y[1]);inputPeak_=std::max(inputPeak_,peak);renderedFrames_++;
        if(s.transition>=0&&session_->transitions[s.transition].rendered)limiter_=1;
        else if(target<limiter_)limiter_=target;else limiter_+=(target-limiter_)*limiterRelease_;
        minSafetyGain_=std::min(minSafetyGain_,limiter_);limitedFrames_+=limiter_<.999;
        left[k]=std::isfinite(y[0])?float(y[0]*limiter_):0;right[k]=std::isfinite(y[1])?float(y[1]*limiter_):0;cursor_++;
        if(diagnostics)for(int bus=0;bus<3;bus++)for(int ch=0;ch<2;ch++)diagnostics[k*6+bus*2+ch]=float(buses_[bus][ch]*limiter_);
        if(roles)for(int deck=0;deck<2;deck++)for(int stem=0;stem<5;stem++)for(int ch=0;ch<2;ch++)roles[k*20+deck*10+stem*2+ch]=float(roleBuses_[deck][stem][ch]*limiter_);
    }
}
}
