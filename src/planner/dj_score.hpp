#pragma once
#include "scene.hpp"
#include "musical_events.hpp"
namespace ladio {
// Original scores over a portable signal contract. No vendor resource is loaded
// or converted here. The score owns source release, spectral movement and FX
// release independently, so a wet phrase can outlive its dry source.
inline Json djScore(const DJStyle&style,Json&va,Json&vb,Frame arrival,Frame end,int sr,double bpm,bool synchronized,Frame protectedUntil,const MusicalEvents*events=nullptr){
    const Frame quiet=end-sr/5,beat=std::llround(sr*60/bpm);
    // Map score gestures onto independently chosen musical events. The score
    // still describes its character; it no longer decides when the singer or
    // the incoming foundation happens to arrive.
    auto f=[&](double part)->Frame{
        if(!events)return std::llround(arrival*part);
        const std::array<double,5> positions{0.,.12,.40,.70,1.};
        const std::array<Frame,5> times{0,events->first,events->hybrid,events->foundation,arrival};
        for(size_t i=1;i<positions.size();i++)if(part<=positions[i])return times[i-1]+std::llround((times[i]-times[i-1])*(part-positions[i-1])/(positions[i]-positions[i-1]));
        return arrival;
    };
    auto points=[&](std::initializer_list<std::pair<Frame,double>> data){
        std::vector<std::pair<Frame,double>> sorted(data);for(auto&x:sorted)x.first=std::clamp<Frame>(x.first,0,quiet);
        std::stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.first<b.first;});Json result=Json::array();
        for(auto x:sorted){if(!result.empty()&&result.back()[0]==x.first)result.back()[1]=x.second;else result.push_back({x.first,x.second,"linear"});}return result;
    };
    auto sweep=[&](Frame from,Frame to,double start,double finish,const char*curve){
        auto result=points({{0,start},{from,start},{std::max(from+1,to),finish},{quiet,finish}});
        for(auto&k:result){k[2]=curve;k.push_back("linear-frequency");}return result;
    };
    auto envelope=[&](Frame from,Frame to,double start,double finish,const char*curve="smooth"){
        auto result=points({{0,start},{from,start},{std::max(from+1,to),finish},{quiet,finish}});for(auto&k:result)k[2]=curve;return result;
    };
    const auto closed=points({{0,0},{quiet,0}}),unity=points({{0,1},{quiet,1}});
    const std::string id=style.id,shape=style.shape;
    bool basic=!style.autoDJ,performance=shape=="build"||shape=="blur"||shape=="release";
    const Frame open=arrival-std::min<Frame>(sr/10,beat/4);
    // Leave protected voice alone. Once it finishes, spectral and source
    // release can begin; this must not retroactively cut the preceding word.
    const Frame protectedEnd=std::min(protectedUntil,arrival-Frame(sr*.025));
    auto afterVoice=[&](double part){return std::min(arrival-1,std::max(protectedEnd,f(part)));};
    Json aGain=envelope(std::max(protectedEnd,arrival-std::min<Frame>(beat,sr*.5)),arrival,1,0);
    Json bGain=points({{0,0},{std::min(beat,f(.1)),.65},{f(.45),.9},{open,1},{quiet,1}});
    Json a,b;
    for(auto*d:{&a,&b}){
        (*d)["input_gain"]=unity;(*d)["dry_gain"]=unity;
        for(auto name:{"eq_wet","flanger_mix","gate_depth","aux_send","aux_return","delay_mix","reverb_mix","room_send","room_return","echo_send","echo_return"})(*d)[name]=closed;
        (*d)["resonance"]=.707;(*d)["tail_highpass_hz"]=90;(*d)["tail_lowpass_hz"]=12000;
        (*d)["highpass_hz"]=points({{0,22},{quiet,22}});(*d)["lowpass_hz"]=points({{0,19000},{quiet,19000}});
    }
    a["filter_wet"]=envelope(afterVoice(0),std::min(arrival,afterVoice(0)+sr/40),0,1);
    if(a["filter_wet"][a["filter_wet"].size()-2][0].get<Frame>()<arrival)
        a["filter_wet"].insert(a["filter_wet"].end()-1,Json::array({arrival,1.,"linear"}));
    a["filter_wet"].back()=Json::array({quiet,0,"linear"});
    b["filter_wet"]=points({{0,1},{open-sr/40,1},{open,0},{quiet,0}});
    // A loses brightness; B gradually acquires its low-frequency foundation.
    a["lowpass_hz"]=sweep(afterVoice(.12),arrival,19000,160,"out:2");
    b["highpass_hz"]=sweep(0,open,1300,22,"out:2");
    b["lowpass_hz"]=sweep(0,open,6500,19000,"out:2");
    if(shape=="sweep"){
        // Reveal a band before the full arrangement, instead of introducing
        // almost the entire midrange as soon as the deck fader opens.
        // A high-only introduction needs a real passband. Crossing a 7.2 kHz
        // high-pass with a 2.8 kHz low-pass hid B instead of introducing it.
        b["highpass_hz"]=sweep(0,open,5200,22,"out:2");
        b["lowpass_hz"]=sweep(0,open,14000,19000,"in:2");
        bGain=points({{0,0},{std::min<Frame>(sr/3,f(.12)),.85},{f(.4),1},{quiet,1}});
    }
    if(shape=="expand"){
        a["lowpass_hz"]=sweep(afterVoice(.5),arrival,19000,130,"out:2");
        a["highpass_hz"]=sweep(afterVoice(.5),open,22,240,"out:0.5");
        b["lowpass_hz"]=sweep(0,f(.72),1400,19000,"out:0.5");
        b["highpass_hz"]=sweep(f(.48),open,950,22,"out:2");
    }
    if(shape=="phrase"){
        // A keeps its phrase and rhythmic body while B's upper arrangement is
        // already present. Trade the low foundation separately from brightness.
        a["lowpass_hz"]=sweep(afterVoice(.55),open,19000,1800,"out:2");
        a["highpass_hz"]=sweep(afterVoice(.50),f(.84),22,650,"out:0.5");
        b["lowpass_hz"]=sweep(0,f(.42),5200,19000,"out:0.5");
        b["highpass_hz"]=sweep(f(.35),f(.84),1100,22,"out:2");
        bGain=points({{0,0},{f(.18),.6},{f(.40),.82},{open,1},{quiet,1}});
    }
    if(shape=="late"&&!basic){
        // Establish B's texture first, then bring its main body forward in a
        // distinct run-up. This is not the eight-bar echo-filter score.
        a["lowpass_hz"]=sweep(afterVoice(.60),open,19000,420,"out:2");
        b["lowpass_hz"]=sweep(f(.12),f(.78),2600,19000,"in:2");
        b["highpass_hz"]=sweep(f(.45),open,1800,22,"out:2");
        bGain=points({{0,0},{f(.12),.24},{f(.45),.55},{f(.78),.90},{open,1},{quiet,1}});
    }
    if(id=="dj_long_sweep"){
        a["lowpass_hz"]=sweep(afterVoice(.25),open,19000,180,"out:2");
        b["highpass_hz"]=sweep(f(.2),open,3800,22,"out:2");
        b["lowpass_hz"]=sweep(f(.2),open,12000,19000,"out:2");
        bGain=points({{0,0},{f(.2),0},{f(.2)+sr/3,1},{quiet,1}});
    }
    if(shape=="soft"||shape=="voice"){
        a["lowpass_hz"]=sweep(afterVoice(shape=="voice"?0:.4),open,19000,shape=="voice"?120:700,"out:2");
        a["highpass_hz"]=sweep(afterVoice(0),open,22,shape=="voice"?300:700,"out:0.5");
        b["lowpass_hz"]=sweep(f(.55),open,350,19000,"in:2");
        b["highpass_hz"]=sweep(f(.65),open,150,22,"in:2");
        bGain=points({{0,0},{f(.1),.75},{f(.65),.75},{f(.8),1},{quiet,1}});
    }
    if(shape=="rhythm"||shape=="hook"){
        a["lowpass_hz"]=sweep(afterVoice(.2),f(.65),19000,shape=="hook"?3600:8000,"out:2");
        a["highpass_hz"]=sweep(afterVoice(.2),f(.6),22,220,"out:0.5");
        b["lowpass_hz"]=sweep(f(.2),open,shape=="hook"?9000:2500,19000,"out:2");
        b["highpass_hz"]=sweep(f(.25),open,shape=="hook"?4800:350,22,"out:2");
        bGain=points({{0,0},{f(.22),0},{f(.6),.85},{open,1},{quiet,1}});
    }
    if(performance||shape=="loop"){
        a["highpass_hz"]=sweep(afterVoice(shape=="build"?.45:.25),f(.85),22,950,"out:2");
        a["lowpass_hz"]=sweep(afterVoice(shape=="build"?.8:.3),arrival,19000,90,shape=="release"?"out:4":"out:2");
        b["highpass_hz"]=sweep(0,f(.62),1300,22,"out:2");
        b["lowpass_hz"]=sweep(0,open,6500,19000,"out:2");
        bGain=points({{0,0},{f(.42),.48},{f(.8),1},{quiet,1}});
        if(shape=="build"||shape=="blur"){
            Frame start=afterVoice(shape=="build"?.66:.5);
            a["flanger_mix"]=points({{0,0},{start,0},{std::max(start+1,f(.86)),shape=="blur"?.6:.45},{arrival,0},{quiet,0}});
            a["flanger_quarters"]=16;a["flanger_feedback"]=.5;
            a["flanger_center_seconds"]=.0045;a["flanger_depth_seconds"]=.0027;
        }
        if(shape=="blur"){
            a["gate_depth"]=points({{0,0},{afterVoice(.375),0},{open,.92},{arrival,0},{quiet,0}});
            a["gate_quarters"]=1.;a["gate_duty"]=.5;
        }
    }
    // A shared auxiliary bus is delay -> reverb -> return EQ. Dry/wet inside
    // either processor differs from both the bus send and the dry deck fader.
    if((style.delay&&!performance&&shape!="loop")||(style.room&&!performance)){
        Frame begin=f(style.delay?.5:.4),handoff=std::max(begin,protectedEnd);
        // A percentage alone stops capture several seconds before the drop
        // in a long phrase; even a four-second room has then almost decayed.
        // Keep a fresh final reflection. Vocal clearance deliberately finishes
        // earlier so that it does not wash over B's leading voice.
        Frame lastCapture=style.delay?open:shape=="voice"?f(.82):std::max(f(.82),arrival-beat/2);
        Frame capture=std::min(arrival-sr/100,std::max(handoff+1,lastCapture));
        Frame dryEnd=std::max(capture,f(.88));
        Frame tailEnd=synchronized?quiet-sr/20:arrival;
        a["aux_send"]=points({{0,0},{begin,0},{begin+(capture-begin)/3,style.delay?.75:1},{capture,1},{std::min(arrival,capture+sr/15),0},{quiet,0}});
        Frame preview=std::min(handoff,begin+beat);
        // Let the captured phrase become audible before its source stops.
        // Phrase protection owns the dry path; it must not also hide every
        // reflection until the last 80 ms of a long vocal.
        a["aux_return"]=points({{0,0},{begin,0},{preview,.18},{handoff,.18},{capture,1},{std::min(tailEnd,capture+beat),1},{tailEnd,0},{quiet,0}});
        a["dry_gain"]=envelope(handoff,dryEnd,1,0,"in:2");
        // Both channels keep their common tempo through the source release.
        if(style.delay){
            a["delay_mix"]=points({{0,0},{begin,0},{std::min(capture,begin+sr/2),1},{tailEnd,1},{quiet,0}});
            a["delay_quarters"]=shape=="late"?.5:1.;a["delay_feedback"]=shape=="hook"?.6:shape=="late"?.28:.38;a["delay_lowpass_hz"]=shape=="rhythm"?3000:3600;
        }
        if(style.room){
            a["reverb_mix"]=points({{0,0},{std::max<Frame>(0,begin-sr/10),0},{begin,1},{quiet-sr/20,1},{quiet,0}});
            a["reverb_low_seconds"]=shape=="voice"?1.6:4.;a["reverb_high_seconds"]=shape=="voice"?.6:4.;
            a["reverb_min_seconds"]=shape=="voice"?.012:.045;a["reverb_max_seconds"]=shape=="voice"?.034:.11;
            a["reverb_gain_db"]=3.;
        }
    }
    if(performance&&style.room){
        Frame start=f(shape=="release"?.3:shape=="blur"?.5:.64);
        if(shape=="release")start=std::min(start,std::max<Frame>(0,arrival-4*beat));
        Frame handoff=std::max(start,protectedEnd);
        Frame capture=std::min(arrival-sr/100,std::max(start+1,f(.85)));
        Frame lastCapture=std::min(arrival-sr/100,std::max(capture,arrival-beat/2));
        // Capture under the intact outgoing phrase. Opening its wet return
        // later must reveal an already populated room, not start an empty one.
        // Keep fresh material until the final half-beat. Fading the send over
        // the whole last section drains long builds before the actual release.
        double send=shape=="build"?.6:1.;
        a["room_send"]=points({{0,0},{start,0},{capture,send},{lastCapture,send},{arrival,0},{quiet,0}});
        Frame preview=std::min(handoff,start+beat);
        a["room_return"]=points({{0,0},{start,0},{preview,.20},{handoff,.20},{std::min(arrival,handoff+beat),1.},{arrival,1.},{quiet-sr/20,0},{quiet,0}});
        a["room_low_seconds"]=shape=="blur"?18.:6.;a["room_high_seconds"]=shape=="blur"?10.:4.;
    }
    if(shape=="blur"||shape=="loop"){
        Frame start=afterVoice(.375);
        a["echo_send"]=points({{0,0},{start,0},{std::max(start+1,f(.65)),.65},{arrival,0},{quiet,0}});
        a["echo_return"]=points({{0,0},{start,0},{std::max(start+1,f(.8)),.55},{arrival,.55},{quiet-sr/20,0},{quiet,0}});
        a["delay_quarters"]=.75;a["echo_feedback"]=.5;a["delay_lowpass_hz"]=5500;
    }
    if(shape=="pulse"){
        // A deliberate pulse change can still develop a rhythmic echo in A's
        // approach. End it before B's lead/new pulse; the independent room then
        // carries the release. Disabling the whole delay made the most common
        // cross-genre score lose its written rhythmic gesture altogether.
        a["aux_return"]=closed;a["aux_send"]=closed;a["delay_mix"]=closed;a["reverb_mix"]=closed;
        a["dry_gain"]=unity;
        Frame echoEnd=events?std::min(arrival,events->bLead):arrival;
        Frame echoBegin=std::max<Frame>(0,std::min(f(.35),echoEnd-3*beat));
        if(echoEnd-echoBegin>=3*beat/2){
            Frame captureEnd=echoEnd-beat/2,preview=std::min(captureEnd,echoBegin+beat/2);
            double level=protectedEnd<captureEnd?.55:.18;
            a["aux_send"]=points({{0,0},{echoBegin,0},{preview,.65},{captureEnd,.65},{std::min(echoEnd,captureEnd+sr/20),0},{quiet,0}});
            a["aux_return"]=points({{0,0},{echoBegin,0},{preview,.14},{captureEnd,level},{echoEnd,0},{quiet,0}});
            a["delay_mix"]=points({{0,1},{echoEnd,1},{quiet,0}});
            a["delay_quarters"]=.5;a["delay_feedback"]=.36;a["delay_lowpass_hz"]=2800;
        }
        // A's spatial capture follows A's own departure, even when B has no
        // safe material until late. A late B introduction must not collapse
        // all outgoing gestures into its final few milliseconds.
        Frame begin=std::min(f(.55),std::max<Frame>(0,arrival-3*beat)),handoff=std::max(protectedEnd,begin);
        a["room_send"]=points({{0,0},{begin,0},{open,.85},{arrival,0},{quiet,0}});
        Frame preview=std::min(handoff,begin+beat);
        a["room_return"]=points({{0,0},{begin,0},{preview,.16},{handoff,.16},{std::max(handoff+1,open),1.},{arrival,1.},{quiet-sr/20,0},{quiet,0}});
        a["room_low_seconds"]=3.2;a["room_high_seconds"]=2.;
    }
    if(style.room){
        // Capture before the dry deck dives under its filter. Previously the
        // narrow HP/LP intersection starved the room precisely at release.
        a["send_before_filter"]=true;
        a["tail_highpass_hz"]=shape=="voice"?340:220;
        a["tail_lowpass_hz"]=shape=="voice"?4200:6800;
    }
    if(basic){
        a["filter_wet"]=closed;b["filter_wet"]=closed;
        if(id=="dj_equal_power"){
            aGain=Json::array();bGain=Json::array();
            for(int k=0;k<=32;k++){double u=double(k)/32;Frame x=f(u);aGain.push_back({x,std::cos(u*pi/2)});bGain.push_back({x,std::sin(u*pi/2)});}
            aGain.back()[1]=0.;bGain.back()[1]=1.;aGain.push_back({quiet,0});bGain.push_back({quiet,1});
        }else if(id=="dj_seamless"||shape=="splice"){
            aGain=envelope(arrival-sr/100,arrival,1,0);bGain=envelope(arrival-sr/100,arrival,0,1);
        }else{
            aGain=envelope(f(.3),arrival,1,0);bGain=envelope(shape=="late"?f(.75):0,arrival,0,1);
        }
    }
    if(events&&!basic){
        Frame first=events->first,foundation=events->foundation;
        Frame leadOpen=std::max<Frame>(0,std::min(open,events->bLead-sr/25));
        Frame bassOpen=std::max<Frame>(first+1,std::min(open,foundation-sr/25));
        // Preserve each score's spectral trajectory. Only pull the end of a
        // gesture forward when an independently measured lead/foundation needs
        // it. Replacing the lanes here made every score sound like one sweep.
        auto completeBy=[&](Json&lane,Frame deadline){
            if(deadline>=open||open<=0)return;
            for(auto&k:lane){Frame x=k[0];if(x>0&&x<=open)k[0]=std::llround(double(x)*deadline/open);}
            // An early pickup can pull a whole sweep to the first frame.
            // Consolidate coincident knots instead of publishing invalid DSP.
            Json unique=Json::array();for(const auto&k:lane){if(!unique.empty()&&unique.back()[0]==k[0])unique.back()=k;else unique.push_back(k);}lane=std::move(unique);
        };
        completeBy(b["highpass_hz"],bassOpen);
        completeBy(b["lowpass_hz"],leadOpen);
        if(foundation<arrival-beat/2){
            // Exchange the low foundation even when A's intact lead prevents
            // its dramatic full-band sweep. Keep the singer and drums present;
            // a broad, smooth bass dip makes room for B's measured foundation.
            Frame releaseStart=std::max<Frame>(first,foundation-beat);
            a["eq_frequency_hz"]=95.;a["eq_gain_db"]=-12.;a["eq_q"]=.55;
            a["eq_wet"]=points({{0,0},{releaseStart,0},{foundation,1},{arrival,1},{quiet,0}});
        }
        // A whole incoming vocal, including its pickup, has priority over the
        // expressive fader. Keep an already-open lead open through recovery.
        if(events->bLead<arrival){
            Frame lead=std::clamp<Frame>(events->bLead-sr/25,std::min(first+1,open),open);
            for(auto&k:bGain)if(k[0].get<Frame>()>=lead)k[1]=1.;
            auto at=std::find_if(bGain.begin(),bGain.end(),[&](const Json&k){return k[0].get<Frame>()>=lead;});
            if(at==bGain.end()||(*at)[0].get<Frame>()!=lead)bGain.insert(at,Json::array({lead,1.,"smooth"}));
        }
        Frame release=std::min(arrival-1,std::max(events->aPhrase,events->aRelease));
        aGain=envelope(release,arrival,1,0);
        // The score's dry/aux handover begins after protectedEnd. Retain it:
        // resetting dry_gain to unity buries its wet phrase under the original.
        if(!synchronized){a["echo_send"]=closed;a["echo_return"]=closed;}
    }
    for(auto role:{"vocals","drums","bass","other","residual"}){va["roles"][role]=aGain;vb["roles"][role]=bGain;}
    return {{"version",3},{"dsp_revision","audible-capture-return-v8"},{"score",style.id},{"decks",Json::array({a,b})},{"bypass_at_recovery",true},{"tempo_unit","shared_quarter"},{"independent_musical_events",events!=nullptr},{"routing","independent dry sweep; selected pre-filter capture; serial auxiliary delay/reverb; common wet return EQ"}};
}
}
