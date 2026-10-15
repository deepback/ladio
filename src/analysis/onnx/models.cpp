#include "models.hpp"
#include "model_bundle.hpp"
#include "event_features.hpp"
#include "checksum.hpp"
#include <mutex>
namespace ladio::mobile {
namespace {
std::mutex runMutex;Ort::RunOptions*activeRun=nullptr;
struct InferenceRun {
    Ort::RunOptions options;
    InferenceRun(){std::lock_guard lock(runMutex);activeRun=&options;}
    ~InferenceRun(){std::lock_guard lock(runMutex);activeRun=nullptr;}
};
}
void cancelInference(){std::lock_guard lock(runMutex);if(activeRun)activeRun->SetTerminate();}
Models::Models(std::filesystem::path p,int threads,bool accelerator):root(modelDirectory(std::move(p))),threads_(std::clamp(threads,1,2)),accelerator_(accelerator){
    auto spec=readJson(root/"beat-model.json");beatFile_=spec.at("file");beatIdentity_=spec.at("sha256");
    if(beatFile_!="beat-this-small0.onnx"&&beatFile_!="beat-this-final0.onnx")throw std::runtime_error("Unsupported beat model contract");
    env.DisableTelemetryEvents();options.SetIntraOpNumThreads(std::clamp(threads,1,2));options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.AddConfigEntry("session.intra_op.allow_spinning","0");options.AddConfigEntry("session.inter_op.allow_spinning","0");
    // Reuse intermediate allocations within one model. The entire session is
    // released before switching beat/stem models and after each preparation.
    options.EnableCpuMemArena();options.EnableMemPattern();
}
std::unique_ptr<Ort::Session> Models::load(const char*name){
    auto selected=options.Clone();
#ifdef __ANDROID__
    if(accelerator_&&std::string(name)=="htdemucs-955717e8.onnx"){
        const auto providers=Ort::GetAvailableProviders();
        if(std::find(providers.begin(),providers.end(),"XnnpackExecutionProvider")!=providers.end())selected.AppendExecutionProvider("XNNPACK",{{"intra_op_num_threads",std::to_string(threads_)}});
    }
#endif
    return std::make_unique<Ort::Session>(env,(root/name).c_str(),selected);
}
std::pair<std::vector<float>,std::vector<float>> Models::grid(const PCMFile&pcm,const std::function<bool()>&cancel,const std::vector<float>*mono,const std::filesystem::path&cache,const std::vector<std::pair<double,double>>&ranges){
    stems.reset();check(cancel);
    std::vector<float> resampled;if(!mono){resampled=mono22050(pcm);mono=&resampled;}const auto&y=*mono;
    auto fb=readFloats(root/"beat-mel-filterbank.f32");if(fb.size()!=513*128)throw std::runtime_error("Invalid mel bank");
    std::array<std::vector<std::pair<int,float>>,128> bands;
    for(int mel=0;mel<128;mel++)for(int k=0;k<513;k++)if(fb[k*128+mel]!=0)bands[mel].push_back({k,fb[k*128+mel]});
    int count=1+int(y.size())/441;std::vector<float> logits(count,-1000),downs(count,-1000);
    std::vector<std::complex<double>> buf(1024);std::array<float,513> power{};
    std::array<double,1024> window{};for(int k=0;k<1024;k++)window[k]=.5-.5*std::cos(2*pi*k/1024);
    std::vector<int> starts;for(int start=-6;start<count-6;start+=1488)starts.push_back(start);if(count>1488)starts.back()=count-1494;
    std::vector<int> owners(starts.size()),selected;int priorEnd=0;
    for(size_t k=0;k<starts.size();k++){
        int start=starts[k],length=std::min(1500,count+6-start),end=start+length-6;owners[k]=std::max(start+6,priorEnd);priorEnd=std::max(priorEnd,end);
        bool wanted=ranges.empty();for(auto [first,last]:ranges)wanted|=end/50.>first&&owners[k]/50.<last;if(wanted)selected.push_back(int(k));
    }
    auto mem=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);const char*in[] = {"spect"};const char*out[]={"beat","downbeat"};
    std::filesystem::path directory;if(!cache.empty()){
        // Exact logits are reusable after a deadline/queue change. Identity
        // binds immutable PCM bytes, checkpoint SHA and chunk geometry.
        const auto&checkpoint=beatIdentity_;
        directory=cache.string()+"-"+checkpoint;std::filesystem::create_directories(directory);
    }
    // Reverse aggregation exactly reproduces Beat This keep_first overlaps.
    int completed=0;for(auto it=selected.rbegin();it!=selected.rend();++it){check(cancel);report("beats",completed++,int(selected.size()));int index=*it,start=starts[index],length=std::min(1500,count+6-start);
        std::filesystem::path cached;std::vector<float> saved;if(!directory.empty()){
            cached=directory/(std::to_string(count)+"-"+std::to_string(start)+"-"+std::to_string(length)+".f32");
            if(std::filesystem::is_regular_file(cached))try{saved=readFloats(cached);if(saved.size()!=size_t(length*2)||!std::all_of(saved.begin(),saved.end(),[](float v){return std::isfinite(v);}))saved.clear();}catch(...){saved.clear();}
        }
        if(saved.empty()){
        if(!beat)beat=load(beatFile_.c_str());check(cancel);std::vector<float> chunk(length*128);
        // Compute features only for a cache miss and its exact context. Cached
        // chunks no longer pay for a whole-track mel transform on every pair.
        for(int i=0;i<length;i++){
            int t=start+i;if(t<0||t>=count)continue;if(i%128==0)check(cancel);
            for(int k=0;k<1024;k++)buf[k]=y[reflected(t*441+k-512,int(y.size()))]*window[k];fft(buf);
            for(int k=0;k<513;k++)power[k]=float(std::abs(buf[k])/32);
            for(int mel=0;mel<128;mel++){float sum=0;for(auto [k,weight]:bands[mel])sum+=power[k]*weight;chunk[i*128+mel]=std::log1p(1000*sum);}
        }
        std::array<int64_t,3> shape{1,length,128};auto tensor=Ort::Value::CreateTensor<float>(mem,chunk.data(),chunk.size(),shape.data(),shape.size());
        InferenceRun run;check(cancel);auto result=beat->Run(run.options,in,&tensor,1,out,2);
        auto b=result[0].GetTensorData<float>(),d=result[1].GetTensorData<float>();
        saved.assign(b,b+length);saved.insert(saved.end(),d,d+length);
        if(!cached.empty())writeFloats(cached,saved);
        }
        check(cancel);for(int i=std::max(6,owners[index]-start);i<length-6;i++){if(start+i<0||start+i>=count)throw std::runtime_error("Beat chunk bounds");logits[start+i]=saved[i];downs[start+i]=saved[length+i];}
    }
    return {std::move(logits),std::move(downs)};
}
void Models::musicalEvents(Json&analysis,const PCMFile&pcm,const std::function<bool()>&cancel,const Json&previous){
    const auto specPath=root/"event-models.json";
    if(!std::filesystem::is_regular_file(specPath)){analysis["musical_event_models"]={{"enabled",false},{"reason","No verified frontend manifest"}};return;}
    auto spec=readJson(specPath);
    if(!spec.value("enabled",false))return;
    if(spec.value("frontend",std::string{})!=EventFeatures::version)throw std::runtime_error("Unsupported event model frontend");
    // Bind cached observations to actual weights, not just a model filename.
    Json identities=Json::object();
    for(auto name:{"InstrumentActivityModel.onnx","StructuralFeaturesModel.onnx"}){
        auto actual=checksumFile(root/name);
        if(spec.at("models").at(name).at("fnv1a64")!=actual)throw std::runtime_error("Event model integrity failed");
        identities[name]=actual;
    }
    Json identity={{"pcm",analysis.at("pcm_fnv1a64")},{"models",identities},{"frontend",EventFeatures::version}};
    const auto cached=std::filesystem::path(analysis.at("pcm_path").get<std::string>()+".musical-events.json");
    Json observations;bool hit=false;
    auto reuse=[&](const Json&old){
        if(old.value("identity",Json{})!=identity||old.value("step_seconds",0.)!=.05||old.value("frame_center_seconds",-1.)!=1024./24000)return;
        size_t count=0;
        for(const auto&group:{std::make_pair("activity",std::vector<const char*>{"vocals","drums","bass","others"}),std::make_pair("structure",std::vector<const char*>{"phrases","segments","sections"})}){
            if(!old.contains(group.first)||!old.at(group.first).is_object())return;
            for(auto field:group.second){
                if(!old.at(group.first).contains(field))return;
                const auto&row=old.at(group.first).at(field);
                if(!row.is_array()||row.size()<400||row.size()>12000||(count&&count!=row.size()))return;
                count=row.size();for(const auto&v:row)if(!v.is_number()||!std::isfinite(v.get<double>())||v.get<double>()<0||v.get<double>()>1)return;
            }
        }
        const double expected=std::max(400.,std::ceil(analysis.at("duration").get<double>()/.05));
        if(std::abs(double(count)-expected)>2)return;
        observations=old;hit=true;
    };
    if(previous.contains("model_observations"))reuse(previous.at("model_observations"));
    if(!hit&&std::filesystem::exists(cached))try{reuse(readJson(cached));}catch(const std::exception&){}
    if(!hit){
        beat.reset();stems.reset();report("musical_features");check(cancel);
        auto audio=monoResampled(pcm,24000,cancel);EventFeatures frontend;
        const int frames=std::max(400,int((audio.size()+1199)/1200));
        std::vector<float> activity,structure;activity.reserve(frames*128);structure.reserve(frames*256);
        for(int t=0;t<frames;t++){if(t%128==0)check(cancel);size_t at=std::min(audio.size(),size_t(t)*1200);frontend.frame(audio.data()+at,audio.size()-at,activity,structure);}
        audio.clear();audio.shrink_to_fit();
        observations={{"identity",identity},{"step_seconds",.05},{"frame_center_seconds",1024./24000},{"activity",Json::object()},{"structure",Json::object()}};
        auto run=[&](const char*name,const std::vector<float>&features,int bands,const std::vector<const char*>&outputs,const char*group){
            report(group);auto session=load(name);auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
            std::vector<int> starts{0};if(frames>6000)starts.push_back(frames-6000);
            std::vector<std::vector<float>> values(outputs.size(),std::vector<float>(frames));
            for(size_t chunk=0;chunk<starts.size();chunk++){
                check(cancel);int first=starts[chunk],length=std::min(6000,frames-first);
                std::array<int64_t,4> shape{1,length,bands,1};
                auto input=Ort::Value::CreateTensor<float>(memory,const_cast<float*>(features.data())+size_t(first)*bands,size_t(length)*bands,shape.data(),shape.size());
                const char*inputNames[]={"MelSpectrogram"};InferenceRun active;check(cancel);
                auto result=session->Run(active.options,inputNames,&input,1,outputs.data(),outputs.size());
                int begin=chunk?frames/2:0,end=chunk+1==starts.size()?frames:frames/2;
                for(size_t channel=0;channel<outputs.size();channel++){
                    auto info=result[channel].GetTensorTypeAndShapeInfo();
                    if(info.GetElementCount()!=size_t(length))throw std::runtime_error("Event model output clock mismatch");
                    const float*row=result[channel].GetTensorData<float>();
                    for(int t=begin;t<end;t++){double x=row[t-first];if(!std::isfinite(x))throw std::runtime_error("Nonfinite event model output");values[channel][t]=float(clamp(x));}
                }
            }
            for(size_t k=0;k<outputs.size();k++){
                // Millipercent precision keeps observations compact; timestamps
                // and the unthresholded models are not rerun for a queue change.
                Json row=Json::array();for(auto x:values[k])row.push_back(std::round(x*10000)/10000.);
                observations[group][outputs[k]]=std::move(row);
            }
        };
        run("InstrumentActivityModel.onnx",activity,128,{"vocals","drums","bass","others"},"activity");activity.clear();activity.shrink_to_fit();
        run("StructuralFeaturesModel.onnx",structure,256,{"phrases","segments","sections"},"structure");
        check(cancel);writeJson(cached,observations);
    }
    const double offset=observations.at("frame_center_seconds"),step=observations.at("step_seconds");
    auto sample=[&](const char*group,const char*field,double time){const auto&v=observations.at(group).at(field);int i=std::clamp(int(std::llround((time-offset)/step)),0,int(v.size())-1);return v.at(i).get<double>();};
    for(auto&c:analysis["curves"]){double t=c.at("time");for(auto name:{"vocals","drums","bass","others"})c[std::string(name)+"_activity"]=sample("activity",name,t);c["vocal"]=sample("activity","vocals",t);for(auto name:{"phrases","segments","sections"}){double peak=0;for(double dt:{-.10,-.05,0.,.05,.10})peak=std::max(peak,sample("structure",name,t+dt));c[std::string(name)+"_boundary"]=peak;}}
    Json intervals=Json::array();const auto&v=observations["activity"]["vocals"];int start=-1,last=-1;double confidence=0;
    for(int i=0;i<=int(v.size());i++){
        double x=i<int(v.size())?v[i].get<double>():0.;
        if(x>=.5&&start<0){start=i;confidence=x;}
        if(start>=0){confidence=std::max(confidence,x);if(x>=.28)last=i;
            if(i==int(v.size())||i-last>=4){double first=std::max(0.,start*step+offset-.12),end=std::min(analysis.at("duration").get<double>(),(last+1)*step+offset+.15);if(end-first>.16)intervals.push_back({{"start",first},{"end",end},{"confidence",std::min(.95,confidence)},{"method","supplied activity model; no text recognition"}});start=last=-1;}
        }
    }
    // Presence is not phrase segmentation: sustained singing may remain active
    // across a whole verse. Split protection only at a strong musical boundary
    // on the measured grid, with extra support from an activity valley unless
    // boundary confidence is very high. These remain estimates, never words.
    Json phrases=Json::array();double bpm=analysis.value("tempo",120.),minPhrase=std::max(1.5,120/bpm);
    for(const auto&range:intervals){
        double begin=range.at("start"),finish=range.at("end");
        for(double down:analysis.value("downbeats",std::vector<double>{})){
            if(down-begin<minPhrase||finish-down<minPhrase)continue;
            double boundary=0;for(double dt:{-.15,-.10,-.05,0.,.05,.10,.15})boundary=std::max(boundary,sample("structure","phrases",down+dt));
            double centre=sample("activity","vocals",down),surround=.5*(sample("activity","vocals",down-.4)+sample("activity","vocals",down+.4));
            if(boundary<.65||(boundary<.86&&centre>surround*.96))continue;
            phrases.push_back({{"start",begin},{"end",down-.08},{"confidence",range.at("confidence")},{"boundary_confidence",boundary},{"method","activity plus measured musical phrase; words unverified"}});begin=down+.08;
        }
        if(finish>begin)phrases.push_back({{"start",begin},{"end",finish},{"confidence",range.at("confidence")},{"method","activity range end"}});
    }
    analysis["vocal_intervals"]=intervals;analysis["protected_vocal_phrases"]=phrases;analysis["vocal_phrases"]=phrases;
    analysis["vocal_boundary_version"]="activity-and-musical-phrase-v1";
    analysis["musical_event_models"]={{"enabled",true},{"cache_hit",hit},{"frontend",EventFeatures::version},{"identity",identity},{"separation",false},{"transcription",false},{"provenance",spec.value("provenance",std::string("supplied model bundle"))},{"boundaries","activity ranges, not recognized words"}};
    analysis["model_observations"]=std::move(observations);
}
std::array<std::vector<float>,4> Models::separate(const PCMFile&,Frame,Frame,const std::function<bool()>&){
    throw std::runtime_error("No separator is bundled; provide prepared role windows through the scene contract");
}
}
