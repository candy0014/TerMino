#include "tetr/replay/recent.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/replay_hash.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
namespace {
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int main() {
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
    fs::current_path(previous);fs::remove_all(directory);
    std::cerr<<"recent replay retention, explicit protection, missing files, concurrent writers and corrupt-index preservation passed\n";
}
