#include "ladio/ladio.h"
#include "ladio/version.h"
#include "dj_catalog.hpp"
#include <iostream>
#include <string>
#include "types.hpp"
#include "audio.hpp"
#include "../src/analysis/structure.hpp"
int main(int argc,char**argv){
    int ok=0;std::string command=argc>1?argv[1]:"";
    if(command=="version"||command=="--version"){std::cout<<"LADIO "<<ladio_version()<<"\n";return 0;}
    if(command=="catalog"){ladio::Json rows=ladio::Json::array();for(const auto&s:ladio::djStyles)rows.push_back({{"id",s.id},{"title",s.title},{"preferred_bars",s.bars},{"shape",s.shape},{"repeat",s.repeat},{"delay",s.delay},{"reverb",s.room},{"automatic_candidate",s.autoDJ}});std::cout<<rows.dump(2)<<'\n';return 0;}

    if(command=="structure"&&argc==4){try{auto j=ladio::readJson(argv[2]);for(auto&t:j.at("tracks"))ladio::analyzeStructure(t);ladio::writeJson(argv[3],j);return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
    else if((command=="pair-requests"||command=="pair-plan"||command=="pair-prepare")&&argc>=4){try{auto library=ladio::readJson(argv[2]);uint64_t seed=argc>4?std::stoull(argv[4]):20261012;double minimum=argc>5?std::stod(argv[5]):0;if(command=="pair-prepare")ladio::preparePair(library,std::filesystem::path(argv[2]).parent_path(),argv[3],seed,minimum);else{auto policy=ladio::policyFor("dj");policy.requestStems=command=="pair-requests";ladio::writeJson(argv[3],ladio::planPair(library,policy,seed,minimum).json());}return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
    else if(command=="freeze"&&argc==3){try{ladio::freezeSession(argv[2]);return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
    else if(command=="diagnose"&&argc==4){try{ladio::renderDiagnostics(argv[2],argv[3]);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
    else if(command=="prepare"&&argc>=5)ok=ladio_prepare_library(argv[2],argv[3],argv[4],argc>5?std::stoull(argv[5]):20261012);
    else if(command=="render"&&argc==5)ok=ladio_render_session(argv[2],argv[3],argv[4]);
    else if((command=="plan"||command=="requests")&&argc>=5){try{auto j=ladio::readJson(argv[2]);std::vector<ladio::Track>tracks;for(auto&t:j.at("tracks"))tracks.push_back(ladio::trackFromJson(t));auto policy=ladio::policyFor(argv[4]);policy.requestStems=command=="requests";auto plans=ladio::planPlaylist(tracks,policy,argc>5?std::stoull(argv[5]):20261012);ladio::Json output=ladio::Json::array();for(auto&p:plans)output.push_back(p.json());ladio::writeJson(argv[3],{{"schema_version",1},{"mode",argv[4]},{"draft_requires_resources",policy.requestStems},{"plans",output}});return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
    else {std::cerr<<"Usage:\n  ladio prepare LIBRARY.json OUTPUT_DIR MODE [SEED]\n  ladio plan LIBRARY.json OUTPUT.json MODE [SEED]\n  ladio render SESSION.json OUTPUT.wav REPORT.json\n";return 2;}
    if(!ok){std::cerr<<ladio_last_error()<<'\n';return 1;}return 0;
}
