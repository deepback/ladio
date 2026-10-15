#include "scene.hpp"
namespace ladio {
namespace {
FilterAutomation filterCurve(const Json&points,int sr,Frame extent,bool compile){
    std::vector<Knot> knots;Frame previous=-1;
    for(const auto&point:points){
        Knot knot{point.at(0),point.at(1)};
        if(knot.frame<0||knot.frame>extent||knot.frame<=previous||!std::isfinite(knot.value)||knot.value<15||knot.value>sr*.49)throw std::runtime_error("Invalid log-frequency cutoff automation");
        knot.value=std::log(knot.value);knots.push_back(knot);previous=knot.frame;
    }
    if(knots.size()<2)throw std::runtime_error("A cutoff sweep needs two frequency knots");
    FilterAutomation out;out.first=knots.front().frame;out.last=knots.back().frame;
    if(!compile)return out;
    Envelope logHz{std::move(knots)};
    for(Frame f=out.first;;f=std::min(f+FilterAutomation::stride,out.last)){
        out.values.push_back(float(1-std::exp(-2*pi*std::exp(logHz.at(f))/sr)));
        if(f==out.last)break;
    }
    return out;
}
}
DJLane compileDJLane(const Json&points,Frame frames,double fallback,double low,double high,bool logarithmic,bool compile,int sr){
    struct P{Frame frame;double value;std::string shape;bool linearFrequency=false;double exponent=1;};std::vector<P> knots;
    if(!points.is_null())for(const auto&p:points){
        P k{p.at(0),p.at(1),p.size()>2?p.at(2).get<std::string>():"smooth"};
        if(k.shape.starts_with("in:")||k.shape.starts_with("out:")){
            size_t used=0;auto number=k.shape.substr(k.shape.find(':')+1);
            k.exponent=std::stod(number,&used);
            if(used!=number.size()||!std::isfinite(k.exponent)||k.exponent<.25||k.exponent>8)throw std::runtime_error("Invalid DJ curve power");
            k.shape=k.shape.starts_with("in:")?"in":"out";
        }
        if(p.size()>3){if(p.at(3)!="linear-frequency")throw std::runtime_error("Unknown DJ frequency domain");k.linearFrequency=true;}
        if(k.frame<0||k.frame>frames||(!knots.empty()&&k.frame<=knots.back().frame)||!std::isfinite(k.value)||k.value<low||k.value>high||
           (k.shape!="smooth"&&k.shape!="linear"&&k.shape!="accelerate"&&k.shape!="decelerate"&&k.shape!="in"&&k.shape!="out"))throw std::runtime_error("Invalid DJ automation lane: knot="+p.dump()+"; previous="+std::to_string(knots.empty()?-1:knots.back().frame)+"; extent="+std::to_string(frames));
        knots.push_back(k);
    }
    if(knots.empty())knots.push_back({0,fallback,"smooth"});
    DJLane lane;lane.end=frames;lane.constant=float(knots.front().value);
    for(const auto&k:knots){if(k.value!=0)break;lane.zeroUntil=k.frame;}
    for(auto i=knots.rbegin();i!=knots.rend()&&i->value==0;++i)lane.zeroFrom=i->frame;
    if(!compile)return lane;
    size_t index=1;
    for(Frame f=0;;f=std::min(frames,f+DJLane::stride)){
        while(index<knots.size()&&f>=knots[index].frame)++index;
        double value=knots.front().value;
        if(index==knots.size())value=knots.back().value;
        else if(f>knots.front().frame){const auto&a=knots[index-1];const auto&b=knots[index];double u=double(f-a.frame)/(b.frame-a.frame);
            if(b.shape=="in")u=std::pow(u,b.exponent);else if(b.shape=="out")u=1-std::pow(1-u,b.exponent);
            else if(b.shape=="accelerate")u=u*u*u;else if(b.shape=="decelerate")u=1-std::pow(1-u,3);else if(b.shape=="smooth")u=smooth(u);
            value=logarithmic&&!b.linearFrequency?std::exp(std::log(a.value)+(std::log(b.value)-std::log(a.value))*u):a.value+(b.value-a.value)*u;
        }
        // A TPT state-variable filter uses g=tan(pi*f/fs), not an audio gain.
        lane.values.push_back(float(logarithmic?std::tan(pi*value/sr):value));
        if(f==frames)break;
    }
    return lane;
}
Scene Scene::from(const Json&j,bool compileFilters){
    Scene s;s.contract=j;s.version=j.at("contract_version");if(s.version!=1)throw std::runtime_error("Unsupported CompiledScene contract; explicit migration required");
    s.family=j.at("family");if(!sceneFamily(s.family))throw std::runtime_error("Unknown scene family");
    s.sr=j.at("sample_rate");s.frames=j.at("duration_frames");s.bpm=j.at("transport").at("bpm");s.arrival=j.at("anchors").at("B_full_arrival").at("output_frame");s.fx=j.at("effects_enabled");s.reset=j.at("transport").value("pulse_reset",false);
    s.voiceRelease=j.at("anchors").at("A_vocal_release").at("seconds");s.fxEnd=std::min(s.voiceRelease+.8,double(s.frames)/s.sr-.12);
    s.voicePhraseEnd=j.at("anchors").contains("A_vocal_phrase_end")?j.at("anchors").at("A_vocal_phrase_end").at("seconds").get<double>():s.voiceRelease;
    if(!std::isfinite(s.voicePhraseEnd)||s.voicePhraseEnd<0||s.voicePhraseEnd>s.voiceRelease)throw std::runtime_error("Invalid outgoing vocal tail capture");
    s.accent=j.value("accent",std::string("none"));if(s.accent!="none"&&s.accent!="roll"&&s.accent!="reverb")throw std::runtime_error("Unknown scene accent");
    if(s.sr<8000||s.sr>192000||s.frames<=0||s.frames>s.sr*240LL||s.arrival<0||s.arrival>=s.frames||!std::isfinite(s.bpm)||s.bpm<30||s.bpm>300)throw std::runtime_error("Invalid scene transport");
    s.transport.sr=s.sr;double prevTime=-1,prevQuarter=-1;
    for(auto&part:j.at("transport").at("segments")){
        MusicalTransport::Segment v{part.at("seconds"),part.at("quarter"),part.at("bpm"),part.at("meter_numerator"),part.at("meter_denominator")};
        v.endBpm=part.value("end_bpm",v.bpm);v.rampSeconds=part.value("ramp_seconds",0.);v.smoothRamp=part.value("curve","")=="smoothstep";
        if(!std::isfinite(v.seconds)||!std::isfinite(v.quarter)||!std::isfinite(v.bpm)||v.seconds<0||v.quarter<0||v.bpm<30||v.bpm>300||v.seconds<=prevTime||v.quarter<=prevQuarter||v.numerator<1||v.numerator>16||(v.denominator!=2&&v.denominator!=4&&v.denominator!=8&&v.denominator!=16))throw std::runtime_error("Invalid tempo/meter segment");
        if(!std::isfinite(v.endBpm)||v.endBpm<30||v.endBpm>300||!std::isfinite(v.rampSeconds)||v.rampSeconds<0||v.rampSeconds>240)throw std::runtime_error("Invalid transport ramp");
        if(!s.transport.segments.empty()){auto&p=s.transport.segments.back();if(abs(p.quarter+p.quarters(v.seconds-p.seconds)-v.quarter)>1e-6)throw std::runtime_error("Discontinuous quarter transport");}
        s.transport.segments.push_back(v);prevTime=v.seconds;prevQuarter=v.quarter;
    }
    if(s.transport.segments.empty()||s.transport.segments.front().seconds!=0||s.transport.segments.front().quarter!=0)throw std::runtime_error("Missing transport origin");
    if(j.contains("dj_program")){
        const auto&program=j.at("dj_program");
        if((program.at("version")!=1&&program.at("version")!=2&&program.at("version")!=3)||program.at("decks").size()!=2||j.contains("effect_chain"))throw std::runtime_error("Invalid DJ signal graph");
        s.dj.enabled=true;
        for(int deck=0;deck<2;deck++){
            const auto&d=program.at("decks").at(deck);auto&p=s.dj.decks[deck];
            auto lane=[&](const char*name,double fallback,double lo,double hi,bool hz=false){return compileDJLane(d.value(name,Json{}),s.frames,fallback,lo,hi,hz,compileFilters,s.sr);};
            auto parameter=[&](const char*name,double fallback,double lo,double hi){double x=d.value(name,fallback);if(!std::isfinite(x)||x<lo||x>hi)throw std::runtime_error(std::string("Invalid DJ parameter: ")+name);return x;};
            p.highpass=lane("highpass_hz",22,15,s.sr*.44,true);p.lowpass=lane("lowpass_hz",std::min(19500.,s.sr*.44),15,s.sr*.44,true);
            p.filterWet=lane("filter_wet",0,0,1);p.eqWet=lane("eq_wet",0,0,1);p.flangerMix=lane("flanger_mix",0,0,.8);p.gateDepth=lane("gate_depth",0,0,.95);
            p.delaySend=lane("delay_send",0,0,1);p.reverbSend=lane("reverb_send",0,0,1);
            p.delayReturn=lane("delay_return",0,0,1);p.reverbReturn=lane("reverb_return",0,0,1);
            p.resonance=parameter("resonance",.707,.5,1.5);p.delayFeedback=parameter("delay_feedback",.36,0,.72);
            p.flangerFeedback=parameter("flanger_feedback",.28,0,.55);p.flangerQuarters=parameter("flanger_quarters",8,.5,32);
            p.gateQuarters=parameter("gate_quarters",.5,.125,4);p.gateDuty=parameter("gate_duty",.65,.15,.85);
            p.reverbLowSeconds=parameter("reverb_low_seconds",2.5,.2,8);p.reverbHighSeconds=parameter("reverb_high_seconds",1.6,.15,8);p.predelaySeconds=parameter("predelay_seconds",.025,0,.18);
            p.sendPreFilter=d.value("send_before_filter",false);p.delayToReverb=parameter("delay_to_reverb",0,0,.5);
            p.serial=program.at("version").get<int>()>=2;
            if(p.serial){
                p.inputGain=lane("input_gain",1,0,1);p.dryGain=lane("dry_gain",1,0,1);
                p.auxSend=lane("aux_send",0,0,1);p.auxReturn=lane("aux_return",0,0,1);
                p.delayMix=lane("delay_mix",0,0,1);p.reverbMix=lane("reverb_mix",0,0,1);
                p.roomSend=lane("room_send",0,0,1);p.roomReturn=lane("room_return",0,0,1);
                p.echoSend=lane("echo_send",0,0,1);p.echoReturn=lane("echo_return",0,0,1);
                auto active=[&](const char*name){if(d.contains(name))for(const auto&k:d.at(name))if(k.at(1).get<double>()>0)return true;return false;};
                p.auxActive=active("aux_return");p.auxReverbActive=p.auxActive&&active("reverb_mix");p.roomActive=active("room_return");p.echoActive=active("echo_return");
                p.delayLowpass=parameter("delay_lowpass_hz",3500,20,s.sr*.44);p.delayLowpassAlpha=1-std::exp(-2*pi*p.delayLowpass/s.sr);
                p.reverbMinSeconds=parameter("reverb_min_seconds",.022,.001,.2);p.reverbMaxSeconds=parameter("reverb_max_seconds",.093,.001,.23);
                if(p.reverbMinSeconds>p.reverbMaxSeconds)throw std::runtime_error("Invalid reverb reflection interval");
                p.reverbGain=std::pow(10.,parameter("reverb_gain_db",0,-20,6)/20);
                p.roomLowSeconds=parameter("room_low_seconds",6,.2,20);p.roomHighSeconds=parameter("room_high_seconds",3.5,.2,20);
                p.flangerCenterSeconds=parameter("flanger_center_seconds",.004,.001,.012);p.flangerDepthSeconds=parameter("flanger_depth_seconds",.002,0,.01);
                if(p.flangerDepthSeconds>=p.flangerCenterSeconds)throw std::runtime_error("Flanger depth exceeds center delay");
                p.echoFeedback=parameter("echo_feedback",.5,0,.72);
            }
            double hp=parameter("tail_highpass_hz",260,20,1800),lp=parameter("tail_lowpass_hz",5800,200,s.sr*.44);
            if(hp>=lp)throw std::runtime_error("Empty DJ tail spectrum");p.tailHP=1-std::exp(-2*pi*hp/s.sr);p.tailLP=1-std::exp(-2*pi*lp/s.sr);
            double gain=parameter("eq_gain_db",0,-12,3),hz=parameter("eq_frequency_hz",1700,40,s.sr*.4),q=parameter("eq_q",.8,.3,4);
            double A=std::pow(10.,gain/40),w=2*pi*hz/s.sr,alpha=std::sin(w)/(2*q),a0=1+alpha/A;
            p.eq={(1+alpha*A)/a0,-2*std::cos(w)/a0,(1-alpha*A)/a0,-2*std::cos(w)/a0,(1-alpha/A)/a0};
            double beats=parameter("delay_quarters",.75,.125,4);p.delayFrames.end=s.frames;
            if(compileFilters)for(Frame f=0;;f=std::min(s.frames,f+DJLane::stride)){
                double quarter=s.transport.quarterAt(f),earlier=quarter-beats;
                double then=earlier<0?earlier*60/s.transport.segments.front().bpm*s.sr:double(s.transport.frameAt(earlier));
                p.delayFrames.values.push_back(float(std::clamp(f-then,1.,s.sr*8.-2)));if(f==s.frames)break;
            }
            // Every expressive route must finish before the original B join.
            for(auto name:{"delay_send","reverb_send","delay_return","reverb_return","flanger_mix","gate_depth","filter_wet","eq_wet","aux_send","aux_return","delay_mix","reverb_mix","room_send","room_return","echo_send","echo_return"}){
                if(!d.contains(name))continue;const auto&points=d.at(name);if(points.empty()||points.back().at(0).get<Frame>()>s.frames-64||points.back().at(1).get<double>()!=0)throw std::runtime_error("DJ effect outlives original recovery");
            }
            if(p.serial){
                auto last=[&](const char*name,double fallback){return d.contains(name)?d.at(name).back().at(1).get<double>():fallback;};
                if(deck==1&&(last("input_gain",1)!=1||last("dry_gain",1)!=1))throw std::runtime_error("DJ graph mutes recovered B");
            }
        }
    }
    if(j.contains("effect_chain")){
        const auto&e=j.at("effect_chain");auto&fx=s.effects;
        if(e.at("version")!=1)throw std::runtime_error("Unknown independent effect contract");
        fx.enabled=true;fx.sourceDeck=e.at("source_deck");fx.delayBeats=e.at("delay_beats");fx.delayWet=e.at("delay_wet");fx.feedback=e.at("feedback");fx.reverbWet=e.at("reverb_wet");fx.tailBpm=e.at("tail_pulse_bpm");
        if(fx.sourceDeck<0||fx.sourceDeck>1||!std::isfinite(fx.delayBeats)||fx.delayBeats<.125||fx.delayBeats>2||!std::isfinite(fx.tailBpm)||fx.tailBpm<30||fx.tailBpm>300)throw std::runtime_error("Invalid effect timing");
        for(double x:{fx.delayWet,fx.feedback,fx.reverbWet})if(!std::isfinite(x)||x<0||x>.55)throw std::runtime_error("Invalid effect feedback/gain");
        auto envelope=[&](const char*name,Envelope&out){Frame previous=-1;for(const auto&v:e.at(name)){Knot k{v.at(0),v.at(1)};if(k.frame<0||k.frame>s.frames||k.frame<=previous||!std::isfinite(k.value)||k.value<0||k.value>1)throw std::runtime_error("Invalid effect envelope");out.knots.push_back(k);previous=k.frame;}if(out.knots.size()<2)throw std::runtime_error("Missing effect automation");};
        envelope("capture",fx.capture);envelope("return",fx.wet);
        if(fx.capture.at(s.frames-64)!=0||fx.wet.at(s.frames-64)!=0)throw std::runtime_error("Effect outlives scene recovery");
        auto parameter=[&](const char*name,double fallback,double lo,double hi){double x=e.value(name,fallback);if(!std::isfinite(x)||x<lo||x>hi)throw std::runtime_error(std::string("Invalid tail parameter: ")+name);return x;};
        double hp=parameter("tail_highpass_hz",220,20,2000),lp=parameter("tail_lowpass_hz",4200,200,s.sr*.45);
        if(lp<=hp)throw std::runtime_error("Tail filter passband is empty");
        fx.tailHP=1-std::exp(-2*pi*hp/s.sr);fx.tailLP=1-std::exp(-2*pi*lp/s.sr);
        fx.flangerWet=parameter("flanger_wet",0,0,.3);fx.flangerFeedback=parameter("flanger_feedback",0,0,.35);fx.flangerQuarters=parameter("flanger_quarters",8,1,32);
        fx.gateDepth=parameter("gate_depth",0,0,.65);fx.gateQuarters=parameter("gate_quarters",.5,.125,4);fx.gateDuty=parameter("gate_duty",.65,.15,.85);
        double hz=parameter("eq_frequency_hz",1600,40,s.sr*.4),q=parameter("eq_q",.7,.3,4),gain=parameter("eq_gain_db",0,-12,3);
        // Standard peaking biquad, designed independently from public DSP
        // equations. No Audio Unit, vendor coefficients or plugin required.
        double A=std::pow(10.,gain/40),w=2*pi*hz/s.sr,alpha=std::sin(w)/(2*q),a0=1+alpha/A;
        fx.eq={(1+alpha*A)/a0,-2*std::cos(w)/a0,(1-alpha*A)/a0,-2*std::cos(w)/a0,(1-alpha/A)/a0};
    }
    const char*names[]={"vocals","drums","bass","other","residual"};
    std::vector<std::string> ids;
    for(auto&voice:j.at("voices")){
        SceneVoice v;v.id=voice.at("id");v.deck=voice.at("deck");v.mode=voice.at("playhead_mode");v.start=voice.at("start_frame");v.end=voice.at("end_frame");v.offset=voice.value("pack_offset_frames",Frame(0));v.loop=voice.value("loop_frames",Frame(0));v.sourceStart=voice.at("source_start_seconds");v.sourceEnd=voice.at("source_end_seconds");v.map=TimeMap::from(voice.at("time_map"));v.roll=voice.value("percussion_roll",false);v.rollStart=voice.value("roll_start_frame",v.end);
        v.loopPeriod=voice.value("loop_period_frames",double(v.loop));v.gain=db(voice.value("gain_db",0.));v.originalOnly=voice.value("original_only",false);v.channels=voice.value("channels",12);
        v.rollFoundationMix=voice.value("roll_foundation_mix",0.);
        if(!std::isfinite(v.rollFoundationMix)||v.rollFoundationMix<0||v.rollFoundationMix>1)throw std::runtime_error("Invalid roll foundation mix");
        if(voice.contains("source_loop")){v.sourceLoopStart=voice["source_loop"].at("start");v.sourceLoopEnd=voice["source_loop"].at("end");if(!std::isfinite(v.sourceLoopStart)||!std::isfinite(v.sourceLoopEnd)||v.sourceLoopStart<0||v.sourceLoopEnd<=v.sourceLoopStart||v.mode!="rendered_hold"||v.loop)throw std::runtime_error("Invalid rendered source hold");}
        if(voice.contains("source_loop")){
            const auto&loop=voice["source_loop"];
            double foundation=loop.value("foundation_mix",.75);
            v.sourceLoopActivate=loop.value("activate",v.sourceLoopStart);
            if(!std::isfinite(foundation)||foundation<0||foundation>1||v.sourceLoopEnd-v.sourceLoopStart<.02||v.sourceStart>v.sourceLoopStart+1./s.sr||!std::isfinite(v.sourceLoopActivate)||(abs(v.sourceLoopActivate-v.sourceLoopStart)>1./s.sr&&abs(v.sourceLoopActivate-v.sourceLoopEnd)>1./s.sr)||abs(v.sourceEnd-v.sourceLoopEnd)>1./s.sr)throw std::runtime_error("Invalid prepared loop material");
            if(loop.value("roll",false)){
                double begin=loop.at("roll_start"),end=loop.at("roll_end");
                if(!std::isfinite(begin)||!std::isfinite(end)||begin<v.sourceStart||end<=begin||end>v.sourceStart+v.map.sourceAt(voice.at("prepare_frames").get<double>()/s.sr)+.001||loop.at("roll_periods").size()!=4)throw std::runtime_error("Invalid prepared roll extent");
                double previous=v.sourceLoopEnd-v.sourceLoopStart+.001;
                for(const auto&period:loop.at("roll_periods")){double seconds=period;if(!std::isfinite(seconds)||seconds<.01||seconds>=previous)throw std::runtime_error("Invalid prepared roll subdivision");previous=seconds;}
            }
            if(loop.contains("repeat_stages")){
                double previous=-1,periodLimit=v.sourceLoopEnd-v.sourceLoopStart+.001;
                const auto&stages=loop.at("repeat_stages");if(stages.empty()||stages.size()>8)throw std::runtime_error("Invalid repeater stages");
                for(const auto&k:stages){double at=k.at(0),period=k.at(1);
                    if(!std::isfinite(at)||!std::isfinite(period)||at<v.sourceLoopActivate||at<=previous||period<.01||period>periodLimit||at>v.sourceStart+v.map.sourceAt(voice.at("prepare_frames").get<double>()/s.sr))throw std::runtime_error("Invalid repeater material/timing");
                    previous=at;periodLimit=period+.001;
                    v.sourceRepeatStages.emplace_back(at,period);
                }
            }
        }
        if(v.roll){
            if(v.loop<=0||v.rollStart<v.start||v.rollStart>=v.end||!voice.contains("roll_quarters")||voice["roll_quarters"].size()!=4)throw std::runtime_error("Invalid percussion roll extent");
            for(int k=0;k<4;k++){double q=voice["roll_quarters"][k];v.rollPeriods[k]=q*s.sr*60/s.bpm;if(!std::isfinite(q)||q<=0||v.rollPeriods[k]>v.loopPeriod+.51||(k&&v.rollPeriods[k]>=v.rollPeriods[k-1]))throw std::runtime_error("Invalid percussion roll subdivision");}
        }
        if(v.originalOnly&&v.loop)throw std::runtime_error("Full-mix loops require separate vocal evidence");
        if(voice.contains("texture_filter")){auto&f=voice.at("texture_filter");v.hpAlpha=float(1-std::exp(-2*pi*f.at("highpass_hz").get<double>()/s.sr));v.lpAlpha=float(1-std::exp(-2*pi*f.at("lowpass_hz").get<double>()/s.sr));v.filterStart=f.at("release_frame");v.filterEnd=f.at("full_frame");if(v.filterStart<0||v.filterEnd<=v.filterStart||v.filterEnd>s.frames||v.hpAlpha<=0||v.hpAlpha>=1||v.lpAlpha<=0||v.lpAlpha>=1)throw std::runtime_error("Invalid texture filter");}
        for(auto&filter:voice.value("soft_filters",Json::array())){
            std::string role=filter.at("role");int stem=0;while(stem<5&&role!=names[stem])stem++;
            double hp=filter.value("highpass_hz",0.),lp=filter.value("lowpass_hz",0.);
            if(stem==5||!std::isfinite(hp)||!std::isfinite(lp)||hp<0||lp<0||(hp>0&&lp>0&&lp<=hp)||std::max(hp,lp)>s.sr*.49||!v.softFilters[stem].wet.knots.empty())throw std::runtime_error("Invalid role filter");
            auto&f=v.softFilters[stem];f.hpAlpha=float(1-std::exp(-2*pi*hp/s.sr));f.lpAlpha=float(1-std::exp(-2*pi*lp/s.sr));f.stages=filter.value("stages",1);
            if(f.stages<1||f.stages>2)throw std::runtime_error("Invalid role filter slope");
            bool hpCurve=filter.contains("highpass_curve_hz"),lpCurve=filter.contains("lowpass_curve_hz");
            if(hpCurve)f.hp=filterCurve(filter.at("highpass_curve_hz"),s.sr,s.frames,compileFilters);
            if(lpCurve)f.lp=filterCurve(filter.at("lowpass_curve_hz"),s.sr,s.frames,compileFilters);
            if((hpCurve||lpCurve)&&(hp>0||lp>0||(hpCurve&&lpCurve)))throw std::runtime_error("A dynamic role filter must have one explicit pass direction");
            if(hp==0&&lp==0&&!hpCurve&&!lpCurve)throw std::runtime_error("Empty role filter");
            Frame previous=-1;
            for(auto&point:filter.at("wet")){Knot knot{point.at(0),point.at(1)};if(knot.frame<0||knot.frame>s.frames||knot.frame<=previous||!std::isfinite(knot.value)||knot.value<0||knot.value>1)throw std::runtime_error("Invalid role filter automation");f.wet.knots.push_back(knot);previous=knot.frame;}
            if(f.wet.knots.empty())throw std::runtime_error("Empty role filter automation");
        }
        if(v.deck<0||v.deck>1||v.start<0||v.end<=v.start||v.end>s.frames||v.offset<0||v.loop<0||!std::isfinite(v.sourceStart)||v.sourceStart<0||!std::isfinite(v.sourceEnd)||v.sourceEnd<=v.sourceStart||(v.mode!="main"&&v.mode!="hold"&&v.mode!="slip"&&v.mode!="clip"&&v.mode!="rendered_hold")||((v.mode=="hold"||v.mode=="slip")&&v.loop<=0))throw std::runtime_error("Invalid independent voice");
        Frame prepared=voice.at("prepare_frames");
        if(prepared<=0||prepared>s.frames||(v.channels!=12&&!(v.originalOnly&&v.channels==2))||v.id.empty()||std::find(ids.begin(),ids.end(),v.id)!=ids.end())throw std::runtime_error("Invalid voice identity or prepared extent");
        ids.push_back(v.id);
        if((v.id=="B.main"&&v.deck!=1)||(v.id=="A.main"&&v.deck!=0)||(v.mode!="rendered_hold"&&abs(v.sourceStart+v.map.sourceAt(double(prepared)/s.sr)-v.sourceEnd)>1.1/s.sr))throw std::runtime_error("Voice source/prepared map mismatch");
        if(!std::isfinite(v.map.finalRate)||v.map.finalRate<=0)throw std::runtime_error("Invalid final source rate");
        double djWarp=(j.value("planner",std::string{})=="independent-dj-2"||j.value("planner",std::string{})=="independent-dj-3"||j.value("planner",std::string{})=="independent-dj-4"||j.value("planner",std::string{})=="independent-dj-5")?1.18:1.06;
        double lower=independentFamily(s.family)?1./djWarp:v.map.clocked()?.5:.94,upper=independentFamily(s.family)?djWarp:v.map.clocked()?2.:1.06;
        if(v.mode=="rendered_hold"&&(v.sourceLoopStart<0||(v.sourceStart+v.map.sourceAt(double(prepared)/s.sr)-v.sourceLoopActivate)/(v.sourceLoopEnd-v.sourceLoopStart)>16.01))throw std::runtime_error("Unbounded rendered hold");
        for(double r:{v.map.initialRate,v.map.steadyRate})if(!std::isfinite(r)||r<lower-1e-9||r>upper+1e-9)throw std::runtime_error("Scene source exceeded total warp budget");
        // A shared recovery map can extend beyond this voice's lifetime. Apply
        // the warp limit to every extremum that is actually prepared/audible.
        const double extent=double(prepared)/s.sr;
        for(double t:{0.,std::min(extent,v.map.rampIn),std::min(extent,v.map.holdEnd),extent}){double r=v.map.rateAt(t);if(!std::isfinite(r)||r<lower-1e-9||r>upper+1e-9)throw std::runtime_error("Scene source exceeded total warp budget");}
        for(size_t i=1;i<v.map.phaseCorrections.size();i++){
            const auto&a=v.map.phaseCorrections[i-1];const auto&b=v.map.phaseCorrections[i];double d=1.5*(b.second-a.second)/(b.first-a.first);
            double lo=std::min(v.map.baseRateAt(a.first),v.map.baseRateAt(b.first))+std::min(0.,d),hi=std::max(v.map.baseRateAt(a.first),v.map.baseRateAt(b.first))+std::max(0.,d);
            if(lo<lower-1e-9||hi>upper+1e-9)throw std::runtime_error("Bar correction exceeded warp budget");
        }
        for(double t:{v.map.rampIn,v.map.holdEnd,v.map.rampOut})if(!std::isfinite(t)||t<0||t>240)throw std::runtime_error("Invalid source-map duration");
        if(!std::isfinite(v.gain)||v.gain<0||v.gain>4||!std::isfinite(v.loopPeriod)||v.loopPeriod<0||abs(v.loopPeriod-v.loop)>.51)throw std::runtime_error("Invalid clip phase/gain");
        if(v.loop?(v.loopPeriod<=0||v.offset+std::ceil(v.loopPeriod)+1>prepared):(v.offset+v.end-v.start>prepared+1))throw std::runtime_error("Voice reads beyond its prepared clip");
        if(v.loop&&double(v.end-v.start)/v.loop>16.01)throw std::runtime_error("Unbounded loop repeat count");
        for(int stem=0;stem<5;stem++){
            Frame prev=-1;for(auto&pair:voice.at("roles").at(names[stem])){Knot k{pair.at(0),pair.at(1)};if(k.frame<0||k.frame>s.frames||k.frame<=prev||!std::isfinite(k.value)||k.value<0||k.value>1)throw std::runtime_error("Invalid role automation");v.roles[stem].knots.push_back(k);prev=k.frame;}if(v.roles[stem].knots.empty())throw std::runtime_error("Missing explicit role envelope");
        }if(v.originalOnly)for(auto role:{"vocals","drums","bass","other"})if(voice.at("roles").at(role)!=voice.at("roles").at("residual"))throw std::runtime_error("Full-mix roles must have one gain owner");s.voices.push_back(v);
    }
    if(s.dj.enabled){
        if((s.voices.size()!=2&&s.voices.size()!=3)||s.voices[0].id!="A.main"||s.voices[1].id!="B.main"||s.voices[0].deck!=0||s.voices[1].deck!=1||!s.voices[0].originalOnly||!s.voices[1].originalOnly)throw std::runtime_error("DJ stereo graph requires main originals A and B");
        if(s.voices.size()==3){
            const auto&clip=s.voices[2];
            if(clip.id!="B.teaser"||clip.deck!=1||!clip.originalOnly||clip.mode!="rendered_hold"||clip.sourceLoopEnd<=clip.sourceLoopStart||clip.sourceLoopEnd-clip.sourceLoopStart>6.001||clip.end>s.arrival+s.sr/8||clip.roles[4].at(clip.end)!=0)throw std::runtime_error("Invalid independent DJ introduction");
        }
    }
    if(s.voices.empty()||s.voices.size()>4)throw std::runtime_error("Scene voice budget exceeded");
    bool recovered=false;for(auto&v:s.voices){if(v.id!="B.main")continue;recovered=v.end==s.frames&&v.map.rateAt(double(s.frames-v.start-64)/s.sr)==1;for(auto&role:v.roles)recovered&=abs(role.at(s.frames-64)-1)<1e-9;for(auto&filter:v.softFilters)recovered&=filter.wet.knots.empty()||abs(filter.wet.at(s.frames-64))<1e-9;}
    if(!recovered)throw std::runtime_error("Recovery still has a muted stem or a warped source: "+s.family);
    if(j.at("recovery").contains("tempo")){
        const auto&b=*std::find_if(s.voices.begin(),s.voices.end(),[](const auto&v){return v.id=="B.main";});
        const double start=j.at("recovery").at("tempo").at("start_seconds"),finish=j.at("recovery").at("tempo").at("complete_seconds");
        if(!std::isfinite(start)||!std::isfinite(finish)||finish<start||finish>double(s.frames-64)/s.sr)throw std::runtime_error("Invalid shared tempo extent");
        if(b.map.clocked()){
            if(abs(start-b.map.clockStart)>1./s.sr||abs(finish-b.map.clockEnd)>1./s.sr||abs(finish-double(s.arrival)/s.sr)>.002||abs(b.map.rateAt(finish-double(b.start)/s.sr)-1)>1e-7)throw std::runtime_error("B must reach its native tempo at its own drop");
        }else if(start<double(s.arrival)/s.sr-.05||abs(start-double(b.start)/s.sr-b.map.holdEnd)>1./s.sr||abs(finish-start-b.map.rampOut)>1./s.sr)throw std::runtime_error("Invalid shared tempo recovery");
        for(const auto&v:s.voices){if(v.id=="B.main"||double(v.end)/s.sr<=start)continue;
            if(v.loop||(!b.map.clocked()&&double(v.start)/s.sr>start))throw std::runtime_error("Late clip does not follow the shared tempo recovery");
            double scale=v.map.steadyRate/b.map.steadyRate,end=std::min(finish,double(v.end)/s.sr);
            for(double t:{std::max(start,double(v.start)/s.sr),(std::max(start,double(v.start)/s.sr)+end)*.5,end})if(abs(v.map.baseRateAt(t-double(v.start)/s.sr)-scale*b.map.baseRateAt(t-double(b.start)/s.sr))>1e-5)throw std::runtime_error("Active source escaped the shared tempo recovery");
        }
    }
    for(auto&[name,a]:j.at("anchors").items()){Frame f=a.at("output_frame");double q=a.at("scene_quarter"),seconds=a.at("seconds");if(f<0||f>s.frames||!std::isfinite(q)||!std::isfinite(seconds)||abs(seconds*s.sr-f)>.51||abs(s.transport.quarterAt(f)-q)>1e-6)throw std::runtime_error("Anchor unit mismatch: "+name);}
    auto af=[&](const char*name){return j.at("anchors").at(name).at("output_frame").get<Frame>();};
    if(af("A_prepare_start")!=0||af("B_first_audible")>af("hybrid_start")||af("hybrid_start")>s.arrival||af("foundation_takeover")>s.arrival||af("A_vocal_release")>af("A_last_audible")||af("B_lead_entry")>=s.frames||af("recovery_complete")!=s.frames)throw std::runtime_error("Inconsistent scene anchor order: "+s.family);
    if(j.at("recovery").at("output_frame").get<Frame>()!=s.frames||j.at("recovery").at("b_rate").get<double>()!=1||!j.at("recovery").at("full_original").get<bool>())throw std::runtime_error("Missing native full-song recovery");
    return s;
}
}
