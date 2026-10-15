#pragma once
// Re-express the complete arrangement on one musical clock. Source cues and
// phrase boundaries stay fixed; automation and every independent voice move.
namespace ladio {
inline void rendezvousAtB(Plan&p,double sourceBPM,double nativeBPM){
    if(p.reset)return;
    auto&j=p.scene;const double sr=p.sr,oldArrival=p.impact;
    const double start=p.bStart,ratio=1/p.mapB.initialRate;
    if(oldArrival<=start+.1)throw std::runtime_error("No interval for tempo rendezvous");
    const double finish=start+2*(oldArrival-start)/(1+ratio);
    TimeMap clock{1,ratio,0,0,0,ratio};clock.clockStart=start;clock.clockEnd=finish;
    auto time=[&](double old){double lo=0,hi=std::max(1.,old/std::min(1.,ratio)+1);for(int k=0;k<44;k++){double mid=(lo+hi)*.5;if(clock.sourceAt(mid)<old)lo=mid;else hi=mid;}return (lo+hi)*.5;};
    auto frame=[&](Frame f){return std::llround(time(double(f)/sr)*sr);};
    auto automation=[&](Json&lane){std::map<Frame,double> points;for(auto&k:lane)points[frame(k[0])]=k[1];lane=Json::array();for(auto&[f,v]:points)lane.push_back({f,v});};
    auto map=[&](double rate,double offset){double target=rate*ratio;if(abs(target-1)<1e-12)target=1;TimeMap m{rate,target,0,0,0,target};m.clockStart=start;m.clockEnd=finish;m.clockOffset=offset;return m;};
    Frame total=frame(j.at("duration_frames"));j["duration_frames"]=total;
    for(auto&v:j["voices"]){
        const double oldStart=v["start_frame"].get<double>()/sr;
        const double oldEnd=v["end_frame"].get<double>()/sr;
        TimeMap previous=TimeMap::from(v["time_map"]);
        double from=time(oldStart),to=time(oldEnd),rate=previous.initialRate;
        v["start_frame"]=std::llround(from*sr);v["end_frame"]=std::llround(to*sr);
        Frame prepared=v["end_frame"].get<Frame>()-v["start_frame"].get<Frame>();
        auto next=map(rate,v["start_frame"].get<double>()/sr);
        if(v.value("loop_frames",Frame(0))>0){
            // Expand a short source hold on the worker, before pitch-preserving
            // stretch. Playback now reads linear PCM at the same clock as B.
            double source=v["source_start_seconds"].get<double>()+previous.sourceAt(v["pack_offset_frames"].get<double>()/sr);
            double length=v["loop_period_frames"].get<double>()/sr*rate;
            double rollStart=v.value("roll_start_frame",v["end_frame"].get<Frame>())/sr;
            Json periods=Json::array();for(auto q:v.value("roll_quarters",Json::array()))periods.push_back(q.get<double>()*60/sourceBPM*rate);
            v["source_loop"]={{"start",source},{"end",source+length},{"roll",v.value("percussion_roll",false)},
                {"roll_start",source+(rollStart-oldStart)*rate},{"roll_end",source+(oldEnd-oldStart)*rate},
                {"roll_periods",periods},{"foundation_mix",v.value("roll_foundation_mix",.75)}};
            v["source_start_seconds"]=source;v["source_end_seconds"]=source+length;
            v["playhead_mode"]="rendered_hold";v["percussion_roll"]=false;
            v["loop_frames"]=0;v["loop_period_frames"]=0;v["pack_offset_frames"]=0;
        }else v["source_end_seconds"]=v["source_start_seconds"].get<double>()+next.sourceAt(double(prepared)/sr);
        v["prepare_frames"]=prepared;v["time_map"]=next.json();
        for(auto&[role,lane]:v["roles"].items())automation(lane);
        if(v.contains("soft_filters"))for(auto&f:v["soft_filters"]){automation(f["wet"]);for(auto key:{"highpass_curve_hz","lowpass_curve_hz"})if(f.contains(key))automation(f[key]);}
        if(v.contains("texture_filter"))for(auto key:{"release_frame","full_frame"})v["texture_filter"][key]=frame(v["texture_filter"][key]);
        if(v.contains("release_to_anchor"))v["release_to_anchor"]["output_frame"]=frame(v["release_to_anchor"]["output_frame"]);
    }
    auto&t=j["transport"];t["bpm"]=sourceBPM;t["pulse_reset"]=false;t["rendezvous_at_B"]=true;
    t["segments"]=Json::array({Json{{"seconds",0.},{"quarter",0.},{"bpm",sourceBPM},{"meter_numerator",t["segments"][0]["meter_numerator"]},{"meter_denominator",t["segments"][0]["meter_denominator"]}},
        Json{{"seconds",start},{"quarter",start*sourceBPM/60},{"bpm",sourceBPM},{"end_bpm",sourceBPM*ratio},{"ramp_seconds",finish-start},{"curve","smoothstep"},{"meter_numerator",t["segments"][0]["meter_numerator"]},{"meter_denominator",t["segments"][0]["meter_denominator"]}}});
    for(auto&[name,a]:j["anchors"].items()){Frame f=frame(a["output_frame"]);a["output_frame"]=f;a["seconds"]=double(f)/sr;a["scene_quarter"]=clock.sourceAt(double(f)/sr)*sourceBPM/60;}
    
    double oldEnd=p.lead+p.seconds;
    p.lead=time(p.lead);p.seconds=time(oldEnd)-p.lead;p.post=double(total)/sr-time(oldEnd);
    p.bStart=time(p.bStart);p.vocalEnd=time(p.vocalEnd);p.vocalOpen=time(p.vocalOpen);p.impact=time(p.impact);p.swap=time(p.swap);
    j["recovery"]["output_frame"]=total;
    const auto&b=*std::find_if(j["voices"].begin(),j["voices"].end(),[](const auto&v){return v["id"]=="B.main";});
    j["recovery"]["b_source_seconds"]=b["source_end_seconds"];
    const auto&a=*std::find_if(j["voices"].begin(),j["voices"].end(),[](const auto&v){return v["id"]=="A.main";});
    p.mapA=TimeMap::from(a["time_map"]);p.mapB=TimeMap::from(b["time_map"]);p.bStart=b["start_frame"].get<double>()/sr;
    p.impact=j["anchors"]["B_full_arrival"]["seconds"];p.swap=j["anchors"]["bass_takeover"]["seconds"];
    p.bassRamp=std::min(p.bassRamp,60/(sourceBPM*ratio));
    j["recovery"]["tempo"]={{"strategy","A_and_B_rendezvous_at_native_B_drop"},{"start_seconds",start},{"complete_seconds",finish},{"a_start_bpm",sourceBPM},{"b_initial_bpm",sourceBPM},{"b_native_bpm",nativeBPM},{"all_active_stems_follow",true},{"curve","smoothstep"}};
    j["permissions"]["max_warp_relative_to_original"]=1.;
    j["musical_continuity"]["native_B_at_arrival"]=true;
    j["musical_continuity"]["source_cues_preserved_under_retiming"]=true;
    if(j.contains("spectral_exchange"))for(auto key:{"start_seconds","handoff_seconds"})j["spectral_exchange"][key]=time(j["spectral_exchange"][key]);
    if(j["teaser_candidate"].contains("output_start_seconds"))j["teaser_candidate"]["output_start_seconds"]=time(j["teaser_candidate"]["output_start_seconds"]);
    // The director rebuilds component ranges after this final time map.
}
}
