#include "models.hpp"
namespace ladio::mobile {
static double level(const PCMFile&x,Frame lo,Frame hi){lo=std::max<Frame>(0,lo);hi=std::min(hi,x.frames());double s=0;for(Frame k=lo;k<hi;k++)for(int c=0;c<2;c++){double v=x.sample(k,c);s+=v*v;}return hi>lo?std::sqrt(s/((hi-lo)*2)):0;}
static std::pair<double,double> attack(const PCMFile&x,double time){Frame lo=std::max<Frame>(0,std::llround((time-.065)*44100)),hi=std::min(x.frames(),Frame(std::llround((time+.050)*44100)));constexpr int hop=44;int n=int((hi-lo)/hop);if(n<30)return {time,0};std::vector<double> energy(n);for(int i=0;i<n;i++)energy[i]=std::pow(level(x,lo+i*hop,lo+(i+1)*hop),2);struct Edge{double sec,ratio,jump;};std::vector<Edge> edges;double best=-1e9;for(int i=8;i<n-6;i++){double sec=double(lo+i*hop)/44100;if(sec<time-.035||sec>time+.025)continue;double before=0,after=0;for(int k=i-7;k<i-1;k++)before+=energy[k]/6;for(int k=i;k<i+4;k++)after+=energy[k]/4;double ratio=std::log((after+1e-8)/(before+1e-8));edges.push_back({sec,ratio,after-before});best=std::max(best,ratio);}if(best>=.35)for(auto e:edges)if(e.ratio>=best*.78&&e.jump>.0002)return {std::clamp(e.sec-.001,time-.035,time+.025),std::min(1.,best/1.4)};return {time,0};}
void descriptors(Json&a){
    PCMFile original(a.at("pcm_path"));double period=60/a.at("tempo").get<double>();std::vector<std::pair<Json,std::shared_ptr<PCMFile>>> windows;
    for(const auto&w:a.value("stem_windows",Json::array()))if(w.value("state","")=="ready"&&w.at("stem_paths").contains("drums"))windows.push_back({w,std::make_shared<PCMFile>(w["stem_paths"]["drums"].get<std::string>())});
    for(auto&c:a.at("cues")){
        double sec=c.at("time");const PCMFile*source=&original;double origin=0,confidence=.55;
        for(auto&[w,file]:windows){double lo=w["valid_range"][0].get<double>()/44100,hi=w["valid_range"][1].get<double>()/44100;if(lo<=sec&&hi>=sec+16*period){source=file.get();origin=lo;confidence=.9;break;}}
        if(sec-origin>=0&&sec-origin+16*period<=double(source->frames())/44100){std::array<double,128> energy{},flux{};std::array<double,64> pulse{};
            Frame first=std::llround((sec-origin)*44100);for(int k=0;k<128;k++)energy[k]=level(*source,std::llround(first+k*period*44100/8),std::llround(first+(k+1)*period*44100/8));
            for(int k=0;k<128;k++)flux[k]=std::max(0.,energy[k]-energy[(k+127)%128]);double norm=0;for(int k=0;k<64;k++){pulse[k]=(flux[k]+flux[k+64])*.5;norm+=pulse[k]*pulse[k];}norm=std::sqrt(norm);for(auto&v:pulse)v/=norm+1e-12;
            c["pulse_8quarters"]=pulse;c["pulse_confidence"]=norm<.003?0:confidence;c["pulse_time"]=sec;
        }
        source=&original;origin=0;std::string kind="original";for(auto&[w,file]:windows){double lo=w["valid_range"][0].get<double>()/44100,hi=w["valid_range"][1].get<double>()/44100;if(lo<=sec-.07&&hi>=sec+.06){source=file.get();origin=lo;kind="drums";break;}}
        auto [at,quality]=attack(*source,sec-origin);c["attack_seconds"]=at+origin;c["attack_confidence"]=quality;c["attack_material"]=kind;
        std::array<double,12> chroma{};for(const auto&r:a["curves"])if(r["time"].get<double>()>=sec&&r["time"].get<double>()<sec+8){double rms=r.value("rms",0.),weight=rms>.0003&&!r.value("percussive_only",false)?rms*std::sqrt(r.value("tonality",0.)):0;for(int k=0;k<12;k++)chroma[k]+=weight*r["chroma"][k].get<double>();}double norm=0;for(double v:chroma)norm+=v*v;for(auto&v:chroma)v/=std::sqrt(norm)+1e-8;c["key"]=keyInfo(chroma);
        Frame first=std::max<Frame>(0,std::llround(sec*44100)),last=std::min(original.frames(),Frame(std::llround((sec+16*period)*44100)));std::array<double,4> timbre{},bandCounts{};std::vector<std::complex<double>> buf(2048);
        for(Frame pos=first;pos+8192<=last;pos+=4096){for(int k=0;k<2048;k++)buf[k]=(original.sample(pos+k*4,0)+original.sample(pos+k*4,1))*.5*(.5-.5*std::cos(2*pi*k/2047));fft(buf);for(int k=0;k<=1024;k++){double hz=k*44100./8192;int band=hz<150?0:hz<500?1:hz<2000?2:3;if(hz>=20&&hz<5500){timbre[band]+=std::norm(buf[k]);bandCounts[band]++;}}}
        for(int band=0;band<4;band++)timbre[band]/=std::max(1.,bandCounts[band]);norm=std::accumulate(timbre.begin(),timbre.end(),0.);for(auto&v:timbre)v=std::sqrt(v/(norm+1e-12));c["timbre"]=timbre;
        Frame step=std::llround(period*44100/8);std::array<double,8> rhythm{},counts{};double previous=0;int j=0;for(Frame pos=first;pos<last;pos+=step,j++){double v=level(original,pos,std::min(last,pos+step));if(j)rhythm[j%8]+=std::max(0.,v-previous);counts[j%8]++;previous=v;}norm=0;for(int k=0;k<8;k++){rhythm[k]/=std::max(1.,counts[k]);norm+=rhythm[k]*rhythm[k];}for(auto&v:rhythm)v/=std::sqrt(norm)+1e-12;c["rhythm"]=rhythm;c["descriptor_version"]="local-rhythm-timbre-transient-v5";
    }
    a["context_version"]="local-rhythm-timbre-transient-v5";
}
}
