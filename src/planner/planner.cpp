#include "types.hpp"
#include "scene.hpp"
#include <sstream>
#include <optional>
namespace ladio {
Json Plan::json()const {
    Json clips=Json::array();for(auto&c:voiceClips)clips.push_back({{"deck",c.deck},{"source",c.source},{"start",c.start},{"length",c.length},{"gain",c.gain}});
    return {{"schema_version",1},{"planner_version",scene.empty()?"structure-aware-v4":scene.value("planner",std::string("independent-dj-5"))},{"compiled_scene",scene},{"scene_attempt",sceneAttempt},{"id",id},{"queue_generation",generation},{"a",a},{"b",b},{"recipe",recipe},{"family",family},{"seed",seed},{"sample_rate",sr},
        {"source_out_seconds",outCue},{"source_in_seconds",inCue},{"source_a_start_seconds",aSourceStart()},{"source_b_end_seconds",bSourceEnd()},
        {"mix_seconds",seconds},{"lead_seconds",lead},{"post_seconds",post},{"b_start_seconds",bStart},{"handover_frame",std::llround((scene.empty()?lead+seconds:impact)*sr)},
        {"duration_frames",frames()},{"bars",bars},{"beat_unit","quarter"},{"a_time_map",mapA.json()},{"b_time_map",mapB.json()},
        {"vocal_end_seconds",vocalEnd},{"vocal_open_seconds",vocalOpen},{"bass_swap_seconds",swap},{"pattern",pattern},{"delay_beats",delayBeats},{"feedback",feedback},
        {"stems",useStems},{"echo",echo},{"roll",roll},{"stop",stop},{"impact_reset",reset},{"score",score},{"harmony_fit",harmony},{"explanation",explanation},{"reason_codes",reasons},
        {"blend",blend},{"pulse_ramp",pulseRamp},{"structural_alignment",{{"exit_bar",outBar},{"entry_bar",inBar},{"entry_anchor_seconds",entryAnchor},{"exit_section_boundary",exitBoundary},{"exit_suitability",exitStructure},{"entry_suitability",entryStructure}}},
        {"similarity",{{"overall",similarity},{"rhythm",grooveFit},{"timbre",timbreFit},{"energy_ratio",energyRatio}}},
        {"effects",{{"bass_ramp_seconds",bassRamp},{"tail_seconds",tailSeconds},{"delay_wet",wet},{"reverb_wet",reverbWet},{"filter_style",filterStyle},{"capture_start",captureStart},{"capture_end",captureEnd},{"impact_seconds",impact},{"incoming_source_attack_seconds",attackSource},{"relay_stem",relayStem},{"capture_stem",captureStem},{"voice_clips",clips}}},
        {"stem_requests",!scene.empty()?scene.at("resources"):(family!="natural"&&(useStems||outBar>=0))?Json::array({Json{{"track",a},{"start",std::max(0.,aSourceStart()-1.)},{"end",outCue+1.}},Json{{"track",b},{"start",std::max(0.,inCue-1.)},{"end",bSourceEnd()+1.}}}):Json::array()},
        {"automation",{{"incoming_attack",{{"start",bStart},{"full_gain_at",impact}}},{"bass",{{"center",swap},{"duration",bassRamp},{"complementary",true}}},{"vocal_a",{{"end",vocalEnd},{"fade_after_word_ms",100}}},{"vocal_b",{{"phrase_start",vocalOpen}}},{"fx",{{"capture_from",captureStart},{"capture_to",captureEnd},{"end",captureEnd+tailSeconds},{"filtered",true}}}}},
        {"escape_plan",{{"adaptive_compact",true},{"when","try measured earlier cues and shorter stereo phrases; preserve original if no scene fits"}}},{"limits",{{"max_warp",!scene.empty()&&(scene.value("planner",std::string{})=="independent-dj-2"||scene.value("planner",std::string{})=="independent-dj-3"||scene.value("planner",std::string{})=="independent-dj-4"||scene.value("planner",std::string{})=="independent-dj-5")?.18:.06},{"max_feedback",.72},{"max_fx_tail_seconds",10},{"max_loop_repeats",16}}},{"candidates",candidates}};
}
Plan Plan::from(const Json&j){
    Plan p; p.id=j.at("id"); p.a=j.at("a");p.b=j.at("b");p.sr=j.at("sample_rate");p.recipe=j.at("recipe");p.family=j.at("family");p.seed=j.at("seed");p.generation=j.at("queue_generation");
    p.scene=j.value("compiled_scene",Json::object());
    p.sceneAttempt=j.value("scene_attempt",Json::object());
    p.outCue=j.at("source_out_seconds");p.inCue=j.at("source_in_seconds");p.seconds=j.at("mix_seconds");p.lead=j.at("lead_seconds");p.post=j.at("post_seconds");p.bStart=j.at("b_start_seconds");p.bars=j.at("bars");p.pattern=j.at("pattern");
    p.mapA=TimeMap::from(j.at("a_time_map"));p.mapB=TimeMap::from(j.at("b_time_map"));p.vocalEnd=j.at("vocal_end_seconds");p.vocalOpen=j.at("vocal_open_seconds");p.swap=j.at("bass_swap_seconds");
    p.delayBeats=j.at("delay_beats");p.feedback=j.at("feedback");p.useStems=j.at("stems");p.echo=j.at("echo");p.roll=j.at("roll");p.stop=j.at("stop");p.reset=j.at("impact_reset");p.score=j.at("score");p.harmony=j.at("harmony_fit");p.explanation=j.at("explanation");
    p.blend=j.value("blend",isOverlay(p.family));p.pulseRamp=j.value("pulse_ramp",false);
    auto e=j.value("effects",Json::object());p.bassRamp=e.value("bass_ramp_seconds",1.);p.tailSeconds=e.value("tail_seconds",4.);p.wet=e.value("delay_wet",.65);p.reverbWet=e.value("reverb_wet",.18);p.filterStyle=e.value("filter_style",0);p.captureStart=e.value("capture_start",p.lead+p.seconds-1.);p.captureEnd=e.value("capture_end",p.lead+p.seconds);p.impact=e.value("impact_seconds",p.lead+p.seconds);p.relayStem=e.value("relay_stem",3);p.captureStem=e.value("capture_stem",0);
    auto structure=j.value("structural_alignment",Json::object());p.outBar=structure.value("exit_bar",-1);p.inBar=structure.value("entry_bar",-1);p.entryAnchor=structure.value("entry_anchor_seconds",p.inCue);p.attackSource=e.value("incoming_source_attack_seconds",p.inCue);p.exitBoundary=structure.value("exit_section_boundary",false);p.exitStructure=structure.value("exit_suitability",0.);p.entryStructure=structure.value("entry_suitability",0.);
    auto s=j.value("similarity",Json::object());p.similarity=s.value("overall",.5);p.grooveFit=s.value("rhythm",.5);p.timbreFit=s.value("timbre",.5);p.energyRatio=s.value("energy_ratio",1.);
    for(auto&c:e.value("voice_clips",Json::array()))p.voiceClips.push_back({c.at("deck"),c.at("source"),c.at("start"),c.at("length"),c.at("gain")});
    return p;
}
Json recipeCatalog(){
    Json result=Json::array();
    for(const auto&s:djStyles)result.push_back({{"id",s.id},{"family",s.id},{"title",s.title},{"version",2},{"implemented",true},{"separation","none"},{"preferred_bars",s.bars},{"tempo","shared measured pulse or explicit pulse bridge"},{"repeat",s.repeat},{"delay",s.delay},{"reverb",s.room},{"auto_dj",s.autoDJ}});
    return result;
}
std::vector<Plan> planPlaylist(const std::vector<Track>&ts,const Policy&policy,uint64_t seed){
    if(ts.size()<2)throw std::runtime_error("Need two prepared tracks");double firstMinExit=0;
    for(int attempt=0;attempt<3;attempt++){
      std::vector<Plan> plans;std::vector<std::string> history;double minExit=firstMinExit;bool retry=false;
      for(size_t ix=0;ix<ts.size();ix++){
        const Track&a=ts[ix],&b=ts[(ix+1)%ts.size()];if(a.sampleRate!=b.sampleRate)throw std::runtime_error("Canonical sample rates differ");
        if(policy.mode=="off"){Plan p;p.a=int(ix);p.b=int((ix+1)%ts.size());p.id=a.id+"-"+b.id;p.sr=a.sampleRate;p.family="natural";p.recipe="OFF";p.outCue=a.duration;p.post=0;plans.push_back(p);continue;}
        bool closing=ix+1==ts.size()&&!plans.empty();double bLimit=closing?std::max(0.,plans.front().aSourceStart()-2):-1;
        // A resource pass may relax unobserved vocal boundaries, but must not
        // replace a playable pair just because another pair still needs PCM.
        // Otherwise each refinement can move already prepared cues again.
        auto select=[&](Json&vetoes,double limit){
            if(policy.requestStems){
                Policy readyPolicy=policy;readyPolicy.requestStems=false;
                auto ready=directScene(a,b,int(ix),int((ix+1)%ts.size()),readyPolicy,seed,minExit,history,vetoes,limit);
                if(ready){ready->scene["execution"]["resource_plan_state"]="ready";return ready;}
            }
            return directScene(a,b,int(ix),int((ix+1)%ts.size()),policy,seed,minExit,history,vetoes,limit);
        };
        Json vetoes=Json::array();auto scene=select(vetoes,bLimit);
        if(!scene&&closing&&attempt<2){
            Json unboundedVetoes=Json::array();auto unbounded=select(unboundedVetoes,-1);
            // If an earlier/compact return cannot fit, reserve that return
            // before choosing the first exit and rebuild the musical chain.
            // The loop guard remains active in both planning and compilation.
            if(unbounded&&unbounded->bSourceEnd()+2>plans.front().aSourceStart()){
                firstMinExit=std::max(firstMinExit+2,unbounded->bSourceEnd());retry=true;break;
            }
        }
        if(!scene)throw std::runtime_error("No measured independent scene for "+a.id+" -> "+b.id+". Silent/end-to-start fallback is disabled. "+vetoes.dump());
        Plan p=*scene;if(attempt)p.scene["execution"]["playlist_loop_replan_attempts"]=attempt;
        plans.push_back(p);history.push_back(p.family);if(history.size()>3)history.erase(history.begin());minExit=p.bSourceEnd();
      }
      if(!retry)return plans;
    }
    throw std::runtime_error("No jointly valid playlist loop after bounded musical replanning");
}
}
