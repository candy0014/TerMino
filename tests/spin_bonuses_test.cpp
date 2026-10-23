#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/user_config.h"
#include "tetr/replay/config_codec.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/rules/scoring.h"
#include "tetr/rules/spin.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <sstream>

namespace TetrTests::spin_bonuses_test {
namespace {
using Mode = Runtime::SpinBonuses;
using Cells = std::set<std::pair<int,int>>;
Rules::SpinResult detect(Piece::SpinCheck check, Mode mode, const Cells& cells,
                         bool immobile = false, bool rotated = true, int r = 0, int kick = 0) {
    Rules::SpinInput in;
    in.spinChecker = check;
    in.bonuses = mode;
    in.pose = {{10,5}, Runtime::Rotation::from_index(r)};
    in.lastRotation = rotated ? Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick)) : Runtime::RotationRecord{};
    in.mapWidth = 10; in.mapHeight = 20; in.immobile = immobile;
    return Rules::detect_spin(in, [&](int row,int col) { return cells.count({row,col}) != 0; });
}
void rules() {
    const Cells full{{9,4},{9,6},{11,6}}, mini{{9,4},{11,4},{11,6}};
    for (const auto& option : Runtime::kSpinBonuses) {
        const auto mode = option.value;
        const auto t = detect(Piece::SpinCheck::TCorner,mode,full);
        assert(t.spin == (mode != Mode::None));
        assert(t.mini == (mode == Mode::MiniOnly));
        const auto tm = detect(Piece::SpinCheck::TCorner,mode,mini);
        assert(tm.spin == (mode != Mode::None));
        assert(tm.mini == (mode != Mode::None && mode != Mode::Stupid));
        const auto upgraded = detect(Piece::SpinCheck::TCorner,mode,mini,false,true,0,4);
        assert(upgraded.mini == (mode == Mode::MiniOnly));
        const auto trappedT = detect(Piece::SpinCheck::TCorner,mode,{},true);
        const bool fallback = mode == Mode::TSpinsPlus || mode == Mode::AllSpinPlus ||
            mode == Mode::AllMiniPlus || mode == Mode::MiniOnly || mode == Mode::Stupid;
        assert(trappedT.spin == fallback);
        assert(trappedT.mini == (fallback && mode != Mode::Stupid));
        for (const auto check : {Piece::SpinCheck::I,Piece::SpinCheck::J,Piece::SpinCheck::L,
                Piece::SpinCheck::O,Piece::SpinCheck::S,Piece::SpinCheck::Z,Piece::SpinCheck::None}) {
            const auto trapped = detect(check,mode,{},true);
            const bool eligible = mode != Mode::None && mode != Mode::TSpins &&
                mode != Mode::TSpinsPlus && mode != Mode::Handheld;
            assert(trapped.spin == eligible);
            assert(trapped.mini == (eligible && (mode == Mode::AllMini || mode == Mode::AllMiniPlus || mode == Mode::MiniOnly)));
            assert(!detect(check,mode,{},true,false).spin);
        }
        assert(!detect(Piece::SpinCheck::TCorner,mode,full,true,false).spin);
    }
    assert(detect(Piece::SpinCheck::J,Mode::Handheld,{{9,5},{9,6},{11,6}}).spin);
    assert(!detect(Piece::SpinCheck::J,Mode::Handheld,{{9,5},{9,6}}).spin);
    assert(detect(Piece::SpinCheck::L,Mode::Handheld,{{9,4},{9,5},{11,6}}).spin);
    assert(detect(Piece::SpinCheck::S,Mode::Handheld,{{9,4},{9,7},{10,6}}).spin);
    assert(detect(Piece::SpinCheck::Z,Mode::Handheld,{{9,3},{9,6},{10,7}}).spin);
    Rules::SpinInput ars;
    ars.bonuses=Mode::Handheld; ars.rotationSystem=Runtime::RotationSystem::Ars;
    ars.pose={{10,5},Runtime::Rotation::from_index(0)};
    ars.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(0));
    ars.mapWidth=10; ars.mapHeight=20;
    const auto arsSpin=[&](Piece::SpinCheck type, int rotation, const Cells& cells) {
        ars.spinChecker=type; ars.pose.rotation=Runtime::Rotation::from_index(rotation);
        return Rules::detect_spin(ars,[&](int y,int x){return cells.count({y,x})!=0;});
    };
    assert(arsSpin(Piece::SpinCheck::ArsTCorner,0,{{8,4},{10,4},{10,6}}).spin);
    assert(!arsSpin(Piece::SpinCheck::ArsTCorner,0,{{8,4},{10,4},{10,6}}).mini);
    assert(arsSpin(Piece::SpinCheck::ArsTCorner,0,{{8,4},{8,6},{10,4}}).mini);
    assert(arsSpin(Piece::SpinCheck::J,0,{{8,4},{8,6},{10,5}}).spin);
    assert(arsSpin(Piece::SpinCheck::L,0,{{8,4},{8,6},{10,6}}).spin);
    assert(arsSpin(Piece::SpinCheck::S,1,{{7,4},{8,5},{10,4}}).spin);
    assert(arsSpin(Piece::SpinCheck::Z,1,{{8,5},{7,6},{11,5}}).spin);
    for (int system = 0; system < 3; ++system) {
        const Piece::SpinCheck expected[]{Piece::SpinCheck::I,Piece::SpinCheck::J,Piece::SpinCheck::L,
            Piece::SpinCheck::O,Piece::SpinCheck::S, system == 2 ? Piece::SpinCheck::ArsTCorner : Piece::SpinCheck::TCorner,Piece::SpinCheck::Z};
        for (int i=0;i<7;++i) assert(Piece::metadata(Piece::Type::from_index(system*7+i)).spinCheck == expected[i]);
    }
    for (std::size_t i=21;i<173;++i) assert(Piece::catalog()[i].metadata.spinCheck == Piece::SpinCheck::None);
    Rules::SpinInput in; in.bonuses=Mode::Stupid; in.grounded=false;
    in.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(0));
    assert(!Rules::detect_spin(in,[](int,int){return false;}).spin);

    Rules::AttackInput attack;
    attack.linesCleared=2; attack.spin=true; attack.piece={1,Runtime::RotationSystem::SrsPlus};
    attack.spinBonuses=Mode::Handheld; attack.combo=1; attack.b2b=1;
    assert(Rules::calculate_attack(attack).attack==2);
    attack.b2b=2; attack.combo=3; attack.b2bTransition=Rules::B2BTransition::Increment;
    assert(Rules::calculate_attack(attack).attack==4); // floor((4/2 + 1)*1.5)
    attack.piece.familyIndex=5;
    assert(Rules::calculate_attack(attack).attack==7);
    attack.piece.familyIndex=1; attack.spin=false;
    assert(Rules::calculate_attack(attack).attack==3); // ordinary clear is not halved
    Rules::ScoreInput score; score.rewardAllSpinPieces=true; score.spin=true; score.pieceFamilyIndex=1;
    const int fullScore[]{400,800,1200,1600,2600,3200};
    for (int lines=0;lines<=5;++lines) {
        score.linesCleared=lines; score.mini=false;
        assert(Rules::calculate_placement_score(score)==fullScore[lines]);
        score.mini=true; assert(Rules::calculate_placement_score(score)==(100<<lines));
    }
}
void configs() {
    UserConfig::Settings user;
    assert(user.spinBonuses==5);
    const auto path=(std::filesystem::temp_directory_path()/"termino_spin_bonuses_test.ini").string();
    user.persistencePath=path;
    const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::SpinBonuses);
    assert(choices.labels.size()==10 && choices.values.size()==10);
    for (int i=0;i<10;++i) {
        user.spinBonuses=i;
        const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        assert(config.spinBonuses==Runtime::kSpinBonuses[i].value);
        assert(choices.labels[i]==Runtime::kSpinBonuses[i].label);
        assert(UserConfig::save(user));
        UserConfig::Settings restored; UserConfig::load(restored,path);
        assert(restored.spinBonuses==i);
        Runtime::RuntimeConfig decoded; Replay::ConfigPrefixResult result;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,result));
        assert(decoded.spinBonuses==config.spinBonuses && !decoded.legacySpinScoring);
    }
    Runtime::RuntimeConfig decoded; Replay::ConfigPrefixResult result;
    assert(Replay::decode_config_prefix("{{RotationSystem,0}}",decoded,result));
    assert(decoded.spinBonuses==Mode::AllMiniPlus && decoded.legacySpinScoring);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(decoded)+"}",decoded,result));
    assert(decoded.legacySpinScoring);
    user.spinBonuses=9; user.qpAllSpin=1;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).spinBonuses==Mode::AllSpinPlus);
    std::filesystem::remove(path);
}
// Static boards can be compared with the official engine by injecting the
// preceding orientation and issuing one zero-offset clockwise rotation.
void fixtures() {
    std::mt19937 random(781203);
    std::cout << '['; bool first=true;
    for (int kind=0;kind<7;++kind) for (int r=0;r<4;++r) for (int sample=0;sample<24;++sample) {
        const auto& entry=Piece::catalog_entry(Piece::Type::from_index(kind));
        const auto& shape=entry.shapes[r];
        Cells cells, pieceCells;
        for (const auto& p:shape) pieceCells.emplace(10+p[0],5+p[1]);
        for (int y=7;y<=13;++y) for (int x=2;x<=8;++x) {
            if (!pieceCells.count({y,x}) && (sample==0 || (sample!=1 && random()%4==0))) cells.emplace(y,x);
        }
        const auto legal=[&](int dy,int dx) {
            for (const auto& p:pieceCells) if(cells.count({p.first+dy,p.second+dx})) return false;
            return true;
        };
        const bool grounded=!legal(1,0), immobile=grounded && !legal(-1,0) && !legal(0,1) && !legal(0,-1);
        if(!first) std::cout << ','; first=false;
        std::cout << "{\"type\":\"" << "ijlostz"[kind] << "\",\"r\":" << r << ",\"cells\":[";
        bool firstCell=true;
        for(const auto& p:cells) { if(!firstCell)std::cout<<','; firstCell=false; std::cout<<'['<<p.second<<','<<p.first+20<<']'; }
        std::cout << "],\"results\":[";
        for(int mode=0;mode<10;++mode) {
            Rules::SpinInput in; in.spinChecker=entry.metadata.spinCheck; in.bonuses=Runtime::kSpinBonuses[mode].value;
            in.pose={{10,5},Runtime::Rotation::from_index(r)};
            in.lastRotation=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(0));
            in.mapWidth=10; in.mapHeight=20; in.immobile=immobile; in.grounded=grounded;
            const auto spin=Rules::detect_spin(in,[&](int y,int x){return cells.count({y,x})!=0;});
            if(mode)std::cout<<',';
            std::cout << (spin.spin ? spin.mini ? 2 : 1 : 0);
        }
        std::cout << "]}";
    }
    std::cout << "]\n";
}
}
int run(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--fixtures") { fixtures(); return 0; }
    rules(); configs();
    std::cout << "Spin bonus rules, piece markers, attacks, scores and config round trips passed\n";
    return 0;
}
} // namespace TetrTests::spin_bonuses_test
