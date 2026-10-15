#include "ladio/analysis.h"
#include "models.hpp"
#include <mutex>
namespace {
thread_local std::string createError;
std::mutex execution;
}
struct LadioAnalyzer {
    ladio::mobile::Models models;
    std::atomic<bool> cancelled{false}, running{false};
    std::string error;
    explicit LadioAnalyzer(const char *root, int threads): models(root, threads) {}
};
template<class F> static int run(LadioAnalyzer *a, F fn) {
    if (!a) return 0;
    std::unique_lock lock(execution, std::try_to_lock);
    if (!lock.owns_lock()) { a->error="Another analyzer operation is active"; return 0; }
    a->cancelled.store(false); a->running.store(true); a->error.clear();
    try { fn(); a->models.release(); a->running.store(false); return 1; }
    catch (const std::exception &e) { a->error=e.what(); }
    catch (...) { a->error="Unknown analysis failure"; }
    a->models.release(); a->running.store(false); return 0;
}
extern "C" {
LadioAnalyzer *ladio_analyzer_create(const char *root, int threads) {
    try { if(!root) throw std::runtime_error("Missing model directory");
        auto *a=new LadioAnalyzer(root, threads); createError.clear(); return a;
    } catch(const std::exception &e) { createError=e.what(); return nullptr; }
    catch(...) { createError="Cannot create analyzer"; return nullptr; }
}
const char *ladio_analyzer_create_error(void) { return createError.c_str(); }
void ladio_analyzer_destroy(LadioAnalyzer *a) { delete a; }
void ladio_analyzer_set_progress(LadioAnalyzer *a,LadioProgressCallback cb,void *ctx) {
    if(a) a->models.progress=cb?std::function<void(const std::string&,int,int)>([=](const auto&s,int n,int d){cb(ctx,s.c_str(),n,d);}):nullptr;
}
int ladio_analyzer_analyze(LadioAnalyzer *a,const char *pcm,const char *id,const char *out,const char *scope) {
    return run(a,[&]{ if(!pcm||!id||!out||!scope) throw std::runtime_error("Missing analysis argument");
        auto old=std::filesystem::is_regular_file(out)?ladio::readJson(out):ladio::Json::object();
        auto path=std::filesystem::absolute(pcm).string();
        ladio::writeJson(out,ladio::mobile::analyze(path,id,a->models,[&]{return a->cancelled.load();},scope,old)); });
}
int ladio_analyzer_prepare(LadioAnalyzer *a,const char *request,const char *out) {
    return run(a,[&]{ if(!request||!out) throw std::runtime_error("Missing preparation argument");
        ladio::writeJson(out,ladio::mobile::prepareTransition(ladio::readJson(request),a->models,[&]{return a->cancelled.load();})); });
}
void ladio_analyzer_cancel(LadioAnalyzer *a) { if(a&&a->running.load()) { a->cancelled.store(true); ladio::mobile::cancelInference(); } }
const char *ladio_analyzer_error(const LadioAnalyzer *a) { return a?a->error.c_str():"No analyzer"; }
}
