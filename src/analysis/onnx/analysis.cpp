#include "models.hpp"
#include "../structure.hpp"
#include "checksum.hpp"
#include "measured_grid.hpp"
#include <chrono>
namespace ladio::mobile {
static double field(const Json&curves,int begin,int end,const char*name){double sum=0;int n=0;for(int k=std::max(0,begin);k<std::min(int(curves.size()),end);k++){sum+=curves[k].value(name,0.);n++;}return n?sum/n:0;}
Json analyze(const std::string&path,const std::string&id,Models&models,const std::function<bool()>&cancel,const std::string&scope,const Json&priorAnalysis){
    auto begun=std::chrono::steady_clock::now();Json timings=Json::object();
    auto mark=[&](const char*stage){auto now=std::chrono::steady_clock::now();timings[stage]=std::chrono::duration<double>(now-begun).count();begun=now;};
    PCMFile pcm(path);if(pcm.frames()<44100*20||pcm.frames()>44100*360)throw std::runtime_error("Analysis supports 20 seconds to 6 minutes");
    const double duration=double(pcm.frames())/44100;check(cancel);auto pcmHash=checksumFile(path);
    std::vector<std::string> scopes{scope};if(scope!="full"&&priorAnalysis.value("pcm_fnv1a64",std::string{})==pcmHash&&priorAnalysis.value("beat_model_sha256",std::string{})==models.beatIdentity()){for(auto s:priorAnalysis.value("grid_scopes",std::vector<std::string>{"full"}))if(std::find(scopes.begin(),scopes.end(),s)==scopes.end())scopes.push_back(s);}
    auto ranges=gridRanges(duration,scopes);
    models.report("resample");auto mono=mono22050(pcm);mark("resample_seconds");
    const bool featureHit=priorAnalysis.value("pcm_fnv1a64",std::string{})==pcmHash&&priorAnalysis.value("feature_version",std::string{})=="hpss-chroma-loudness-v2"&&priorAnalysis.contains("curves")&&priorAnalysis.contains("key")&&priorAnalysis.contains("tuning");
    Json curves=Json::array(),globalKey;double tuning=0;
    if(featureHit){curves=priorAnalysis.at("curves");globalKey=priorAnalysis.at("key");tuning=priorAnalysis.at("tuning");}
    else {
    constexpr int size=2048,hop=256,bins=1025,block=512;int nf=1+int(mono.size())/hop;
    std::vector<double> rms(nf),low(nf),onset(nf),henergy(nf),penergy(nf),allenergy(nf);std::vector<std::array<double,12>> chroma(nf);std::vector<std::complex<double>> buf(size);
    // Two sequential passes keep spectral memory bounded. The temporary file
    // is anonymous and closes on cancellation; no whole-song stems are saved.
    std::unique_ptr<FILE,decltype(&std::fclose)> spectral(std::tmpfile(),std::fclose);
    if(!spectral)throw std::runtime_error("Cannot allocate analysis scratch file");
    std::vector<std::pair<double,double>> pitches;double maxPercussive=0;
    std::array<float,bins*2> row{};
    std::array<double,size> window{};for(int k=0;k<size;k++)window[k]=.5-.5*std::cos(2*pi*k/size);
    for(int base=0;base<nf;base+=block){check(cancel);int count=std::min(block,nf-base),lo=std::max(0,base-15),hi=std::min(nf,base+count+15);std::vector<float> spectrum((hi-lo)*bins);
        models.report("structure",base,nf);
        for(int t=lo;t<hi;t++){for(int k=0;k<size;k++){int at=t*hop+k-size/2;buf[k]=(at>=0&&at<int(mono.size())?mono[at]:0)*window[k];}fft(buf);for(int k=0;k<bins;k++)spectrum[(t-lo)*bins+k]=float(std::abs(buf[k]));}
        std::vector<float> harmonic(count*bins);
        for(int k=0;k<bins;k++){
            auto sample=[&](int t){return spectrum[(symmetric(t,nf)-lo)*bins+k];};Median31 temporal;temporal.reset([&](int z){return sample(base+z);});
            for(int t=base;t<base+count;t++){harmonic[(t-base)*bins+k]=temporal.value();if(t+1<base+count)temporal.advance(sample(t-15),sample(t+16));}
        }
        for(int t=base;t<base+count;t++){
            double energy=0,maxH=0;
            auto sample=[&](int k){return spectrum[(t-lo)*bins+symmetric(k,bins)];};Median31 frequency;frequency.reset(sample);
            for(int k=0;k<bins;k++){
                double h=harmonic[(t-base)*bins+k],p=frequency.value(),m=spectrum[(t-lo)*bins+k];if(k+1<bins)frequency.advance(sample(k-15),sample(k+16));double weight=(h*h+p*p)>1e-30?h*h/(h*h+p*p):.5;h=m*weight;p=m*(1-weight);
                double power=m*m;energy+=power*(k==0||k==bins-1?1:2);allenergy[t]+=power;penergy[t]+=p*p;if(k>=12&&k<325)henergy[t]+=h*h;if(k>=1&&k<19)low[t]+=power;
                row[k]=float(h*h);row[bins+k]=float(20*std::log10(std::max(1e-5,p+1e-7)));maxPercussive=std::max(maxPercussive,p+1e-7);maxH=std::max(maxH,double(row[k]));
            }
            rms[t]=std::sqrt(energy/(size*size));
            // librosa estimate_tuning: local parabolic peaks above 10% per
            // frame, then the stronger half across the recording.
            for(int k=1;k<bins-1;k++){double hz=k*22050./size,prev=row[k-1],now=row[k],next=row[k+1];if(hz<150||hz>=4000||now<=maxH*.1||now<=prev||now<next)continue;double denominator=prev-2*now+next,delta=denominator==0?0:.5*(prev-next)/denominator;if(std::abs(delta)>1)delta=0;pitches.push_back({now+.25*(next-prev)*delta,(k+delta)*22050./size});}
            if(std::fwrite(row.data(),sizeof(float),row.size(),spectral.get())!=row.size())throw std::runtime_error("Analysis scratch write failed");
        }
    }
    mark("hpss_seconds");models.report("harmony");
    std::vector<double> magnitudes;for(auto p:pitches)magnitudes.push_back(p.first);double pitchThreshold=median(magnitudes);std::array<int,100> histogram{};
    for(auto p:pitches)if(p.first>=pitchThreshold){double residual=12*std::log2(p.second/(440./16));residual-=std::floor(residual);if(residual>=.5)residual-=1;int bin=std::clamp(int(std::floor((residual+.5)*100)),0,99);histogram[bin]++;}
    tuning=pitches.empty()?0:std::distance(histogram.begin(),std::max_element(histogram.begin(),histogram.end()))*.01-.5;
    std::array<std::array<float,12>,bins> bank{};std::vector<double> frequency(size);for(int k=1;k<size;k++)frequency[k]=12*std::log2((k*22050./size)/(440./16))-tuning;frequency[0]=frequency[1]-18;
    for(int k=0;k<bins;k++){double width=std::max(1.,frequency[k+1]-frequency[k]),norm=0;std::array<double,12>w{};for(int c=0;c<12;c++){double d=std::fmod(frequency[k]-c+126,12)-6;w[c]=std::exp(-.5*std::pow(2*d/width,2));norm+=w[c]*w[c];}double octave=std::exp(-.5*std::pow((frequency[k]/12-5)/2,2));for(int c=0;c<12;c++)bank[k][c]=float(w[(c+3)%12]/std::sqrt(norm)*octave);}
    std::rewind(spectral.get());std::array<double,bins> previous{};double floor=20*std::log10(std::max(1e-5,maxPercussive))-80;
    for(int t=0;t<nf;t++){if(t%512==0)check(cancel);if(std::fread(row.data(),sizeof(float),row.size(),spectral.get())!=row.size())throw std::runtime_error("Analysis scratch read failed");std::array<double,12> c{};double flux=0;for(int k=0;k<bins;k++){for(int z=0;z<12;z++)c[z]+=row[k]*bank[k][z];double db=std::max(floor,double(row[bins+k]));if(t)flux+=std::max(0.,db-previous[k]);previous[k]=db;}
        double norm=*std::max_element(c.begin(),c.end());if(norm>1e-30)for(auto&v:c)v/=norm;chroma[t]=c;if(t+4<nf)onset[t+4]=flux/bins;
    }
    spectral.reset();
    std::vector<double> smoothed(nf),novelty(nf),values;
    std::array<double,321> gaussian{};double gaussianWeight=0;for(int k=-160;k<=160;k++){gaussian[k+160]=std::exp(-.5*k*k/1600.);gaussianWeight+=gaussian[k+160];}
    std::vector<double> logRms(nf);for(int t=0;t<nf;t++)logRms[t]=std::log(rms[t]+1e-7);
    for(int t=0;t<nf;t++){double sum=0;for(int k=-160;k<=160;k++)sum+=gaussian[k+160]*logRms[symmetric(t+k,nf)];smoothed[t]=sum/gaussianWeight;novelty[t]=std::abs(smoothed[t]-(t?smoothed[t-1]:0));values.push_back(novelty[t]);}
    std::sort(values.begin(),values.end());double p99=std::max(1e-5,values[size_t((values.size()-1)*.99)]*(1-std::fmod((values.size()-1)*.99,1.))+values[size_t(std::ceil((values.size()-1)*.99))]*std::fmod((values.size()-1)*.99,1.));
    double square=0;for(int ci=0;ci<int(std::ceil(duration/.2));ci++){
        check(cancel);double sec=ci*.2;Frame first=std::llround(sec*44100),last=std::min(pcm.frames(),first+8820);double sq=0;for(Frame i=first;i<last;i++)for(int ch=0;ch<2;ch++){double v=pcm.sample(i,ch);sq+=v*v;}square+=sq;
        int f=int(sec*22050/hop),end=std::min(nf,f+17);std::array<double,12> c{};double l=0,o=0,n=0,h=0,p=0,all=0;
        for(int k=f;k<end;k++){l+=low[k];o+=onset[k];n+=novelty[k]/p99;h+=henergy[k];p+=penergy[k];all+=allenergy[k];double weight=rms[k]>.0003?rms[k]*std::sqrt(henergy[k]/std::max(1e-18,allenergy[k])):0;for(int z=0;z<12;z++)c[z]+=weight*chroma[k][z];}
        double norm=0;for(double v:c)norm+=v*v;for(double&v:c)v/=std::sqrt(norm)+1e-8;int length=std::max(1,end-f);
        const bool percussion=h<.025*all&&p>.92*all;
        curves.push_back({{"time",sec},{"rms",std::sqrt(sq/std::max<Frame>(1,(last-first)*2)+1e-14)},{"low_energy",l/length},{"onset",o/length},{"novelty",n/length},{"chroma",c},{"tonality",percussion?0.:clamp(h/std::max(1e-18,all))},{"vocal",percussion?0.:.5},{"percussive_only",percussion}});
    }
    std::array<double,12> global{};for(int k=0;k<nf;k++){double weight=rms[k]>.0003?rms[k]*std::sqrt(henergy[k]/std::max(1e-18,allenergy[k])):0;for(int z=0;z<12;z++)global[z]+=weight*chroma[k][z];}double gn=0;for(double v:global)gn+=v*v;for(double&v:global)v/=std::sqrt(gn)+1e-8;globalKey=keyInfo(global);
    }
    mark("descriptors_seconds");auto [beatlog,downlog]=models.grid(pcm,cancel,&mono,path+".grid-"+pcmHash,ranges);mark("grid_seconds");
    auto grid=measureGrid(beatlog,downlog);Json initialGrid=grid.diagnostic();bool expanded=false;
    if(!grid.reliable()&&!ranges.empty()){
        // One bounded context expansion; exact chunk logits and descriptors
        // already computed above are reused. Never lower model thresholds or
        // invent regular beat positions to get past a preparation error.
        auto wider=gridRanges(duration,scopes,true);
        if(wider!=ranges){check(cancel);models.report("grid_repair");ranges=std::move(wider);auto more=models.grid(pcm,cancel,&mono,path+".grid-"+pcmHash,ranges);beatlog=std::move(more.first);downlog=std::move(more.second);grid=measureGrid(beatlog,downlog);expanded=true;mark("grid_repair_seconds");}
    }
    if(!grid.reliable())throw std::runtime_error("Unreliable measured beat grid: "+Json{{"scope",scope},{"duration",duration},{"expanded",expanded},{"observed",grid.diagnostic()}}.dump());
    auto beats=grid.beats,downs=grid.downs;double bpm=grid.bpm,meterFit=grid.meterFit,rhythm=grid.rhythm,dc=grid.downConfidence;
    Json cues=Json::array();for(size_t i=0;i<downs.size();i++){double sec=downs[i];if(sec<.5||sec>duration-4)continue;int ci=std::min(int(curves.size()-1),int(std::nearbyint(sec/.2)));double novelty=0;for(int k=std::max(0,ci-8);k<std::min(int(curves.size()),ci+8);k++)novelty=std::max(novelty,curves[k]["novelty"].get<double>());if(i%4&&novelty<.78)continue;
        std::array<double,12> c{};for(int k=ci;k<std::min(int(curves.size()),ci+40);k++){double rms=curves[k]["rms"].get<double>(),weight=rms>.0003?rms*std::sqrt(curves[k].value("tonality",0.)):0;for(int z=0;z<12;z++)c[z]+=weight*curves[k]["chroma"][z].get<double>();}double norm=0;for(double v:c)norm+=v*v;for(double&v:c)v/=std::sqrt(norm)+1e-8;
        double before=field(curves,ci-10,ci,"rms"),after=field(curves,ci,ci+10,"rms");cues.push_back({{"time",sec},{"frame",std::llround(sec*44100)},{"bar",i},{"confidence",std::min(confidence({sec},downlog),dc)},{"novelty",std::min(1.,novelty)},{"energy",after},{"energy_jump",std::max(0.,after-before)},{"kind","phrase"},{"key",keyInfo(c)},{"vocal",.5},{"vocal_before",.5},{"vocal_after",.5}});
    }
    Json a={{"schema_version",1},{"id",id},{"title",id},{"source_hash",id},{"revision","independent-native-v1"},{"pcm_path",path},{"sample_rate",44100},{"frames",pcm.frames()},{"duration",duration},{"channels",2},{"curve_step",.2},{"curves",curves},{"cues",cues},{"tempo",bpm},{"tempo_confidence",rhythm},{"downbeat_confidence",dc},{"downbeat_method","Beat This, minimal postprocessor"},{"beats",beats},{"downbeats",downs},{"meter",4},{"meter_denominator",4},{"meter_confidence",meterFit},{"beat_unit","quarter"},{"tempo_hypotheses",Json::array({Json{{"bpm",bpm},{"score",rhythm},{"method","Beat This quarter events"}}})},{"loudness_lufs",-14},{"capabilities",{"pcm","energy","local_harmony","neural_downbeats"}},{"stem_windows",Json::array()},{"vocal_intervals",Json::array()},{"protected_vocal_phrases",Json::array()},{"words",Json::array()},{"analyzer_version","independent-native-v1"},{"refinement_version","independent-structure-v1"},{"beat_qc",{{"bar_meter_fit",meterFit},{"measured_from_audio",true},{"beat_count",beats.size()},{"downbeat_count",downs.size()}}}};
    a["beat_qc"]=grid.diagnostic();a["beat_qc"]["measured_from_audio"]=true;a["beat_qc"]["version"]="measured-support-v4";a["beat_qc"]["context_expanded"]=expanded;if(expanded)a["beat_qc"]["initial"]=initialGrid;
    if(!grid.rawDowns.empty())a["raw_downbeats"]=grid.rawDowns;
    a["key"]=globalKey;a["tuning"]=tuning;a["feature_version"]="hpss-chroma-loudness-v2";a["feature_cache_hit"]=featureHit;a["beat_model"]=models.beatFile();a["beat_model_sha256"]=models.beatIdentity();
    a["analyzer_version"]="independent-native-1-"+models.beatFile();a["downbeat_method"]=models.beatFile()+" minimal postprocessor";
    if(featureHit){a["loudness_lufs"]=priorAnalysis.at("loudness_lufs");a["loudness_method"]=priorAnalysis.at("loudness_method");}
    else {
    // Loudness is filled by the shared K-weighted integration below, never
    // inferred from a track name or an assumed reference mastering level.
    struct Biquad{double b0,b1,b2,a1,a2,z1=0,z2=0;double run(double x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}};
    std::array<Biquad,2> shelf{{{1.530841230050347,-2.650979995154729,1.169079079921587,-1.663655113256020,.712595428073225},{1.530841230050347,-2.650979995154729,1.169079079921587,-1.663655113256020,.712595428073225}}};
    std::array<Biquad,2> high{{{1,-2,1,-1.989169673629796,.989199035787039},{1,-2,1,-1.989169673629796,.989199035787039}}};
    std::vector<double> chunks;double sum=0;int n=0;for(Frame f=0;f<pcm.frames();f++){for(int c=0;c<2;c++){double y=high[c].run(shelf[c].run(pcm.sample(f,c)));sum+=y*y;}if(++n==4410){chunks.push_back(sum/n);sum=0;n=0;}}
    std::vector<double> blocks;for(size_t k=3;k<chunks.size();k++){double e=(chunks[k]+chunks[k-1]+chunks[k-2]+chunks[k-3])*.25;if(-.691+10*std::log10(e+1e-20)>-70)blocks.push_back(e);}double threshold=mean(blocks)*.1;blocks.erase(std::remove_if(blocks.begin(),blocks.end(),[&](double e){return e<threshold;}),blocks.end());a["loudness_lufs"]=blocks.empty()?-70:-.691+10*std::log10(mean(blocks));a["loudness_method"]="BS1770 gated K-weighted stereo 44.1k";
    }
    a["pcm_fnv1a64"]=pcmHash;a["source_hash"]=a["pcm_fnv1a64"];
    a["grid_scopes"]=scopes;a["grid_requested_ranges"]=ranges;Json coverage=Json::array();
    for(size_t k=0;k<beatlog.size();){if(beatlog[k]==-1000){k++;continue;}size_t first=k;while(k<beatlog.size()&&beatlog[k]!=-1000)k++;coverage.push_back({double(first)/50,std::min(duration,double(k)/50)});}a["grid_coverage"]=coverage;
    if(priorAnalysis.value("pcm_fnv1a64",std::string{})==pcmHash){
        // Expanding entry analysis into an exit must retain already measured
        // vocal protection and sparse material, rather than separate it again.
        for(auto name:{"stem_windows","vocal_intervals","protected_vocal_phrases","role_profiles","role_profile_version","vocal_boundary_version","vocal_protection_version"})if(priorAnalysis.contains(name))a[name]=priorAnalysis[name];
        const auto&old=priorAnalysis.value("curves",Json::array());
        for(size_t k=0;k<std::min(old.size(),a["curves"].size());k++)if(std::abs(old[k].value("time",-1.)-a["curves"][k]["time"].get<double>())<1e-8)
            for(auto name:{"vocal","vocals_rms","drums_rms","bass_rms","other_rms"})if(old[k].contains(name))a["curves"][k][name]=old[k][name];
    }
    mono.clear();mono.shrink_to_fit();
    models.musicalEvents(a,pcm,cancel,priorAnalysis);mark("musical_models_seconds");
    // Fresh and reused analyses must expose the same observed activity at
    // cues. Cache presence must not change which transition family wins.
    for(auto&cue:a["cues"]){int k=int(std::nearbyint(cue["time"].get<double>()/.2));cue["vocal"]=field(a["curves"],k,k+1,"vocal");cue["vocal_before"]=field(a["curves"],k-15,k,"vocal");cue["vocal_after"]=field(a["curves"],k,k+15,"vocal");}
    models.report("phrases");analyzeStructure(a);
    // Artificial window edges have no neighbouring peaks/bar context. Keep
    // them for duplicate repair, but never publish them as usable cue material.
    // Full-track endpoints retain the original postprocessing unchanged.
    Json safeCoverage=Json::array();const double guard=std::max(1.,480./bpm);
    for(const auto&range:coverage){double first=range[0],last=range[1];if(first>0)first+=guard;if(last<duration-.01)last-=guard;if(last>first)safeCoverage.push_back({first,last});}
    a["grid_coverage"]=safeCoverage;
    auto usable=[&](double time){for(const auto&range:safeCoverage)if(time>=range[0].get<double>()&&time<=range[1].get<double>())return true;return false;};
    if(!ranges.empty()){
        for(auto name:{"beats","downbeats","raw_downbeats"})if(a.contains(name)){Json safe=Json::array();for(const auto&time:a[name])if(usable(time.get<double>()))safe.push_back(time);a[name]=safe;}
        Json safe=Json::array();const auto measured=a["downbeats"].get<std::vector<double>>();
        for(auto cue:a["cues"])if(usable(cue.at("time").get<double>())){
            // Cropping the measured grid changes indices. Keep cue time and
            // bar identity consistent, otherwise every exit-only plan is
            // rejected and the phone unnecessarily analyzes the entire song.
            auto it=std::lower_bound(measured.begin(),measured.end(),cue.at("time").get<double>()-.001);
            if(it==measured.end()||std::abs(*it-cue.at("time").get<double>())>.001)continue;
            cue["bar"]=std::distance(measured.begin(),it);safe.push_back(cue);
        }a["cues"]=safe;
    }
    descriptors(a);mark("phrases_loudness_seconds");a["analysis_timings"]=timings;return a;
}
}
