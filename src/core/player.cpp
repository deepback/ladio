#include "audio.hpp"
#include "ladio/ladio.h"
#include <chrono>
#include <cstring>
#include <numeric>
#if defined(__APPLE__)
#include <pthread.h>
#endif
namespace {
thread_local std::string error;
template<class F>int guarded(F fn){try{fn();error.clear();return 1;}catch(const std::exception&e){error=e.what();return 0;}catch(...){error="Unknown native failure";return 0;}}
constexpr uint32_t BLOCK=512;
struct AudioChunk {uint64_t generation=0;ladio::Frame start=0;uint32_t count=0;std::array<float,BLOCK>left{},right{};};
struct Command {ladio::Frame target=0;uint64_t generation=0;};
template<class T,size_t N>class SPSC {
    std::array<T,N> data_;alignas(64)std::atomic<uint64_t>write_{0};alignas(64)std::atomic<uint64_t>read_{0};
public:
    bool push(const T&v)noexcept{auto w=write_.load(std::memory_order_relaxed);if(w-read_.load(std::memory_order_acquire)>=N)return false;data_[w%N]=v;write_.store(w+1,std::memory_order_release);return true;}
    const T*front()const noexcept{auto r=read_.load(std::memory_order_relaxed);return r<write_.load(std::memory_order_acquire)?&data_[r%N]:nullptr;}
    void pop()noexcept{read_.fetch_add(1,std::memory_order_release);}
    uint64_t size()const noexcept{return write_.load(std::memory_order_acquire)-read_.load(std::memory_order_acquire);}
};
static_assert(std::atomic<uint64_t>::is_always_lock_free&&std::atomic<ladio::Frame>::is_always_lock_free&&std::atomic<float>::is_always_lock_free);
}
struct LadioPlayer {
    std::shared_ptr<ladio::Session>session;ladio::Engine engine;SPSC<AudioChunk,256>pcm;SPSC<Command,64>commands;
    std::atomic<bool>stop{false},playing{false},looping{true};std::atomic<uint64_t>desiredGeneration{1},underruns{0},rendered{0};
    std::atomic<ladio::Frame>audible{0},requestedFrame{0};std::atomic<uint64_t>audibleGeneration{1};std::atomic<float>peakL{0},peakR{0};std::atomic<double>workerP99{0};
    std::atomic<int>state{LADIO_PAUSED};uint32_t partial=0,capacity=96;std::mutex controlMutex;std::condition_variable controlChanged;std::thread worker;
    explicit LadioPlayer(std::shared_ptr<ladio::Session>s,uint32_t ahead):session(s),engine(s){looping.store(s->loop);capacity=std::clamp<uint32_t>((ahead+BLOCK-1)/BLOCK,4,256);worker=std::thread([this]{pump();});}
    // Only UI/control calls notify. The audio callback never locks, signals or
    // allocates. Holding this mutex when notifying prevents a lost wakeup.
    void notifyWorker(){std::lock_guard<std::mutex>lock(controlMutex);controlChanged.notify_one();}
    ~LadioPlayer(){stop.store(true,std::memory_order_release);notifyWorker();if(worker.joinable())worker.join();}
    void pump(){
        #if defined(__APPLE__)
        // This bounded producer has an audio deadline and never performs ML or
        // file loading. Do not inherit a background/UI preparation QoS.
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        #endif
        uint64_t generation=1;AudioChunk chunk;std::array<double,256>measurements{};size_t count=0;bool ended=false;
        try{while(!stop.load(std::memory_order_acquire)){
            bool seek=false;ladio::Frame target=0;while(auto cmd=commands.front()){target=cmd->target;generation=cmd->generation;commands.pop();seek=true;}
            if(seek){engine.seek(target);ended=false;}
            if(pcm.size()>=capacity||ended){
                std::unique_lock<std::mutex>lock(controlMutex);
                if(playing.load(std::memory_order_acquire))controlChanged.wait_for(lock,std::chrono::milliseconds(8),[this]{return stop.load(std::memory_order_acquire)||commands.size()>0||!playing.load(std::memory_order_acquire);});
                else controlChanged.wait(lock,[this]{return stop.load(std::memory_order_acquire)||commands.size()>0||playing.load(std::memory_order_acquire);});
                continue;
            }
            if(engine.cursor()>=session->frames){if(looping.load(std::memory_order_acquire))engine.seek(session->loopStart);else{ended=true;continue;}}
            chunk.start=engine.cursor();chunk.generation=generation;chunk.count=uint32_t(std::min<ladio::Frame>(BLOCK,session->frames-chunk.start));
            auto before=std::chrono::steady_clock::now();engine.render(chunk.left.data(),chunk.right.data(),chunk.count);
            measurements[count%measurements.size()]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();count++;
            if(count%256==0){auto sorted=measurements;std::sort(sorted.begin(),sorted.end());workerP99.store(sorted[253],std::memory_order_relaxed);}
            while(!pcm.push(chunk)&&!stop.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
            rendered.fetch_add(chunk.count,std::memory_order_relaxed);
        }}catch(...){state.store(LADIO_BUFFERING,std::memory_order_release);stop.store(true,std::memory_order_release);}
    }
    bool seek(ladio::Frame frame){auto generation=desiredGeneration.load(std::memory_order_relaxed)+1;Command command{std::clamp<ladio::Frame>(frame,0,session->frames-1),generation};if(!commands.push(command))return false;requestedFrame.store(command.target,std::memory_order_relaxed);desiredGeneration.store(generation,std::memory_order_release);notifyWorker();return true;}
};
struct LadioPreparation {
    std::atomic<bool>cancelled{false};std::atomic<int>state{LADIO_PREPARING};std::atomic<float>progress{0};std::string failure;std::thread worker;
    LadioPreparation(std::string library,std::string output,std::string mode,uint64_t seed){worker=std::thread([this,library=std::move(library),output=std::move(output),mode=std::move(mode),seed]{
        try{ladio::prepareLibrary(library,output,ladio::policyFor(mode),seed,[this]{return cancelled.load(std::memory_order_acquire);},[this](float p){progress.store(p,std::memory_order_relaxed);});state.store(cancelled.load()?LADIO_CANCELLED:LADIO_READY,std::memory_order_release);}
        catch(const std::exception&e){failure=e.what();state.store(cancelled.load()?LADIO_CANCELLED:LADIO_FAILED,std::memory_order_release);}
        catch(...){failure="Unknown preparation failure";state.store(LADIO_FAILED,std::memory_order_release);}
    });}
    ~LadioPreparation(){cancelled.store(true,std::memory_order_release);if(worker.joinable())worker.join();}
};
extern "C" {
const char*ladio_last_error(void){return error.c_str();}
LadioPlayer*ladio_player_create(const char*path,uint32_t ahead){LadioPlayer*p=nullptr;guarded([&]{if(!path)throw std::runtime_error("No session path");p=new LadioPlayer(ladio::Session::load(path),ahead);});return p;}
void ladio_player_destroy(LadioPlayer*p){delete p;}
int ladio_player_play(LadioPlayer*p){if(!p)return 0;p->playing.store(true,std::memory_order_release);p->state.store(LADIO_PLAYING,std::memory_order_release);p->notifyWorker();return 1;}
int ladio_player_pause(LadioPlayer*p){if(!p)return 0;p->playing.store(false,std::memory_order_release);p->state.store(LADIO_PAUSED,std::memory_order_release);p->notifyWorker();return 1;}
int ladio_player_set_loop(LadioPlayer*p,int enabled){if(!p)return 0;p->looping.store(enabled!=0,std::memory_order_release);return 1;}
int ladio_player_seek(LadioPlayer*p,int64_t frame){return p&&p->seek(frame);}
int ladio_player_skip(LadioPlayer*p,int direction){
    if(!p)return 0;ladio::Frame pos=p->desiredGeneration.load(std::memory_order_acquire)>p->audibleGeneration.load(std::memory_order_acquire)?p->requestedFrame.load(std::memory_order_relaxed):p->audible.load(std::memory_order_acquire),target=0;
    std::vector<ladio::Frame>markers{0};int owner=-1;for(auto&s:p->session->spans){if(s.transition>=0){auto &plan=p->session->transitions[s.transition].plan;markers.push_back(s.start+std::llround((plan.scene.empty()?plan.lead+plan.seconds:plan.impact)*plan.sr));owner=plan.b;}else{if(owner>=0&&owner!=s.track)markers.push_back(s.start);owner=s.track;}}
    if(direction>=0){target=p->looping.load(std::memory_order_acquire)?0:p->session->frames-1;for(auto m:markers)if(m>pos+p->session->sr/4){target=m;break;}}
    else{size_t current=0;while(current+1<markers.size()&&markers[current+1]<=pos)current++;target=pos-markers[current]>=p->session->sr*2?markers[current]:markers[current?current-1:0];}
    return p->seek(target);
}
uint32_t ladio_player_pull(LadioPlayer*p,float*left,float*right,uint32_t frames){
    if(!left||!right)return 0;std::fill_n(left,frames,0);std::fill_n(right,frames,0);
    if(!p||!p->playing.load(std::memory_order_acquire))return 0;
    uint32_t copied=0;uint64_t wanted=p->desiredGeneration.load(std::memory_order_acquire);float pl=0,pr=0;uint32_t discardBudget=256;
    while(copied<frames){auto chunk=p->pcm.front();if(!chunk)break;
        if(chunk->generation>wanted)break;
        if(chunk->generation<wanted){p->partial=0;p->pcm.pop();if(--discardBudget==0)break;continue;}
        uint32_t count=std::min(frames-copied,chunk->count-p->partial);
        for(uint32_t k=0;k<count;k++){float l=chunk->left[p->partial+k],r=chunk->right[p->partial+k];left[copied+k]=l;right[copied+k]=r;pl=std::max(pl,abs(l));pr=std::max(pr,abs(r));}
        p->partial+=count;copied+=count;p->audible.store(chunk->start+p->partial,std::memory_order_release);p->audibleGeneration.store(wanted,std::memory_order_release);
        if(p->partial==chunk->count){p->partial=0;p->pcm.pop();}
    }
    p->peakL.store(pl,std::memory_order_relaxed);p->peakR.store(pr,std::memory_order_relaxed);
    auto pos=p->audible.load(std::memory_order_relaxed);
    if(copied<frames){if(pos>=p->session->frames&&!p->looping.load(std::memory_order_acquire)){p->state.store(LADIO_ENDED,std::memory_order_release);p->playing.store(false,std::memory_order_release);}else{p->underruns.fetch_add(1,std::memory_order_relaxed);p->state.store(LADIO_BUFFERING,std::memory_order_release);}}
    else{auto &s=p->session->spans[p->session->spanAt(pos>=p->session->frames?0:pos)];p->state.store(s.transition>=0?LADIO_MIXING:LADIO_PLAYING,std::memory_order_release);}
    return copied;
}
LadioSnapshot ladio_player_snapshot(const LadioPlayer*p){
    LadioSnapshot s{};if(!p)return s;s.generation=p->desiredGeneration.load(std::memory_order_acquire);s.output_frame=p->audible.load(std::memory_order_acquire);s.total_frames=p->session->frames;s.sample_rate=p->session->sr;s.state=p->state.load(std::memory_order_acquire);s.underruns=p->underruns.load(std::memory_order_relaxed);s.rendered_frames=p->rendered.load(std::memory_order_relaxed);s.worker_p99_ms=p->workerP99.load(std::memory_order_relaxed);s.peak_left=p->peakL.load(std::memory_order_relaxed);s.peak_right=p->peakR.load(std::memory_order_relaxed);
    if(s.generation>p->audibleGeneration.load(std::memory_order_acquire))s.output_frame=p->requestedFrame.load(std::memory_order_relaxed);
    auto &span=p->session->spans[p->session->spanAt(std::min(s.output_frame,s.total_frames-1))];s.track_a=span.track;s.track_b=-1;s.transition_index=span.transition;
    double local=double(s.output_frame-span.start)/s.sample_rate;
    if(span.transition>=0){auto &q=p->session->transitions[span.transition].plan;s.track_a=q.a;s.track_b=q.b;s.source_a_seconds=q.aSourceStart()+q.mapA.sourceAt(local);s.source_b_seconds=q.inCue+q.mapB.sourceAt(std::max(0.0,local-q.bStart));s.mix_progress=float(ladio::clamp((local-q.lead)/std::max(q.scene.empty()?q.seconds:q.impact-q.lead,.001)));}
    else{s.source_a_seconds=double(span.source)/s.sample_rate+local;}
    return s;
}
int ladio_prepare_library(const char*library,const char*out,const char*mode,uint64_t seed){return guarded([&]{if(!library||!out||!mode)throw std::runtime_error("Missing preparation arguments");ladio::prepareLibrary(library,out,ladio::policyFor(mode),seed);});}
LadioPreparation*ladio_preparation_start(const char*library,const char*out,const char*mode,uint64_t seed){LadioPreparation*j=nullptr;guarded([&]{if(!library||!out||!mode)throw std::runtime_error("Missing preparation arguments");j=new LadioPreparation(library,out,mode,seed);});return j;}
void ladio_preparation_cancel(LadioPreparation*j){if(j)j->cancelled.store(true,std::memory_order_release);}
int ladio_preparation_state(const LadioPreparation*j){return j?j->state.load(std::memory_order_acquire):LADIO_FAILED;}
float ladio_preparation_progress(const LadioPreparation*j){return j?j->progress.load(std::memory_order_relaxed):0;}
const char*ladio_preparation_error(const LadioPreparation*j){if(!j)return "No preparation job";if(j->state.load(std::memory_order_acquire)==LADIO_PREPARING)return "";return j->failure.c_str();}
void ladio_preparation_destroy(LadioPreparation*j){delete j;}
int ladio_render_session(const char*session,const char*wav,const char*report){return guarded([&]{if(!session||!wav||!report)throw std::runtime_error("Missing render arguments");ladio::renderWav(session,wav,report);});}
}
