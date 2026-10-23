#include "tetr/runtime/piece.h"
#include "tetr/runtime/map_definition.h"
#include "tetr/runtime/runtime_config.h"

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace TetrTests::piece_catalog_test {
namespace {
void fixtures() {
    std::cout<<'[';bool first=true;
    for(const auto& entry:Piece::catalog()) {
        if(!Piece::event_rotation_system(entry.metadata.rotationSystem) || !entry.metadata.tetrioRotation)continue;
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<entry.type.index()<<",\"system\":"<<static_cast<int>(entry.metadata.rotationSystem)
            <<",\"family\":"<<entry.metadata.familyIndex<<",\"name\":\""<<entry.metadata.name<<"\",\"shapes\":[";
        for(int r=0;r<4;++r) {
            if(r)std::cout<<',';std::cout<<'[';bool cellFirst=true;
            for(auto cell:entry.shapes[r]) {if(!cellFirst)std::cout<<',';cellFirst=false;std::cout<<'['<<cell[0]<<','<<cell[1]<<']';}
            std::cout<<']';
        }
        std::cout<<"],\"kicks\":[";
        for(int r=0;r<12;++r) {
            if(r)std::cout<<',';std::cout<<'[';bool kickFirst=true;
            for(auto kick:entry.kickTests[r]) {if(!kickFirst)std::cout<<',';kickFirst=false;std::cout<<'['<<kick[0]<<','<<kick[1]<<']';}
            std::cout<<']';
        }
        std::cout<<"],\"spawns\":[";bool spawnFirst=true;
        for(int width:{4,9,10,11,20,100}) {
            const auto p=Piece::spawn_pose(width,entry.type);
            if(!spawnFirst)std::cout<<',';spawnFirst=false;
            std::cout<<'['<<width<<','<<p.position.row<<','<<p.position.col<<','<<p.rotation.index()<<']';
        }
        std::cout<<"]}";
    }
    std::cout<<"]\n";
}
}
int run(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--fixtures") {fixtures();return 0;}
    const auto& catalog = Piece::catalog();
    assert(catalog.size() == 377);
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto& entry = catalog[index];
        assert(entry.type.index() == static_cast<int>(index));
        assert(Piece::metadata(entry.type).name == entry.metadata.name);
        assert(Piece::get_definition(entry.type).color_id() == entry.metadata.colorId);
        assert(entry.metadata.nameWidth >= 0);
        assert(entry.metadata.displayHeight >= 0);
        assert(entry.metadata.displayWidth >= 0);
    }

    const int expectedSizes[] = {7, 7, 7, 18, 4, 60, 77};
    for (int system = 0; system < 7; ++system) {
        const std::vector<Piece::Type> types = Piece::types_for_rotation_system(
            Runtime::rotation_system_from_index(system));
        assert(static_cast<int>(types.size()) == expectedSizes[system]);
        for (Piece::Type type : types) {
            if (system == 6 && type.index() < 7) continue;
            assert(static_cast<int>(Piece::metadata(type).rotationSystem) == system);
        }
    }
    std::uint64_t legacyHash=14695981039346656037ull;
    const auto hash=[&](int value){legacyHash^=static_cast<std::uint64_t>(value+128);legacyHash*=1099511628211ull;};
    for(int id=0;id<229;++id)for(const auto& shape:catalog[id].shapes) {
        hash(static_cast<int>(shape.size()));for(const auto& cell:shape){hash(cell[0]);hash(cell[1]);}
    }
    assert(legacyHash==13527463804434191194ull);
    for(auto system:{Runtime::RotationSystem::Pentomino,Runtime::RotationSystem::Hexomino,Runtime::RotationSystem::AprilFools2026}) {
        const auto modern=Piece::types_for_rotation_system(system);
        const auto legacy=Piece::types_for_rotation_system(system,false,10,true);
        assert(modern.size()==legacy.size());
        for(std::size_t i=0;i<modern.size();++i) {
            if(system==Runtime::RotationSystem::AprilFools2026 && i<7)continue;
            assert(modern[i].index()>=229 && legacy[i].index()<173);
            assert(Piece::metadata(modern[i]).familyIndex==Piece::metadata(legacy[i]).familyIndex);
        }
    }
    assert(Piece::april_fools_piece(51).index()>=229 && Piece::april_fools_piece(51,true).index()==154);
    const auto o8=Piece::get_definition(Piece::april_fools_piece(51));
    const auto oldO8=Piece::get_definition(Piece::april_fools_piece(51,true));
    auto expected=oldO8.cells(Runtime::Rotation{});for(auto& cell:expected)++cell[1];
    auto actual=o8.cells(Runtime::Rotation{});std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
    assert(actual==expected);
    Runtime::MapDefinition map;map.generator=Runtime::MapGenerator::CustomSequence;
    map.sequence={Piece::Type::from_index(22),Piece::Type::from_index(51),Piece::Type::from_index(154)};
    Runtime::RuntimeConfig current;Runtime::apply_map_config(current,map);
    for(std::size_t i=0;i<map.sequence.size();++i)
        assert(current.initialMap->sequence[i]==Piece::canonical_event_piece(map.sequence[i]));
    assert(map.sequence.front().index()==22); // Input map/file is never rewritten.
    Runtime::RuntimeConfig historical;historical.legacyEventGeometry=true;Runtime::apply_map_config(historical,map);
    assert(historical.initialMap->sequence==map.sequence);
    return 0;
}
} // namespace TetrTests::piece_catalog_test
