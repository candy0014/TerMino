#include "tetr/compat/legacy_encoding.h"
#include "tetr/runtime/playfield.h"
#include "tetr/runtime/piece.h"

#include <cassert>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

bool legacy_can_place(const Piece::Definition& piece,int row,int col,int rotation,const Runtime::Playfield& field) {
    for (const auto& det : piece.cells(Compat::rotation_from_legacy(rotation))) {
        const int testRow=row+det[0];
        const int testCol=col+det[1];
        if(testCol<0||testCol>=field.width()||testRow>=field.height()) return false;
        if(field.cell(testRow,testCol).occupied()) return false;
    }
    return true;
}

int legacy_transition(int fromRotation,int toRotation) {
    if(fromRotation==0&&toRotation==1) return 0;
    if(fromRotation==1&&toRotation==0) return 1;
    if(fromRotation==1&&toRotation==2) return 2;
    if(fromRotation==2&&toRotation==1) return 3;
    if(fromRotation==2&&toRotation==3) return 4;
    if(fromRotation==3&&toRotation==2) return 5;
    if(fromRotation==3&&toRotation==0) return 6;
    if(fromRotation==0&&toRotation==3) return 7;
    if(fromRotation==0&&toRotation==2) return 8;
    if(fromRotation==2&&toRotation==0) return 9;
    if(fromRotation==1&&toRotation==3) return 10;
    if(fromRotation==3&&toRotation==1) return 11;
    return 8;
}

bool legacy_try_rotate(const Piece::Definition& piece,int& row,int& col,int fromRotation,int toRotation,
                       const Runtime::Playfield& field,int& kickIndex) {
    const int op=legacy_transition(fromRotation,toRotation);
    const Piece::Metadata metadata = Piece::metadata(piece.type());
    if(metadata.rotationSystem==Runtime::RotationSystem::Ars&&
       (metadata.familyIndex==1||metadata.familyIndex==2||metadata.familyIndex==5)) {
        for(int testRow=row-1;testRow<=row+1;testRow++) {
            for(int testCol=col-1;testCol<=col+1;testCol++) {
                if(testRow>=field.height()||testCol<0||testCol>=field.width()||field.cell(testRow,testCol).occupied()) {
                    if(testCol==col) return false;
                    testRow=row+1;
                    break;
                }
            }
        }
    }
    int index=0;
    for(const auto& det:piece.kick_tests()[op]) {
        if(legacy_can_place(piece,row-det[1],col+det[0],toRotation,field)) {
            row-=det[1];
            col+=det[0];
            kickIndex=index;
            return true;
        }
        ++index;
    }
    return false;
}

void test_playfield() {
    Runtime::Playfield field;
    assert(field.width()==0&&field.height()==0&&!field.initialized());
    field.initialize(10,20);
    assert(field.width()==10&&field.height()==20);
    assert(field.contains(-100,0));
    assert(field.contains(19,9));
    assert(!field.contains(-101,0));
    assert(!field.contains(20,0));
    assert(!field.contains(0,10));
    bool badDimensions=false;
    try { Runtime::Playfield invalid; invalid.initialize(Runtime::Playfield::kStorageColumns+1,20); }
    catch(const std::out_of_range&) { badDimensions=true; }
    assert(badDimensions);

    bool badRow=false;
    try { (void)field.cell(-Runtime::kHiddenRowCapacity-1,0); }
    catch(const std::out_of_range&) { badRow=true; }
    assert(badRow);

    for(int row=-100;row<20;++row) for(int col=0;col<10;++col) assert(field.cell(row,col).empty_cell());
    field.set_cell(-7,3,Compat::board_cell_from_legacy(11));
    field.set_cell(19,9,Compat::board_cell_from_legacy(5));
    assert(Compat::to_legacy(field.cell(-7,3))==11&&Compat::to_legacy(field.cell(19,9))==5);

    Runtime::Playfield copied=field;
    assert(copied.width()==10&&copied.height()==20);
    assert(Compat::to_legacy(copied.cell(-7,3))==11&&Compat::to_legacy(copied.cell(19,9))==5);
    copied.copy_row(3,-7);
    assert(Compat::to_legacy(copied.cell(3,3))==11);
    copied.clear();
    for(int row=-100;row<20;++row) for(int col=0;col<10;++col) assert(copied.cell(row,col).empty_cell());
    copied.set_cell(5,5,Compat::board_cell_from_legacy(7));
    copied.initialize(10,20);
    assert(copied.cell(5,5).empty_cell());
    Runtime::Playfield empty;
    copied=empty;
    assert(!copied.initialized()&&copied.width()==0&&copied.height()==0);
}

