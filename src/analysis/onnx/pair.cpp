#include "models.hpp"
#include "checksum.hpp"
#include "level_match.hpp"
#include "waveform_join.hpp"
#include <chrono>
namespace ladio::mobile {
static void u16(std::ostream&f,uint16_t n){char b[2]={char(n),char(n>>8)};f.write(b,2);}
static void u32(std::ostream&f,uint32_t n){u16(f,n);u16(f,n>>16);}
Json prepareTransition(const Json&r,Models&models,const std::function<bool()>&cancel,const std::function<double()>&minimumPosition){
    constexpr int sr=44100;
    auto directory=std::filesystem::absolute(r.at("directory").get<std::string>());
    auto resources=std::filesystem::absolute(r.value("resource_directory",(directory/"resources").string()));
    std::filesystem::create_directories(directory);std::filesystem::create_directories(resources);
    Json library={{"schema_version",1},{"tracks",Json::array({readJson(r.at("analysis_a").get<std::string>()),readJson(r.at("analysis_b").get<std::string>())})}};
    library["analysis_paths"]={r.at("analysis_a"),r.at("analysis_b")};
    library["prepared_voice_cache"]=(resources/"voices").string();
    library["capture_scene"]=true;
    for(auto&a:library["tracks"]){
        for(auto&row:a["curves"])if(row.value("percussive_only",false)&&!row.contains("vocals_rms"))row["vocal"]=0.;
        // The production stereo path must not inspect, map or revalidate
        // retired separator caches merely because they remain in metadata.
        a.erase("stems");a.erase("role_profiles");a["stem_windows"]=Json::array();
    }
    uint64_t seed=r.value("seed",uint64_t(20261012));double minimum=r.value("minimum_exit_seconds",0.);
    std::vector<std::string> history=r.value("history",std::vector<std::string>{});
    if(r.contains("max_pre_bars"))library["tracks"][0]["scene_request"]["pre_bars"]=std::clamp(r.at("max_pre_bars").get<int>(),2,32);
    if(r.contains("scene_family"))library["tracks"][0]["preferred_transition"]=r["scene_family"];
    auto begun=std::chrono::steady_clock::now();int passes=0;auto resourcePolicy=policyFor("dj");resourcePolicy.requestStems=false;
    for(;passes<8;passes++){
        // Playback continues while models work. Reconsider cues against the
        // actual playhead (which stops on pause), never against wall time.
        if(minimumPosition)minimum=std::max(minimum,minimumPosition());
        check(cancel);auto plan=planPair(library,resourcePolicy,seed,minimum,history).json();writeJson(directory/"requested-plan.json",plan);
        for(const auto&voice:plan.at("compiled_scene").at("voices")){
            const auto&analysis=library["tracks"][voice.at("deck").get<int>()];if(!analysis.contains("grid_coverage"))continue;
            double first=voice.at("source_start_seconds"),last=voice.at("source_end_seconds");bool measured=false;
            for(const auto&range:analysis["grid_coverage"])measured|=range[0].get<double>()<=first+.02&&range[1].get<double>()>=last-.02;
            if(!measured)throw std::runtime_error(std::string("Measured grid coverage missing for scene: ")+(voice.at("deck").get<int>()==0?"A":"B"));
        }
        if(!plan.at("stem_requests").empty())throw std::runtime_error("Scene requires an unavailable separation capability");
        library["resource_worked"]=false;
        // Persist measured windows before the next planning pass or cancellation.
        // A queue reorder can reuse acoustic work, but not an old pair plan.
        writeJson(r.at("analysis_a").get<std::string>(),library["tracks"][0]);writeJson(r.at("analysis_b").get<std::string>(),library["tracks"][1]);
        if(!library.value("resource_worked",false)){
            if(minimumPosition&&plan.at("source_a_start_seconds").get<double>()<minimumPosition()+2)continue;
            break;
        }
    }
    check(cancel);models.release();
    if(passes==8)throw std::runtime_error("Playback moved past all prepared candidates");
    auto manifest=preparePair(library,directory,directory,seed,minimum,history,cancel);
    auto session=Session::load(directory/"session.json");auto&compiled=session->transitions.at(0);auto plan=compiled.plan;
    if(!compiled.scene)throw std::runtime_error("Musical scene is missing");const auto&scene=*compiled.scene;
    const auto&contract=plan.scene;double seconds=double(scene.frames)/sr;
    double aStart=plan.aSourceStart(),bResume=contract.at("recovery").at("b_source_seconds");
    const SceneVoice*incoming=nullptr;for(const auto&v:scene.voices)if(v.id=="B.main")incoming=&v;
    if(!incoming||std::abs(incoming->map.rateAt(seconds-double(incoming->start)/sr)-1)>1e-8)throw std::runtime_error("Scene does not return B to native speed");
    auto&originalA=*session->sources.at(0).original;auto&originalB=*session->sources.at(1).original;
    Frame resume=std::llround(bResume*sr),total=scene.frames+originalB.frames()-resume;
    if(resume<0||resume>=originalB.frames()||total<=scene.frames||total>sr*600LL)throw std::runtime_error("Invalid B continuation");
    auto wav=directory/"transition.wav",tmp=directory/"transition.wav.pending",levelFile=directory/"host-scene.f32";
    Engine engine(session);std::ofstream levelOutput(levelFile,std::ios::binary);
    std::array<float,512> left{},right{};double peak=0;
    std::array<float,1024> stereo{};
    const double levelFrom=contract.at("anchors").at("B_first_audible").at("seconds"),levelEnd=double(scene.arrival)/sr;
    for(Frame n=0;n<scene.frames;n+=512){check(cancel);if(n%16384==0)models.report("render",int(n),int(scene.frames));int count=int(std::min<Frame>(512,scene.frames-n));engine.render(left.data(),right.data(),count);
        for(int k=0;k<count;k++){
            Frame index=n+k;
            // Match the entire arrangement to the originals played by Lane,
            // moving between their mastering levels over the musical phrase.
            double compensation=originalLevelGain(session->tracks[0].lufs,session->tracks[1].lufs,double(index)/sr,levelFrom,levelEnd);
            double head=1-smooth((double(index)/sr-.02)/.08);
            for(int ch=0;ch<2;ch++){
                float value=float((ch?right[k]:left[k])*compensation);
                if(head>0)value+=float(head)*(originalA.sample(std::llround(aStart*sr)+index,ch)-value);
                if(!std::isfinite(value))throw std::runtime_error("Nonfinite scene output");stereo[k*2+ch]=value;
            }
        }
        levelOutput.write(reinterpret_cast<const char*>(stereo.data()),count*8);
    }
    if(!levelOutput)throw std::runtime_error("Cannot level-match scene");levelOutput.close();
    rejoinNative(levelFile,2,sr,scene.frames,scene.frames-std::llround(sr*.60),std::llround(sr*.50),[&](Frame index,int ch){return originalB.sample(resume-scene.frames+index,ch);});
    // End on the exact original, with no residual processing or second codec.
    {std::fstream tail(levelFile,std::ios::in|std::ios::out|std::ios::binary);Frame first=scene.frames-sr/10;tail.seekp(first*8);for(Frame index=first;index<scene.frames;index++){std::array<float,2>x{originalB.sample(resume-scene.frames+index,0),originalB.sample(resume-scene.frames+index,1)};tail.write(reinterpret_cast<const char*>(x.data()),8);}if(!tail)throw std::runtime_error("Cannot write native continuation edge");}
    PCMFile matched(levelFile);ScenePeakGuard guard(matched,sr);
    std::ofstream f(tmp,std::ios::binary);f.write("RIFF",4);u32(f,36+scene.frames*4);f.write("WAVEfmt ",8);u32(f,16);u16(f,1);u16(f,2);u32(f,sr);u32(f,sr*4);u16(f,4);u16(f,16);f.write("data",4);u32(f,scene.frames*4);
    std::array<char,4096*4> block{};
    for(Frame first=0;first<scene.frames;first+=4096){check(cancel);int count=int(std::min<Frame>(4096,scene.frames-first));for(int k=0;k<count;k++)for(int ch=0;ch<2;ch++){
        double value=matched.sample(first+k,ch)*guard.at(first+k);peak=std::max(peak,std::abs(value));if(std::abs(value)>.99001)throw std::runtime_error("Host scene peak guard exceeded ceiling");
        auto v=uint16_t(int16_t(std::lround(value*32767)));block[(k*2+ch)*2]=char(v);block[(k*2+ch)*2+1]=char(v>>8);
    }f.write(block.data(),count*4);}
    if(!f)throw std::runtime_error("Cannot save transition");f.close();check(cancel);std::filesystem::rename(tmp,wav);
    Json result={{"version",2},{"engine","independent-dj-2"},{"family",scene.family},{"variant",contract.at("variant")},{"material_mode",contract.at("material_mode")},{"recipe",plan.recipe},{"seed",seed},{"compiled_scene",contract},{"source_a_start_seconds",aStart},{"source_a_release_seconds",plan.outCue},{"source_b_start_seconds",incoming->sourceStart},{"b_start_seconds",double(incoming->start)/sr},{"b_time_map",incoming->map.json()},{"b_initial_rate",incoming->map.initialRate},{"b_anchor_seconds",plan.entryAnchor},{"b_resume_seconds",double(resume)/sr},{"duration_seconds",seconds},{"arrival_seconds",double(scene.arrival)/sr},{"transition_frames",scene.frames},{"playback_frames",total},{"playback_duration_seconds",double(total)/sr},{"b_duration_seconds",session->tracks[1].duration},{"continuation_mode","same_pcm_stream"},{"sample_rate",sr},{"wav",wav.string()},{"tempo_a",session->tracks[0].bpm},{"tempo_b",session->tracks[1].bpm},{"shared_pulse",!scene.reset},{"peak",peak},{"resource_passes",passes},{"vocal_boundary_verified",contract.at("execution").value("vocal_plan_state","")=="measured"},{"analysis_tier","independent-native-stereo"},{"host_level_join",{{"entry_seconds",.10},{"exit_seconds",.60},{"reason","Lane original mastering to normalized scene and back"}}},{"native_preparation_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count()}};
    result["host_level_join"]={{"entry_seconds",.10},{"exit_seconds",.60},{"whole_scene_original_level",true},{"a_compensation_db",session->tracks[0].lufs+14},{"b_compensation_db",session->tracks[1].lufs+14},{"curve","smooth dB phrase blend; phase-correlated covariance-preserving native join"},{"peak_guard_ceiling",.99},{"peak_guard_min_gain_db",20*std::log10(std::max(1e-12,guard.minimum))},{"peak_guard_limited_fraction",double(guard.limited)/scene.frames}};
    result["engine"]="independent-dj-5";
    result["analysis_tier"]=models.beatFile()+"+native-structure; stereo";
    result["continuation_mode"]="virtual_pcm_stream";
    result["continuation_pcm"]=session->tracks[1].pcm;
    result["continuation_start_frame"]=resume;
    result["stored_transition_bytes"]=std::filesystem::file_size(wav);
    result["avoided_continuation_bytes"]=(originalB.frames()-resume)*4;
    writeJson(directory/"plan.json",result);
    // Published playback only needs the bounded WAV and diagnostics. Release
    // rendered voice buffers after all readers close; sparse source windows
    // live in a separate LRU cache and are reusable for another queue order.
    session.reset();
    if(!r.value("retain_diagnostics_assets",false)){
        for(auto&item:std::filesystem::directory_iterator(directory)){auto p=item.path();if(p.extension()==".f32"||p.extension()==".pcm")std::filesystem::remove(p);}
        std::filesystem::remove(directory/"session.json");
    }
    return result;
}
}
