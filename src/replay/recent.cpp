#include "tetr/util/file_path.h"
#include "tetr/replay/recent.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/multiplayer_codec.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace Replay {
namespace {
auto directory(){return replay_directory()/"recent";}
struct Entry {std::string name,label,digest;bool saved=false;};
class Lock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;OVERLAPPED overlapped{};
#else
    int handle=-1;
#endif
    bool locked=false;
public:
    Lock() {
        std::error_code error;std::filesystem::create_directories(directory(),error);if(error)return;
        const auto path=directory()/".lock";
#ifdef _WIN32
        handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle!=INVALID_HANDLE_VALUE)locked=LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK,0,MAXDWORD,MAXDWORD,&overlapped)!=0;
#else
        handle=open(path.c_str(),O_CREAT|O_RDWR,0600);if(handle>=0)locked=flock(handle,LOCK_EX)==0;
#endif
    }
    explicit operator bool() const{return locked;}
    ~Lock(){
#ifdef _WIN32
        if(locked)UnlockFileEx(handle,0,MAXDWORD,MAXDWORD,&overlapped);if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
        if(handle>=0) {if(locked)flock(handle,LOCK_UN);close(handle);}
#endif
    }
};
std::string unique_id() {
    static std::atomic<unsigned> sequence{0};std::random_device random;
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(random())+"-"+std::to_string(sequence++);
}
bool owned(const std::string& name) {
    return name.size()>9 && name.compare(0,5,"auto-")==0 && name.substr(name.size()-4)==".txt" &&
        name.find_first_not_of("auto-0123456789.txt")==std::string::npos;
}
bool load(std::vector<Entry>& entries) {
    const auto index=directory()/"index.dat";std::error_code error;
    if(!std::filesystem::exists(index,error))return !error;
    std::ifstream file(index);std::string header;if(!std::getline(file,header))return false;
    // Indexes travel with the shared profile between Windows and Linux.
    if(!header.empty() && header.back()=='\r')header.pop_back();
    const bool legacy=header=="TETR_RECENT_1";
    if(!legacy && header!="TETR_RECENT_2")return false;
    while(file>>std::ws && !file.eof()) {
        Entry e;if(!(file>>std::quoted(e.name)>>std::quoted(e.label)>>e.saved>>std::quoted(e.digest)) ||
            e.name.empty() || (legacy && !owned(e.name)))return false;
        if(legacy)e.name=(directory()/std::filesystem::u8path(e.name)).generic_u8string();
        if(std::any_of(entries.begin(),entries.end(),[&](const auto& old){return old.name==e.name;}))return false;
        entries.push_back(std::move(e));if(entries.size()>kRecentReplayLimit)return false;
    }
    return !file.bad();
}
bool store(const std::vector<Entry>& entries) {
    const auto temporary=directory()/("index-"+unique_id()+".tmp"),index=directory()/"index.dat";
    bool written=false;
    {std::ofstream file(temporary);file<<"TETR_RECENT_2\n";
     for(const auto& e:entries)file<<std::quoted(e.name)<<' '<<std::quoted(e.label)<<' '<<e.saved<<' '<<std::quoted(e.digest)<<'\n';
     file.flush();written=static_cast<bool>(file);}
    std::error_code error;
    if(written) {
#ifdef _WIN32
        written=MoveFileExW(temporary.c_str(),index.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(temporary,index,error);written=!error;
#endif
    }
    if(!written)std::filesystem::remove(temporary,error);return written;
}
bool same(const std::filesystem::path& a,const std::filesystem::path& b) {
    if(a.empty() || b.empty())return false;
    std::error_code error;const auto first=Util::FilePath::absolute(a,error);if(error)return false;
    const auto second=Util::FilePath::absolute(b,error);if(error)return false;
    // Case aliases on Windows (and hard links) must never cause the newly
    // saved destination to be pruned as though it were a different source.
    if(first==second)return true;
    return std::filesystem::equivalent(first,second,error) && !error;
}
bool delete_file(const Entry& entry) {
    const auto path=std::filesystem::u8path(entry.name);
    if(same(path,directory()/"index.dat") || same(path,directory()/".lock"))return false;
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if(error==std::errc::no_such_file_or_directory || (!error && status.type()==std::filesystem::file_type::not_found))return true;
    // Never remove a directory if an indexed recording has been replaced.
    // Removing a symlink deletes only the link, not its target.
    if(error || (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)))return false;
    std::filesystem::remove(path,error);return !error;
}
void prune(const Entry& entry) {
    if(entry.saved)return;
    const auto path=std::filesystem::u8path(entry.name);std::error_code error;
    const auto root=std::filesystem::weakly_canonical(replay_directory(),error);if(error)return;
    const auto resolved=std::filesystem::weakly_canonical(path,error);if(error)return;
    auto component=resolved.begin();
    for(const auto& prefix:root) {if(component==resolved.end() || *component!=prefix)return;++component;}
    if(component==resolved.end())return;
    if(std::filesystem::is_symlink(path,error) || error || std::filesystem::file_size(path,error)>kMaxMultiplayerReplayBytes+17 || error)return;
    std::ifstream file(path,std::ios::binary);const std::string content((std::istreambuf_iterator<char>(file)),{});
    // A user may have replaced a cached file outside this program. Preserve it.
    if(!file.bad() && replay_hash_suffix(content)==entry.digest) {file.close();std::filesystem::remove(path,error);}
}
}
std::optional<std::filesystem::path> save_recent(const std::string& recording,const std::string& label) {
    Lock lock;if(!lock || recording.empty())return {};
    std::vector<Entry> entries;if(!load(entries))return {};
    const auto content=recording+replay_hash_suffix(recording);
    const auto path=directory()/("auto-"+unique_id()+".txt");
    Entry entry{path.generic_u8string(),label,replay_hash_suffix(content),false};
    bool written=false;{std::ofstream file(path,std::ios::binary);file<<content;file.flush();written=static_cast<bool>(file);}
    std::optional<Entry> expired;
    entries.insert(entries.begin(),entry);if(entries.size()>kRecentReplayLimit){expired=entries.back();entries.pop_back();}
    if(!written || !store(entries)) {std::error_code error;std::filesystem::remove(path,error);return {};}
    if(expired)prune(*expired);return path;
}
std::vector<RecentReplay> recent_replays() {
    std::error_code error;if(!std::filesystem::exists(directory(),error) || error)return {};
    Lock lock;if(!lock)return {};std::vector<Entry> entries;if(!load(entries))return {};
    std::vector<RecentReplay> result;
    for(const auto& e:entries) {
        const auto path=std::filesystem::u8path(e.name);
        if(std::filesystem::is_regular_file(path,error) && !error) {
            const auto label=owned(path.filename().u8string())?e.label:path.filename().u8string()+"  "+e.label;
            result.push_back({path,label});
        }
        error.clear();
    }
    return result;
}
std::optional<std::filesystem::path> replace_recent(const std::filesystem::path& source,
    const std::string& recording,const std::string& label) {
    Lock lock;if(!lock || recording.empty() || recording.size()>kMaxMultiplayerReplayBytes)return {};
    std::vector<Entry> entries;if(!load(entries))return {};
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return !e.saved && same(std::filesystem::u8path(e.name),source);});
    if(found==entries.end())return {};
    std::error_code error;if(std::filesystem::file_size(source,error)>kMaxMultiplayerReplayBytes+17 || error)return {};
    std::ifstream file(source,std::ios::binary);const std::string original((std::istreambuf_iterator<char>(file)),{});
    if(file.bad() || replay_hash_suffix(original)!=found->digest)return {};file.close();
    const auto previous=*found;const auto path=directory()/("auto-"+unique_id()+".txt");
    const auto content=recording+replay_hash_suffix(recording);
    bool written=false;{std::ofstream out(path,std::ios::binary);out.write(content.data(),content.size());out.close();written=bool(out);}
    *found=Entry{path.generic_u8string(),label,replay_hash_suffix(content),false};
    if(!written || !store(entries)){std::filesystem::remove(path,error);return {};}
    prune(previous);return path;
}
bool delete_recent(const std::filesystem::path& source) {
    Lock lock;if(!lock)return false;
    std::vector<Entry> entries;if(!load(entries))return false;
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return same(std::filesystem::u8path(e.name),source);});
    if(found==entries.end() || !delete_file(*found))return false;
    entries.erase(found);return store(entries);
}
bool clear_recent() {
    Lock lock;if(!lock)return false;
    std::vector<Entry> entries;if(!load(entries))return false;
    std::vector<Entry> remaining;
    for(const auto& entry:entries)if(!delete_file(entry))remaining.push_back(entry);
    if(remaining.size()==entries.size())return entries.empty();
    // Keep failed entries available for retry. Missing files are harmless if
    // an interrupted deletion left an old index behind.
    return store(remaining) && remaining.empty();
}
bool rename_recent(const std::filesystem::path& source,const std::filesystem::path& destination) {
    Lock lock;if(!lock)return false;
    std::vector<Entry> entries;if(!load(entries))return false;
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return same(std::filesystem::u8path(e.name),source);});
    if(found==entries.end())return false;
    if(same(source,destination))return true;
    std::error_code error;
    if(!std::filesystem::is_regular_file(destination,error) || error || std::filesystem::file_size(destination,error)>kMaxMultiplayerReplayBytes+17 || error)return false;
    std::ifstream file(destination,std::ios::binary);if(!file)return false;
    const std::string content((std::istreambuf_iterator<char>(file)),{});
    if(file.bad())return false;
    const Entry previous=*found;
    found->name=replay_setting_path(destination.generic_u8string());
    if(found->name.empty())return false;
    found->digest=replay_hash_suffix(content);found->saved=false;
    // Overwriting a filename replaces its older history entry as well.
    const auto position=static_cast<std::size_t>(found-entries.begin());
    std::vector<Entry> updated;
    for(std::size_t i=0;i<entries.size();++i)
        if(i==position || !same(std::filesystem::u8path(entries[i].name),destination))updated.push_back(entries[i]);
    if(!store(updated))return false;
    prune(previous);return true;
}
bool mark_explicitly_saved(const std::filesystem::path& source,const std::filesystem::path& destination) {
    std::error_code error;if(!std::filesystem::exists(directory(),error))return !error;
    Lock lock;if(!lock)return false;std::vector<Entry> entries;if(!load(entries))return false;
    bool changed=false;for(auto& e:entries)if(!e.saved && (same(std::filesystem::u8path(e.name),source) || same(std::filesystem::u8path(e.name),destination))) {e.saved=true;changed=true;}
    return !changed || store(entries);
}
}
