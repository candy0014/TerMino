#include "tetr/util/runtime_directory_lock.h"
#include <cassert>
#include <chrono>
#include <fstream>
int main() {
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
    std::ofstream(folder/"regular-file")<<"retained";
    {Util::RuntimeDirectoryLock invalid(folder/"regular-file"/"client.lock");assert(!invalid.acquired());}
    std::filesystem::remove_all(folder);
}
