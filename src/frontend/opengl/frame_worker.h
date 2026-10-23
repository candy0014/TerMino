#ifndef TETR_OPENGL_FRAME_WORKER_H
#define TETR_OPENGL_FRAME_WORKER_H
#include "interaction.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Graphical {
class Renderer;
struct FrameRequest {
    int width=0,height=0;
    bool presentable=false,textActive=false,fullWindow=false,touchControls=false;
    float contentScale=1;
    float minimumTextSize=0;
    std::string composition;
    std::optional<std::array<float,2>> pointer;
    std::vector<MenuScroll> menuScroll;
    std::function<void(Renderer&)> overlay;
    std::function<bool(Renderer&,const Frontend::UiFrame&)> surface;
};
struct FrameFeedback {
    Interaction interaction;
    int width=0,height=0,frames=0;
};
class FrameWorker {
public:
    struct Callbacks {
        std::function<void()> initialize,shutdown;
        std::function<Interaction(const FrameRequest&,bool)> draw;
    };
    FrameWorker(Callbacks callbacks,FrameRequest initial,int rate,int limit=0,bool captureOnStop=false)
        :request_(std::move(initial)),worker_([this,callbacks=std::move(callbacks),rate,limit,captureOnStop] {
            try {
                if(callbacks.initialize)callbacks.initialize();
                int frames=0;
                for(;;) {
                    const auto started=std::chrono::steady_clock::now();
                    FrameRequest request;bool stopping;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);request=request_;stopping=stop_;
                        request.menuScroll=std::move(menuScroll_);menuScroll_.clear();
                    }
                    if(stopping && !captureOnStop)break;
                    const bool last=stopping || (limit>0 && frames+1>=limit);
                    if(request.width>0 && request.height>0) {
                        auto next=std::make_shared<FrameFeedback>();
                        next->interaction=callbacks.draw(request,last);
                        next->width=request.width;next->height=request.height;next->frames=++frames;
                        {std::lock_guard<std::mutex> lock(mutex_);feedback_=std::move(next);}
                    }
                    if(last)break;
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait_until(lock,started+std::chrono::microseconds(1000000/std::max(1,rate)),[&] {return stop_;});
                }
            } catch(...) {std::lock_guard<std::mutex> lock(mutex_);error_=std::current_exception();}
            try {if(callbacks.shutdown)callbacks.shutdown();}
            catch(...) {std::lock_guard<std::mutex> lock(mutex_);if(!error_)error_=std::current_exception();}
            std::lock_guard<std::mutex> lock(mutex_);finished_=true;
        }) {}
    ~FrameWorker() {stop();}
    FrameWorker(const FrameWorker&)=delete;
    FrameWorker& operator=(const FrameWorker&)=delete;
    void submit(FrameRequest request) {std::lock_guard<std::mutex> lock(mutex_);request_=std::move(request);}
    void scroll(MenuScroll movement) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& pending:menuScroll_)if(pending.page==movement.page && pending.key==movement.key) {
            pending.rows+=movement.rows;return;
        }
        menuScroll_.push_back(std::move(movement));
    }
    std::shared_ptr<const FrameFeedback> feedback() const {
        std::lock_guard<std::mutex> lock(mutex_);if(error_)std::rethrow_exception(error_);return feedback_;
    }
    bool finished() const {std::lock_guard<std::mutex> lock(mutex_);if(error_)std::rethrow_exception(error_);return finished_;}
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;}
        changed_.notify_all();
        if(worker_.joinable())worker_.join();
    }
private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    FrameRequest request_;
    std::vector<MenuScroll> menuScroll_;
    std::shared_ptr<const FrameFeedback> feedback_;
    std::exception_ptr error_;
    bool stop_=false,finished_=false;
    std::thread worker_;
};
}
#endif