void test_hidden_row_boundary() {
    Runtime::Playfield field;
    field.initialize(10,20);
    field.clear();
    const auto piece=Piece::get_definition(Piece::Type::from_index(0));
    assert(!piece.can_place(Piece::Pose{{-Runtime::kHiddenRowCapacity-10,4},Runtime::Rotation::from_index(0)},field));
}

void test_piece_tables() {
    const auto& entries = Piece::catalog();
    assert(!entries.empty());
    for(const auto& entry:entries) {
        const auto piece=Piece::get_definition(entry.type);
        assert(piece.type()==entry.type);
        assert(piece.color_id()==entry.metadata.colorId);
        assert(piece.kick_tests()==entry.kickTests);
        for(int rotation=0;rotation<4;++rotation) {
            assert(piece.cells(Runtime::Rotation::from_index(rotation))==entry.shapes[rotation]);
        }
        assert(Piece::metadata(entry.type).name==entry.metadata.name);
    }
    for(const auto& entry:entries) {
        Piece::Type expected=entry.type;
        if(entry.metadata.rotationSystem==Runtime::RotationSystem::AprilFools2026) {
            static constexpr int mapped[7]={6,2,3,4,0,1,5};
            expected=Piece::Type::from_index(mapped[entry.metadata.familyIndex%Piece::standard_tetromino_count()]);
        }
        assert(Piece::fool_display_type(entry.type)==expected);
    }
}

void test_collision_and_rotation_equivalence() {
    std::mt19937 rng(0x20c0ffeeu);
    constexpr int typeCount=173;
    for(int iteration=0;iteration<30000;++iteration) {
        Runtime::Playfield field;
        const int width=4+static_cast<int>(rng()%17);
        const int height=8+static_cast<int>(rng()%25);
        field.initialize(width,height);
        field.clear();
        for(int row=-8;row<height;++row) {
            for(int col=0;col<width;++col) {
                if((rng()%100)<17) field.set_cell(row,col,Compat::board_cell_from_legacy(static_cast<int>(rng()%10)));
            }
        }

        const int type=static_cast<int>(rng()%typeCount);
        const auto piece=Piece::get_definition(Compat::piece_type_from_legacy(type));
        const int rotation=static_cast<int>(rng()%4);
        const int row=-5+static_cast<int>(rng()%(height+7));
        const int col=-3+static_cast<int>(rng()%(width+6));
        const Piece::Pose pose{{row,col},Compat::rotation_from_legacy(rotation)};
        assert(piece.can_place(pose,field)==legacy_can_place(piece,row,col,rotation,field));

        const int from=static_cast<int>(rng()%4);
        int to=static_cast<int>(rng()%4);
        if(to==from) to=(to+2)%4;
        int legacyRow=row,legacyCol=col,legacyKick=-77;
        const bool expected=legacy_try_rotate(piece,legacyRow,legacyCol,from,to,field,legacyKick);
        Piece::Pose newPose{{row,col},Compat::rotation_from_legacy(from)};
        Piece::KickIndex newKick;
        const bool actual=piece.try_rotate(newPose,Compat::rotation_from_legacy(to),field,newKick);
        assert(actual==expected);
        assert(newPose.position.row==legacyRow&&newPose.position.col==legacyCol);
        if(actual){
            assert(newPose.rotation==Compat::rotation_from_legacy(to));
            assert(Compat::to_legacy(newKick)==legacyKick);
        }
    }
}

} // namespace

int main() {
    test_playfield();
    test_piece_tables();
    test_hidden_row_boundary();
    test_collision_and_rotation_equivalence();
    return 0;
}
