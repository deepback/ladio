#pragma once
#include "audio.hpp"
namespace ladio::mobile {
// Accept both the existing flat application asset directory and the movable
// ModelKit root. Resolve only a directory contained in the bundle itself.
inline std::filesystem::path modelDirectory(std::filesystem::path root){
    const auto manifest=root/"manifest.json";
    if(!std::filesystem::is_regular_file(manifest))return root;
    const auto spec=readJson(manifest);
    if(spec.value("format","")!="ladio-model-kit"||spec.value("schema_version",0)!=1)
        throw std::runtime_error("Unsupported Ladio model bundle");
    std::filesystem::path relative=spec.at("runtime").at("model_directory").get<std::string>();
    if(relative.empty()||relative.is_absolute())throw std::runtime_error("Invalid model bundle directory");
    for(const auto&part:relative)if(part=="..")throw std::runtime_error("Model directory escapes bundle");
    auto base=std::filesystem::weakly_canonical(root),resolved=std::filesystem::weakly_canonical(root/relative);
    auto b=base.begin(),p=resolved.begin();for(;b!=base.end();++b,++p)if(p==resolved.end()||*b!=*p)throw std::runtime_error("Model directory escapes bundle");
    if(!std::filesystem::is_directory(resolved))throw std::runtime_error("Model bundle directory is missing");
    return resolved;
}
}
