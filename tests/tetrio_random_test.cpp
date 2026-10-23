#include "tetr/runtime/tetrio_random.h"
#include "tetr/app/first_to_seven.h"
#include <array>
#include <cassert>
#include <iostream>
#include <set>

namespace TetrTests::tetrio_random_test {
int run(int argc,char** argv) {
    for (int loser = 0; loser <= 6; ++loser) for (int winner = 0; winner < 2; ++winner) {
        App::FirstToSeven match;
        for (int i = 1; i <= loser; ++i) {
            match.accept_score(i, i - 1);
            assert(!match.finished());
            match.accept_score(i, i);
        }
        for (int i = loser + 1; i <= 7; ++i) {
            assert(!match.finished());
            match.accept_score(winner == 0 ? i : loser, winner == 1 ? i : loser);
        }
        assert(match.finished() && match.rounds() == 7 + loser);
        try { match.accept_score(7, 7); assert(false); } catch (const std::runtime_error&) {}
    }
    App::FirstToSeven invalid;
    for (const auto score : {std::array<int,2>{0,0}, {1,1}, {-1,2}, {8,0}}) {
        try { invalid.accept_score(score[0], score[1]); assert(false); } catch (const std::runtime_error&) {}
        assert(invalid.rounds() == 0);
    }
    Runtime::TetrioRandom rng(1);
    for (const auto expected : {16807, 282475249, 1622650073, 984943658, 1144108930})
        assert(rng.next() == expected);
    rng.reseed(1);
    const std::array<int, 7> first{3, 1, 0, 2, 4, 5, 6};
    assert(rng.bag() == first);
    const std::array<int, 7> second{5, 3, 2, 1, 4, 0, 6};
    assert(rng.bag() == second);
    assert(rng.state() == 1115438165);
    for (int i = 0; i < 10000; ++i) {
        const auto bag = rng.bag();
        assert(std::set<int>(bag.begin(), bag.end()).size() == 7);
    }
    Runtime::TetrioRandom zero(0), modulus(2147483647);
    assert(zero.state() == 2147483646 && modulus.state() == zero.state());
    Runtime::TetrioGarbageRandom holes(1);
    assert(holes.row(10) == 0 && holes.random.state() == 16807);
    assert(holes.row(10) == 0 && holes.random.state() == 282475249);
    holes.finish_packet(10);
    assert(holes.column == 4 && holes.random.state() == 984943658);
    assert(holes.row(10) == 4 && holes.random.state() == 1144108930);
    holes.finish_packet(10);
    assert(holes.column == 0 && holes.random.state() == 101027544);
    // Entirely cancelled packets still advance hole RNG, even before any rise.
    Runtime::TetrioGarbageRandom cancelled(1);
    cancelled.finish_packet(10);
    assert(cancelled.column == 1 && cancelled.random.state() == 282475249);
    assert(cancelled.row(10) == 1 && cancelled.random.state() == 1622650073);
    std::cout << "TETR.IO random profile and first-to-seven tests passed\n";
    return 0;
}
} // namespace TetrTests::tetrio_random_test
