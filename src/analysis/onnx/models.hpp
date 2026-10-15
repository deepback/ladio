#pragma once
#include "dsp.hpp"
#include <onnxruntime_cxx_api.h>
namespace ladio::mobile {
void cancelInference();
class Models {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"Ladio"};
    std::filesystem::path root;std::unique_ptr<Ort::Session> beat,stems;
    Ort::SessionOptions options;
    int threads_=2;bool accelerator_=true;std::string beatFile_,beatIdentity_;
public:
    const std::string& beatFile()const{return beatFile_;}
    const std::string& beatIdentity()const{return beatIdentity_;}
private:
    std::unique_ptr<Ort::Session> load(const char*name);
public:
    std::function<void(const std::string&,int,int)> progress;
    void report(const std::string&stage,int done=0,int total=1){if(progress)progress(stage,done,total);}
    explicit Models(std::filesystem::path,int threads=2,bool accelerator=true);
    // Optional supplied activity/structure models. No source separation or ASR.
    void musicalEvents(Json&,const PCMFile&,const std::function<bool()>&,const Json&previous);
    void release(){beat.reset();stems.reset();}
    // Returns unthresholded frame logits. Postprocessing remains the Mac minimal method.
    std::pair<std::vector<float>,std::vector<float>> grid(const PCMFile&,const std::function<bool()>&,
        const std::vector<float>*mono=nullptr,const std::filesystem::path&cache={},
        const std::vector<std::pair<double,double>>&ranges={});
    std::array<std::vector<float>,4> separate(const PCMFile&,Frame,Frame,const std::function<bool()>&);
};
Json analyze(const std::string&pcm,const std::string&id,Models&,const std::function<bool()>&,
             const std::string&scope="full",const Json&previous=Json::object());
void descriptors(Json&);
Json prepareTransition(const Json&,Models&,const std::function<bool()>&,
                       const std::function<double()>&minimumPosition={});
void observeWindow(Json&,const std::array<std::vector<float>,4>&,Frame,Frame,Json&);
Json prepareResources(Json,const Json&plan,const std::filesystem::path&,Models&,const std::function<bool()>&);
}
