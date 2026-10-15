#include "models.hpp"
#include "checksum.hpp"
#include "../structure.hpp"
#include <set>
namespace ladio::mobile {
static constexpr const char*names[]={"vocals","drums","bass","other"};
static double energy(const std::vector<float>&v,int first,int last){last=std::min(last,int(v.size()/2));double sum=0;for(int i=std::max(0,first)*2;i<last*2;i++)sum+=double(v[i])*v[i];return std::sqrt(sum/std::max(1,(last-first)*2)+1e-12);}
void observeWindow(Json&a,const std::array<std::vector<float>,4>&v,Frame first,Frame last,Json&m){
    const double tuning=a.value("tuning",0.);
    PCMFile original(a.at("pcm_path"));int length=int(last-first);double lo=double(first)/44100,hi=double(last)/44100;
    Json curves=Json::array();for(int start=0;start<length;start+=8820){double sq=0;int n=std::min(8820,length-start);for(int i=0;i<n;i++)for(int ch=0;ch<2;ch++){double x=original.sample(first+start+i,ch);sq+=x*x;}double mix=std::sqrt(sq/(n*2)+1e-12),voice=energy(v[0],start,start+n);Json row={{"time",lo+double(start)/44100},{"vocal",clamp((20*std::log10(voice/(mix+1e-8))+30)/24)*(voice>.004)}};for(int r=0;r<4;r++)row[std::string(names[r])+"_rms"]=energy(v[r],start,start+n);curves.push_back(row);}
    m["curves"]=curves;for(const auto&r:curves){int index=int(std::nearbyint(r["time"].get<double>()/.2));if(index<int(a["curves"].size()))for(auto&[k,value]:r.items())if(k!="time")a["curves"][index][k]=value;}
    // Acoustic phonation is the same 20 ms + median3 + closing7 contract.
    int count=length/882;std::vector<double> prob(count);std::vector<bool> active(count),dilate(count),closed(count);
    for(int i=0;i<count;i++){double voice=energy(v[0],i*882,(i+1)*882),sq=0;for(int k=0;k<882;k++)for(int c=0;c<2;c++){double x=original.sample(first+i*882+k,c);sq+=x*x;}double mix=std::sqrt(sq/1764+1e-12);prob[i]=clamp((20*std::log10(voice/(mix+1e-8))+30)/24)*(voice>.004);}
    for(int i=0;i<count;i++)active[i]=median({prob[std::max(0,i-1)],prob[i],prob[std::min(count-1,i+1)]})>.38;
    for(int i=0;i<count;i++)for(int k=-3;k<=3;k++)dilate[i]=dilate[i]||active[std::clamp(i+k,0,count-1)];
    for(int i=0;i<count;i++){closed[i]=true;for(int k=-3;k<=3;k++)closed[i]=closed[i]&&dilate[std::clamp(i+k,0,count-1)];}
    Json phonation=Json::array();for(int i=0;i<count;){if(!closed[i]){i++;continue;}int start=i;while(i<count&&closed[i])i++;if(i-start>=6)phonation.push_back({{"start",lo+start*.02},{"end",lo+i*.02},{"confidence",.8},{"source","stem-phonation-20ms-v1"},{"open_left",start==0},{"open_right",i==count}});}
    m["phonation"]=phonation;m["vocal_boundary_version"]="stem-phonation-20ms-v1";Json intervals=Json::array();
    for(auto r:a.value("vocal_intervals",Json::array())){double s=r["start"],e=r["end"];if(e<=lo||s>=hi)intervals.push_back(r);else{if(s<lo){auto p=r;p["end"]=lo;p["open_right"]=true;intervals.push_back(p);}if(e>hi){r["start"]=hi;r["open_left"]=true;intervals.push_back(r);}}}
    for(auto r:phonation)intervals.push_back(r);std::sort(intervals.begin(),intervals.end(),[](const Json&x,const Json&y){return x["start"]<y["start"];});a["vocal_intervals"]=intervals;
    Json merged=Json::array();for(auto r:intervals){if(!merged.empty()&&r["start"].get<double>()<=merged.back()["end"].get<double>()+.02){auto&p=merged.back();if(r["end"]>p["end"]){p["end"]=r["end"];p["open_right"]=r.value("open_right",false);}p["confidence"]=std::min(p["confidence"].get<double>(),r["confidence"].get<double>());}else merged.push_back(r);}a["protected_vocal_phrases"]=merged;a["vocal_boundary_version"]="stem-phonation-20ms-v1";a["vocal_protection_version"]="phonation+credible-words-v2";
    Json profiles=Json::array();constexpr int fftsize=8192;std::vector<std::complex<double>> buf(fftsize);std::array<std::vector<double>,4> mono;for(auto&x:mono)x.resize(fftsize);
    for(int start=0;start<length;start+=22050){int end=std::min(length,start+22050),context=std::min(32768,length-start);Json roles=Json::object();
        for(int role=0;role<4;role++){auto&y=mono[role];std::fill(y.begin(),y.end(),0);for(int i=0;i<context;i++)y[i/4]+=(v[role][(start+i)*2]+v[role][(start+i)*2+1])*.125;double rms=energy(v[role],start,end);Json f={{"rms",rms}};
            if(role!=1){for(int k=0;k<fftsize;k++)buf[k]=y[k]*(.5-.5*std::cos(2*pi*k/(fftsize-1)));fft(buf);std::vector<double> power(fftsize/2+1);for(int k=0;k<=fftsize/2;k++)power[k]=std::norm(buf[k]);double low=role==2?25:75,high=role==2?650:3500,max=0,total=0,concentration=0;for(int k=1;k<fftsize/2;k++){double hz=k*44100./32768;if(hz>=low&&hz<=high){max=std::max(max,power[k]);total+=power[k];}}
                std::array<double,12> chroma{};for(int k=1;k<fftsize/2;k++){double hz=k*44100./32768;if(hz<low||hz>high||power[k]<=power[k-1]||power[k]<=power[k+1]||power[k]<=max*.025)continue;double prev=std::log(power[k-1]+1e-18),now=std::log(power[k]+1e-18),next=std::log(power[k+1]+1e-18),delta=.5*(prev-next)/(prev-2*now+next+1e-18);hz=(k+std::clamp(delta,-.5,.5))*44100/32768.;double note=69+12*std::log2(hz/440)-tuning;int nearest=int(std::nearbyint(note));chroma[(nearest%12+12)%12]+=std::sqrt(power[k])*std::exp(-std::pow((note-nearest)/.35,2));concentration+=power[k];}
                double norm=0;for(double x:chroma)norm+=x*x;for(double&x:chroma)x/=std::sqrt(norm)+1e-12;f["chroma"]=chroma;f["tonality"]=rms>.003?clamp(concentration/std::max(1e-18,total)*context/32768.):0;
            }roles[names[role]]=f;
        }
        double ve=std::inner_product(mono[0].begin(),mono[0].end(),mono[0].begin(),0.);for(int role=1;role<4;role++){double y=std::inner_product(mono[role].begin(),mono[role].end(),mono[role].begin(),0.),dot=std::inner_product(mono[0].begin(),mono[0].end(),mono[role].begin(),0.);roles[names[role]]["vocal_correlation"]=roles["vocals"]["rms"].get<double>()>.006?clamp(std::abs(dot)/std::sqrt(std::max(1e-18,ve*y))):0;}
        profiles.push_back({{"start",lo+double(start)/44100},{"end",lo+double(end)/44100},{"roles",roles}});
    }
    m["role_profiles"]=profiles;Json rows=Json::array();for(auto row:a.value("role_profiles",Json::array())){double s=row["start"],e=row["end"];if(e<=lo||s>=hi)rows.push_back(row);else{if(s<lo){auto p=row;p["end"]=lo;rows.push_back(p);}if(e>hi){row["start"]=hi;rows.push_back(row);}}}for(auto r:profiles)rows.push_back(r);std::sort(rows.begin(),rows.end(),[](const Json&x,const Json&y){return x["start"]<y["start"];});a["role_profiles"]=rows;a["role_profile_version"]="stem-material-v2";m["role_profile_version"]="stem-material-v2";m["tuning_semitones"]=tuning;
    for(auto&c:a["cues"]){double t=c["time"];if(t<lo||t>=hi)continue;int index=int(std::nearbyint(t/.2));if(index<int(a["curves"].size())){c["vocal"]=a["curves"][index]["vocal"];double sum=0;int n=0;for(int k=index;k<std::min(index+15,int(a["curves"].size()));k++){sum+=a["curves"][k].value("vocal",.5);n++;}c["vocal_after"]=sum/std::max(1,n);}}
}
Json prepareResources(Json library,const Json&plan,const std::filesystem::path&root,Models&models,const std::function<bool()>&cancel){
    struct Request{int track;double lo,hi;std::set<std::string>roles;};std::vector<Request> requests;
    for(const auto&r:plan.at("stem_requests")){Request q{r.at("track"),r.at("start"),r.at("end"),{}};for(const auto&role:r.at("roles"))q.roles.insert(role.get<std::string>());if(!q.roles.empty())requests.push_back(q);}
    std::sort(requests.begin(),requests.end(),[](auto&a,auto&b){return a.track==b.track?a.lo<b.lo:a.track<b.track;});std::vector<Request> merged;
    for(auto q:requests){if(!merged.empty()&&q.track==merged.back().track&&q.lo<=merged.back().hi){merged.back().hi=std::max(merged.back().hi,q.hi);merged.back().roles.insert(q.roles.begin(),q.roles.end());}else merged.push_back(q);}
    // Subtract verified coverage per role before choosing model windows. A
    // newly exposed two-second edge must not recompute forty seconds of audio.
    std::vector<Request> gaps;
    for(const auto&q:merged){const auto&a=library["tracks"][q.track];double lo=std::max(0.,q.lo),hi=std::min(q.hi,a["duration"].get<double>());std::set<double> edges{lo,hi};
        for(const auto&w:a["stem_windows"]){double left=std::max(lo,w["valid_range"][0].get<double>()/44100),right=std::min(hi,w["valid_range"][1].get<double>()/44100);if(right>left){edges.insert(left);edges.insert(right);}}
        std::vector<double> points(edges.begin(),edges.end());for(size_t i=1;i<points.size();i++){
            auto roles=q.roles;for(const auto&w:a["stem_windows"])if(w["valid_range"][0].get<double>()/44100<=points[i-1]+1e-8&&w["valid_range"][1].get<double>()/44100>=points[i]-1e-8)for(auto&[name,path]:w["stem_paths"].items())roles.erase(name);
            if(roles.empty())continue;
            if(!gaps.empty()&&gaps.back().track==q.track&&gaps.back().hi==points[i-1]&&gaps.back().roles==roles)gaps.back().hi=points[i];else gaps.push_back({q.track,points[i-1],points[i],roles});
        }
    }
    merged=std::move(gaps);
    bool worked=false;
    for(auto q:merged){auto&a=library["tracks"][q.track];PCMFile original(a.at("pcm_path"));
        for(double cursor=q.lo;cursor<q.hi;){double end=std::min(q.hi,cursor+44);check(cancel);bool covered=true;
            for(const auto&role:q.roles){std::vector<std::pair<double,double>> ranges;for(auto&w:a["stem_windows"])if(w.value("state","")=="ready"&&w.at("stem_paths").contains(role)){auto file=w["stem_paths"][role].get<std::string>();if(std::filesystem::exists(file)&&checksumFile(file)==w["fnv1a64"][role].get<std::string>())ranges.push_back({w["valid_range"][0].get<double>()/44100,w["valid_range"][1].get<double>()/44100});}std::sort(ranges.begin(),ranges.end());double reach=std::max(0.,cursor);for(auto r:ranges){if(r.first>reach+1./44100)break;reach=std::max(reach,r.second);}covered&=reach>=std::min(end,a["duration"].get<double>())-1./44100;}
            if(!covered){
                Frame first=std::max<Frame>(0,std::llround(std::floor(cursor/4)*4*44100)),last=std::min(original.frames(),std::max(first+12*44100,Frame(std::llround(std::ceil(end/4)*4*44100))));if(last-first>48*44100)first=std::max<Frame>(0,last-48*44100);
                if(first>cursor*44100+1)throw std::runtime_error("Window would miss requested start");
                auto directory=root/(a["id"].get<std::string>()+"-"+std::to_string(first)+"-"+std::to_string(last));for(auto role:q.roles)directory+=std::string("-")+role;std::filesystem::create_directories(directory);
                Frame left=std::max<Frame>(0,first-88200),right=std::min(original.frames(),last+88200);auto estimates=models.separate(original,left,right,cancel);
                for(auto&v:estimates){std::vector<float> trimmed(v.begin()+(first-left)*2,v.begin()+(last-left)*2);v=std::move(trimmed);}
                Json m={{"state","ready"},{"source_hash",a["source_hash"]},{"sample_rate",44100},{"frames",last-first},{"channels",2},{"latency_frames",0},{"gain_scale",1.},{"valid_range",{first,last}},{"model_version","htdemucs-955717e8-onnx-fp32-shift42-v1"},{"stem_paths",Json::object()},{"fnv1a64",Json::object()},{"qc",{{"finite",true},{"reconstruction_rms",0.}}}};
                double error=0;for(Frame i=0;i<last-first;i++)for(int c=0;c<2;c++){double sum=0;for(auto&v:estimates)sum+=v[i*2+c];error+=std::pow(sum-original.sample(first+i,c),2);}m["qc"]["reconstruction_rms"]=std::sqrt(error/((last-first)*2));if(m["qc"]["reconstruction_rms"].get<double>()>=1e-5)throw std::runtime_error("Stem reconstruction failed");
                observeWindow(a,estimates,first,last,m);
                for(int role=0;role<4;role++)if(q.roles.count(names[role])){auto file=directory/(std::string(names[role])+".f32");writeFloats(file,estimates[role]);m["stem_paths"][names[role]]=file.string();m["fnv1a64"][names[role]]=checksumFile(file);}
                m["separation_contract"]={{"requested_roles",q.roles},{"model_output_roles",{"vocals","drums","bass","other"}},{"remainder","original minus selected components"}};
                // Replace only the same component files. Other sparse windows
                // on the same interval can still own a different required role.
                auto&windows=a["stem_windows"];windows.erase(std::remove_if(windows.begin(),windows.end(),[&](const Json&w){return w.at("stem_paths")==m["stem_paths"];}),windows.end());windows.push_back(m);writeJson(directory/"window.json",m);worked=true;
                if(library.contains("analysis_paths"))writeJson(library["analysis_paths"][q.track].get<std::string>(),a);
            }
            if(end>=q.hi)break;cursor+=42;
        }
    }
    if(worked)for(auto&a:library["tracks"]){analyzeStructure(a);descriptors(a);}
    library["resource_worked"]=worked;return library;
}
}
