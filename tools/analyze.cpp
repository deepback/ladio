#include "ladio/analysis.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
int main(int argc,char **argv) {
    if(argc<5) { std::cerr<<"Usage: ladio_analyze analyze PCM MODEL_DIR OUTPUT.json [full|entry|exit]\n"
        <<"       ladio_analyze prepare REQUEST.json MODEL_DIR OUTPUT.json\n"; return 2; }
    std::filesystem::path output=argv[4];
    if(!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::unique_ptr<LadioAnalyzer,decltype(&ladio_analyzer_destroy)> a(ladio_analyzer_create(argv[3],2),ladio_analyzer_destroy);
    if(!a) { std::cerr<<ladio_analyzer_create_error()<<'\n'; return 1; }
    int ok=0;
    std::string asset=std::filesystem::path(argv[2]).stem().string();
    if(std::string(argv[1])=="analyze") ok=ladio_analyzer_analyze(a.get(),argv[2],asset.c_str(),argv[4],argc>5?argv[5]:"full");
    else if(std::string(argv[1])=="prepare") ok=ladio_analyzer_prepare(a.get(),argv[2],argv[4]);
    else { std::cerr<<"Unknown command\n"; return 2; }
    if(!ok) std::cerr<<ladio_analyzer_error(a.get())<<'\n';
    return ok?0:1;
}
