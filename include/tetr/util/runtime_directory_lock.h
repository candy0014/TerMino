#ifndef TETR_UTIL_RUNTIME_DIRECTORY_LOCK_H
#define TETR_UTIL_RUNTIME_DIRECTORY_LOCK_H
#include <filesystem>
#include <memory>
#include <string>
namespace Util {
// Operating-system lock shared by the two clients. The lock file is retained;
// closing a process releases ownership, including after an abnormal exit.
class RuntimeDirectoryLock {
public:
    explicit RuntimeDirectoryLock(const std::filesystem::path& file);
    ~RuntimeDirectoryLock();
    RuntimeDirectoryLock(const RuntimeDirectoryLock&)=delete;
    RuntimeDirectoryLock& operator=(const RuntimeDirectoryLock&)=delete;
    bool acquired() const noexcept;
    const std::string& error() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
#endif
