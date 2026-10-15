#include "audio.hpp"
#include "checksum.hpp"
#include "waveform_join.hpp"
#include "signalsmith/signalsmith-stretch.h"
#include <chrono>
#include <iostream>
namespace ladio {
static constexpr int previewCriticVersion = 19;
// The measured transient may precede the musical downbeat. Preserve it at its
// actual mapped position; moving that source transient onto arrival would
// silently shift the attack even with a correct beat grid.
static double featuredAttackOutput(const SceneVoice&voice,double source,int sr){
    double lo=0,hi=double(voice.end-voice.start)/sr;
    for(int k=0;k<40;k++){double t=(lo+hi)*.5;if(voice.sourceStart+voice.map.sourceAt(t)<source)lo=t;else hi=t;}
    return (lo+hi)*.5;
}
struct LoopRead {
    Frame first=0,last=0,activate=0,rollStart=0,rollEnd=0,seam=1,ramp=1;std::array<Frame,4> periods{};double wet=0;bool roll=false,stereoPercussion=false;
    std::vector<std::pair<Frame,Frame>> repeatStages;
    LoopRead(const Json&j,int sr){if(j.empty())return;first=std::llround(j.at("start").get<double>()*sr);last=std::llround(j.at("end").get<double>()*sr);activate=std::llround(j.value("activate",j.at("start").get<double>())*sr);seam=std::max(1,sr/200);ramp=std::max(1,sr/50);roll=j.value("roll",false);stereoPercussion=j.value("stereo_percussion",false)||j.value("full_mix_repeat",false);if(roll){rollStart=std::llround(j.at("roll_start").get<double>()*sr);rollEnd=std::llround(j.at("roll_end").get<double>()*sr);wet=1-j.value("foundation_mix",.75);for(int k=0;k<4;k++)periods[k]=std::max<Frame>(1,std::llround(j.at("roll_periods")[k].get<double>()*sr));}for(const auto&k:j.value("repeat_stages",Json::array()))repeatStages.push_back({std::llround(k.at(0).get<double>()*sr),std::llround(k.at(1).get<double>()*sr)});}
};
struct InputBus {
    const Source &source;Frame start=0;const LoopRead&loop;const std::vector<int>*layout=nullptr;
    float value(Frame f,int channel)const {
        if(loop.last<=loop.first||f<loop.activate)return source.value(f,channel);
        Frame n=loop.last-loop.first;
        auto at=[&](Frame pos,int ch){Frame x=((pos-loop.first)%n+n)%n;float sample=source.value(loop.first+x,ch);Frame remaining=n-x;if(remaining<loop.seam)sample+=float(smooth(1-double(remaining)/loop.seam))*(source.value(loop.first-remaining,ch)-sample);return sample;};
        float raw=at(f,channel);
        if(!loop.repeatStages.empty()&&f>=loop.repeatStages.front().first){
            auto play=[&](size_t k){auto [start,period]=loop.repeatStages[k];Frame offset=(f-start)%period;float x=at(loop.first+offset,channel);Frame remaining=period-offset;
                if(remaining<loop.seam)x+=float(smooth(1-double(remaining)/loop.seam))*(at(loop.first-remaining,channel)-x);return x;};
            size_t k=0;while(k+1<loop.repeatStages.size()&&f>=loop.repeatStages[k+1].first)++k;
            double u=smooth(double(f-loop.repeatStages[k].first)/loop.ramp);
            return float((k?play(k-1):raw)*(1-u)+play(k)*u);
        }
        if(!loop.roll||(channel!=0&&channel!=1&&channel!=4&&channel!=5)||f<loop.rollStart||f>=loop.rollEnd||loop.rollEnd<=loop.rollStart)return raw;
        double u=double(f-loop.rollStart)/(loop.rollEnd-loop.rollStart);int stage=u<.5?0:u<.75?1:u<.875?2:3;constexpr double phase[]={0.,.5,.75,.875};
        Frame boundary=loop.rollStart+std::llround(phase[stage]*(loop.rollEnd-loop.rollStart));
        Frame roll=loop.first+(f-boundary)%loop.periods[stage];int drum=loop.stereoPercussion?channel%2:4+channel%2;
        double wet=loop.wet*smooth(double(f-loop.rollStart)/loop.ramp);
        return raw+float(wet)*(at(roll,drum)-at(f,drum));
    }
    struct Channel {const InputBus &bus;int channel;float operator[](int index)const{return bus.value(bus.start+index,channel);}};
    Channel operator[](int channel)const{return {*this,layout?layout->at(channel):channel};}
};
struct OutputBus {
    float *samples;int stride=10;
    struct Channel {float *samples;int stride;float &operator[](int i){return samples[size_t(i)*stride];}};
    Channel operator[](int channel){return {samples+channel,stride};}
};
static void prepareBus(const Source&s,double sourceStart,const TimeMap&map,double seconds,int sr,const std::filesystem::path&path,bool stems,const std::function<bool()>&cancel,Frame endSource=-1,int channels=10,double attackOutput=-1,double attackSource=-1,const Json&sourceLoop=Json{},double nativeJoin=-1,const std::vector<int>&layout={}){
    Source chosen=s;
    if(stems&&!s.stems[0]){
        chosen.windows.clear();Frame from=std::llround((sourceStart-.7)*sr),to=std::llround((sourceStart+map.sourceAt(seconds)+.5)*sr);
        if(!sourceLoop.empty()){from=std::llround((sourceLoop.at("start").get<double>()-.7)*sr);to=std::llround((sourceLoop.at("end").get<double>()+.7)*sr);}
        if(channels==12||!layout.empty()){chosen.windows=s.windows;}
        else{for(auto it=s.windows.rbegin();it!=s.windows.rend();++it)if(it->start<=std::max<Frame>(0,from)&&it->end>=to){chosen.windows.push_back(*it);break;}if(chosen.windows.empty())throw std::runtime_error("No coherent stem window for prepared bus");}
    }
    Frame n=std::llround(seconds*sr);if(n<=0)n=1;auto tmp=path;tmp+=".pending";std::ofstream file(tmp,std::ios::binary);if(!file)throw std::runtime_error("Cannot prepare deck buffer");
    const double nativeBlend=nativeJoin>=0?.10:.35;
    constexpr int chunk=256;std::array<float,chunk*12> output{};
    bool warp=abs(map.steadyRate-1)>1e-9||abs(map.initialRate-1)>1e-9||abs(map.rateAt(seconds)-1)>1e-9||std::any_of(map.phaseCorrections.begin(),map.phaseCorrections.end(),[](const auto&p){return abs(p.second)>1e-8;});
    const double join=nativeJoin>=0?nativeJoin:seconds-.08;
    const bool canRejoin=join>=0&&std::abs(map.rateAt(join)-1)<=.001&&map.rateAt(seconds)==1;
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    Frame position=0,readIndex=0;double context=warp?.5:0;
    Frame start=std::llround((sourceStart+map.sourceAt(-context))*sr);
    LoopRead loopRead(sourceLoop,sr);InputBus reader{chosen,0,loopRead};
    if(warp){stretch.presetDefault(channels,sr);int prime=stretch.outputSeekLength(float(map.rateAt(-context)));InputBus in{chosen,start,loopRead,layout.empty()?nullptr:&layout};stretch.outputSeek(in,prime);readIndex=prime;}
    Frame discard=std::llround(context*sr),total=n+discard;
    while(position<total){int count=int(std::min<Frame>(chunk,total-position));
        if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");
        if(warp){double outTime=double(position+count+stretch.outputLatency())/sr-context;
            Frame target=std::llround((map.sourceAt(outTime)-map.sourceAt(-context))*sr)+stretch.inputLatency();
            int take=int(std::max<Frame>(0,target-readIndex));InputBus in{chosen,start+readIndex,loopRead,layout.empty()?nullptr:&layout};OutputBus out{output.data(),channels};stretch.process(in,take,out,count);readIndex+=take;
        }else{for(int f=0;f<count;f++)for(int c=0;c<channels;c++)output[f*channels+c]=reader.value(start+position+f,layout.empty()?c:layout[c]);}
        // Keep the head sample-exact. The return to native B is patched in
        // one bounded, covariance-aware window after rendering, not a second
        // linear fade between differently phased versions of the same music.
        if(warp)for(int f=0;f<count;f++){
            Frame index=position+f-discard;if(index<0||index>=n)continue;
            double time=double(index)/sr;
            double first=abs(map.rateAt(0)-1)<1e-9?1-smooth(double(index)/(sr*.035)):0;
            bool nativeTail=canRejoin&&time>=join+nativeBlend&&std::abs(map.rateAt(time)-1)<=.001;
            if(first<=0&&!nativeTail)continue;
            Frame raw=nativeTail?std::llround((sourceStart+map.sourceAt(time))*sr):std::llround(sourceStart*sr)+index;
            double wet=nativeTail?1:first;
            for(int c=0;c<channels;c++){float original=reader.value(raw,layout.empty()?c:layout[c]);if(nativeTail)output[f*channels+c]=original;else output[f*channels+c]+=float(wet)*(original-output[f*channels+c]);}
        }
        // The first featured attack is phase-preserved on every lane. The
        // taper is outside its first 16 ms, so the mix cannot eat the kick.
        if(warp&&attackOutput>=0&&attackSource>=0)for(int f=0;f<count;f++){
            Frame index=position+f-discard;double dt=double(index)/sr-attackOutput;
            double weight=1-smooth((abs(dt)-.016)/.024);if(weight<=0)continue;
            Frame raw=std::llround(attackSource*sr)+index-std::llround(attackOutput*sr);
            for(int c=0;c<channels;c++)output[f*channels+c]+=float(weight)*(reader.value(raw,layout.empty()?c:layout[c])-output[f*channels+c]);
        }
        // A prepared neutral route sums exactly to its own stretched original.
        if(layout.empty())for(int f=0;f<count;f++)for(int c=0;c<2;c++){
            if(stems&&channels==10)output[f*channels+8+c]=output[f*channels+c]-output[f*channels+2+c]-output[f*channels+4+c]-output[f*channels+6+c];
            else if(stems)output[f*channels+10+c]=output[f*channels+c]-output[f*channels+2+c]-output[f*channels+4+c]-output[f*channels+6+c]-output[f*channels+8+c];
            else for(int k=2;k<channels;k++)output[f*channels+k]=0;
        }
        int first=int(std::max<Frame>(0,discard-position));if(first<count)file.write(reinterpret_cast<char*>(output.data()+first*channels),(count-first)*channels*sizeof(float));position+=count;
    }
    if(!file)throw std::runtime_error("Prepared PCM write failed");file.close();
    if(warp){
        Frame first=std::llround(join*sr),length=std::min<Frame>(std::llround(nativeBlend*sr),n-first);
        if(canRejoin)rejoinNative(tmp,channels,sr,n,first,length,[&](Frame f,int c){Frame raw=std::llround((sourceStart+map.sourceAt(double(f)/sr))*sr);return reader.value(raw,layout.empty()?c:layout[c]);});
    }
    std::filesystem::rename(tmp,path);
}
void prepareOriginalVoice(const Source&s,double start,const TimeMap&map,double seconds,int sr,const std::filesystem::path&path,const std::function<bool()>&cancel,double attackOutput,double attackSource,double nativeJoin){
    prepareBus(s,start,map,seconds,sr,path,false,cancel,std::llround((start+map.sourceAt(seconds))*sr),2,attackOutput,attackSource,Json{},nativeJoin);
}
static Source sourceFromTrack(const Track&t,const std::filesystem::path&base){
    Source s;s.original=std::make_shared<PCMFile>(resolve(base,t.pcm));if(s.original->frames()!=t.frames)throw std::runtime_error("Canonical PCM changed");
    const char*names[]={"vocals","drums","bass","other"};
    if(t.stems&&t.source.contains("stems")&&t.source["stems"].value("state","")=="ready"){auto&m=t.source.at("stems");for(int i=0;i<4;i++){s.stems[i]=std::make_shared<PCMFile>(resolve(base,m.at("stem_paths").at(names[i])));if(s.stems[i]->frames()!=t.frames)throw std::runtime_error("Stem timeline mismatch");}}
    for(auto&w:t.source.value("stem_windows",Json::array()))if(w.value("state","")=="ready"&&w.value("source_hash","")==t.hash&&w.value("sample_rate",0)==t.sampleRate&&w.value("qc",Json{}).value("finite",false)&&w.value("qc",Json{}).value("reconstruction_rms",1.)<1e-5){
        Source::Window window;window.start=w.at("valid_range")[0];window.end=w.at("valid_range")[1];
        if(window.start<0||window.end>t.frames||window.end<=window.start)throw std::runtime_error("Invalid stem window timeline");
        for(int i=0;i<4;i++){if(!w.at("stem_paths").contains(names[i]))continue;auto file=resolve(base,w.at("stem_paths").at(names[i]));window.stems[i]=std::make_shared<PCMFile>(file);if(window.stems[i]->frames()!=window.end-window.start)throw std::runtime_error("Stem window frame mismatch");if(w.contains("fnv1a64")&&checksumFile(file)!=w.at("fnv1a64").at(names[i]).get<std::string>())throw std::runtime_error("Stem window checksum mismatch");}
        s.windows.push_back(std::move(window));
    }
    std::sort(s.windows.begin(),s.windows.end(),[](auto&a,auto&b){return a.start<b.start;});return s;
}
static Json makeSpan(Frame start,Frame count,int track,Frame source,int transition=-1){return {{"start_frame",start},{"frames",count},{"track",track},{"source_frame",source},{"transition",transition}};}
static Json previewScene(const Compiled&compiled,const std::vector<Track>&tracks,const std::vector<Source>&sources,int sr,const std::function<bool()>&cancel,const std::filesystem::path&capture={}){
    auto s=std::make_shared<Session>();s->sr=sr;s->frames=compiled.plan.frames();s->tracks=tracks;s->sources=sources;s->transitions={compiled};s->spans={{0,s->frames,0,compiled.plan.a,0}};
    Engine engine(s);std::array<float,512>l{},r{};std::array<float,3072>buses{};std::array<float,10240>roles{};
    std::ofstream saved;std::filesystem::path temporary;if(!capture.empty()){temporary=capture;temporary+=".pending";saved.open(temporary,std::ios::binary);if(!saved)throw std::runtime_error("Cannot capture accepted scene");}
    std::array<float,1024>interleaved{};
    const auto&scene=*compiled.scene;const auto&anchors=scene.contract.at("anchors");
    Frame hybrid=anchors.at("hybrid_start").at("output_frame"),hybridEnd=anchors.at(scene.family=="vocal_relay"?"A_vocal_release":"foundation_takeover").at("output_frame");
    double aEnergy=0,bEnergy=0,drumEnergy=0,lateA=0;Frame hybridN=0;bool finite=true;double peak=0;double error=0;
    const double barFrames=sr*MusicalTransport::quartersPerBar(tracks[compiled.plan.a].meter,tracks[compiled.plan.a].meterDenominator)*60/scene.bpm;
    struct BarLevel {double a=0,b=0,d=0;Frame samples=0,expected=0,end=0;};
    const double quartersPerBar=MusicalTransport::quartersPerBar(tracks[compiled.plan.a].meter,tracks[compiled.plan.a].meterDenominator);
    const double hybridQuarter=scene.transport.quarterAt(hybrid);
    std::vector<BarLevel>barLevels;
    for(double q=hybridQuarter;q<scene.transport.quarterAt(hybridEnd)-1e-7;q+=quartersPerBar){Frame from=scene.transport.frameAt(q),to=scene.transport.frameAt(q+quartersPerBar);barLevels.push_back({0,0,0,0,to-from,to});}
    size_t hybridBar=0;
    double arrivalB=0,arrivalFoundation=0,arrivalVocal=0,expectedVocal=0,openedVocal=0,quietEnergy=0;Frame arrivalSamples=0,quietSamples=0,quietRun=0,maxQuiet=0;
    size_t mainB=0,mainA=0,heldA=scene.voices.size();for(size_t k=0;k<scene.voices.size();k++){if(scene.voices[k].id=="B.main")mainB=k;if(scene.voices[k].id=="A.main")mainA=k;if(scene.voices[k].id=="A.loop")heldA=k;}
    const auto&incoming=scene.voices[mainB];const auto&incomingPack=*compiled.voices[mainB];
    Frame arrivalEnd=std::min(s->frames,scene.arrival+Frame(barFrames*.5)),quietHop=std::max(1,sr/100);
    double originalVoiceEnergy=0,outgoingVoiceEnergy=0,incomingVoiceEnergy=0,originalDrumEnergy=0,foundationEnergy=0;
    double originalLowEnergy=0,foundationLowEnergy=0,expectedDrumEnergy=0;OnePole originalLow,foundationLow,originalLow2,foundationLow2,expectedDrumLow,expectedDrumLow2;
    const float lowAlpha=float(1-std::exp(-2*pi*180/sr));
    const bool originalA=scene.voices[mainA].originalOnly,originalB=incoming.originalOnly;
    const bool vocalBed=scene.contract.value("material_mode",std::string())=="vocal_plus_bed";
    const bool combinedFoundation=originalA||originalB||vocalBed;
    const int foundationChannelA=originalA?0:vocalBed?10:4,foundationChannelB=originalB?0:vocalBed?10:4;
    auto foundationAt=[&](const float*roles,int k,int ch){return roles[k*20+2+ch]+roles[k*20+12+ch]+((originalA||vocalBed)?roles[k*20+8+ch]:0)+((originalB||vocalBed)?roles[k*20+18+ch]:0);};
    const bool spectral=combinedFoundation||scene.contract.value("variant",std::string())=="spectral_exchange";
    Frame continuityN=0,blockedVoice=0,blockedFoundation=0,fullBandAttenuation=0;
    Json foundationFailures=Json::array();
    const bool continuity=scene.contract.contains("musical_continuity");
    const Frame incomingLead=anchors.at("B_lead_entry").at("output_frame"),foundationTakeover=anchors.at("foundation_takeover").at("output_frame");
    for(Frame pos=0;pos<s->frames;){if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");uint32_t n=uint32_t(std::min<Frame>(512,s->frames-pos));engine.render(l.data(),r.data(),n,buses.data(),roles.data());
        if(!capture.empty()){for(uint32_t k=0;k<n;k++){interleaved[k*2]=l[k];interleaved[k*2+1]=r[k];}saved.write(reinterpret_cast<char*>(interleaved.data()),n*8);}
        for(uint32_t k=0;k<n;k++)for(int ch=0;ch<2;ch++){
            Frame f=pos+k;double out=ch?r[k]:l[k];finite&=std::isfinite(out);peak=std::max(peak,abs(out));double sum=0;for(int deck=0;deck<3;deck++)sum+=buses[k*6+deck*2+ch];error=std::max(error,abs(sum-out));
            if(f>=hybrid&&f<hybridEnd){double a=buses[k*6+ch],b=buses[k*6+2+ch],d=foundationAt(roles.data(),k,ch);aEnergy+=a*a;bEnergy+=b*b;drumEnergy+=d*d;hybridN++;while(hybridBar+1<barLevels.size()&&f>=barLevels[hybridBar].end)++hybridBar;auto&bar=barLevels[hybridBar];bar.a+=a*a;bar.b+=b*b;bar.d+=d*d;bar.samples++;}
            if(f>=s->frames-sr/10)lateA+=buses[k*6+ch]*buses[k*6+ch]+buses[k*6+4+ch]*buses[k*6+4+ch];
            if(f>=scene.arrival&&f<arrivalEnd){double b=buses[k*6+2+ch],foundation=roles[k*20+12+ch]+roles[k*20+14+ch]+((originalB||vocalBed)?roles[k*20+18+ch]:0);arrivalB+=b*b;arrivalFoundation+=foundation*foundation;arrivalSamples++;
                double reference=incomingPack.sample(incoming.position(f,0),(originalB?0:2)+ch)*incoming.gain,opened=reference*incoming.roles[originalB?4:0].at(f),vocal=roles[k*20+(originalB?18:10)+ch];
                expectedVocal+=reference*reference;openedVocal+=opened*opened;arrivalVocal+=vocal*vocal;
            }
            if(f>=hybrid&&f<arrivalEnd){quietEnergy+=out*out;quietSamples++;if(quietSamples==quietHop*2){quietRun=std::sqrt(quietEnergy/quietSamples)<db(-60)?quietRun+quietHop:0;maxQuiet=std::max(maxQuiet,quietRun);quietEnergy=0;quietSamples=0;}}
            if(continuity&&(f<scene.arrival||f<incomingLead)){
                const auto&v=scene.voices[mainA];const auto&pack=*compiled.voices[mainA];
                double rawVoice=!originalA&&f<v.end?pack.sample(v.position(f,0),2+ch)*v.gain:0,rawDrums=f<v.end?pack.sample(v.position(f,1),foundationChannelA+ch)*v.gain:0;
                if(heldA<scene.voices.size()){
                    const auto&hold=scene.voices[heldA];
                    if(f>=hold.start&&f<hold.end){
                        double handoff=smooth(double(f-hold.start)/(sr*60/scene.bpm));
                        // The musical reference follows the selected held
                        // groove, without the roll and without role muting.
                        double held=compiled.voices[heldA]->sample(hold.position(f,2),4+ch)*hold.gain;
                        rawDrums+=handoff*(held-rawDrums);
                    }
                }
                // After an explicitly measured ownership change, compare with
                // B's rhythm. A different kick pattern is not a dropped stem.
                if(f>=foundationTakeover&&f>=incoming.start&&f<incoming.end)rawDrums=incomingPack.sample(incoming.position(f,1),foundationChannelB+ch)*incoming.gain;
                double oldVoice=roles[k*20+ch],newVoice=roles[k*20+(originalB?18:10)+ch],foundation=foundationAt(roles.data(),k,ch);
                if(f<incomingLead){originalVoiceEnergy+=rawVoice*rawVoice;outgoingVoiceEnergy+=oldVoice*oldVoice;incomingVoiceEnergy+=newVoice*newVoice;}
                if(f<scene.arrival){originalDrumEnergy+=rawDrums*rawDrums;foundationEnergy+=foundation*foundation;
                    if(spectral){
                        double rawLow=originalLow2.low(originalLow.low(float(rawDrums),ch,lowAlpha),ch,lowAlpha),mixedLow=foundationLow2.low(foundationLow.low(float(foundation),ch,lowAlpha),ch,lowAlpha);originalLowEnergy+=rawLow*rawLow;foundationLowEnergy+=mixedLow*mixedLow;
                        // Expected percussion keeps unity role gain, with only
                        // the declared spectral gesture. Thus an intentional
                        // removed hat is distinguished from a prematurely
                        // muted stem. The original low-pulse check remains
                        // independent and still rejects a buried kick.
                        const auto&filter=v.softFilters[combinedFoundation?4:1];float alpha=filter.lp.values.empty()?filter.lpAlpha:filter.lp.at(f);
                        float filtered=alpha>0?expectedDrumLow2.low(expectedDrumLow.low(float(rawDrums),ch,alpha),ch,alpha):float(rawDrums);
                        double expected=rawDrums+filter.wet.at(f)*(filtered-rawDrums);expectedDrumEnergy+=expected*expected;
                    }
                }
                continuityN++;
                if(continuityN>=quietHop*4){
                    double rv=std::sqrt(originalVoiceEnergy/continuityN),av=std::sqrt(outgoingVoiceEnergy/continuityN),bv=std::sqrt(incomingVoiceEnergy/continuityN),rd=std::sqrt(originalDrumEnergy/continuityN),d=std::sqrt(foundationEnergy/continuityN);
                    if(rv>.008&&av<rv*.55&&bv<.004)blockedVoice+=quietHop*2;
                    bool attenuated=rd>.012&&d<rd*.35;if(attenuated)fullBandAttenuation+=quietHop*2;
                    double referenceLow=std::sqrt(originalLowEnergy/continuityN),audibleLow=std::sqrt(foundationLowEnergy/continuityN);
                    // A deliberate low-pass removes hats. It must retain the
                    // expected kick foundation, not the original full-band
                    // drum RMS. When no low rhythm is measured, the existing
                    // percussion check still applies; silence never passes.
                    double expectedDrums=std::sqrt(expectedDrumEnergy/continuityN);
                    bool unsupported=spectral?(referenceLow>.004?audibleLow<referenceLow*.5:expectedDrums>.003&&d<expectedDrums*.35):attenuated;
                    if(unsupported){blockedFoundation+=quietHop*2;
                        if(foundationFailures.size()<80)foundationFailures.push_back({{"seconds",double(f)/sr},{"reference_drums_rms",rd},{"expected_spectral_drums_rms",expectedDrums},{"audible_drums_rms",d},{"reference_low_rms",referenceLow},{"audible_low_rms",audibleLow},{"A_drums_gain",v.roles[1].at(f)}});
                    }
                    originalVoiceEnergy=outgoingVoiceEnergy=incomingVoiceEnergy=originalDrumEnergy=foundationEnergy=originalLowEnergy=foundationLowEnergy=expectedDrumEnergy=0;continuityN=0;
                }
            }
        }
        pos+=n;
    }
    if(!capture.empty()){if(!saved)throw std::runtime_error("Cannot save accepted scene");saved.close();std::filesystem::rename(temporary,capture);}
    double ar=std::sqrt(aEnergy/std::max<Frame>(1,hybridN)),br=std::sqrt(bEnergy/std::max<Frame>(1,hybridN)),dr=std::sqrt(drumEnergy/std::max<Frame>(1,hybridN));
    Json levels=Json::array();int consecutive=0,longest=0;
    for(const auto&bar:barLevels){double a=std::sqrt(bar.a/std::max<Frame>(1,bar.samples)),b=std::sqrt(bar.b/std::max<Frame>(1,bar.samples)),d=std::sqrt(bar.d/std::max<Frame>(1,bar.samples));bool audible=bar.samples>=bar.expected*1.98&&a>.001&&b>.001&&d>.005&&b/std::max(.001,a)>=db(-24);consecutive=audible?consecutive+1:0;longest=std::max(longest,consecutive);levels.push_back({{"A_rms",a},{"B_rms",b},{"drums_rms",d},{"complete_bar",bar.samples>=bar.expected*1.98},{"audible",audible}});}
    Json veto=Json::array();if(!finite||engine.nonfiniteFrames())veto.push_back("NONFINITE");if(error>1e-5)veto.push_back("DOUBLE_DRY_OR_BUS_SUM_ERROR");if(lateA>1e-8)veto.push_back("TAIL_SURVIVED_RECOVERY");
    if(engine.minSafetyGain()<db(-6))veto.push_back("EXCESSIVE_MASTER_REDUCTION");
    double arrivalRms=std::sqrt(arrivalB/std::max<Frame>(1,arrivalSamples)),foundationRms=std::sqrt(arrivalFoundation/std::max<Frame>(1,arrivalSamples));
    if(arrivalRms<.003||foundationRms<.005)veto.push_back("INCOMING_ARRIVAL_HAS_NO_AUDIBLE_FOUNDATION");
    double vocalReferenceRms=std::sqrt(expectedVocal/std::max<Frame>(1,arrivalSamples)),vocalOpenRatio=std::sqrt(openedVocal/std::max(1e-12,expectedVocal)),vocalAudibleRatio=std::sqrt(arrivalVocal/std::max(1e-12,expectedVocal));
    if(vocalReferenceRms>.006&&(vocalOpenRatio<.98||vocalAudibleRatio<db(-3)))veto.push_back(originalB?"INCOMING_ORIGINAL_REMOVED_AT_ARRIVAL":"INCOMING_VOCAL_REMOVED_AT_DROP");
    if(maxQuiet>sr*.20)veto.push_back("SILENT_GAP_IN_SCENE");
    if(blockedVoice>sr*.24)veto.push_back("OUTGOING_VOCAL_REMOVED_BEFORE_HANDOFF");
    if(blockedFoundation>sr*.35)veto.push_back("RHYTHMIC_FOUNDATION_REMOVED_BEFORE_B");
    int required=scene.contract.value("required_hybrid_bars",scene.contract.value("planner",std::string{})=="independent-automix-1"?0:4);if((scene.dj.enabled||scene.family=="groove_weave"||scene.family=="loop_lift")&&longest<required)veto.push_back("HYBRID_NOT_SUBSTANTIALLY_AUDIBLE");
    Json seams=Json::array();for(size_t k=0;k<scene.voices.size();k++){const auto&v=scene.voices[k];if(!v.loop)continue;auto&pack=*compiled.voices[k];double jump=0,signal=0;for(int stem:{1,2})for(int ch=0;ch<2;ch++){double end=pack.sample(v.offset+v.loop-1,2+stem*2+ch),start=pack.sample(v.offset,2+stem*2+ch);jump+=std::pow(end-start,2);for(Frame f=v.offset;f<v.offset+v.loop;f+=97)signal+=std::pow(pack.sample(f,2+stem*2+ch),2);}seams.push_back({{"voice",v.id},{"raw_boundary_rms_difference",std::sqrt(jump/4)},{"raw_signal_energy",signal},{"seam_pre_crossfade_ms",5},{"first_attack_fade",false}});}
    return {{"version",previewCriticVersion},{"foundation_failure_windows",foundationFailures},{"foundation_check",spectral?"original low drum pulse plus declared spectral percussion at unity role gain":"full-band drum continuity"},{"full_band_drum_attenuation_seconds",double(fullBandAttenuation)/sr},{"discarded_A_vocal_seconds",double(blockedVoice)/sr},{"unsupported_foundation_seconds",double(blockedFoundation)/sr},{"outgoing_vocal_checked",!originalA&&continuity},{"incoming_reference_scope",originalB?"complete original stereo, no isolated vocal measurement":"isolated vocal"},{"incoming_vocal_reference_rms",vocalReferenceRms},{"incoming_vocal_open_ratio",vocalOpenRatio},{"incoming_vocal_audible_ratio",vocalAudibleRatio},{"arrival_B_rms",arrivalRms},{"arrival_foundation_rms",foundationRms},{"max_silent_gap_seconds",double(maxQuiet)/sr},{"stage","actual prepared voice -> role routing -> FX -> linked master safety gain"},{"passed",veto.empty()},{"vetoes",veto},{"hybrid_seconds",double(std::max<Frame>(0,hybridEnd-hybrid))/sr},{"hybrid_A_rms",ar},{"hybrid_B_rms",br},{"hybrid_drums_rms",dr},{"hybrid_bars",levels},{"continuous_audible_hybrid_bars",longest},{"required_hybrid_bars",required},{"bus_sum_max_error",error},{"finite",finite&&engine.nonfiniteFrames()==0},{"peak_dbfs",20*std::log10(std::max(peak,1e-12))},{"peak_before_master_dbfs",20*std::log10(std::max(engine.inputPeak(),1e-12))},{"master_min_gain_db",20*std::log10(std::max(engine.minSafetyGain(),1e-12))},{"master_limited_fraction",engine.limitedFraction()},{"loop_seams",seams},{"scope","technical/acoustic acceptance; musical listening is a separate judgement"}};
}
struct SceneBundle {Plan plan;Json assets,critic;Json rendered=Json::object();};
static double nativeJoinSeconds(const Scene&scene,const SceneVoice&voice){
    if(voice.id!="B.main"||!voice.map.clocked())return -1;
    double arrival=double(scene.arrival-voice.start)/scene.sr,early=std::max(0.,arrival-.12);
    // Complete B's native waveform before the featured downbeat. Waiting one
    // more beat left a vocoder/original blend on the drop, whose transient
    // overshoot then ducked the complete mix. The last mapped samples differ
    // in rate by at most 0.1%; their musical/source positions stay unchanged.
    return std::abs(voice.map.rateAt(early)-1)<=.001?early:arrival;
}
static Json voiceIdentity(const Track&t,const SceneVoice&v,Frame frames,int sr,Frame endSource,double attackOutput,double attack){
    Json models=Json::array();
    if(!v.originalOnly){
        if(t.source.contains("stems"))models.push_back(t.source.at("stems"));
        else for(const auto&w:t.source.value("stem_windows",Json::array()))if(w.value("state","")=="ready"&&w["valid_range"][1].get<double>()/sr>v.sourceStart-.7&&w["valid_range"][0].get<double>()/sr<v.sourceEnd+.7)models.push_back({{"range",w.at("valid_range")},{"version",w.value("model_version","")},{"checksums",w.value("fnv1a64",w.value("checksums",Json::object()))}});
    }
    // Roles, gain, FX and the random seed do not change this pre-routing PCM.
    return {{"owner","automix-prepared-voice-v1"},{"dsp","role-bus-v13-native-drop-signalsmith-1.3.2"},{"source_hash",t.hash},{"original_fnv1a64",t.source.at("pcm_fnv1a64")},{"sample_rate",sr},{"frames",frames},{"original_only",v.originalOnly},{"channels",v.channels},{"source_start",v.sourceStart},{"time_map",v.map.json()},{"end_source",endSource},{"attack_output",attackOutput},{"attack_source",attack},{"models",models}};
}
static std::filesystem::path voiceEntry(const std::filesystem::path&root,const Json&identity){
    uint64_t h=14695981039346656037ULL;for(unsigned char c:identity.dump()){h^=c;h*=1099511628211ULL;}std::ostringstream name;name<<"amvoice-"<<std::hex<<h;return root/name.str();
}
static void linkPCM(const std::filesystem::path&from,const std::filesystem::path&to){
    std::error_code error;std::filesystem::create_hard_link(from,to,error);
    if(error)std::filesystem::copy_file(from,to,std::filesystem::copy_options::overwrite_existing);
}
static bool cachedVoice(const std::filesystem::path&root,const Json&identity,const std::filesystem::path&file,std::string&checksum){
    if(root.empty())return false;
    auto entry=voiceEntry(root,identity);
    try{
        auto receipt=readJson(entry/"receipt.json");auto pcm=entry/"pcm.f32";
        if(receipt.at("identity")!=identity||std::filesystem::file_size(pcm)!=identity.at("frames").get<uint64_t>()*identity.value("channels",12)*4)return false;
        checksum=receipt.at("fnv1a64");if(checksumFile(pcm)!=checksum){if(receipt.at("identity").value("owner","")=="automix-prepared-voice-v1"){std::error_code error;std::filesystem::remove_all(entry,error);}return false;}
        auto temporary=file;temporary+=".cached";std::filesystem::remove(temporary);linkPCM(pcm,temporary);std::filesystem::rename(temporary,file);
        std::error_code error;std::filesystem::last_write_time(entry,std::filesystem::file_time_type::clock::now(),error);return true;
    }catch(const std::exception&){return false;} // A cache miss never prevents a fresh preparation.
}
static void trimVoiceCache(const std::filesystem::path&root);
static void rememberVoice(const std::filesystem::path&root,const Json&identity,const std::filesystem::path&file,const std::string&checksum){
    if(root.empty())return;std::filesystem::path temporary;
    try{
        auto entry=voiceEntry(root,identity);if(std::filesystem::exists(entry))return;
        std::filesystem::create_directories(root);temporary=entry;temporary+=".pending-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directory(temporary);
        linkPCM(file,temporary/"pcm.f32");writeJson(temporary/"receipt.json",{{"identity",identity},{"fnv1a64",checksum}});std::filesystem::rename(temporary,entry);trimVoiceCache(root);
    }catch(const std::exception&){if(!temporary.empty()){std::error_code error;std::filesystem::remove_all(temporary,error);}}
}
static void trimVoiceCache(const std::filesystem::path&root){
    if(root.empty()||!std::filesystem::exists(root))return;
    try{
        struct Item{std::filesystem::path path;std::filesystem::file_time_type time;uint64_t bytes;};std::vector<Item>items;uint64_t total=0;
        for(const auto&entry:std::filesystem::directory_iterator(root))if(entry.is_directory()&&!entry.is_symlink()&&entry.path().filename().string().starts_with("amvoice-")&&entry.path().filename().string().find(".pending-")==std::string::npos){
            try{auto receipt=readJson(entry.path()/"receipt.json");if(receipt.at("identity").value("owner","")!="automix-prepared-voice-v1")continue;uint64_t bytes=std::filesystem::file_size(entry.path()/"pcm.f32");total+=bytes;items.push_back({entry.path(),entry.last_write_time(),bytes});}catch(const std::exception&){}
        }
        std::sort(items.begin(),items.end(),[](const auto&a,const auto&b){return a.time<b.time;});
        for(const auto&item:items){if(total<=256ULL*1024*1024)break;std::error_code error;std::filesystem::remove_all(item.path,error);if(!error)total-=item.bytes;}
    }catch(const std::exception&){}
}
static SceneBundle compileScene(Plan p,const std::vector<Track>&tracks,const std::vector<Source>&sources,const std::filesystem::path&directory,const std::filesystem::path&cache,const std::function<bool()>&cancel,bool capture=false){
    p.scene.erase("candidate_plans");int sr=p.sr;
    p.scene["compiler_version"]="independent-dj-8";
    p.scene["critic_version"]=previewCriticVersion;
    Json identity={{"compiler","independent-dj-8"},{"scene",p.scene},{"source_revisions",{tracks[p.a].hash,tracks[p.b].hash}},{"models",Json::array()}};
    for(int deck:{p.a,p.b})if(tracks[deck].source.contains("stem_windows"))for(const auto&w:tracks[deck].source.at("stem_windows"))identity["models"].push_back({{"source_hash",w.value("source_hash","")},{"version",w.value("model_version","")},{"range",w.at("valid_range")},{"checksums",w.value("fnv1a64",w.value("checksums",Json::object()))}});
    uint64_t h=14695981039346656037ULL;for(unsigned char ch:identity.dump()){h^=ch;h*=1099511628211ULL;}std::ostringstream suffix;suffix<<std::hex<<h;
    std::string prefix=p.id+"-"+suffix.str();auto receipt=directory/(prefix+"-prepared.json");Json assets=Json::array();
    if(std::filesystem::exists(receipt)){
        auto old=readJson(receipt);bool ready=old.value("identity",Json{})==identity&&old.contains("voice_assets")
            &&old.contains("preview_critic")&&old.at("preview_critic").value("version",0)==previewCriticVersion;
        if(ready){assets=old["voice_assets"];for(auto&asset:assets){auto file=directory/asset["pcm"].get<std::string>();ready&=std::filesystem::exists(file)&&std::filesystem::file_size(file)==asset["frames"].get<uint64_t>()*asset.value("channels",12)*4&&checksumFile(file)==asset["fnv1a64"].get<std::string>();}}
        if(capture)ready=false; // New mobile publication captures its critic pass.
        if(ready){
            p.scene=old.at("compiled_scene");auto cached=Scene::from(p.scene);
            for(size_t k=0;k<cached.voices.size();k++){const auto&v=cached.voices[k];int track=v.deck?p.b:p.a;double attackOutput=v.id=="B.main"?featuredAttackOutput(v,p.attackSource,sr):-1,attack=v.id=="B.main"?p.attackSource:-1;if(v.loop){attackOutput=double(v.offset)/sr;attack=v.sourceStart+v.map.sourceAt(attackOutput);}Frame end=v.id=="B.main"?std::llround(p.bSourceEnd()*sr):-1;
                auto key=voiceIdentity(tracks[track],v,assets[k].at("frames"),sr,end,attackOutput,attack);key["channels"]=assets[k].value("channels",12);key["channel_map"]=assets[k].value("channel_map",Json::array());key["source_loop"]=p.scene["voices"][k].value("source_loop",Json{});key["native_join"]=nativeJoinSeconds(cached,v);rememberVoice(cache,key,directory/assets[k].at("pcm").get<std::string>(),assets[k].at("fnv1a64"));
            }
            p.scene["execution"]["prepared_voice_cache_hits"]=assets.size();p.scene["execution"]["prepared_voice_cache_misses"]=0;return {p,assets,old.at("preview_critic")};
        }
    }
    assets=Json::array();auto scene=Scene::from(p.scene);int reused=0;
    for(size_t k=0;k<scene.voices.size();k++){
        if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");const auto&v=scene.voices[k];int track=v.deck?p.b:p.a;Frame count=p.scene["voices"][k]["prepare_frames"];auto file=directory/(prefix+"-"+v.id+".f32");
        for(const auto&range:roleSourceRanges(p.scene["voices"][k],sr)){
            const auto role=range.at("role").get<std::string>();int component=role=="vocals"?0:role=="drums"?1:role=="bass"?2:3;
            if(!sources[track].coveredRole(std::llround(std::max(0.,range.at("start").get<double>()-.7)*sr),std::llround((range.at("end").get<double>()+.7)*sr),component))throw std::runtime_error("Scene component range is incomplete: "+role);
        }
        double attackOutput=v.id=="B.main"?featuredAttackOutput(v,p.attackSource,sr):-1,attack=v.id=="B.main"?p.attackSource:-1;
        if(v.loop){attackOutput=double(v.offset)/sr;attack=v.sourceStart+v.map.sourceAt(attackOutput);}
        Frame endSource=v.id=="B.main"?std::llround(p.bSourceEnd()*sr):-1;
        const auto layout=preparedChannels(p.scene["voices"][k]);
        auto key=voiceIdentity(tracks[track],v,count,sr,endSource,attackOutput,attack);key["channels"]=layout.size();key["channel_map"]=layout;const auto sourceLoop=p.scene["voices"][k].value("source_loop",Json{});key["source_loop"]=sourceLoop;double nativeJoin=nativeJoinSeconds(scene,v);key["native_join"]=nativeJoin;std::string checksum;
        if(cachedVoice(cache,key,file,checksum))++reused;
        else{prepareBus(sources[track],v.sourceStart,v.map,double(count)/sr,sr,file,!v.originalOnly,cancel,endSource,int(layout.size()),attackOutput,attack,sourceLoop,nativeJoin,layout);checksum=checksumFile(file);rememberVoice(cache,key,file,checksum);}
        assets.push_back({{"id",v.id},{"pcm",file.filename().string()},{"channels",layout.size()},{"channel_map",layout},{"frames",count},{"fnv1a64",checksum}});
    }
    Compiled c;c.plan=p;c.scene=scene;for(auto&asset:assets)c.voices.push_back(std::make_shared<PCMFile>(directory/asset["pcm"].get<std::string>(),asset.at("channels"),asset.value("channel_map",std::vector<int>{})));
    auto captured=directory/(prefix+"-scene.f32");auto critic=previewScene(c,tracks,sources,sr,cancel,capture?captured:std::filesystem::path{});p.scene["preview_critic"]=critic;p.scene["execution"]["pre_commit_critic"]=critic.at("passed").get<bool>()?"passed":"rejected";
    Json rendered=Json::object();if(capture){auto checksum=checksumFile(captured);rendered={{"pcm",captured.filename().string()},{"frames",p.frames()},{"channels",2},{"fnv1a64",checksum},{"format","float32-stereo-post-master"}};p.scene["rendered_scene_checksum"]=checksum;p.scene["execution"]["scene_render_passes"]=1;}
    p.scene["source_models"]=identity["models"];p.scene["compiled_asset_checksums"]=Json::array();for(auto&asset:assets)p.scene["compiled_asset_checksums"].push_back({{"id",asset["id"]},{"frames",asset["frames"]},{"channels",asset.at("channels")},{"channel_map",asset.value("channel_map",Json::array())},{"fnv1a64",asset["fnv1a64"]}});
    p.scene["execution"]["latency_compensated"]=true;p.scene["execution"]["assets_ready_before_audible_commit"]=true;
    p.scene["execution"]["prepared_voice_cache_hits"]=reused;p.scene["execution"]["prepared_voice_cache_misses"]=scene.voices.size()-reused;
    writeJson(receipt,{{"state",critic.at("passed").get<bool>()?"ready":"rejected"},{"identity",identity},{"compiled_scene",p.scene},{"voice_assets",assets},{"preview_critic",critic}});return {p,assets,critic,rendered};
}
// Both playlist and streaming hosts use this exact acceptance path. A mobile
// pair does not fabricate the reverse B -> A transition of a two-item playlist.
static SceneBundle compileAcceptedScene(Plan p,const std::vector<Track>&tracks,const std::vector<Source>&sources,const std::filesystem::path&directory,const std::filesystem::path&voiceCache,const std::vector<std::string>&history,const std::function<bool()>&cancel,bool capture=false){
    p.scene["source_pcm_checksums"]=Json::array({tracks[p.a].source.at("pcm_fnv1a64"),tracks[p.b].source.at("pcm_fnv1a64")});
    std::vector<Plan>pool={p};for(auto&candidate:p.scene.value("candidate_plans",Json::array()))pool.push_back(Plan::from(candidate));
    std::vector<SceneBundle>previews;Json audit=Json::array();double best=-1e9;size_t winner=0;bool passed=false;
    size_t acceptedPreviews=0;
    for(size_t index=0;index<pool.size();index++){
        auto&candidate=pool[index];
        candidate.scene["source_pcm_checksums"]=p.scene.at("source_pcm_checksums");
        auto preview=compileScene(candidate,tracks,sources,directory,voiceCache,cancel,capture);bool valid=preview.critic.at("passed");
        double balance=preview.critic.at("hybrid_B_rms").get<double>()/std::max(.001,preview.critic.at("hybrid_A_rms").get<double>());
        double repeat=std::count(history.begin(),history.end(),candidate.family)*.012;
        double limiting=preview.critic.value("master_limited_fraction",0.);
        double attenuation=std::max(0.,-preview.critic.value("master_min_gain_db",0.));
        double rank=candidate.score+.04*clamp(balance/.45)-.08*clamp(limiting/.25)*clamp((attenuation-1.5)/4)-repeat;
        audit.push_back({{"family",candidate.family},{"variant",candidate.scene.value("variant",std::string("standard"))},{"cue_a",candidate.outCue},{"cue_b",candidate.entryAnchor},{"rank",rank},{"critic",preview.critic}});
        previews.push_back(std::move(preview));if(valid){acceptedPreviews++;if(rank>best){best=rank;winner=previews.size()-1;passed=true;}}
        // Normally the highest musical score needs one preparation. Compare
        // one close alternative only when actual acoustics expose weak entry
        // or substantial limiting; never multiply every transition's work.
        bool close=index+1<pool.size()&&candidate.score-pool[index+1].score<.06;
        bool unresolved=balance<.35||(limiting>.18&&attenuation>3.);
        if(valid&&(!close||!unresolved||acceptedPreviews>=2))break;
    }
    if(!passed){
        writeJson(directory/(p.id+"-rejection.json"),{{"reason","ALL_SCENE_PREVIEWS_REJECTED"},{"candidate_previews",audit},{"session_published",false}});
        throw std::runtime_error("All independent scene previews rejected for "+p.id+"; no silent/end-to-start fallback, session was not published");
    }
    auto&chosen=previews[winner];const auto&request=p.scene.at("execution");
    std::string attempted=request.value("attempted_family",p.family),transition=request.value("attempted_transition",attempted),variant=request.value("attempted_variant",std::string("auto"));
    p=chosen.plan;p.scene["execution"]["attempted_family"]=attempted;p.scene["execution"]["attempted_transition"]=transition;p.scene["execution"]["attempted_variant"]=variant;p.scene["execution"]["executed_family"]=p.family;
    p.scene["execution"]["selection"]={{"method","all analytic candidates ranked, then bounded acoustic comparison"},{"prepared_candidates",previews.size()},{"accepted_candidates",acceptedPreviews},{"selected_rank",best},{"maximum_accepted_previews",2}};
    p.scene["execution"]["executed_transition"]=p.scene.value("variant",std::string())=="spectral_exchange"?"spectral_exchange":p.family;
    p.scene["execution"]["adapted_from_request"]=(attempted!="auto"&&attempted!=p.family)||(variant!="auto"&&variant!=p.scene.value("variant",std::string("standard")));p.scene["execution"]["candidate_previews"]=audit;
    chosen.plan=p;return chosen;
}
// Commit the already accepted scene as two lossless float channels. Offline
// routing/FX and the shared clock are unchanged; playback no longer repeats DSP.
void freezeSession(const std::filesystem::path&path){
    auto session=Session::load(path);auto j=session->manifest;auto base=path.parent_path();
    for(size_t index=0;index<session->transitions.size();index++){
        const auto&c=session->transitions[index];if(c.rendered||!c.scene)continue;
        auto isolated=std::make_shared<Session>();isolated->sr=session->sr;isolated->frames=c.plan.frames();isolated->tracks=session->tracks;isolated->sources=session->sources;isolated->transitions={c};isolated->spans={{0,c.plan.frames(),0,c.plan.a,0}};
        Engine engine(isolated);std::array<float,512>l{},r{};std::array<float,1024>stereo{};
        std::string name=c.plan.id+"-scene.f32";auto file=base/name,tmp=file;tmp+=".pending";std::ofstream out(tmp,std::ios::binary);
        if(!out)throw std::runtime_error("Cannot write compact scene");
        for(Frame f=0;f<c.plan.frames();){int n=int(std::min<Frame>(512,c.plan.frames()-f));engine.render(l.data(),r.data(),n);for(int k=0;k<n;k++){stereo[k*2]=l[k];stereo[k*2+1]=r[k];}out.write(reinterpret_cast<char*>(stereo.data()),n*8);f+=n;}
        if(!out)throw std::runtime_error("Compact scene write failed");out.close();std::filesystem::rename(tmp,file);
        auto checksum=checksumFile(file);auto&item=j["transitions"][index];item.erase("voice_assets");
        item["rendered_scene"]={{"pcm",name},{"frames",c.plan.frames()},{"channels",2},{"fnv1a64",checksum},{"format","float32-stereo-post-master"}};
        item["plan"]["compiled_scene"]["rendered_scene_checksum"]=checksum;
    }
    // A playback session owns originals and final scenes only. Separation
    // material remains a bounded, independently evictable preparation cache.
    const bool compact=std::all_of(j["transitions"].begin(),j["transitions"].end(),[](const auto&item){return item.contains("rendered_scene");});
    if(!compact)throw std::runtime_error("Compact storage requires musical scenes");
    for(auto&t:j["tracks"]){t.erase("stem_windows");t.erase("stems");}
    j["storage_version"]=2;j["playback_processing"]="prevalidated stereo; no model, stretch or effect worker";
    auto check=base/"compact-validation.pending.json";writeJson(check,j);Session::load(check);std::filesystem::remove(check);
    writeJson(path,j);
    for(const auto&item:j["transitions"]){auto cardPath=base/(item["plan"]["id"].get<std::string>()+".session.json");if(std::filesystem::exists(cardPath)){auto card=readJson(cardPath);card["tracks"]=j["tracks"];card["transitions"]=Json::array({item});card["storage_version"]=2;writeJson(cardPath,card);}}
}
Plan planPair(const Json&library,const Policy&policy,uint64_t seed,double minimum,const std::vector<std::string>&history){
    if(library.at("schema_version")!=1||library.at("tracks").size()!=2)throw std::runtime_error("Pair requires exactly two tracks");
    if(!std::isfinite(minimum)||minimum<0)throw std::runtime_error("Invalid minimum source position");
    auto a=trackFromJson(library.at("tracks")[0]),b=trackFromJson(library.at("tracks")[1]);
    if(a.sampleRate!=b.sampleRate)throw std::runtime_error("Canonical sample rates differ");
    Json vetoes;std::optional<Plan> selected;
    if(policy.requestStems){auto ready=policy;ready.requestStems=false;selected=directScene(a,b,0,1,ready,seed,minimum,history,vetoes);}
    if(!selected)selected=directScene(a,b,0,1,policy,seed,minimum,history,vetoes);
    if(!selected)throw std::runtime_error("No measured independent scene for "+a.id+" -> "+b.id+". "+vetoes.dump());
    return *selected;
}
Json preparePair(Json library,const std::filesystem::path&base,const std::filesystem::path&directory,uint64_t seed,double minimum,const std::vector<std::string>&history,const std::function<bool()>&cancel){
    std::filesystem::create_directories(directory);
    if(library.at("schema_version")!=1||library.at("tracks").size()!=2)throw std::runtime_error("Pair requires exactly two tracks");
    std::vector<Track> tracks;std::vector<Source> sources;
    for(auto&t:library.at("tracks")){
        if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");
        t["pcm_path"]=std::filesystem::absolute(resolve(base,t.at("pcm_path"))).string();
        t["pcm_fnv1a64"]=checksumFile(t.at("pcm_path").get<std::string>());
        if(t.contains("stems"))for(auto&[name,path]:t["stems"]["stem_paths"].items())path=std::filesystem::absolute(resolve(base,path.get<std::string>())).string();
        for(auto&w:t["stem_windows"])for(auto&[name,path]:w["stem_paths"].items())path=std::filesystem::absolute(resolve(base,path.get<std::string>())).string();
        tracks.push_back(trackFromJson(t));sources.push_back(sourceFromTrack(tracks.back(),base));
    }
    auto plan=planPair(library,policyFor("dj"),seed,minimum,history);
    auto cache=library.value("prepared_voice_cache",std::string());
    auto chosen=compileAcceptedScene(plan,tracks,sources,directory,cache.empty()?std::filesystem::path{}:resolve(base,cache),history,cancel,library.value("capture_scene",false));
    plan=chosen.plan;
    Json session={{"schema_version",1},{"scene_contract_version",1},{"engine_version","automix-native-independent-dj-5"},{"sample_rate",plan.sr},{"mode","dj"},{"seed",seed},{"loop",false},{"loop_start_frame",0},{"tracks",library.at("tracks")},{"transitions",Json::array({Json{{"plan",plan.json()},{"voice_assets",chosen.assets},{"compiled_scene_version",1}}})},{"spans",Json::array({makeSpan(0,plan.frames(),0,0,0)})},{"total_frames",plan.frames()}};
    if(!chosen.rendered.empty()){session["transitions"][0].erase("voice_assets");session["transitions"][0]["rendered_scene"]=chosen.rendered;}
    if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");
    writeJson(directory/"session.json",session);return session;
}
void prepareLibrary(const std::filesystem::path&library,const std::filesystem::path&directory,const Policy&policy,uint64_t seed,const std::function<bool()>&cancel,const std::function<void(float)>&progress){
    auto j=readJson(library);if(j.at("schema_version")!=1)throw std::runtime_error("Unsupported library schema");std::filesystem::create_directories(directory);
    std::filesystem::path voiceCache=j.value("prepared_voice_cache",std::string{});if(!voiceCache.empty())voiceCache=std::filesystem::absolute(resolve(library.parent_path(),voiceCache));
    std::vector<Track>tracks;std::vector<Source>sources;for(auto&t:j.at("tracks")){
        t["pcm_path"]=std::filesystem::absolute(resolve(library.parent_path(),t.at("pcm_path"))).string();
        t["pcm_fnv1a64"]=checksumFile(t.at("pcm_path").get<std::string>());
        if(t.contains("stems"))for(auto&[name,path]:t["stems"]["stem_paths"].items())path=std::filesystem::absolute(resolve(library.parent_path(),path.get<std::string>())).string();
        if(t.contains("stem_windows"))for(auto&w:t["stem_windows"])for(auto&[name,path]:w["stem_paths"].items())path=std::filesystem::absolute(resolve(library.parent_path(),path.get<std::string>())).string();
        tracks.push_back(trackFromJson(t));sources.push_back(sourceFromTrack(tracks.back(),library.parent_path()));}
    int sr=tracks.front().sampleRate;for(auto&t:tracks)if(t.sampleRate!=sr)throw std::runtime_error("Resample before preparing playlist");
    auto plans=planPlaylist(tracks,policy,seed);Json compiled=Json::array();std::vector<std::string>executedHistory;
    if(policy.mode!="off")for(size_t i=0;i<plans.size();i++){
        if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");if(progress)progress(float(i)/plans.size());
        auto&p=plans[i];auto aPath=directory/(p.id+"-a.f32"),bPath=directory/(p.id+"-b.f32"),receipt=directory/(p.id+"-prepared.json");
        if(!p.scene.empty()){
            auto chosen=compileAcceptedScene(p,tracks,sources,directory,voiceCache,executedHistory,cancel);
            p=chosen.plan;
            executedHistory.push_back(p.family);if(executedHistory.size()>3)executedHistory.erase(executedHistory.begin());
            compiled.push_back({{"plan",p.json()},{"voice_assets",chosen.assets},{"compiled_scene_version",1}});std::cout<<p.id<<" "<<p.recipe<<" | "<<p.explanation<<'\n';continue;
        }
        throw std::runtime_error("Only compiled musical scenes are supported for new mixes");
    }
    Json session={{"schema_version",1},{"scene_contract_version",1},{"engine_version","automix-native-independent-dj-5"},{"sample_rate",sr},{"mode",policy.mode},{"seed",seed},{"loop",true},{"tracks",j.at("tracks")},{"transitions",compiled},{"spans",Json::array()}};
    Frame cursor=0;
    if(policy.mode=="off"){
        for(size_t i=0;i<tracks.size();i++){session["spans"].push_back(makeSpan(cursor,tracks[i].frames,int(i),0));cursor+=tracks[i].frames;}
    }else{
        // Play the first song from its beginning; subsequent loops join the final B exactly.
        double source=0;
        for(size_t i=0;i<plans.size();i++){
            auto&p=plans[i];Frame begin=std::llround(source*sr),end=std::llround(p.aSourceStart()*sr),count=end-begin;
            if(count<0)throw std::runtime_error("Transition cues intersect previous handover");
            if(count>0){session["spans"].push_back(makeSpan(cursor,count,p.a,begin));cursor+=count;}
            if(p.frames()>0){session["spans"].push_back(makeSpan(cursor,p.frames(),p.a,0,int(i)));cursor+=p.frames();}
            source=p.bSourceEnd();
        }
    }
    if(policy.mode!="off"&&plans.back().bSourceEnd()+2>plans.front().aSourceStart())throw std::runtime_error("Compiled playlist loop intersects first scene; session was not published");
    trimVoiceCache(voiceCache);
    if(cancel&&cancel())throw std::runtime_error("Preparation cancelled");session["total_frames"]=cursor;session["loop_start_frame"]=policy.mode!="off"?std::llround(plans.back().bSourceEnd()*sr):0;writeJson(directory/"session.json",session);
    // Isolated listening cards include context before and after each transition.
    for(size_t i=0;i<compiled.size();i++){
        auto&p=plans[i];Json card=session;card["loop"]=false;card["transitions"]=Json::array({compiled[i]});card["spans"]=Json::array();Frame pre=std::min<Frame>(std::llround(12.*sr),std::llround(p.aSourceStart()*sr));Frame total=pre;
        if(pre>0)card["spans"].push_back(makeSpan(0,pre,p.a,std::llround(p.aSourceStart()*sr)-pre));
        if(p.frames()>0){card["spans"].push_back(makeSpan(total,p.frames(),p.a,0,0));total+=p.frames();}
        Frame source=std::llround(p.bSourceEnd()*sr),after=std::min<Frame>(std::llround(12.*sr),tracks[p.b].frames-source);
        if(after>0){card["spans"].push_back(makeSpan(total,after,p.b,source));total+=after;}
        card["total_frames"]=total;card["loop_start_frame"]=0;writeJson(directory/(p.id+".session.json"),card);
    }
    writeJson(directory/"plans.json",{{"schema_version",1},{"mode",policy.mode},{"seed",seed},{"plans",compiled},{"recipes",recipeCatalog()}});
    if(progress)progress(1);
}
static void u16(std::ostream&o,uint16_t v){char b[2]={char(v),char(v>>8)};o.write(b,2);}
static void u32(std::ostream&o,uint32_t v){char b[4]={char(v),char(v>>8),char(v>>16),char(v>>24)};o.write(b,4);}
void renderWav(const std::filesystem::path&manifest,const std::filesystem::path&file,const std::filesystem::path&report){
    auto s=Session::load(manifest);if(uint64_t(s->frames)*8>0xffffffffULL-36)throw std::runtime_error("WAV exceeds RIFF limit");Engine e(s);auto tmp=file;tmp+=".pending";
    std::ofstream f(tmp,std::ios::binary);if(!f)throw std::runtime_error("Cannot write WAV");f.write("RIFF",4);u32(f,uint32_t(36+s->frames*8));f.write("WAVEfmt ",8);u32(f,16);u16(f,3);u16(f,2);u32(f,s->sr);u32(f,s->sr*8);u16(f,8);u16(f,32);f.write("data",4);u32(f,uint32_t(s->frames*8));
    std::array<float,512>l{},r{};std::array<float,1024>inter{};std::vector<double>times;double peak=0,sum=0;uint64_t nonfinite=0;auto start=std::chrono::steady_clock::now();
    for(Frame pos=0;pos<s->frames;){uint32_t n=uint32_t(std::min<Frame>(512,s->frames-pos));auto t=std::chrono::steady_clock::now();e.render(l.data(),r.data(),n);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());
        for(uint32_t k=0;k<n;k++){inter[k*2]=l[k];inter[k*2+1]=r[k];for(float v:{l[k],r[k]}){peak=std::max(peak,double(abs(v)));sum+=v*v;nonfinite+=!std::isfinite(v);}}f.write(reinterpret_cast<char*>(inter.data()),n*8);pos+=n;}
    if(!f)throw std::runtime_error("WAV write failed");f.close();std::filesystem::rename(tmp,file);std::sort(times.begin(),times.end());auto pct=[&](double q){return times[std::min(times.size()-1,size_t(q*times.size()))];};
    writeJson(report,{{"schema_version",1},{"frames",s->frames},{"sample_rate",s->sr},{"channels",2},{"seconds",double(s->frames)/s->sr},{"peak_dbfs",20*std::log10(std::max(peak,1e-12))},{"rms_dbfs",10*std::log10(std::max(sum/(s->frames*2),1e-24))},{"nonfinite",nonfinite},
        {"render_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()},{"worker_block_frames",512},{"worker_p50_ms",pct(.5)},{"worker_p95_ms",pct(.95)},{"worker_p99_ms",pct(.99)},{"worker_worst_ms",times.back()},{"block_deadline_ms",512.*1000/s->sr},{"note","Worker measurements; hardware callback only consumes bounded SPSC PCM"}});
}
void renderDiagnostics(const std::filesystem::path&manifest,const std::filesystem::path&directory){
    auto s=Session::load(manifest);std::filesystem::create_directories(directory);
    if(uint64_t(s->frames)*8>0xffffffffULL-36)throw std::runtime_error("Diagnostic WAV exceeds RIFF");
    bool hasRoles=false;for(auto&c:s->transitions)hasRoles|=bool(c.scene);
    std::vector<std::string>names={"A","B","FX","mix"};if(hasRoles)for(auto deck:{"A","B"})for(auto role:{"vocals","drums","bass","other","residual"})names.push_back(std::string(deck)+"_"+role);
    std::vector<std::ofstream> files(names.size());
    for(size_t b=0;b<files.size();b++){
        files[b].open(directory/(names[b]+".wav"),std::ios::binary);
        auto&f=files[b];if(!f)throw std::runtime_error("Cannot write diagnostic bus");
        f.write("RIFF",4);u32(f,uint32_t(36+s->frames*8));f.write("WAVEfmt ",8);u32(f,16);u16(f,3);u16(f,2);u32(f,s->sr);u32(f,s->sr*8);u16(f,8);u16(f,32);f.write("data",4);u32(f,uint32_t(s->frames*8));
    }
    Engine e(s);std::array<float,512>l{},r{};std::array<float,512*6>buses{};std::array<float,512*20>roles{};std::array<float,1024>inter{};
    Json bins=Json::array();std::vector<double>energy(names.size()),peak(names.size());Frame bucket=0,count=0;double sumError=0;
    auto flush=[&]{Json item={{"frame",bucket},{"seconds",double(bucket)/s->sr},{"frames",count}};
        for(size_t b=0;b<names.size();b++)item[names[b]]={{"rms_dbfs",10*std::log10(std::max(1e-24,energy[b]/std::max<Frame>(1,count*2)))},{"peak_dbfs",20*std::log10(std::max(1e-12,peak[b]))}};
        bins.push_back(item);std::fill(energy.begin(),energy.end(),0);std::fill(peak.begin(),peak.end(),0);bucket+=count;count=0;};
    for(Frame pos=0;pos<s->frames;){uint32_t n=uint32_t(std::min<Frame>(512,s->frames-pos));e.render(l.data(),r.data(),n,buses.data(),hasRoles?roles.data():nullptr);
        for(size_t b=0;b<files.size();b++){for(uint32_t k=0;k<n;k++)for(int ch=0;ch<2;ch++)inter[k*2+ch]=b==3?(ch?r[k]:l[k]):b<3?buses[k*6+b*2+ch]:roles[k*20+(b-4)*2+ch];files[b].write(reinterpret_cast<char*>(inter.data()),n*8);}
        for(uint32_t k=0;k<n;k++){
            for(int ch=0;ch<2;ch++){double sum=0;for(int b=0;b<3;b++){double v=buses[k*6+b*2+ch];energy[b]+=v*v;peak[b]=std::max(peak[b],abs(v));sum+=v;}double mix=ch?r[k]:l[k];energy[3]+=mix*mix;peak[3]=std::max(peak[3],abs(mix));sumError=std::max(sumError,abs(sum-mix));for(size_t b=4;b<names.size();b++){double v=roles[k*20+(b-4)*2+ch];energy[b]+=v*v;peak[b]=std::max(peak[b],abs(v));}}
            if(++count>=s->sr/4)flush();
        }pos+=n;
    }if(count)flush();for(auto&f:files)if(!f)throw std::runtime_error("Diagnostic write failed");
    Json events=Json::array();for(auto&span:s->spans)if(span.transition>=0){const auto&c=s->transitions[span.transition];if(!c.scene)continue;for(auto&[name,anchor]:c.scene->contract.at("anchors").items()){
        Frame local=anchor.at("output_frame");Json voicePositions=Json::array();for(auto&v:c.scene->voices)voicePositions.push_back({{"voice",v.id},{"audible_window",local>=v.start&&local<v.end},{"prepared_read_frame",v.position(local,1)},{"virtual_source_seconds",v.virtualSourceSeconds(local,s->sr)},{"loop_source_seconds",v.loop?v.sourceStart+v.map.sourceAt(v.position(local,1)/s->sr):0.}});
        events.push_back({{"scene",c.plan.id},{"anchor",name},{"session_frame",span.start+local},{"scene_frame",local},{"scene_quarter",anchor.at("scene_quarter")},{"voices",voicePositions}});
    }}
    writeJson(directory/"trace.json",{{"contract_version",1},{"sample_rate",s->sr},{"frames",s->frames},{"source_manifest",std::filesystem::absolute(manifest).string()},{"manifest_fnv1a64",checksumFile(manifest)},{"stage","after routing, effects and shared master safety gain"},{"role_buses_available",hasRoles},{"bus_sum_max_error",sumError},{"render_health",{{"nonfinite_frames_before_safety",e.nonfiniteFrames()},{"peak_before_master",e.inputPeak()},{"minimum_master_gain",e.minSafetyGain()},{"limited_fraction",e.limitedFraction()}}},{"events",events},{"bins",bins},{"session",s->manifest}});
}
}
