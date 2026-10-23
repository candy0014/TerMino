#ifndef TETR_UTIL_WORKER_THREAD_H
#define TETR_UTIL_WORKER_THREAD_H
#include <functional>
#include <memory>

namespace Util {
// Gameplay passes complete playfields by value. Android's small default
// pthread stack cannot hold the same call paths used by the desktop clients.
class WorkerThread {
public:
    explicit WorkerThread(std::function<void()> work);
    ~WorkerThread();
    WorkerThread(const WorkerThread&)=delete;
    WorkerThread& operator=(const WorkerThread&)=delete;
    bool joinable() const;
    void join();
    void detach();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
#endif
