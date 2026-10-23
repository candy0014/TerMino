#ifndef TETR_UTIL_RUNTIME_DIRECTORY_LOCK_H
#define TETR_UTIL_RUNTIME_DIRECTORY_LOCK_H
#include <filesystem>
#include <memory>
#include <string>
namespace Util {
// Operating-system lock used by the clients. The lock file is retained;
// closing a process releases ownership, including after an abnormal exit.
class RuntimeDirectoryLock {
public:
    enum class Access { Exclusive, Shared };
    enum class Wait { No, Yes };
    explicit RuntimeDirectoryLock(const std::filesystem::path& file,
        Access access=Access::Exclusive,Wait wait=Wait::No);
    ~RuntimeDirectoryLock();
    RuntimeDirectoryLock(const RuntimeDirectoryLock&)=delete;
    RuntimeDirectoryLock& operator=(const RuntimeDirectoryLock&)=delete;
    bool acquired() const noexcept;
    const std::string& error() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
// Serialize a file's complete read-modify-write operation across processes.
// Keep the sidecar lock separate from the data file that atomic saves replace.
class FileWriteLock {
public:
    explicit FileWriteLock(std::filesystem::path file)
        :lock_(file.concat(".lock"),RuntimeDirectoryLock::Access::Exclusive,RuntimeDirectoryLock::Wait::Yes) {}
    bool acquired() const noexcept {return lock_.acquired();}
private:
    RuntimeDirectoryLock lock_;
};
}
#endif
