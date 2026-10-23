#include "tetr/replay/recent.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/replay_hash.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
namespace TetrTests::recent_replays_test {
namespace {
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int run(int argc,char** argv) {
    namespace fs=std::filesystem;
    const auto previous=fs::current_path(),directory=fs::temp_directory_path()/("termino_recent_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);fs::current_path(directory);
    std::vector<fs::path> paths;
    const std::string data="{{Model,0}}{end_all,1.000000}";
    for(int i=0;i<35;++i) {
        const auto path=Replay::save_recent(data,std::to_string(i));assert(path);paths.push_back(*path);
        assert(path->parent_path()==Replay::replay_directory()/"recent");
        assert(read(*path)==data+Replay::replay_hash_suffix(data));
        if(i==0) {fs::copy_file(*path,"explicit.txt");assert(Replay::mark_explicitly_saved(*path,"explicit.txt"));}
        if(i==2)std::ofstream(*path)<<"modified outside the automatic cache";
    }
    auto entries=Replay::recent_replays();assert(entries.size()==30 && entries.front().label=="34" && entries.back().label=="5");
    assert(fs::exists(paths[0]) && fs::exists("explicit.txt"));
    assert(!fs::exists(paths[1]) && !fs::exists(paths[3]) && !fs::exists(paths[4]));
    assert(fs::exists(paths[2])); // Never prune externally modified content.
    fs::remove(entries[3].path);assert(Replay::recent_replays().size()==29);
    std::vector<std::future<void>> writers;
    for(int i=0;i<4;++i)writers.push_back(std::async(std::launch::async,[&,i]{assert(Replay::save_recent(data,"parallel-"+std::to_string(i)));}));
    for(auto& done:writers)done.get();
    entries=Replay::recent_replays();
    for(int i=0;i<4;++i) {bool found=false;for(const auto& e:entries)found=found || e.label=="parallel-"+std::to_string(i);assert(found);}
    const auto index=Replay::replay_directory()/"recent"/"index.dat";const auto original=read(index);
    std::ofstream(index)<<"broken index";
    assert(!Replay::save_recent(data,"must not destroy an unreadable index"));
    assert(read(index)=="broken index");for(const auto& e:entries)assert(fs::exists(e.path));
    std::ofstream(index)<<original;
    assert(read("explicit.txt")==data+Replay::replay_hash_suffix(data));

    // A Windows-written v1 index must remain usable on Linux and upgrade on
    // the next save, without touching any unrelated recording.
    fs::create_directory("windows-index");fs::current_path("windows-index");
    fs::create_directories(Replay::replay_directory()/"recent");
    const auto legacyPath=Replay::replay_directory()/"recent"/"auto-123-456-0.txt";
    const auto content=data+Replay::replay_hash_suffix(data);
    {std::ofstream file(legacyPath,std::ios::binary);file<<content;}
    const auto legacyIndex=Replay::replay_directory()/"recent"/"index.dat";
    {std::ofstream file(legacyIndex,std::ios::binary);
        file<<"TETR_RECENT_1\r\n\"auto-123-456-0.txt\" \"Windows round\" 0 \""<<Replay::replay_hash_suffix(content)<<"\"\r\n";}
    entries=Replay::recent_replays();assert(entries.size()==1 && entries.front().path==legacyPath);
    assert(Replay::save_recent(data,"Linux round"));
    entries=Replay::recent_replays();assert(entries.size()==2 && entries.back().label=="Windows round");
    assert(read(legacyIndex).rfind("TETR_RECENT_2",0)==0);
    fs::current_path(directory);

    fs::create_directory("renames");fs::current_path("renames");
    const auto cached=Replay::save_recent(data,"named round");assert(cached);
    const auto named=Replay::replay_directory()/fs::u8path("结束后命名.txt");
    fs::copy_file(*cached,named);
    assert(Replay::rename_recent(*cached,named));
    assert(!fs::exists(*cached) && read(named)==content);
    entries=Replay::recent_replays();assert(entries.size()==1 && entries.front().path==named);
    assert(entries.front().label.find("结束后命名.txt")!=std::string::npos);
    // Playback Save As is an extra file, never a rename or cache protection.
    const auto extra=Replay::replay_directory()/"playback-copy.txt";fs::copy_file(named,extra);
    assert(Replay::recent_replays().front().path==named && fs::exists(named));
    for(int i=0;i<30;++i)assert(Replay::save_recent(data,"next-"+std::to_string(i)));
    assert(!fs::exists(named) && fs::exists(extra) && Replay::recent_replays().size()==30);
    entries=Replay::recent_replays();fs::remove(entries.front().path);
    assert(Replay::recent_replays().size()==29);
    const auto another=Replay::save_recent(data,"failed rename");assert(another);
    assert(!Replay::rename_recent(*another,"missing.txt") && fs::exists(*another));
    const auto overwrite=Replay::save_recent(data,"overwrite");assert(overwrite);
    assert(Replay::rename_recent(*overwrite,*another));
    entries=Replay::recent_replays();
    assert(std::count_if(entries.begin(),entries.end(),[&](const auto& e){return e.path==*another;})==1);
    assert(!fs::exists(*overwrite) && fs::exists(*another));

    fs::current_path(directory);fs::create_directory("deletion");fs::current_path("deletion");
    const auto automatic=Replay::save_recent(data,"delete automatic");assert(automatic);
    const auto renamed=Replay::save_recent(data,"delete renamed");assert(renamed);
    const auto namedDelete=Replay::replay_directory()/fs::u8path("待删除回放.txt");
    fs::copy_file(*renamed,namedDelete);assert(Replay::rename_recent(*renamed,namedDelete));
    const auto savedCopy=Replay::replay_directory()/"extra-copy.txt";fs::copy_file(namedDelete,savedCopy);
    assert(!Replay::delete_recent(savedCopy) && fs::exists(savedCopy));
    assert(Replay::delete_recent(namedDelete) && !fs::exists(namedDelete));
    assert(fs::exists(*automatic) && fs::exists(savedCopy) && Replay::recent_replays().size()==1);
    assert(Replay::delete_recent(*automatic) && !fs::exists(*automatic) && Replay::recent_replays().empty());
    const auto stale=Replay::save_recent(data,"missing round");assert(stale);fs::remove(*stale);
    assert(Replay::delete_recent(*stale) && Replay::recent_replays().empty());

    // Explicit clearing includes protected/modified indexed files, but never
    // the extra Save As copy or an unrelated recording in the same directory.
    const auto protectedRound=Replay::save_recent(data,"protected");assert(protectedRound);
    assert(Replay::mark_explicitly_saved(*protectedRound,*protectedRound));
    const auto modified=Replay::save_recent(data,"modified");assert(modified);std::ofstream(*modified)<<"replaced content";
    const auto blocked=Replay::save_recent(data,"blocked");assert(blocked);
    fs::remove(*blocked);fs::create_directory(*blocked);std::ofstream(*blocked/"keep.txt")<<"must survive";
    const auto unrelated=Replay::replay_directory()/"unrelated.txt";std::ofstream(unrelated)<<"unindexed";
    assert(!Replay::clear_recent());
    assert(!fs::exists(*protectedRound) && !fs::exists(*modified));
    assert(read(*blocked/"keep.txt")=="must survive" && fs::exists(savedCopy) && read(unrelated)=="unindexed");
    assert(!Replay::delete_recent(*blocked));
    // A failed entry stays indexed so deletion can be retried after recovery.
    fs::remove_all(*blocked);std::ofstream(*blocked)<<content;
    entries=Replay::recent_replays();assert(entries.size()==1 && entries.front().path==*blocked);
    assert(Replay::clear_recent() && !fs::exists(*blocked) && Replay::recent_replays().empty());
    assert(Replay::clear_recent() && read(savedCopy)==content && read(unrelated)=="unindexed");
    const auto corruptRound=Replay::save_recent(data,"corrupt index");assert(corruptRound);
    const auto deletionIndex=Replay::replay_directory()/"recent"/"index.dat";
    const auto validIndex=read(deletionIndex);std::ofstream(deletionIndex)<<"broken index";
    assert(!Replay::delete_recent(*corruptRound) && !Replay::clear_recent());
    assert(fs::exists(*corruptRound) && read(deletionIndex)=="broken index");
    std::ofstream(deletionIndex)<<validIndex;
    assert(Replay::clear_recent() && !fs::exists(*corruptRound) && fs::exists(savedCopy));
    fs::current_path(previous);fs::remove_all(directory);
    std::cerr<<"recent replay retention, CRLF migration, renaming, extra copies, deletion/clearing, failure recovery, concurrent writers and corrupt-index preservation passed\n";
    return 0;
}
} // namespace TetrTests::recent_replays_test
