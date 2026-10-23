#include "tetr/util/runtime_directory_lock.h"
#include <cerrno>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace Util {
struct RuntimeDirectoryLock::State {
    bool held=false;
    std::string error;
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
    OVERLAPPED region{};
#else
    int file=-1;
#endif
};
RuntimeDirectoryLock::RuntimeDirectoryLock(const std::filesystem::path& file,Access access,Wait wait):state_(std::make_unique<State>()) {
    std::error_code error;
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path(),error);
    if(error) {state_->error="Cannot create the runtime data directory: "+error.message();return;}
#ifdef _WIN32
    state_->file=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(state_->file==INVALID_HANDLE_VALUE) {state_->error="Cannot open the runtime data lock.";return;}
    const DWORD flags=(access==Access::Exclusive?LOCKFILE_EXCLUSIVE_LOCK:0)|(wait==Wait::No?LOCKFILE_FAIL_IMMEDIATELY:0);
    state_->held=LockFileEx(state_->file,flags,0,1,0,&state_->region)!=0;
#else
    state_->file=::open(file.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(state_->file<0) {state_->error="Cannot open the runtime data lock.";return;}
    int result;
    do {result=::flock(state_->file,(access==Access::Exclusive?LOCK_EX:LOCK_SH)|(wait==Wait::No?LOCK_NB:0));}
    while(result<0 && errno==EINTR);
    state_->held=result==0;
#endif
    if(!state_->held)state_->error="This runtime data directory is in use by another TerMino client. Use a separate directory for another instance.";
}
RuntimeDirectoryLock::~RuntimeDirectoryLock() {
#ifdef _WIN32
    if(state_->file!=INVALID_HANDLE_VALUE) {
        if(state_->held)UnlockFileEx(state_->file,0,1,0,&state_->region);
        CloseHandle(state_->file);
    }
#else
    if(state_->file>=0) {if(state_->held)::flock(state_->file,LOCK_UN);::close(state_->file);}
#endif
}
bool RuntimeDirectoryLock::acquired() const noexcept {return state_->held;}
const std::string& RuntimeDirectoryLock::error() const noexcept {return state_->error;}
}
