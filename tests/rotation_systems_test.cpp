#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/replay/config_codec.h"
#include "tetr/runtime/rotation_systems.h"
#include "tetr/rules/spin.h"
#include "../src/bot/bot_piece_access.h"
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>

namespace {
void configuration() {
    const auto options=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);
    assert(options.labels.size()==12); // Eight standard systems and retained event sets.
    UserConfig::Settings user;
    user.persistencePath=(std::filesystem::temp_directory_path()/"termino_rotation_systems_test.ini").string();
    for(std::size_t i=0;i<Runtime::kRotationSystems.size();++i) {
        const auto& info=Runtime::kRotationSystems[i];
        assert(options.labels[i]==info.label && options.values[i]==static_cast<int>(info.value));
        user.rotationSystem=options.values[i];
        auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        assert(config.rotationSystem==info.value && !config.legacyRotationRules);
        assert(UserConfig::save(user));
        UserConfig::Settings restored; UserConfig::load(restored,user.persistencePath);
        assert(restored.rotationSystem==user.rotationSystem);
        Runtime::RuntimeConfig decoded; Replay::ConfigPrefixResult result;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,result));
        assert(decoded.rotationSystem==info.value && !decoded.legacyRotationRules);
        const auto types=Piece::types_for_rotation_system(info.value);
        assert(types.size()==7);
        for(int family=0;family<7;++family) {
            const auto& metadata=Piece::metadata(types[family]);
            assert(metadata.familyIndex==family && metadata.rotationSystem==info.value && metadata.tetrioRotation);
            assert(types[family].index()>=173);
            const auto spawn=Engine::make_spawn_movement_state(10,types[family]);
            const auto& origin=metadata.rotationOrigins[metadata.spawnRotation];
            assert(spawn.pose.rotation.index()==metadata.spawnRotation);
            assert(spawn.pose.position.row==-2+origin[0] && spawn.pose.position.col==4+origin[1]);
            assert(Piece::preview_cells(types[family]).size()==4);
        }
    }
    Runtime::RuntimeConfig decoded; Replay::ConfigPrefixResult result;
    assert(Replay::decode_config_prefix("{{RotationSystem,2}}",decoded,result));
    assert(decoded.legacyRotationRules);
    assert(Piece::types_for_rotation_system(decoded.rotationSystem,true).front().index()==14);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(decoded)+"}",decoded,result));
    assert(decoded.legacyRotationRules);
    for(int system=0;system<3;++system) {
        const auto old=Piece::types_for_rotation_system(Runtime::rotation_system_from_index(system),true);
        assert(old.size()==7 && old.front().index()==system*7);
    }
    std::filesystem::remove(user.persistencePath);
}
void rotations(bool output) {
    std::mt19937 random(321704);
    if(output)std::cout<<'[';
    bool first=true;
    int successes=0, failures=0, kicks=0;
    for(const auto& info:Runtime::kRotationSystems) {
        const auto types=Piece::types_for_rotation_system(info.value);
        for(int family=0;family<7;++family) for(int r=0;r<4;++r) for(int turn=1;turn<4;++turn) for(int sample=0;sample<16;++sample) {
            const auto piece=Piece::get_definition(types[family]);
            const auto& shape=piece.cells(Runtime::Rotation::from_index(r));
            int minCol=0,maxCol=0,maxRow=0;
            for(const auto& p:shape) {minCol=std::min(minCol,p[1]);maxCol=std::max(maxCol,p[1]);maxRow=std::max(maxRow,p[0]);}
            const int row=std::min(19-maxRow,4+static_cast<int>(random()%16));
            const int col=std::clamp(static_cast<int>(random()%12)-1,-minCol,9-maxCol);
            std::set<std::pair<int,int>> occupied, source;
            for(const auto& p:shape) source.emplace(row+p[0],col+p[1]);
            Runtime::Playfield field; field.initialize(10,20);
            for(int y=row-4;y<=std::min(19,row+4);++y) for(int x=std::max(0,col-4);x<=std::min(9,col+4);++x) {
                if(!source.count({y,x}) && (sample==0 || (sample!=1 && random()%5==0))) {
                    field.set_cell(y,x,Runtime::BoardCell::garbage()); occupied.emplace(y,x);
                }
            }
            Piece::Pose pose{{row,col},Runtime::Rotation::from_index(r)};
            const auto original=pose;
            assert(piece.can_place(pose,field));
            Piece::KickIndex kick;
            const bool success=piece.try_rotate(pose,Runtime::Rotation::from_index((r+turn)%4),field,kick);
            if(success) {++successes; kicks+=kick.index()>0; assert(piece.can_place(pose,field));}
            else {++failures; assert(pose==original);}
            std::vector<unsigned> bits(120);
            for(const auto& p:occupied) bits[19-p.first]|=1u<<p.second;
            int bitRow=row,bitCol=col,bitKick=-1;
            const bool bitSuccess=BotPiece::bitboard_try_rotate(piece,bitRow,bitCol,r,(r+turn)%4,bits,10,20,bitKick);
            assert(bitSuccess==success && bitRow==pose.position.row && bitCol==pose.position.col);
            if(success)assert(bitKick==kick.index());
            Rules::SpinInput spinInput;
            spinInput.spinChecker=Piece::metadata(types[family]).spinCheck;
            spinInput.rotationSystem=info.value; spinInput.canonicalGeometry=true;
            spinInput.mapWidth=10; spinInput.mapHeight=20; spinInput.pose=pose;
            if(success)spinInput.lastRotation=Runtime::RotationRecord::from_kick(kick);
            spinInput.grounded=!piece.can_place(pose.translated(1,0),field);
            spinInput.immobile=spinInput.grounded && !piece.can_place(pose.translated(-1,0),field) &&
                !piece.can_place(pose.translated(0,-1),field) && !piece.can_place(pose.translated(0,1),field);
            const auto spin=Rules::detect_spin(spinInput,[&](int y,int x){return field.cell(y,x).occupied();});
            if(output) {
                if(!first)std::cout<<',';first=false;
                std::cout<<"{\"system\":\""<<info.tetrioName<<"\",\"type\":\""<<"ijlostz"[family]<<"\",\"from\":"<<r
                    <<",\"turn\":"<<turn<<",\"row\":"<<row+20<<",\"col\":"<<col<<",\"cells\":[";
                bool firstCell=true;
                for(const auto& p:occupied) {if(!firstCell)std::cout<<',';firstCell=false;std::cout<<'['<<p.second<<','<<p.first+20<<']';}
                std::cout<<"],\"success\":"<<(success?"true":"false")<<",\"result\":["<<pose.position.col<<','<<pose.position.row+20
                    <<','<<pose.rotation.index()<<','<<kick.index()<<"],\"spin\":"<<(spin.spin ? spin.mini ? 2 : 1 : 0)<<"}";
            }
        }
    }
    assert(successes>0 && failures>0 && kicks>0);
    if(output)std::cout<<"]\n";
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--fixtures") {rotations(true);return 0;}
    configuration(); rotations(false);
    std::cout<<"Rotation profiles, spawn orientations, bitboard paths, configuration and replay compatibility passed\n";
}
