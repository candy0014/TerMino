#include "tetr/util/runtime_directory_lock.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <atomic>
#include <memory>
#include <thread>
namespace TetrTests::runtime_directory_lock_test {
int run(int argc,char** argv) {
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-lock-中文 "+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file=folder/"client.lock";
    {
        Util::RuntimeDirectoryLock first(file);assert(first.acquired());
        Util::RuntimeDirectoryLock conflict(file);assert(!conflict.acquired() && !conflict.error().empty());
        Util::RuntimeDirectoryLock other(folder/"other"/"client.lock");assert(other.acquired());
    }
    assert(std::filesystem::is_regular_file(file));
    {Util::RuntimeDirectoryLock reopened(file);assert(reopened.acquired());}
    using Access=Util::RuntimeDirectoryLock::Access;
    {
        Util::RuntimeDirectoryLock one(file,Access::Shared),two(file,Access::Shared);
        assert(one.acquired() && two.acquired());
        Util::RuntimeDirectoryLock terminal(file);assert(!terminal.acquired());
    }
    {
        Util::RuntimeDirectoryLock terminal(file);assert(terminal.acquired());
        Util::RuntimeDirectoryLock graphics(file,Access::Shared);assert(!graphics.acquired());
    }
    {
        auto owner=std::make_unique<Util::FileWriteLock>(folder/"settings.ini");assert(owner->acquired());
        std::atomic<bool> started{false},saved{false};
        std::thread waiting([&] {started=true;Util::FileWriteLock lock(folder/"settings.ini");assert(lock.acquired());saved=true;});
        while(!started)std::this_thread::yield();
        assert(!saved);owner.reset();waiting.join();assert(saved);
    }
    std::ofstream(folder/"regular-file")<<"retained";
    {Util::RuntimeDirectoryLock invalid(folder/"regular-file"/"client.lock");assert(!invalid.acquired());}
    std::filesystem::remove_all(folder);
    return 0;
}
} // namespace TetrTests::runtime_directory_lock_test
