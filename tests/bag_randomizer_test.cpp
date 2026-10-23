#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/replay/config_codec.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>

namespace TetrTests::bag_randomizer_test {
namespace {
using Mode=Runtime::BagRandomizer;
struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Fixture(Mode mode, unsigned seed, bool interoperable=false, Runtime::RotationSystem system=Runtime::RotationSystem::SrsPlus) {
        context.config.bagRandomizer=mode;
        session.player.pieces.allowedPieces=Piece::types_for_rotation_system(system);
        session.player.pieces.random.seed(seed);
        if(interoperable) session.player.pieces.tetrioRandom.emplace(seed);
        auto& field=session.player.gameplay.playfield;
        field.width=10;field.visibleHeight=20;field.nextCount=5;
    }
    std::vector<int> bag() {
        auto& p=session.player;
        p.pieces.bag.clear();Engine::add_bag(context,p);
        std::vector<int> result;
        for(std::size_t i=0;i<p.pieces.bag.size();++i) {
            assert(p.pieces.bag[i].startsNewBag==(i==0));
            result.push_back(p.pieces.bag[i].type.index());
        }
        return result;
    }
};
void contents() {
    for(const bool interoperable:{false,true}) for(const auto& info:Runtime::kBagRandomizers) {
        Fixture f(info.value,12345,interoperable);
        std::map<int,int> firstFour;
        for(int index=0;index<16;++index) {
            const auto bag=f.bag();std::map<int,int> counts;
            for(const auto type:bag) {++counts[type];if(index<4)++firstFour[type];}
            const auto size=bag.size();
            if(info.value==Mode::SevenBag) {assert(size==7 && counts.size()==7);}
            if(info.value==Mode::FourteenBag) {assert(size==14 && counts.size()==7);for(const auto& entry:counts)assert(entry.second==2);}
            if(info.value==Mode::SevenPlusOne || info.value==Mode::SevenPlusTwo) {
                assert(size==(info.value==Mode::SevenPlusOne ? 8 : 9));assert(counts.size()==7);
            }
            if(info.value==Mode::SevenPlusX) {
                const int sizes[]{10,9,8,8};assert(size==static_cast<std::size_t>(index<4 ? sizes[index] : 7));assert(counts.size()==7);
            }
            if(info.value==Mode::Pairs) {assert(size==6 && counts.size()==2);for(const auto& entry:counts)assert(entry.second==3);}
            if(info.value==Mode::Classic || info.value==Mode::TotalMayhem) assert(size==7);
            if(info.value==Mode::Drought){assert(size==6);for(auto type:bag)assert(Piece::metadata(Piece::Type::from_index(type)).familyIndex!=0);}
            if(info.value==Mode::Adversarial)assert(size==1);
        }
        if(info.value==Mode::SevenPlusX) for(const auto& entry:firstFour) assert(entry.second==5);
    }
    // Event catalogs use the same policies over their configured piece set.
    for(const auto system:{Runtime::RotationSystem::M123,Runtime::RotationSystem::Pentomino,Runtime::RotationSystem::Hexomino}) {
        for(const auto mode:{Mode::SevenBag,Mode::FourteenBag,Mode::SevenPlusOne,Mode::SevenPlusTwo,Mode::SevenPlusX,Mode::Pairs}) {
            Fixture f(mode,19,false,system);const auto bag=f.bag();
            const auto count=f.session.player.pieces.allowedPieces.size();
            assert(bag.size()==(mode==Mode::FourteenBag ? count*2 : mode==Mode::SevenPlusOne ? count+1 :
                mode==Mode::SevenPlusTwo ? count+2 : mode==Mode::SevenPlusX ? count+3 : mode==Mode::Pairs ? 6 : count));
            for(const auto type:bag) assert(std::find(f.session.player.pieces.allowedPieces.begin(),f.session.player.pieces.allowedPieces.end(),Piece::Type::from_index(type))!=f.session.player.pieces.allowedPieces.end());
        }
    }
    for(const bool interoperable:{false,true}) {
        auto repeats=[&](Mode mode) {
            Fixture f(mode,331,interoperable);int previous=-1,result=0;
            for(int i=0;i<200;++i) for(const auto type:f.bag()) {result+=previous==type;previous=type;}
            return result;
        };
        const auto classic=repeats(Mode::Classic), total=repeats(Mode::TotalMayhem);
        assert(classic>0 && classic<total); // Protection permits repeats after one reroll.
    }
    // Existing quick-play event additions still compose with the new bags.
    Fixture fool(Mode::FourteenBag,41);fool.context.config.quickPlay.fool=Runtime::FoolMode::AprilFools2026;
    assert(fool.bag().size()==14);assert(fool.bag().size()==16);
    Fixture penalty(Mode::SevenPlusTwo,41);penalty.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    penalty.context.config.quickPlay.continuousCancellation=true;penalty.session.quickPlay.progress.cancellation=40;
    const auto bag=penalty.bag();assert(bag.size()==13);
    assert(std::count(bag.begin(),bag.end(),21)==1);assert(penalty.session.quickPlay.progress.extraI5Issued);
}
void settings_and_replays() {
    UserConfig::Settings user;
    user.persistencePath=(std::filesystem::temp_directory_path()/"termino_bag_randomizer_test.ini").string();
    const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::BagRandomizer);
    assert(choices.labels.size()==Runtime::kBagRandomizers.size() && choices.values.size()==Runtime::kBagRandomizers.size());
    for(std::size_t i=0;i<Runtime::kBagRandomizers.size();++i) {
        const auto& info=Runtime::kBagRandomizers[i];
        assert(choices.labels[i]==info.label && choices.values[i]==static_cast<int>(info.value));
        user.randomPieces=static_cast<int>(info.value);
        const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        assert(config.bagRandomizer==info.value && !config.legacyRandomizerRules);
        assert(UserConfig::save(user));UserConfig::Settings loaded;UserConfig::load(loaded,user.persistencePath);
        assert(loaded.randomPieces==user.randomPieces);
        Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult result;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,result));
        assert(decoded.bagRandomizer==info.value && !decoded.legacyRandomizerRules);
    }
    {std::ofstream out(user.persistencePath);out<<"[GAME]\nRandom = 1\n";}
    UserConfig::Settings legacy;UserConfig::load(legacy,user.persistencePath);
    assert(Runtime::normalize_user_config(legacy,Runtime::GameMode::Zen).bagRandomizer==Mode::TotalMayhem);
    Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult result;
    assert(Replay::decode_config_prefix("{{Random,1}}",decoded,result));
    assert(decoded.bagRandomizer==Mode::TotalMayhem && decoded.legacyRandomizerRules);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(decoded)+"}",decoded,result));
    assert(decoded.legacyRandomizerRules);
    user.randomPieces=static_cast<int>(Mode::Pairs);user.qpNoHold=2;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay).bagRandomizer==Mode::TotalMayhem);
    std::filesystem::remove(user.persistencePath);
}
void restarting() {
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    for(const auto& info:Runtime::kBagRandomizers) {
        Fixture fresh(info.value,123), dirty(info.value,123);
        dirty.session.player.pieces.bagCount=123;
        dirty.session.player.pieces.randomizerState.lastGenerated=5;
        dirty.session.player.pieces.randomizerState.extras={0,0,0,0,0,0,0};
        dirty.session.player.gameplay.playfield.field.initialize(10,20);
        dirty.session.player.gameplay.playfield.field.set_cell(19,0,Runtime::BoardCell::piece(3));
        Engine::initialize_board(fresh.context,fresh.session.player);
        Engine::initialize_board(dirty.context,dirty.session.player);
        assert(fresh.session.player.pieces.bag==dirty.session.player.pieces.bag);
        assert(fresh.session.player.pieces.random==dirty.session.player.pieces.random);
        assert(fresh.session.player.pieces.randomizerState.extras==dirty.session.player.pieces.randomizerState.extras);
        assert(fresh.session.player.pieces.randomizerState.lastGenerated==dirty.session.player.pieces.randomizerState.lastGenerated);
        assert(fresh.session.player.pieces.bag.size()>5 && fresh.session.player.pieces.nextPreview.size()==5);
        assert(fresh.session.player.pieces.bagCount==(info.value==Mode::Adversarial?6:1)); // Board-dependent generation supplies one piece at a time.
    }
    std::cout.rdbuf(previous);
}
void fixtures() {
    std::cout<<'[';bool first=true;
    for(const auto& info:Runtime::kBagRandomizers) for(const unsigned seed:{1u,2u,17u,12345u,20261006u,2147483646u}) {
        if(info.value==Mode::Drought || info.value==Mode::Adversarial)continue; // Native extensions have no official TETR.IO counterpart.
        Fixture f(info.value,seed,true);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mode\":\""<<info.tetrioName<<"\",\"seed\":"<<seed<<",\"bags\":[";
        for(int index=0;index<20;++index) {
            if(index)std::cout<<',';std::cout<<'[';const auto bag=f.bag();
            for(std::size_t i=0;i<bag.size();++i) {if(i)std::cout<<',';std::cout<<'"'<<"ijlostz"[Piece::metadata(Piece::Type::from_index(bag[i])).familyIndex]<<'"';}
            std::cout<<']';
        }
        std::cout<<"]}";
    }
    std::cout<<"]\n";
}
}
int run(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--fixtures") {fixtures();return 0;}
    contents();settings_and_replays();restarting();
    std::cout<<"Bag randomizers, event catalogs, queue restart, config and legacy replay checks passed\n";
    return 0;
}
} // namespace TetrTests::bag_randomizer_test
