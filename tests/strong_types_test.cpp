#include "tetr/compat/legacy_encoding.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/board_cell.h"

#include <cassert>
#include <type_traits>

namespace TetrTests::strong_types_test {
static_assert(!std::is_convertible_v<int,Piece::Type>);
static_assert(!std::is_convertible_v<int,Runtime::Rotation>);
static_assert(!std::is_convertible_v<int,Piece::KickIndex>);
static_assert(std::is_copy_constructible_v<Piece::Pose>);
static_assert(!std::is_convertible_v<int, Runtime::BoardCell>);

int run(int argc,char** argv){
    for(int value=-20;value<=20;++value){
        const auto r=Compat::rotation_from_legacy(value);
        assert(Compat::to_legacy(r)>=0&&Compat::to_legacy(r)<4);
        assert(Compat::to_legacy(r)==((value%4)+4)%4);
        assert(Compat::to_legacy(r.rotated(1))==((Compat::to_legacy(r)+1)%4));
        assert(Compat::to_legacy(r.rotated(-1))==((Compat::to_legacy(r)+3)%4));
    }
    for(int value=-2;value<=172;++value){
        const auto type=Compat::piece_type_from_legacy(value);
        assert(Compat::to_legacy(type)==value);
        assert(type.valid()==(value>=0));
    }
    assert(Compat::to_legacy(Piece::Type::none())==-1);

    assert(!Runtime::RotationRecord::none().rotated());
    assert(Compat::to_legacy(Runtime::RotationRecord::none())==0);
    for(int kick=0;kick<=10;++kick){
        const auto k=Compat::kick_index_from_legacy(kick);
        const auto rec=Runtime::RotationRecord::from_kick(k);
        assert(rec.rotated());
        assert(Compat::to_legacy(rec.kick())==kick);
        assert(Compat::to_legacy(rec)==kick+1);
        const auto roundTrip=Compat::rotation_record_from_legacy(Compat::to_legacy(rec));
        assert(roundTrip==rec);
    }

    Piece::Pose pose{{-2,4},Runtime::Rotation{3}};
    const auto shifted=pose.translated(3,-2);
    assert(shifted.position.row==1&&shifted.position.col==2);
    assert(Compat::to_legacy(shifted.rotation)==3);
    assert(pose.position.row==-2&&pose.position.col==4);


    for (int value = -1; value <= 120; ++value) {
        const auto cell = Compat::board_cell_from_legacy(value);
        assert(Compat::to_legacy(cell) == value);
        assert(cell.occupied() == (value >= 0));
    }
    assert(Compat::board_cell_from_legacy(-9).empty_cell());
    assert(Compat::to_legacy(Compat::board_cell_from_legacy(-9)) == -1);
    auto timed = Runtime::BoardCell::timed_solid(3);
    assert(timed.is_timed_number() && timed.timed_countdown() == 3);
    timed = timed.decremented_timed();
    assert(timed.timed_countdown() == 2);
    timed = timed.decremented_timed();
    assert(timed.timed_countdown() == 1);
    timed = timed.decremented_timed();
    assert(timed.is_timed_release() && Compat::to_legacy(timed) == 100);

    // Keep legacy sentinels distinct at explicit boundaries.
    assert(Compat::to_legacy(Compat::piece_type_from_legacy(-1))==-1); // empty hold
    assert(Compat::to_legacy(Compat::piece_type_from_legacy(-2))==-2); // hold disabled
    return 0;
}
} // namespace TetrTests::strong_types_test
