#include "tetr/util/worker_thread.h"
#include <system_error>
#include <thread>
#ifdef __ANDROID__
#include <pthread.h>
#endif

namespace Util {
struct WorkerThread::State {
#ifdef __ANDROID__
    pthread_t thread{};
    bool active=false;
    explicit State(std::function<void()> work) {
        auto entry=std::make_unique<std::function<void()>>(std::move(work));
        pthread_attr_t attributes;
        int error=pthread_attr_init(&attributes);
        if(error)throw std::system_error(error,std::generic_category(),"pthread_attr_init");
        error=pthread_attr_setstacksize(&attributes,16*1024*1024);
        if(!error)error=pthread_create(&thread,&attributes,[](void* pointer)->void* {
            std::unique_ptr<std::function<void()>> job(static_cast<std::function<void()>*>(pointer));
            (*job)();return nullptr;
        },entry.get());
        pthread_attr_destroy(&attributes);
        if(error)throw std::system_error(error,std::generic_category(),"create gameplay worker");
        entry.release();active=true;
    }
#else
    std::thread thread;
    explicit State(std::function<void()> work):thread(std::move(work)) {}
#endif
};
WorkerThread::WorkerThread(std::function<void()> work):state_(std::make_unique<State>(std::move(work))) {}
WorkerThread::~WorkerThread() {if(joinable())std::terminate();}
bool WorkerThread::joinable() const {
#ifdef __ANDROID__
    return state_->active;
#else
    return state_->thread.joinable();
#endif
}
void WorkerThread::join() {
#ifdef __ANDROID__
    const int error=pthread_join(state_->thread,nullptr);
    if(error)throw std::system_error(error,std::generic_category(),"join gameplay worker");
    state_->active=false;
#else
    state_->thread.join();
#endif
}
void WorkerThread::detach() {
#ifdef __ANDROID__
    const int error=pthread_detach(state_->thread);
    if(error)throw std::system_error(error,std::generic_category(),"detach gameplay worker");
    state_->active=false;
#else
    state_->thread.detach();
#endif
}
}
